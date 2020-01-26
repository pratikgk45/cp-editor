/*
 * Copyright (C) 2019-2020 Ashar Khan <ashar786khan@gmail.com>
 *
 * This file is part of CPEditor.
 *
 * CPEditor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * I will not be responsible if CPEditor behaves in unexpected way and
 * causes your ratings to go down and or loose any important contest.
 *
 * Believe Software is "Software" and it isn't immune to bugs.
 *
 */

#include "appwindow.hpp"
#include "../ui/ui_appwindow.h"
#include <EditorTheme.hpp>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonDocument>
#include <QMap>
#include <QMessageBox>
#include <QMetaMethod>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

AppWindow::AppWindow(bool noHotExit, QWidget *parent) : QMainWindow(parent), ui(new Ui::AppWindow)
{
    ui->setupUi(this);
    setAcceptDrops(true);
    allocate();
    setConnections();

    if (settingManager->isCheckUpdateOnStartup())
        updater->checkUpdate();

    setWindowOpacity(settingManager->getTransparency() / 100.0);

    applySettings();
    onSettingsApplied();

    if (!noHotExit && settingManager->isUseHotExit())
    {
        for (int i = 0; i < settingManager->getNumberOfTabs(); ++i)
        {
            openTab("");
            currentWindow()->loadStatus(MainWindow::EditorStatus(settingManager->getEditorStatus(i)));
        }

        int index = settingManager->getCurrentIndex();
        if (index >= 0 && index < settingManager->getNumberOfTabs())
            ui->tabWidget->setCurrentIndex(index);
    }
}

AppWindow::AppWindow(int depth, bool cpp, bool java, bool python, bool noHotExit, const QStringList &paths,
                     QWidget *parent)
    : AppWindow(noHotExit, parent)
{
    for (auto path : paths)
    {
        if (QDir(path).exists())
            openFolder(path, cpp, java, python, depth);
        else
            openTab(path);
    }
    if (ui->tabWidget->count() == 0)
        openTab("");
}

AppWindow::AppWindow(bool cpp, bool java, bool python, bool noHotExit, int number, const QString &path, QWidget *parent)
    : AppWindow(noHotExit, parent)
{
    QString lang = settingManager->getDefaultLang();
    if (cpp)
        lang = "C++";
    else if (java)
        lang = "Java";
    else if (python)
        lang = "Python";
    openContest(path, lang, number);
    if (ui->tabWidget->count() == 0)
        openTab("");
}

AppWindow::~AppWindow()
{
    saveSettings();
    Themes::EditorTheme::release();
    delete settingManager;
    delete ui;
    delete preferenceWindow;
    delete timer;
    delete updater;
    delete server;
}

/******************* PUBLIC METHODS ***********************/

void AppWindow::closeEvent(QCloseEvent *event)
{
    if (quit())
        event->accept();
    else
        event->ignore();
}

void AppWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void AppWindow::dropEvent(QDropEvent *event)
{
    auto files = event->mimeData()->urls();
    for (auto e : files)
    {
        auto fileName = e.toLocalFile();
        openTab(fileName);
    }
}

/******************** PRIVATE METHODS ********************/
void AppWindow::setConnections()
{
    connect(ui->tabWidget, SIGNAL(tabCloseRequested(int)), this, SLOT(onTabCloseRequested(int)));
    connect(ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(onTabChanged(int)));
    ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->tabWidget->tabBar(), SIGNAL(customContextMenuRequested(const QPoint &)), this,
            SLOT(onTabContextMenuRequested(const QPoint &)));
    connect(timer, SIGNAL(timeout()), this, SLOT(onSaveTimerElapsed()));
    connect(editorChangeApply, SIGNAL(timeout()), this, SLOT(applyEditorChanged()));

    connect(preferenceWindow, SIGNAL(settingsApplied()), this, SLOT(onSettingsApplied()));

    if (settingManager->isCompetitiveCompanionActive())
        companionEditorConnections =
            connect(server, &Network::CompanionServer::onRequestArrived, this, &AppWindow::onIncomingCompanionRequest);
}

void AppWindow::allocate()
{
    settingManager = new Settings::SettingManager();
    timer = new QTimer();
    updater = new Telemetry::UpdateNotifier(settingManager->isBeta());
    preferenceWindow = new PreferenceWindow(settingManager, this);
    server = new Network::CompanionServer(settingManager->getConnectionPort());
    editorChangeApply = new QTimer();

    timer->setInterval(3000);
    timer->setSingleShot(false);
    editorChangeApply->setSingleShot(true);
}

