/*-
 * Copyright (c) 2011, 2012, 2013, 2014 Spectra Logic Corporation
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
 *
 * $FreeBSD$
 */

/**
 * \file zfsd.h
 *
 * Class definitions and supporting data strutures for the ZFS fault
 * management daemon.
 *
 * Header requirements:
 *
 *    #include <sys/fs/zfs.h>
 *
 *    #include <libzfs.h>
 *
 *    #include <list>
 *    #include <map>
 *    #include <string>
 *
 *    #include <devdctl/guid.h>
 *    #include <devdctl/event.h>
 *    #include <devdctl/event_factory.h>
 *    #include <devdctl/consumer.h>
 *
 *    #include "vdev_iterator.h"
 */
#ifndef	_ZFSD_H_
#define	_ZFSD_H_

/*=========================== Forward Declarations ===========================*/
struct pidfh;

struct zpool_handle;
typedef struct zpool_handle zpool_handle_t;

struct zfs_handle;
typedef struct libzfs_handle libzfs_handle_t;

struct nvlist;
typedef struct nvlist nvlist_t;

typedef int LeafIterFunc(zpool_handle_t *, nvlist_t *, void *);

/*================================ Global Data ===============================*/
extern int              g_debug;
extern libzfs_handle_t *g_zfsHandle;

/*============================= Class Definitions ============================*/
/*--------------------------------- ZfsDaemon --------------------------------*/
/**
 * Static singleton orchestrating the operations of the ZFS daemon program.
 */
class ZfsDaemon : public DevdCtl::Consumer
{
public:
	/** Return the ZfsDaemon singleton. */
	static ZfsDaemon &Get();

	/**
	 * Used by signal handlers to ensure, in a race free way, that
	 * the event loop will perform at least one more full loop
	 * before sleeping again.
	 */
	static void WakeEventLoop();

	/**
	 * Schedules a rescan of devices in the system for potential
	 * candidates to replace a missing vdev.  The scan is performed
	 * during the next run of the event loop.
	 */
	static void RequestSystemRescan();

	/** Daemonize and perform all functions of the ZFS daemon. */
	static void Run();

private:
	ZfsDaemon();
	~ZfsDaemon();

	static VdevCallback_t VdevAddCaseFile;

	/** Purge our cache of outstanding ZFS issues in the system. */
	void PurgeCaseFiles();

	/** Build a cache of outstanding ZFS issues in the system. */
	void BuildCaseFiles();

	/**
	 * Iterate over all known issues and attempt to solve them
	 * given resources currently available in the system.
	 */
	void RescanSystem();

	/**
	 * Interrogate the system looking for previously unknown
	 * faults that occurred either before ZFSD was started,
	 * or during a period of lost communication with Devd.
	 */
	void DetectMissedEvents();

	/**
	 * Wait for and process event source activity.
	 */
	void EventLoop();

	/**
	 * Signal handler for which our response is to
	 * log the current state of the daemon.
	 *
	 * \param sigNum  The signal caught.
	 */
	static void InfoSignalHandler(int sigNum);

	/**
	 * Signal handler for which our response is to
	 * request a case rescan.
	 *
	 * \param sigNum  The signal caught.
	 */
	static void RescanSignalHandler(int sigNum);

	/**
	 * Signal handler for which our response is to
	 * gracefully terminate.
	 *
	 * \param sigNum  The signal caught.
	 */
	static void QuitSignalHandler(int sigNum);

	/**
	 * Open and lock our PID file.
	 */
	static void OpenPIDFile();

	/**
	 * Update our PID file with our PID.
	 */
	static void UpdatePIDFile();

	/**
	 * Close and release the lock on our PID file.
	 */
	static void ClosePIDFile();

	/**
	 * Perform syslog configuration.
	 */
	static void InitializeSyslog();

	static ZfsDaemon		       *s_theZfsDaemon;

	/**
	 * Set to true when our program is signaled to
	 * gracefully exit.
	 */
	static bool				s_logCaseFiles;

	/**
	 * Set to true when our program is signaled to
	 * gracefully exit.
	 */
	static bool				s_terminateEventLoop;

	/**
	 * The canonical path and file name of zfsd's PID file.
	 */
	static char				s_pidFilePath[];

	/**
	 * Control structure for PIDFILE(3) API.
	 */
	static pidfh			       *s_pidFH;

	/**
	 * Pipe file descriptors used to close races with our
	 * signal handlers.
	 */
	static int				s_signalPipeFD[2];

	/**
	 * Flag controlling a rescan from ZFSD's event loop of all
	 * GEOM providers in the system to find candidates for solving
	 * cases.
	 */
	static bool				s_systemRescanRequested;

	/**
	 * Flag controlling whether events can be queued.  This boolean
	 * is set during event replay to ensure that events for pools or
	 * devices no longer in the system are not retained forever.
	 */
	static bool				s_consumingEvents;

	static DevdCtl::EventFactory::Record	s_registryEntries[];
};

#endif	/* _ZFSD_H_ */
