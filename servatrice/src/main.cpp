/***************************************************************************
 *   Copyright (C) 2008 by Max-Wilhelm Bruker   *
 *   brukie@laptop   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <QCoreApplication>
#include <QTextCodec>
#include <QtGlobal>
#include <iostream>
#include <QMetaType>
#include <QDateTime>
#include "passwordhasher.h"
#include "servatrice.h"
#include "server_logger.h"
#include "settingscache.h"
#include "rng_sfmt.h"
#include "version_string.h"
#include <google/protobuf/stubs/common.h>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#endif

#define SIGSEGV_TRACE_LINES 40

RNG_Abstract *rng;
ServerLogger *logger;
QThread *loggerThread;
SettingsCache *settingsCache;

/* Prototypes */

void testRNG();
void testHash();
#if QT_VERSION < 0x050000
void myMessageOutput(QtMsgType type, const char *msg);
void myMessageOutput2(QtMsgType type, const char *msg);
#else
void myMessageOutput(QtMsgType type, const QMessageLogContext &, const QString &msg);
void myMessageOutput2(QtMsgType type, const QMessageLogContext &, const QString &msg);
#endif
#ifdef Q_OS_UNIX
void sigSegvHandler(int sig);
#endif

/* Implementations */

void testRNG()
{
	const int n = 500000;
	std::cerr << "Testing random number generator (n = " << n << " * bins)..." << std::endl;
	
	const int min = 1;
	const int minMax = 2;
	const int maxMax = 10;
	
	QVector<QVector<int> > numbers(maxMax - minMax + 1);
	QVector<double> chisq(maxMax - minMax + 1);
	for (int max = minMax; max <= maxMax; ++max) {
		numbers[max - minMax] = rng->makeNumbersVector(n * (max - min + 1), min, max);
		chisq[max - minMax] = rng->testRandom(numbers[max - minMax]);
	}
	for (int i = 0; i <= maxMax - min; ++i) {
		std::cerr << (min + i);
		for (int j = 0; j < numbers.size(); ++j) {
			if (i < numbers[j].size())
				std::cerr << "\t" << numbers[j][i];
			else
				std::cerr << "\t";
		}
		std::cerr << std::endl;
	}
	std::cerr << std::endl << "Chi^2 =";
	for (int j = 0; j < chisq.size(); ++j)
		std::cerr << "\t" << QString::number(chisq[j], 'f', 3).toStdString();
	std::cerr << std::endl << "k =";
	for (int j = 0; j < chisq.size(); ++j)
		std::cerr << "\t" << (j - min + minMax);
	std::cerr << std::endl << std::endl;
}

void testHash()
{
	const int n = 5000;
	std::cerr << "Benchmarking password hash function (n =" << n << ")..." << std::endl;
	QDateTime startTime = QDateTime::currentDateTime();
	for (int i = 0; i < n; ++i)
		PasswordHasher::computeHash("aaaaaa", "aaaaaaaaaaaaaaaa");
	QDateTime endTime = QDateTime::currentDateTime();
	std::cerr << startTime.secsTo(endTime) << "secs" << std::endl;
}

#if QT_VERSION < 0x050000
void myMessageOutput(QtMsgType /*type*/, const char *msg)
{
	logger->logMessage(msg);
}

void myMessageOutput2(QtMsgType /*type*/, const char *msg)
{
	logger->logMessage(msg);
	std::cerr << msg << std::endl;
}
#else
void myMessageOutput(QtMsgType /*type*/, const QMessageLogContext &, const QString &msg)
{
	logger->logMessage(msg);
}

void myMessageOutput2(QtMsgType /*type*/, const QMessageLogContext &, const QString &msg)
{
	logger->logMessage(msg);
	std::cerr << msg.toStdString() << std::endl;
}
#endif