void AppWindow::applySettings()
{
    ui->actionAutosave->setChecked(settingManager->isAutoSave());
    Settings::ViewMode mode = settingManager->getViewMode();

    switch (mode)
    {
    case Settings::ViewMode::FULL_EDITOR:
        on_actionEditor_Mode_triggered();
        break;
    case Settings::ViewMode::FULL_IO:
        on_actionIO_Mode_triggered();
        break;
    case Settings::ViewMode::SPLIT:
        on_actionSplit_Mode_triggered();
    }

    if (settingManager->isAutoSave())
        timer->start();

    if (!settingManager->getGeometry().isEmpty() && !settingManager->getGeometry().isNull() &&
        settingManager->getGeometry().isValid() && !settingManager->isMaximizedWindow())
    {
        setGeometry(settingManager->getGeometry());
    }

    if (settingManager->isMaximizedWindow())
    {
        this->showMaximized();
    }

    maybeSetHotkeys();
}

void AppWindow::maybeSetHotkeys()
{
    for (auto e : hotkeyObjects)
        delete e;
    hotkeyObjects.clear();

    if (!settingManager->isHotkeyInUse())
        return;

    if (!settingManager->getHotkeyRun().isEmpty())
    {
        hotkeyObjects.push_back(new QShortcut(settingManager->getHotkeyRun(), this, SLOT(on_actionRun_triggered())));
    }
    if (!settingManager->getHotkeyCompile().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(settingManager->getHotkeyCompile(), this, SLOT(on_actionCompile_triggered())));
    }
    if (!settingManager->getHotkeyCompileRun().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(settingManager->getHotkeyCompileRun(), this, SLOT(on_actionCompile_Run_triggered())));
    }
    if (!settingManager->getHotkeyFormat().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(settingManager->getHotkeyFormat(), this, SLOT(on_actionFormat_code_triggered())));
    }
    if (!settingManager->getHotkeyKill().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(settingManager->getHotkeyKill(), this, SLOT(on_actionKill_Processes_triggered())));
    }
    if (!settingManager->getHotkeyViewModeToggler().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(settingManager->getHotkeyViewModeToggler(), this, SLOT(onViewModeToggle())));
    }
    if (!settingManager->getHotkeySnippets().isEmpty())
    {
        hotkeyObjects.push_back(
            new QShortcut(settingManager->getHotkeySnippets(), this, SLOT(on_actionUse_Snippets_triggered())));
    }
}

bool AppWindow::closeTab(int index)
{
    auto tmp = windowIndex(index);
    if (tmp->closeConfirm())
    {
        ui->tabWidget->removeTab(index);
        onEditorChanged();
        delete tmp;
        return true;
    }
    return false;
}

void AppWindow::saveSettings()
{
    if (!this->isMaximized())
        settingManager->setGeometry(this->geometry());
    settingManager->setMaximizedWindow(this->isMaximized());
}

