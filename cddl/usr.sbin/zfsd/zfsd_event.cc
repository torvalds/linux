/*-
 * Copyright (c) 2011, 2012, 2013, 2014, 2016 Spectra Logic Corporation
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
 * \file zfsd_event.cc
 */
#include <sys/cdefs.h>
#include <sys/time.h>
#include <sys/fs/zfs.h>
#include <sys/vdev_impl.h>

#include <syslog.h>

#include <libzfs.h>
/* 
 * Undefine flush, defined by cpufunc.h on sparc64, because it conflicts with
 * C++ flush methods
 */
#undef   flush

#include <list>
#include <map>
#include <sstream>
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
#include "zfsd.h"
#include "zfsd_exception.h"
#include "zpool_list.h"

__FBSDID("$FreeBSD$");
/*============================ Namespace Control =============================*/
using DevdCtl::Event;
using DevdCtl::Guid;
using DevdCtl::NVPairMap;
using std::stringstream;

/*=========================== Class Implementations ==========================*/

/*-------------------------------- GeomEvent --------------------------------*/

//- GeomEvent Static Public Methods -------------------------------------------
Event *
GeomEvent::Builder(Event::Type type,
		   NVPairMap &nvPairs,
		   const string &eventString)
{
	return (new GeomEvent(type, nvPairs, eventString));
}

//- GeomEvent Virtual Public Methods ------------------------------------------
Event *
GeomEvent::DeepCopy() const
{
	return (new GeomEvent(*this));
}
 
bool
GeomEvent::Process() const
{
	/*
	 * We only use GEOM events to repair damaged pools.  So return early if
	 * there are no damaged pools
	 */
	if (CaseFile::Empty())
		return (false);

	/*
	 * We are only concerned with arrivals and physical path changes,
	 * because those can be used to satisfy online and autoreplace
	 * operations
	 */
	if (Value("type") != "GEOM::physpath" && Value("type") != "CREATE")
		return (false);

	/* Log the event since it is of interest. */
	Log(LOG_INFO);

	string devPath;
	if (!DevPath(devPath))
		return (false);

	int devFd(open(devPath.c_str(), O_RDONLY));
	if (devFd == -1)
		return (false);

	bool inUse;
	bool degraded;
	nvlist_t *devLabel(ReadLabel(devFd, inUse, degraded));

	string physPath;
        bool havePhysPath(PhysicalPath(physPath));

	string devName;
	DevName(devName);
	close(devFd);

	if (inUse && devLabel != NULL) {
		OnlineByLabel(devPath, physPath, devLabel);
	} else if (degraded) {
		syslog(LOG_INFO, "%s is marked degraded.  Ignoring "
		       "as a replace by physical path candidate.\n",
		       devName.c_str());
	} else if (havePhysPath) {
		/* 
		 * TODO: attempt to resolve events using every casefile
		 * that matches this physpath
		 */
		CaseFile *caseFile(CaseFile::Find(physPath));
		if (caseFile != NULL) {
			syslog(LOG_INFO,
			       "Found CaseFile(%s:%s:%s) - ReEvaluating\n",
			       caseFile->PoolGUIDString().c_str(),
			       caseFile->VdevGUIDString().c_str(),
			       zpool_state_to_name(caseFile->VdevState(),
						   VDEV_AUX_NONE));
			caseFile->ReEvaluate(devPath, physPath, /*vdev*/NULL);
		}
	}
	return (false);
}

//- GeomEvent Protected Methods -----------------------------------------------
GeomEvent::GeomEvent(Event::Type type, NVPairMap &nvpairs,
			       const string &eventString)
 : DevdCtl::GeomEvent(type, nvpairs, eventString)
{
}

GeomEvent::GeomEvent(const GeomEvent &src)
 : DevdCtl::GeomEvent::GeomEvent(src)
{
}

