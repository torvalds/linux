/*-
 * Copyright (c) 2011, 2012, 2013, 2014, 2015, 2016  Spectra Logic Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 *
 * Authors: Justin T. Gibbs     (Spectra Logic Corporation)
 */

/**
 * \file zfsd.cc
 *
 * The ZFS daemon consumes kernel devdctl(4) event data via devd(8)'s
 * unix domain socket in order to react to system changes that impact
 * the function of ZFS storage pools.  The goal of this daemon is to
 * provide similar functionality to the Solaris ZFS Diagnostic Engine
 * (zfs-diagnosis), the Solaris ZFS fault handler (zfs-retire), and
 * the Solaris ZFS vdev insertion agent (zfs-mod sysevent handler).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/fs/zfs.h>

#include <err.h>
#include <libgeom.h>
#include <libutil.h>
#include <poll.h>
#include <syslog.h>

#include <libzfs.h>

#include <list>
#include <map>
#include <string>

#include <devdctl/guid.h>
#include <devdctl/event.h>
#include <devdctl/event_factory.h>
#include <devdctl/exception.h>
#include <devdctl/consumer.h>

#include "callout.h"
#include "vdev_iterator.h"
#include "zfsd_event.h"
#include "case_file.h"
#include "vdev.h"
#include "vdev_iterator.h"
#include "zfsd.h"
#include "zfsd_exception.h"
#include "zpool_list.h"

__FBSDID("$FreeBSD$");

/*================================== Macros ==================================*/
#define NUM_ELEMENTS(x) (sizeof(x) / sizeof(*x))

/*============================ Namespace Control =============================*/
using DevdCtl::Event;
using DevdCtl::EventFactory;
using DevdCtl::EventList;

/*================================ Global Data ===============================*/
int              g_debug = 0;
libzfs_handle_t *g_zfsHandle;

/*--------------------------------- ZfsDaemon --------------------------------*/
//- ZfsDaemon Static Private Data ----------------------------------------------
ZfsDaemon	    *ZfsDaemon::s_theZfsDaemon;
bool		     ZfsDaemon::s_logCaseFiles;
bool		     ZfsDaemon::s_terminateEventLoop;
char		     ZfsDaemon::s_pidFilePath[] = "/var/run/zfsd.pid";
pidfh		    *ZfsDaemon::s_pidFH;
int		     ZfsDaemon::s_signalPipeFD[2];
bool		     ZfsDaemon::s_systemRescanRequested(false);
EventFactory::Record ZfsDaemon::s_registryEntries[] =
{
	{ Event::NOTIFY, "GEOM",  &GeomEvent::Builder },
	{ Event::NOTIFY, "ZFS",   &ZfsEvent::Builder }
};

//- ZfsDaemon Static Public Methods --------------------------------------------
ZfsDaemon &
ZfsDaemon::Get()
{
	return (*s_theZfsDaemon);
}

void
ZfsDaemon::WakeEventLoop()
{
	write(s_signalPipeFD[1], "+", 1);
}

void
ZfsDaemon::RequestSystemRescan()
{
	s_systemRescanRequested = true;
	ZfsDaemon::WakeEventLoop();
}

void
ZfsDaemon::Run()
{
	ZfsDaemon daemon;

	while (s_terminateEventLoop == false) {

		try {
			daemon.DisconnectFromDevd();

			if (daemon.ConnectToDevd() == false) {
				sleep(30);
				continue;
			}

			daemon.DetectMissedEvents();

			daemon.EventLoop();

		} catch (const DevdCtl::Exception &exp) {
			exp.Log();
		}
	}

	daemon.DisconnectFromDevd();
}