void AppWindow::openTab(QString path, bool iscompanionOpenedTab)
{
    if (!iscompanionOpenedTab)
    {
        if (QFile::exists(path))
        {
            auto fileInfo = QFileInfo(path);
            for (int t = 0; t < ui->tabWidget->count(); t++)
            {
                auto tmp = dynamic_cast<MainWindow *>(ui->tabWidget->widget(t));
                if (fileInfo == QFileInfo(tmp->getFilePath()))
                {
                    ui->tabWidget->setCurrentIndex(t);
                    return;
                }
            }
        }

        int t = ui->tabWidget->count();
        int index = 0;
        if (path.isEmpty())
        {
            QSet<int> vis;
            for (int t = 0; t < ui->tabWidget->count(); ++t)
            {
                auto tmp = windowIndex(t);
                if (tmp->isUntitled() && tmp->getProblemURL().isEmpty())
                {
                    vis.insert(tmp->getUntitledIndex());
                }
            }
            for (index = 1; vis.contains(index); ++index)
                ;
        }
        auto fsp = new MainWindow(path, settingManager->toData(), index);
        connect(fsp, SIGNAL(confirmTriggered(MainWindow *)), this, SLOT(on_confirmTriggered(MainWindow *)));
        connect(fsp, SIGNAL(editorChanged()), this, SLOT(onEditorChanged()));
        QString lang = settingManager->getDefaultLang();

        if (path.endsWith(".java"))
            lang = "Java";
        else if (path.endsWith(".py") || path.endsWith(".py3"))
            lang = "Python";
        else if (path.endsWith(".cpp") || path.endsWith(".cxx") || path.endsWith(".c") || path.endsWith(".cc") ||
                 path.endsWith(".hpp") || path.endsWith(".h"))
            lang = "C++";

        ui->tabWidget->addTab(fsp, fsp->getFileName());
        fsp->setLanguage(lang);
        ui->tabWidget->setCurrentIndex(t);
    }
    else
    {
        for (int t = 0; t < ui->tabWidget->count(); t++)
        {
            auto tmp = windowIndex(t);
            if (path == tmp->getProblemURL())
            {
                ui->tabWidget->setCurrentIndex(t);
                return;
            }
        }

        int t = ui->tabWidget->count();
        int index = 0;
        QSet<int> vis;
        for (int t = 0; t < ui->tabWidget->count(); ++t)
        {
            auto tmp = windowIndex(t);
            if (tmp->isUntitled() && tmp->getProblemURL().isEmpty())
            {
                vis.insert(tmp->getUntitledIndex());
            }
        }
        for (index = 1; vis.contains(index); ++index)
            ;
        auto fsp = new MainWindow("", settingManager->toData(), index);
        connect(fsp, SIGNAL(confirmTriggered(MainWindow *)), this, SLOT(on_confirmTriggered(MainWindow *)));
        connect(fsp, SIGNAL(editorChanged()), this, SLOT(onEditorChanged()));
        ui->tabWidget->addTab(fsp, fsp->getFileName());
        ui->tabWidget->setCurrentIndex(t);
    }
    currentWindow()->focusOnEditor();
}

void AppWindow::openFolder(const QString &path, bool cpp, bool java, bool python, int depth)
{
    auto entries = QDir(path).entryInfoList(QDir::NoDotAndDotDot | QDir::AllEntries);
    for (auto entry : entries)
    {
        if (entry.isDir())
        {
            if (depth > 0)
                openFolder(entry.canonicalFilePath(), cpp, java, python, depth - 1);
            else if (depth == -1)
                openFolder(entry.canonicalFilePath(), cpp, java, python, -1);
        }
        else if (cpp && QStringList({"cpp", "hpp", "h", "cc", "cxx", "c"}).contains(entry.suffix()) ||
                 java && QStringList({"java"}).contains(entry.suffix()) ||
                 python && QStringList({"py", "py3"}).contains(entry.suffix()))
        {
            openTab(entry.canonicalFilePath());
        }
    }
}

void AppWindow::openContest(const QString &path, const QString &lang, int number)
{
    QDir dir(path), parent(path);
    parent.cdUp();
    if (!dir.exists() && parent.exists())
        parent.mkdir(dir.dirName());

    auto language = lang.isEmpty() ? settingManager->getDefaultLang() : lang;

    for (int i = 0; i < number; ++i)
    {
        QString name('A' + i);
        if (language == "C++")
            name += ".cpp";
        else if (language == "Java")
            name += ".java";
        else if (language == "Python")
            name += ".py";
        openTab(QDir(path).filePath(name));
    }
}

bool AppWindow::quit()
{
    settingManager->clearEditorStatus();
    if (settingManager->isUseHotExit())
    {
        if (ui->tabWidget->count() == 1 && windowIndex(0)->isUntitled() && !windowIndex(0)->isTextChanged())
        {
            settingManager->setNumberOfTabs(0);
            settingManager->setCurrentIndex(-1);
        }
        else
        {
            settingManager->setNumberOfTabs(ui->tabWidget->count());
            settingManager->setCurrentIndex(ui->tabWidget->currentIndex());
            for (int i = 0; i < ui->tabWidget->count(); ++i)
            {
                settingManager->setEditorStatus(i, windowIndex(i)->toStatus().toMap());
            }
        }
        return true;
    }
    else
    {
        on_actionClose_All_triggered();
        return ui->tabWidget->count() == 0;
    }
}

/***************** ABOUT SECTION ***************************/

