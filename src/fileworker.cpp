#include "fileworker.h"

#include <QFileDialog>
#include <QDataStream>
#include <QMessageBox>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>

fileWorker::fileWorker(QObject *parent) : QObject(parent)
{
    currentListenType = Unlisten;

    sendTimes = 0;

    fileServer = new QTcpServer();
    connect(fileServer,SIGNAL(newConnection()),this,SLOT(acceptConnection()));

    sendSocket = new QTcpSocket();
    connect(sendSocket,SIGNAL(connected()),this,SLOT(sendFileInfo()));
    connect(sendSocket,SIGNAL(bytesWritten(qint64)),this,SLOT(continueToSend(qint64)));
}

fileWorker::ListenType fileWorker::status()
{
    return currentListenType;
}

bool fileWorker::startListen()
{
    bool status = fileServer->listen(QHostAddress(IP),PORT.toInt());

    if(status)
        currentListenType = Listen;
    else
        currentListenType = Unlisten;

    return status;
}

void fileWorker::stopWorker()
{
    fileServer->close();
}

bool fileWorker::setSendFile(QString path)
{
    if(!path.isEmpty())
    {
        sendFile = new QFile(path);
        if( sendFile->open(QIODevice::ReadOnly) )
        {
            filePath = path;
            sendFileName = path.right(path.size()-path.lastIndexOf('/')-1);
            emit messageShowReady(chatWorker::System,tr("System"),tr(" -- File Selete: %1").arg(sendFileName));
            sendTimes = 0;
            sendFileBlock.clear();
            return true;
        }
        else
        {
            emit messageShowReady(chatWorker::System,tr("System"),tr(" -- File %1 Read Fail").arg(sendFileName));
        }
    }
    else
    {
        emit messageShowReady(chatWorker::System,tr("System"),tr(" -- Invaild File Path Info: %1").arg(path));
    }
    return false;
}

void fileWorker::setArgs(QString ip, QString port)
{
    IP = ip;
    PORT = port;
}

void fileWorker::acceptConnection()
{
    receiveFileTotalSize = receiveFileTransSize = 0;
    emit messageShowReady(chatWorker::System,tr("System"),tr(" -- New File Arriving"));

    receiveSocket = fileServer->nextPendingConnection();
    connect(receiveSocket,SIGNAL(readyRead()),this,SLOT(readConnection()));

    emit progressBarUpdateReady(Show,0); // 显示文件传输进度条
}

void fileWorker::readConnection()
{
    if(receiveFileTotalSize == 0)
    {
        QDataStream in(receiveSocket);
        in>>receiveFileTotalSize>>receiveFileTransSize>>receiveFileName;

        emit progressBarUpdateReady(SetMax,receiveFileTotalSize);

        QString name = receiveFileName.mid(0,receiveFileName.lastIndexOf("."));
        QString suffix = receiveFileName.mid(receiveFileName.lastIndexOf(".")+1,receiveFileName.size());

        if( QSysInfo::kernelType() == "linux" )
        {
            if(QFile::exists(receiveFileName))
            {
                int id = 1;
                while( QFile::exists( name + "(" + QString::number(id) + ")." + suffix ) )
                    id++;
                receiveFile = new QFile( name + "(" + QString::number(id) + ")." + suffix );
            }
            else
                receiveFile = new QFile(receiveFileName);
        }
        else
        {
            if(QFile::exists(DEFAULT_FILE_STORE+receiveFileName))
            {
                int id = 1;
                while( QFile::exists(DEFAULT_FILE_STORE + name + "(" + QString::number(id) + ")." + suffix) )
                    id++;
                receiveFile = new QFile(DEFAULT_FILE_STORE + name + "(" + QString::number(id) + ")." + suffix);
            }
            else
            {
                receiveFile = new QFile(DEFAULT_FILE_STORE+receiveFileName); // 保存文件
            }
        }

        receiveFile->open(QFile::ReadWrite);

        emit messageShowReady(chatWorker::System,tr("System"),tr(" -- File Name: %1 File Size: %2").arg(receiveFileName,QString::number(receiveFileTotalSize)));
    }
    else
    {
        receiveFileBlock = receiveSocket->readAll();
        receiveFileTransSize += receiveFileBlock.size();

        emit progressBarUpdateReady(SetValue,receiveFileTransSize);

        receiveFile->write(receiveFileBlock);
        receiveFile->flush();
    }

    if(receiveFileTransSize == receiveFileTotalSize) // 文件传输完成
    {
        emit messageShowReady(chatWorker::System,tr("System"),tr(" -- File Transmission Complete"));

        QMessageBox::StandardButton choice;
        choice = QMessageBox::information(nullptr,tr("Open File Folder?"),tr("Open File Folder?"),QMessageBox::Yes,QMessageBox::No);
        if(choice == QMessageBox::Yes)
        {
            if(QSysInfo::kernelType() == "linux")
            {
                QDir dir("");
                QDesktopServices::openUrl(QUrl(dir.absolutePath() , QUrl::TolerantMode)); // 打开文件夹
            }
            else
            {
                QDir dir(DEFAULT_FILE_STORE);
                QDesktopServices::openUrl(QUrl(dir.absolutePath() , QUrl::TolerantMode)); // 打开文件夹
            }
        }

        receiveFileTotalSize = receiveFileTransSize = 0;
        receiveFileName = QString::null;
        emit progressBarUpdateReady(Hide,0);
        receiveFile->close();
    }
}