//- ZfsDaemon Private Methods --------------------------------------------------
ZfsDaemon::ZfsDaemon()
 : Consumer(/*defBuilder*/NULL, s_registryEntries,
	    NUM_ELEMENTS(s_registryEntries))
{
	if (s_theZfsDaemon != NULL)
		errx(1, "Multiple ZfsDaemon instances created. Exiting");

	s_theZfsDaemon = this;

	if (pipe(s_signalPipeFD) != 0)
		errx(1, "Unable to allocate signal pipe. Exiting");

	if (fcntl(s_signalPipeFD[0], F_SETFL, O_NONBLOCK) == -1)
		errx(1, "Unable to set pipe as non-blocking. Exiting");

	if (fcntl(s_signalPipeFD[1], F_SETFL, O_NONBLOCK) == -1)
		errx(1, "Unable to set pipe as non-blocking. Exiting");

	signal(SIGHUP,  ZfsDaemon::RescanSignalHandler);
	signal(SIGINFO, ZfsDaemon::InfoSignalHandler);
	signal(SIGINT,  ZfsDaemon::QuitSignalHandler);
	signal(SIGTERM, ZfsDaemon::QuitSignalHandler);
	signal(SIGUSR1, ZfsDaemon::RescanSignalHandler);

	g_zfsHandle = libzfs_init();
	if (g_zfsHandle == NULL)
		errx(1, "Unable to initialize ZFS library. Exiting");

	Callout::Init();
	InitializeSyslog();
	OpenPIDFile();

	if (g_debug == 0)
		daemon(0, 0);

	UpdatePIDFile();
}

ZfsDaemon::~ZfsDaemon()
{
	PurgeCaseFiles();
	ClosePIDFile();
}

void
ZfsDaemon::PurgeCaseFiles()
{
	CaseFile::PurgeAll();
}

bool
ZfsDaemon::VdevAddCaseFile(Vdev &vdev, void *cbArg)
{
	if (vdev.State() != VDEV_STATE_HEALTHY)
		CaseFile::Create(vdev);

	return (/*break early*/false);
}

void
ZfsDaemon::BuildCaseFiles()
{
	ZpoolList zpl;
	ZpoolList::iterator pool;

	/* Add CaseFiles for vdevs with issues. */
	for (pool = zpl.begin(); pool != zpl.end(); pool++)
		VdevIterator(*pool).Each(VdevAddCaseFile, NULL);

	/* De-serialize any saved cases. */
	CaseFile::DeSerialize();

	/* Simulate config_sync events to force CaseFile reevaluation */
	for (pool = zpl.begin(); pool != zpl.end(); pool++) {
		char evString[160];
		Event *event;
		nvlist_t *config;
		uint64_t poolGUID;
		const char *poolname;

		poolname = zpool_get_name(*pool);
		config = zpool_get_config(*pool, NULL);
		if (config == NULL) {
			syslog(LOG_ERR, "ZFSDaemon::BuildCaseFiles: Could not "
			    "find pool config for pool %s", poolname);
			continue;
		}
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID,
				     &poolGUID) != 0) {
			syslog(LOG_ERR, "ZFSDaemon::BuildCaseFiles: Could not "
			    "find pool guid for pool %s", poolname);
			continue;
		}

		
		snprintf(evString, 160, "!system=ZFS subsystem=ZFS "
		    "type=misc.fs.zfs.config_sync sub_type=synthesized "
		    "pool_name=%s pool_guid=%" PRIu64 "\n", poolname, poolGUID);
		event = Event::CreateEvent(GetFactory(), string(evString));
		if (event != NULL) {
			event->Process();
			delete event;
		}
	}
}

void
ZfsDaemon::RescanSystem()
{
        struct gmesh	  mesh;
        struct gclass	 *mp;
        struct ggeom	 *gp;
        struct gprovider *pp;
	int		  result;

        /*
	 * The devdctl system doesn't replay events for new consumers
	 * of the interface.  Emit manufactured DEVFS arrival events
	 * for any devices that already before we started or during
	 * periods where we've lost our connection to devd.
         */
	result = geom_gettree(&mesh);
	if (result != 0) {
		syslog(LOG_ERR, "ZfsDaemon::RescanSystem: "
		       "geom_gettree faild with error %d\n", result);
		return;
	}

	const string evStart("!system=DEVFS subsystem=CDEV type=CREATE "
			     "sub_type=synthesized cdev=");
        LIST_FOREACH(mp, &mesh.lg_class, lg_class) {
                LIST_FOREACH(gp, &mp->lg_geom, lg_geom) {
                        LIST_FOREACH(pp, &gp->lg_provider, lg_provider) {
				Event *event;

				string evString(evStart + pp->lg_name + "\n");
				event = Event::CreateEvent(GetFactory(),
							   evString);
				if (event != NULL) {
					if (event->Process())
						SaveEvent(*event);
					delete event;
				}
                        }
                }
	}
	geom_deletetree(&mesh);
}