void AppWindow::on_actionSupport_me_triggered()
{
    QDesktopServices::openUrl(QUrl("https://paypal.me/coder3101", QUrl::TolerantMode));
}

void AppWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this,
                       QString::fromStdString(std::string("About CP Editor ") + APP_VERSION_MAJOR + "." +
                                              APP_VERSION_MINOR + "." + APP_VERSION_PATCH),
                       "<p><b>CP Editor</b> is a native Qt-based code editor written in C++. It's specially designed "
                       "for competitive programming, unlike other editors/IDEs which are mainly for developers. It "
                       "helps you focus on your coding and automates the compilation, executing and testing. It even "
                       "fetches test cases for you from webpages and submits codes on Codeforces!</p>"
                       "<p>Copyright (C) 2019-2020 Ashar Khan &lt;ashar786khan@gmail.com&gt;</p>"
                       "<p>This is free software; see the source for copying conditions. There is NO warranty; not "
                       "even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. The source code for CP Editor is "
                       "available at <a href=\"https://github.com/coder3101/cp-editor\"> "
                       "https://github.com/coder3101/cp-editor</a>.</p>");
}

/******************* FILES SECTION *************************/

void AppWindow::on_actionAutosave_triggered(bool checked)
{
    settingManager->setAutoSave(checked);
    if (checked)
        timer->start();
    else
        timer->stop();
}

void AppWindow::on_actionQuit_triggered()
{
    if (quit())
        QApplication::exit();
}

void AppWindow::on_actionNew_Tab_triggered()
{
    openTab("");
}

void AppWindow::on_actionOpen_triggered()
{
    auto fileNames = QFileDialog::getOpenFileNames(this, tr("Open Files"), "",
                                                   "Source Files (*.cpp *.hpp *.h *.cc *.cxx *.c *.py *.py3 *.java)");
    for (auto fileName : fileNames)
        openTab(fileName);
}

void AppWindow::on_actionOpenContest_triggered()
{
    auto path = QFileDialog::getExistingDirectory(this, "Open Contest");
    if (QFile::exists(path) && QFileInfo(path).isDir())
    {
        bool ok = false;
        int number =
            QInputDialog::getInt(this, "Open Contest", "Number of problems in this contest:", 5, 0, 26, 1, &ok);
        if (ok)
        {
            int current = 0;
            if (settingManager->getDefaultLang() == "Java")
                current = 1;
            else if (settingManager->getDefaultLang() == "Python")
                current = 2;
            auto lang = QInputDialog::getItem(this, "Open Contest", "Choose a language", {"C++", "Java", "Python"},
                                              current, false, &ok);
            if (ok)
            {
                openContest(path, lang, number);
            }
        }
    }
}

void AppWindow::on_actionSave_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->save(true, "Save");
}

void AppWindow::on_actionSave_As_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->saveAs();
}

void AppWindow::on_actionSave_All_triggered()
{
    for (int t = 0; t < ui->tabWidget->count(); ++t)
    {
        auto tmp = windowIndex(t);
        tmp->save(true, "Save All");
    }
}

void AppWindow::on_actionClose_Current_triggered()
{
    int index = ui->tabWidget->currentIndex();
    if (index != -1)
        closeTab(index);
}

void AppWindow::on_actionClose_All_triggered()
{
    for (int t = 0; t < ui->tabWidget->count(); t++)
    {
        if (closeTab(t))
            --t;
        else
            break;
    }
}

void AppWindow::on_actionClose_Saved_triggered()
{
    for (int t = 0; t < ui->tabWidget->count(); t++)
        if (!windowIndex(t)->isTextChanged() && closeTab(t))
            --t;
}

/************************ PREFERENCES SECTION **********************/

void AppWindow::on_actionRestore_Settings_triggered()
{
    auto res = QMessageBox::question(this, "Reset preferences?",
                                     "Are you sure you want to reset the"
                                     " all preferences to default?",
                                     QMessageBox::Yes | QMessageBox::No);
    if (res == QMessageBox::Yes)
    {
        settingManager->resetSettings();
        onSettingsApplied();
    }
}

void AppWindow::on_actionSettings_triggered()
{
    preferenceWindow->updateShow();
}

/************************** SLOTS *********************************/

#define FROMJSON(x) auto x = json[#x]

