#include "tftpserver.h"
#include "tftptransfer.h"

#include <QDir>

TFTPServer::TFTPServer(const QString &serverRoot, QObject *parent)
    : QObject(parent)
    , serverRoot(serverRoot)
{
    // Assume failed so we can just return early on failure
    failed = true;
    listener = new QUdpSocket(this);
    if (!listener)
        return;

    if (!listener->bind(69))
        return;

    if (!connect(listener, SIGNAL(readyRead()), this, SLOT(OnPacketReceived())))
        return;

    readBuffer.resize(576);

    failed = false;
}

void TFTPServer::OnPacketReceived()
{
    QHostAddress addr;
    quint16 port;

    while (listener->hasPendingDatagrams())
    {
        int size = listener->pendingDatagramSize();

        if (readBuffer.size() < size)
            readBuffer.resize(size);

        size = listener->readDatagram(readBuffer.data(), readBuffer.size(),
            &addr, &port);

        ParseListenerDatagram(size, addr, port);
    }
}

void TFTPServer::ParseListenerDatagram(int size, QHostAddress &addr, quint16 port)
{
    emit VerboseEvent("TFTP: Parsing listener packet");

    // Check for size being too small to be possibly valid
    if (size < 6)
    {
        emit VerboseEvent("Invalid TFTP request packet (too small)");
        return;
    }

    // Big endian opcode
    quint16 opcode;
    opcode = (readBuffer[0] << 8) | readBuffer[1];

    // The request packet consists of a 16-bit big endian opcode
    // followed by a number of null terminated strings
    OptionOffsetList strings;
    OptionOffsetList::value_type stringStart;

    stringStart = sizeof(quint16);

    for (OptionOffsetList::value_type i = stringStart; i < size; ++i)
    {
        if (readBuffer[i] == (char)0)
        {
            emit VerboseEvent(QString("Option string: %1").arg(readBuffer.constData() + stringStart));
            strings.append(stringStart);
            stringStart = i + 1;
        }
    }

    // Filename and mode fields are required
    if (strings.size() < 2)
    {
        emit VerboseEvent("Invalid TFTP request packet (required filename and mode missing)");
        return;
    }

    // Parse filename, mode, and options
    OptionList options;
    options.append(OptionPair("filename", readBuffer.constData() + strings[0]));
    options.append(OptionPair("mode", readBuffer.constData() + strings[1]));

    // Iterate through optional extensions
    for (int i = 2; i < strings.size() - 1; i += 2)
    {
        options.append(OptionPair(readBuffer.constData() + strings[i],
            readBuffer.constData() + strings[i+1]));
    }

    TFTPTransfer *transfer;
    transfer = new TFTPTransfer(this);

    connect(transfer, SIGNAL(VerboseEvent(QString)), this, SLOT(OnTransferVerboseEvent(QString)));

    emit VerboseEvent("TFTP: Attempting to start transfer");

    if (!transfer->StartTransfer(listener, addr, port, opcode, serverRoot, options))
    {
        emit VerboseEvent("Transfer failed to start");
        return;
    }
}

const char *TFTPServer::LookupOption(const OptionList &options, const char *option)
{
    for (int i = 0; i < options.size(); ++i)
    {
        if (!strcmp(options[i].first, option))
            return options[i].second;
    }
    return nullptr;
}

void TFTPServer::OnTransferVerboseEvent(const QString &msg)
{
    emit VerboseEvent(QString("XFER: %1").arg(msg));
}