#ifdef Q_OS_UNIX
void sigSegvHandler(int sig)
{
    void *array[SIGSEGV_TRACE_LINES];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, SIGSEGV_TRACE_LINES);

    // print out all the frames to stderr
    fprintf(stderr, "Error: signal %d:\n", sig);
    backtrace_symbols_fd(array, size, STDERR_FILENO);

	if (sig == SIGSEGV)
		logger->logMessage("CRASH: SIGSEGV");
	else if (sig == SIGABRT)
		logger->logMessage("CRASH: SIGABRT");
	
	logger->deleteLater();
	loggerThread->wait();
	delete loggerThread;
	
	raise(sig);
}
#endif

int main(int argc, char *argv[])
{
	QCoreApplication app(argc, argv);
	app.setOrganizationName("Cockatrice");
	app.setApplicationName("Servatrice");
	
	QStringList args = app.arguments();
	bool testRandom = args.contains("--test-random");
	bool testHashFunction = args.contains("--test-hash");
	bool logToConsole = args.contains("--log-to-console");
	QString configPath;
	int hasConfigPath=args.indexOf("--config");
	if(hasConfigPath > -1 && args.count() > hasConfigPath + 1)
		configPath = args.at(hasConfigPath + 1);
	
	qRegisterMetaType<QList<int> >("QList<int>");

#if QT_VERSION < 0x050000
	// gone in Qt5, all source files _MUST_ be utf8-encoded
	QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
#endif

	configPath = SettingsCache::guessConfigurationPath(configPath);
	qWarning() << "Using configuration file: " << configPath;
	settingsCache = new SettingsCache(configPath);
	
	loggerThread = new QThread;
	loggerThread->setObjectName("logger");
	logger = new ServerLogger(logToConsole);
	logger->moveToThread(loggerThread);
	
	loggerThread->start();
	QMetaObject::invokeMethod(logger, "startLog", Qt::BlockingQueuedConnection, Q_ARG(QString, settingsCache->value("server/logfile", QString("server.log")).toString()));

#if QT_VERSION < 0x050000
	if (logToConsole)
		qInstallMsgHandler(myMessageOutput);
	else
		qInstallMsgHandler(myMessageOutput2);
#else
	if (logToConsole)
		qInstallMessageHandler(myMessageOutput);
	else
		qInstallMessageHandler(myMessageOutput2);
#endif

#ifdef Q_OS_UNIX	
	struct sigaction hup;
	hup.sa_handler = ServerLogger::hupSignalHandler;
	sigemptyset(&hup.sa_mask);
	hup.sa_flags = 0;
	hup.sa_flags |= SA_RESTART;
	sigaction(SIGHUP, &hup, 0);
	
	struct sigaction segv;
	segv.sa_handler = sigSegvHandler;
	segv.sa_flags = SA_RESETHAND;
	sigemptyset(&segv.sa_mask);
	sigaction(SIGSEGV, &segv, 0);
	sigaction(SIGABRT, &segv, 0);
	
	signal(SIGPIPE, SIG_IGN);
#endif
	rng = new RNG_SFMT;
	
	std::cerr << "Servatrice " << VERSION_STRING << " starting." << std::endl;
	std::cerr << "-------------------------" << std::endl;
	
	PasswordHasher::initialize();
	
	if (testRandom)
		testRNG();
	if (testHashFunction)
		testHash();
	
	Servatrice *server = new Servatrice();
	QObject::connect(server, SIGNAL(destroyed()), &app, SLOT(quit()), Qt::QueuedConnection);
	int retval = 0;
	if (server->initServer()) {
		std::cerr << "-------------------------" << std::endl;
		std::cerr << "Server initialized." << std::endl;

#if QT_VERSION < 0x050000		
		qInstallMsgHandler(myMessageOutput);
#else
		qInstallMessageHandler(myMessageOutput);
#endif
		retval = app.exec();
		
		std::cerr << "Server quit." << std::endl;
		std::cerr << "-------------------------" << std::endl;
	}
	
	delete rng;
	delete settingsCache;
	
	logger->deleteLater();
	loggerThread->wait();
	delete loggerThread;

	// Delete all global objects allocated by libprotobuf.
	google::protobuf::ShutdownProtobufLibrary();

	return retval;
}