void AppWindow::onReceivedMessage(quint32 instanceId, QByteArray message)
{
    auto json = QJsonDocument::fromBinaryData(message);
    FROMJSON(cpp).toBool();
    FROMJSON(java).toBool();
    FROMJSON(python).toBool();

    if (json["type"] == "normal")
    {
        FROMJSON(depth).toInt();
        FROMJSON(paths).toVariant().toStringList();
        for (auto path : paths)
        {
            if (QDir(path).exists())
                openFolder(path, cpp, java, python, depth);
            else
                openTab(path);
        }
    }
    else if (json["type"] == "contest")
    {
        FROMJSON(number).toInt();
        FROMJSON(path).toString();
        QString lang = settingManager->getDefaultLang();
        if (cpp)
            lang = "C++";
        else if (java)
            lang = "Java";
        else if (python)
            lang = "Python";
        openContest(path, lang, number);
    }
}

#undef FROMJSON

void AppWindow::onTabCloseRequested(int index)
{
    closeTab(index);
}

void AppWindow::onTabChanged(int index)
{
    if (index == -1)
    {
        activeLogger = nullptr;
        server->setMessageLogger(nullptr);
        setWindowTitle("CP Editor: An editor specially designed for competitive programming");
        return;
    }

    disconnect(activeSplitterMoveConnections);

    auto tmp = windowIndex(index);

    setWindowTitle(tmp->getTabTitle(true, false) + " - CP Editor");

    activeLogger = tmp->getLogger();
    server->setMessageLogger(activeLogger);

    if (settingManager->isCompetitiveCompanionActive() && diagonistics)
        server->checkServer();

    tmp->setSettingsData(settingManager->toData(), diagonistics);
    diagonistics = false;

    if (ui->actionEditor_Mode->isChecked())
        on_actionEditor_Mode_triggered();
    else if (ui->actionIO_Mode->isChecked())
        on_actionIO_Mode_triggered();
    else if (ui->actionSplit_Mode->isChecked())
        on_actionSplit_Mode_triggered();

    activeSplitterMoveConnections =
        connect(tmp->getSplitter(), SIGNAL(splitterMoved(int, int)), this, SLOT(onSplitterMoved(int, int)));
}

void AppWindow::onEditorChanged()
{
    editorChangeApply->start(10);
}

void AppWindow::applyEditorChanged()
{
    if (currentWindow() != nullptr)
    {
        setWindowTitle(currentWindow()->getTabTitle(true, false) + " - CP Editor");

        QMap<QString, QVector<int>> tabsByName;

        for (int t = 0; t < ui->tabWidget->count(); ++t)
        {
            tabsByName[windowIndex(t)->getTabTitle(false, false)].push_back(t);
        }

        for (auto tabs : tabsByName)
        {
            for (auto index : tabs)
            {
                ui->tabWidget->setTabText(index, windowIndex(index)->getTabTitle(tabs.size() > 1, true));
            }
        }
    }
}

void AppWindow::onSaveTimerElapsed()
{
    for (int t = 0; t < ui->tabWidget->count(); t++)
    {
        auto tmp = windowIndex(t);
        if (!tmp->isUntitled())
        {
            tmp->save(false, "Auto Save");
        }
    }
}

void AppWindow::onSettingsApplied()
{
    updater->setBeta(settingManager->isBeta());
    maybeSetHotkeys();

    disconnect(companionEditorConnections);

    server->updatePort(settingManager->getConnectionPort());

    if (settingManager->isCompetitiveCompanionActive())
        companionEditorConnections =
            connect(server, &Network::CompanionServer::onRequestArrived, this, &AppWindow::onIncomingCompanionRequest);
    diagonistics = true;
    onTabChanged(ui->tabWidget->currentIndex());
    onEditorChanged();
}

void AppWindow::onIncomingCompanionRequest(Network::CompanionData data)
{
    if (settingManager->isCompetitiveCompanionOpenNewTab())
        openTab(data.url, true);
    if (currentWindow() != nullptr)
        currentWindow()->applyCompanion(data);
}

void AppWindow::onViewModeToggle()
{
    if (ui->actionEditor_Mode->isChecked())
    {
        on_actionIO_Mode_triggered();
        return;
    }
    if (ui->actionSplit_Mode->isChecked())
    {
        on_actionEditor_Mode_triggered();
        return;
    }
    if (ui->actionIO_Mode->isChecked())
    {
        on_actionSplit_Mode_triggered();
        return;
    }
}