void fileWorker::startSend()
{
    if(sendTimes == 0)
    {
        sendSocket->connectToHost(QHostAddress(IP),PORT.toInt());
        if(!sendSocket->waitForConnected(2000)) // 检测网络情况
            QMessageBox::information(nullptr,tr("ERROR"),tr("Network Error"),QMessageBox::Yes);
        else
            sendTimes = 1;
    }
    else
        sendFileInfo();
}

void fileWorker::sendFileInfo()
{
    sendFileEachTransSize = 40 * 1024;

    QDataStream out(&sendFileBlock,QIODevice::WriteOnly);
    out<<qint64(0)<<qint64(0)<<sendFileName;

    sendFileTotalSize = sendFile->size() + sendFileBlock.size();
    sendFileLeftSize = sendFile->size() + sendFileBlock.size();

    out.device()->seek(0);
    out<<sendFileTotalSize<<qint64(sendFileBlock.size());

    sendSocket->write(sendFileBlock);

    emit progressBarUpdateReady(SetMax,sendFileTotalSize);
    emit progressBarUpdateReady(SetValue,0);
    emit progressBarUpdateReady(Show,0);

    emit messageShowReady(chatWorker::System,tr("System"),tr(" -- File Name: %1 File Size: %2").arg(sendFileName,QString::number(sendFileTotalSize)));
}

void fileWorker::continueToSend(qint64 size)
{
    if(sendSocket->state() != QAbstractSocket::ConnectedState) // 网络出错
    {
        QMessageBox::information(nullptr,tr("ERROR"),tr("Network Error"),QMessageBox::Yes);
        emit progressBarUpdateReady(Hide,0);
        return;
    }

    sendFileLeftSize -= size;

    if(sendFileLeftSize == 0) // 文件发送完成
    {
        emit messageShowReady(chatWorker::System,tr("System"),tr(" -- File Transmission Complete"));

        sendSocket->disconnectFromHost();

        emit progressBarUpdateReady(Hide,0);

        sendFile->close();

        sendFile = new QFile(filePath);
        sendFile->open(QIODevice::ReadOnly);
        sendTimes = 0;
        sendFileBlock.clear();

    }
    else
    {
        sendFileBlock = sendFile->read( qMin(sendFileLeftSize,sendFileEachTransSize) );

        sendSocket->write(sendFileBlock);

        emit progressBarUpdateReady(SetValue,sendFileTotalSize - sendFileLeftSize);
    }
}