nvlist_t *
GeomEvent::ReadLabel(int devFd, bool &inUse, bool &degraded)
{
	pool_state_t poolState;
	char        *poolName;
	boolean_t    b_inuse;
	int          nlabels;

	inUse    = false;
	degraded = false;
	poolName = NULL;
	if (zpool_in_use(g_zfsHandle, devFd, &poolState,
			 &poolName, &b_inuse) == 0) {
		nvlist_t *devLabel = NULL;

		inUse = b_inuse == B_TRUE;
		if (poolName != NULL)
			free(poolName);

		nlabels = zpool_read_all_labels(devFd, &devLabel);
		/*
		 * If we find a disk with fewer than the maximum number of
		 * labels, it might be the whole disk of a partitioned disk
		 * where ZFS resides on a partition.  In that case, we should do
		 * nothing and wait for the partition to appear.  Or, the disk
		 * might be damaged.  In that case, zfsd should do nothing and
		 * wait for the sysadmin to decide.
		 */
		if (nlabels != VDEV_LABELS || devLabel == NULL) {
			nvlist_free(devLabel);
			return (NULL);
		}

		try {
			Vdev vdev(devLabel);
			degraded = vdev.State() != VDEV_STATE_HEALTHY;
			return (devLabel);
		} catch (ZfsdException &exp) {
			string devName = fdevname(devFd);
			string devPath = _PATH_DEV + devName;
			string context("GeomEvent::ReadLabel: "
				     + devPath + ": ");

			exp.GetString().insert(0, context);
			exp.Log();
			nvlist_free(devLabel);
		}
	}
	return (NULL);
}

bool
GeomEvent::OnlineByLabel(const string &devPath, const string& physPath,
			      nvlist_t *devConfig)
{
	try {
		/*
		 * A device with ZFS label information has been
		 * inserted.  If it matches a device for which we
		 * have a case, see if we can solve that case.
		 */
		syslog(LOG_INFO, "Interrogating VDEV label for %s\n",
		       devPath.c_str());
		Vdev vdev(devConfig);
		CaseFile *caseFile(CaseFile::Find(vdev.PoolGUID(),
						  vdev.GUID()));
		if (caseFile != NULL)
			return (caseFile->ReEvaluate(devPath, physPath, &vdev));

	} catch (ZfsdException &exp) {
		string context("GeomEvent::OnlineByLabel: " + devPath + ": ");

		exp.GetString().insert(0, context);
		exp.Log();
	}
	return (false);
}


/*--------------------------------- ZfsEvent ---------------------------------*/
//- ZfsEvent Static Public Methods ---------------------------------------------
DevdCtl::Event *
ZfsEvent::Builder(Event::Type type, NVPairMap &nvpairs,
		  const string &eventString)
{
	return (new ZfsEvent(type, nvpairs, eventString));
}

//- ZfsEvent Virtual Public Methods --------------------------------------------
Event *
ZfsEvent::DeepCopy() const
{
	return (new ZfsEvent(*this));
}

bool
ZfsEvent::Process() const
{
	string logstr("");

	if (!Contains("class") && !Contains("type")) {
		syslog(LOG_ERR,
		       "ZfsEvent::Process: Missing class or type data.");
		return (false);
	}

	/* On config syncs, replay any queued events first. */
	if (Value("type").find("misc.fs.zfs.config_sync") == 0) {
		/*
		 * Even if saved events are unconsumed the second time
		 * around, drop them.  Any events that still can't be
		 * consumed are probably referring to vdevs or pools that
		 * no longer exist.
		 */
		ZfsDaemon::Get().ReplayUnconsumedEvents(/*discard*/true);
		CaseFile::ReEvaluateByGuid(PoolGUID(), *this);
	}

	if (Value("type").find("misc.fs.zfs.") == 0) {
		/* Configuration changes, resilver events, etc. */
		ProcessPoolEvent();
		return (false);
	}

	if (!Contains("pool_guid") || !Contains("vdev_guid")) {
		/* Only currently interested in Vdev related events. */
		return (false);
	}

	CaseFile *caseFile(CaseFile::Find(PoolGUID(), VdevGUID()));
	if (caseFile != NULL) {
		Log(LOG_INFO);
		syslog(LOG_INFO, "Evaluating existing case file\n");
		caseFile->ReEvaluate(*this);
		return (false);
	}

	/* Skip events that can't be handled. */
	Guid poolGUID(PoolGUID());
	/* If there are no replicas for a pool, then it's not manageable. */
	if (Value("class").find("fs.zfs.vdev.no_replicas") == 0) {
		stringstream msg;
		msg << "No replicas available for pool "  << poolGUID;
		msg << ", ignoring";
		Log(LOG_INFO);
		syslog(LOG_INFO, "%s", msg.str().c_str());
		return (false);
	}

	/*
	 * Create a case file for this vdev, and have it
	 * evaluate the event.
	 */
	ZpoolList zpl(ZpoolList::ZpoolByGUID, &poolGUID);
	if (zpl.empty()) {
		stringstream msg;
		int priority = LOG_INFO;
		msg << "ZfsEvent::Process: Event for unknown pool ";
		msg << poolGUID << " ";
		msg << "queued";
		Log(LOG_INFO);
		syslog(priority, "%s", msg.str().c_str());
		return (true);
	}

	nvlist_t *vdevConfig = VdevIterator(zpl.front()).Find(VdevGUID());
	if (vdevConfig == NULL) {
		stringstream msg;
		int priority = LOG_INFO;
		msg << "ZfsEvent::Process: Event for unknown vdev ";
		msg << VdevGUID() << " ";
		msg << "queued";
		Log(LOG_INFO);
		syslog(priority, "%s", msg.str().c_str());
		return (true);
	}

	Vdev vdev(zpl.front(), vdevConfig);
	caseFile = &CaseFile::Create(vdev);
	if (caseFile->ReEvaluate(*this) == false) {
		stringstream msg;
		int priority = LOG_INFO;
		msg << "ZfsEvent::Process: Unconsumed event for vdev(";
		msg << zpool_get_name(zpl.front()) << ",";
		msg << vdev.GUID() << ") ";
		msg << "queued";
		Log(LOG_INFO);
		syslog(priority, "%s", msg.str().c_str());
		return (true);
	}
	return (false);
}