void AppWindow::onSplitterMoved(int _, int __)
{
    auto splitter = currentWindow()->getSplitter();
    settingManager->setSplitterSizes(splitter->saveState());
}

/************************* ACTIONS ************************/
void AppWindow::on_actionCheck_for_updates_triggered()
{
    updater->checkUpdate(true);
}

void AppWindow::on_actionCompile_triggered()
{
    if (currentWindow() != nullptr)
    {
        if (ui->actionEditor_Mode->isChecked())
            on_actionSplit_Mode_triggered();
        currentWindow()->compileOnly();
    }
}

void AppWindow::on_actionCompile_Run_triggered()
{
    if (currentWindow() != nullptr)
    {
        if (ui->actionEditor_Mode->isChecked())
            on_actionSplit_Mode_triggered();
        currentWindow()->compileAndRun();
    }
}

void AppWindow::on_actionRun_triggered()
{
    if (currentWindow() != nullptr)
    {
        if (ui->actionEditor_Mode->isChecked())
            on_actionSplit_Mode_triggered();
        currentWindow()->runOnly();
    }
}

void AppWindow::on_actionFormat_code_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->formatSource();
}

void AppWindow::on_actionRun_Detached_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->detachedExecution();
}

void AppWindow::on_actionKill_Processes_triggered()
{
    if (currentWindow() != nullptr)
        currentWindow()->killProcesses();
}

void AppWindow::on_actionUse_Snippets_triggered()
{
    auto current = currentWindow();
    if (current != nullptr)
    {
        auto lang = current->getLanguage();
        auto names = settingManager->getSnippetsNames(lang);
        if (names.isEmpty())
        {
            activeLogger->warn("Snippets",
                               "There are no snippets for " + lang + ". Please add snippets in the preference window.");
        }
        else
        {
            auto ok = new bool;
            auto name = QInputDialog::getItem(this, tr("Use Snippets"), tr("Choose a snippet:"), names, 0, true, ok);
            if (*ok)
            {
                if (names.contains(name))
                {
                    auto content = settingManager->getSnippet(lang, name);
                    current->insertText(content);
                }
                else
                {
                    activeLogger->warn("Snippets", "There is no snippet named " + name + " for " + lang);
                }
            }
        }
    }
}

void AppWindow::on_actionEditor_Mode_triggered()
{
    settingManager->setViewMode(Settings::ViewMode::FULL_EDITOR);
    ui->actionEditor_Mode->setChecked(true);
    ui->actionIO_Mode->setChecked(false);
    ui->actionSplit_Mode->setChecked(false);
    if (currentWindow() != nullptr)
        currentWindow()->getSplitter()->setSizes({1, 0});
}

void AppWindow::on_actionIO_Mode_triggered()
{
    settingManager->setViewMode(Settings::ViewMode::FULL_IO);
    ui->actionEditor_Mode->setChecked(false);
    ui->actionIO_Mode->setChecked(true);
    ui->actionSplit_Mode->setChecked(false);
    if (currentWindow() != nullptr)
        currentWindow()->getSplitter()->setSizes({0, 1});
}

void AppWindow::on_actionSplit_Mode_triggered()
{
    settingManager->setViewMode(Settings::ViewMode::SPLIT);
    ui->actionEditor_Mode->setChecked(false);
    ui->actionIO_Mode->setChecked(false);
    ui->actionSplit_Mode->setChecked(true);
    auto state = settingManager->getSplitterSizes();
    if (currentWindow() != nullptr)
        currentWindow()->getSplitter()->restoreState(state);
}

void AppWindow::on_confirmTriggered(MainWindow *widget)
{
    int index = ui->tabWidget->indexOf(widget);
    if (index != -1)
        ui->tabWidget->setCurrentIndex(index);
}