void
ZfsDaemon::DetectMissedEvents()
{
	do {
		PurgeCaseFiles();

		/*
		 * Discard any events waiting for us.  We don't know
		 * if they still apply to the current state of the
		 * system.
		 */
		FlushEvents();

		BuildCaseFiles();

		/*
		 * If the system state has changed during our
		 * interrogation, start over.
		 */
	} while (s_terminateEventLoop == false && EventsPending());

	RescanSystem();
}

void
ZfsDaemon::EventLoop()
{
	while (s_terminateEventLoop == false) {
		struct pollfd fds[2];
		int	      result;

		if (s_logCaseFiles == true) {
			EventList::iterator event(m_unconsumedEvents.begin());
			s_logCaseFiles = false;
			CaseFile::LogAll();
			while (event != m_unconsumedEvents.end())
				(*event++)->Log(LOG_INFO);
		}

		Callout::ExpireCallouts();

		/* Wait for data. */
		fds[0].fd      = m_devdSockFD;
		fds[0].events  = POLLIN;
		fds[0].revents = 0;
		fds[1].fd      = s_signalPipeFD[0];
		fds[1].events  = POLLIN;
		fds[1].revents = 0;
		result = poll(fds, NUM_ELEMENTS(fds), /*timeout*/INFTIM);
		if (result == -1) {
			if (errno == EINTR)
				continue;
			else
				err(1, "Polling for devd events failed");
		} else if (result == 0) {
			errx(1, "Unexpected result of 0 from poll. Exiting");
		}

		if ((fds[0].revents & POLLIN) != 0)
			ProcessEvents();

		if ((fds[1].revents & POLLIN) != 0) {
			static char discardBuf[128];

			/*
			 * This pipe exists just to close the signal
			 * race.  Its contents are of no interest to
			 * us, but we must ensure that future signals
			 * have space in the pipe to write.
			 */
			while (read(s_signalPipeFD[0], discardBuf,
				    sizeof(discardBuf)) > 0)
				;
		}

		if (s_systemRescanRequested == true) {
			s_systemRescanRequested = false;
			syslog(LOG_INFO, "System Rescan request processed.");
			RescanSystem();
		}

		if ((fds[0].revents & POLLERR) != 0) {
			syslog(LOG_INFO, "POLLERROR detected on devd socket.");
			break;
		}

		if ((fds[0].revents & POLLHUP) != 0) {
			syslog(LOG_INFO, "POLLHUP detected on devd socket.");
			break;
		}
	}
}
//- ZfsDaemon staic Private Methods --------------------------------------------
void
ZfsDaemon::InfoSignalHandler(int)
{
	s_logCaseFiles = true;
	ZfsDaemon::WakeEventLoop();
}

void
ZfsDaemon::RescanSignalHandler(int)
{
	RequestSystemRescan();
}

void
ZfsDaemon::QuitSignalHandler(int)
{
	s_terminateEventLoop = true;
	ZfsDaemon::WakeEventLoop();
}

void
ZfsDaemon::OpenPIDFile()
{
	pid_t otherPID;

	s_pidFH = pidfile_open(s_pidFilePath, 0600, &otherPID);
	if (s_pidFH == NULL) {
		if (errno == EEXIST)
			errx(1, "already running as PID %d. Exiting", otherPID);
		warn("cannot open PID file");
	}
}

void
ZfsDaemon::UpdatePIDFile()
{
	if (s_pidFH != NULL)
		pidfile_write(s_pidFH);
}

void
ZfsDaemon::ClosePIDFile()
{
	if (s_pidFH != NULL)
		pidfile_remove(s_pidFH);
}

void
ZfsDaemon::InitializeSyslog()
{
	openlog("zfsd", LOG_NDELAY, LOG_DAEMON);
}