//- ZfsEvent Protected Methods -------------------------------------------------
ZfsEvent::ZfsEvent(Event::Type type, NVPairMap &nvpairs,
			   const string &eventString)
 : DevdCtl::ZfsEvent(type, nvpairs, eventString)
{
}

ZfsEvent::ZfsEvent(const ZfsEvent &src)
 : DevdCtl::ZfsEvent(src)
{
}

/*
 * Sometimes the kernel won't detach a spare when it is no longer needed.  This
 * can happen for example if a drive is removed, then either the pool is
 * exported or the machine is powered off, then the drive is reinserted, then
 * the machine is powered on or the pool is imported.  ZFSD must detach these
 * spares itself.
 */
void
ZfsEvent::CleanupSpares() const
{
	Guid poolGUID(PoolGUID());
	ZpoolList zpl(ZpoolList::ZpoolByGUID, &poolGUID);
	if (!zpl.empty()) {
		zpool_handle_t* hdl;

		hdl = zpl.front();
		VdevIterator(hdl).Each(TryDetach, (void*)hdl);
	}
}

void
ZfsEvent::ProcessPoolEvent() const
{
	bool degradedDevice(false);

	/* The pool is destroyed.  Discard any open cases */
	if (Value("type") == "misc.fs.zfs.pool_destroy") {
		Log(LOG_INFO);
		CaseFile::ReEvaluateByGuid(PoolGUID(), *this);
		return;
	}

	CaseFile *caseFile(CaseFile::Find(PoolGUID(), VdevGUID()));
	if (caseFile != NULL) {
		if (caseFile->VdevState() != VDEV_STATE_UNKNOWN
		 && caseFile->VdevState() < VDEV_STATE_HEALTHY)
			degradedDevice = true;

		Log(LOG_INFO);
		caseFile->ReEvaluate(*this);
	}
	else if (Value("type") == "misc.fs.zfs.resilver_finish")
	{
		/*
		 * It's possible to get a resilver_finish event with no
		 * corresponding casefile.  For example, if a damaged pool were
		 * exported, repaired, then reimported.
		 */
		Log(LOG_INFO);
		CleanupSpares();
	}

	if (Value("type") == "misc.fs.zfs.vdev_remove"
	 && degradedDevice == false) {

		/* See if any other cases can make use of this device. */
		Log(LOG_INFO);
		ZfsDaemon::RequestSystemRescan();
	}
}

bool
ZfsEvent::TryDetach(Vdev &vdev, void *cbArg)
{
	/*
	 * Outline:
	 * if this device is a spare, and its parent includes one healthy,
	 * non-spare child, then detach this device.
	 */
	zpool_handle_t *hdl(static_cast<zpool_handle_t*>(cbArg));

	if (vdev.IsSpare()) {
		std::list<Vdev> siblings;
		std::list<Vdev>::iterator siblings_it;
		boolean_t cleanup = B_FALSE;

		Vdev parent = vdev.Parent();
		siblings = parent.Children();

		/* Determine whether the parent should be cleaned up */
		for (siblings_it = siblings.begin();
		     siblings_it != siblings.end();
		     siblings_it++) {
			Vdev sibling = *siblings_it;

			if (!sibling.IsSpare() &&
			     sibling.State() == VDEV_STATE_HEALTHY) {
				cleanup = B_TRUE;
				break;
			}
		}

		if (cleanup) {
			syslog(LOG_INFO, "Detaching spare vdev %s from pool %s",
			       vdev.Path().c_str(), zpool_get_name(hdl));
			zpool_vdev_detach(hdl, vdev.Path().c_str());
		}

	}

	/* Always return false, because there may be other spares to detach */
	return (false);
}