void AppWindow::onTabContextMenuRequested(const QPoint &pos)
{
    int index = ui->tabWidget->tabBar()->tabAt(pos);
    if (index != -1)
    {
        auto widget = windowIndex(index);
        auto menu = new QMenu();
        menu->addAction("Close", [index, this] { closeTab(index); });
        menu->addAction("Close Others", [widget, this] {
            for (int i = 0; i < ui->tabWidget->count(); ++i)
                if (windowIndex(i) != widget && closeTab(i))
                    --i;
        });
        menu->addAction("Close to the Left", [widget, this] {
            for (int i = 0; i < ui->tabWidget->count() && windowIndex(i) != widget; ++i)
                if (closeTab(i))
                    --i;
        });
        menu->addAction("Close to the Right", [index, this] {
            for (int i = index + 1; i < ui->tabWidget->count(); ++i)
                if (closeTab(i))
                    --i;
        });
        menu->addAction("Close Saved", [this] { on_actionClose_Saved_triggered(); });
        menu->addAction("Close All", [this] { on_actionClose_All_triggered(); });
        QString filePath = widget->getFilePath();
        if (!widget->isUntitled() && QFile::exists(filePath))
        {
            menu->addSeparator();
            menu->addAction("Copy path", [filePath] {
                auto clipboard = QGuiApplication::clipboard();
                clipboard->setText(filePath);
            });
            // Reference: http://lynxline.com/show-in-finder-show-in-explorer/ and https://forum.qt.io/post/296072
#if defined(Q_OS_MACOS)
            menu->addAction("Reveal in Finder", [filePath] {
                QStringList args;
                args << "-e";
                args << "tell application \"Finder\"";
                args << "-e";
                args << "activate";
                args << "-e";
                args << "select POSIX file \"" + filePath + "\"";
                args << "-e";
                args << "end tell";
                QProcess::startDetached("osascript", args);
            });
#elif defined(Q_OS_WIN)
            menu->addAction("Reveal in Explorer", [filePath] {
                QStringList args;
                args << "/select," << QDir::toNativeSeparators(filePath);
                QProcess::startDetached("explorer", args);
            });
#elif defined(Q_OS_UNIX)
            QProcess proc;
            proc.start("xdg-mime", QStringList() << "query"
                                                 << "default"
                                                 << "inode/directory");
            auto finished = proc.waitForFinished(2000);
            if (finished)
            {
                auto output = proc.readLine().simplified();
                QString program;
                QStringList args;
                auto nativePath = QUrl::fromLocalFile(filePath).toString();
                if (output == "dolphin.desktop" || output == "org.kde.dolphin.desktop")
                {
                    program = "dolphin";
                    args << "--select" << nativePath;
                }
                else if (output == "nautilus.desktop" || output == "org.gnome.Nautilus.desktop" ||
                         output == "nautilus-folder-handler.desktop")
                {
                    program = "nautilus";
                    args << "--no-desktop" << nativePath;
                }
                else if (output == "caja-folder-handler.desktop")
                {
                    program = "caja";
                    args << "--no-desktop" << nativePath;
                }
                else if (output == "nemo.desktop")
                {
                    program = "nemo";
                    args << "--no-desktop" << nativePath;
                }
                else if (output == "kfmclient_dir.desktop")
                {
                    program = "konqueror";
                    args << "--select" << nativePath;
                }
                if (program.isEmpty())
                {
                    menu->addAction("Open Containing Folder", [filePath] {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).path()));
                    });
                }
                else
                {
                    menu->addAction("Reveal in File Manager", [program, args] {
                        QProcess openProcess;
                        openProcess.startDetached(program, args);
                    });
                }
            }
            else
            {
                menu->addAction("Open Containing Folder", [filePath] {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).path()));
                });
            }
#else
            menu->addAction("Open Containing Folder",
                            [filePath] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).path())); });
#endif
        }
        else if (!widget->isUntitled() && QFile::exists(QFileInfo(widget->getFilePath()).path()))
        {
            menu->addSeparator();
            menu->addAction("Copy path", [filePath] {
                auto clipboard = QGuiApplication::clipboard();
                clipboard->setText(filePath);
            });
            menu->addAction("Open Containing Folder",
                            [filePath] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).path())); });
        }
        menu->popup(ui->tabWidget->tabBar()->mapToGlobal(pos));
    }
}

MainWindow *AppWindow::currentWindow()
{
    int current = ui->tabWidget->currentIndex();
    if (current == -1)
        return nullptr;
    return dynamic_cast<MainWindow *>(ui->tabWidget->widget(current));
}

MainWindow *AppWindow::windowIndex(int index)
{
    if (index == -1)
        return nullptr;
    return dynamic_cast<MainWindow *>(ui->tabWidget->widget(index));
}
