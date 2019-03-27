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
 * \file case_file.cc
 *
 * We keep case files for any leaf vdev that is not in the optimal state.
 * However, we only serialize to disk those events that need to be preserved
 * across reboots.  For now, this is just a log of soft errors which we
 * accumulate in order to mark a device as degraded.
 */
#include <sys/cdefs.h>
#include <sys/time.h>

#include <sys/fs/zfs.h>

#include <dirent.h>
#include <iomanip>
#include <fstream>
#include <functional>
#include <sstream>
#include <syslog.h>
#include <unistd.h>

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
#include "zfsd.h"
#include "zfsd_exception.h"
#include "zpool_list.h"

__FBSDID("$FreeBSD$");

/*============================ Namespace Control =============================*/
using std::auto_ptr;
using std::hex;
using std::ifstream;
using std::stringstream;
using std::setfill;
using std::setw;

using DevdCtl::Event;
using DevdCtl::EventFactory;
using DevdCtl::EventList;
using DevdCtl::Guid;
using DevdCtl::ParseException;

/*--------------------------------- CaseFile ---------------------------------*/
//- CaseFile Static Data -------------------------------------------------------

CaseFileList  CaseFile::s_activeCases;
const string  CaseFile::s_caseFilePath = "/var/db/zfsd/cases";
const timeval CaseFile::s_removeGracePeriod = { 60 /*sec*/, 0 /*usec*/};

//- CaseFile Static Public Methods ---------------------------------------------
CaseFile *
CaseFile::Find(Guid poolGUID, Guid vdevGUID)
{
	for (CaseFileList::iterator curCase = s_activeCases.begin();
	     curCase != s_activeCases.end(); curCase++) {

		if (((*curCase)->PoolGUID() != poolGUID
		  && Guid::InvalidGuid() != poolGUID)
		 || (*curCase)->VdevGUID() != vdevGUID)
			continue;

		/*
		 * We only carry one active case per-vdev.
		 */
		return (*curCase);
	}
	return (NULL);
}

CaseFile *
CaseFile::Find(const string &physPath)
{
	CaseFile *result = NULL;

	for (CaseFileList::iterator curCase = s_activeCases.begin();
	     curCase != s_activeCases.end(); curCase++) {

		if ((*curCase)->PhysicalPath() != physPath)
			continue;

		if (result != NULL) {
			syslog(LOG_WARNING, "Multiple casefiles found for "
			    "physical path %s.  "
			    "This is most likely a bug in zfsd",
			    physPath.c_str());
		}
		result = *curCase;
	}
	return (result);
}


void
CaseFile::ReEvaluateByGuid(Guid poolGUID, const ZfsEvent &event)
{
	CaseFileList::iterator casefile;
	for (casefile = s_activeCases.begin(); casefile != s_activeCases.end();){
		CaseFileList::iterator next = casefile;
		next++;
		if (poolGUID == (*casefile)->PoolGUID())
			(*casefile)->ReEvaluate(event);
		casefile = next;
	}
}

CaseFile &
CaseFile::Create(Vdev &vdev)
{
	CaseFile *activeCase;

	activeCase = Find(vdev.PoolGUID(), vdev.GUID());
	if (activeCase == NULL)
		activeCase = new CaseFile(vdev);

	return (*activeCase);
}

void
CaseFile::DeSerialize()
{
	struct dirent **caseFiles;

	int numCaseFiles(scandir(s_caseFilePath.c_str(), &caseFiles,
			 DeSerializeSelector, /*compar*/NULL));

	if (numCaseFiles == -1)
		return;
	if (numCaseFiles == 0) {
		free(caseFiles);
		return;
	}

	for (int i = 0; i < numCaseFiles; i++) {

		DeSerializeFile(caseFiles[i]->d_name);
		free(caseFiles[i]);
	}
	free(caseFiles);
}

bool
CaseFile::Empty()
{
	return (s_activeCases.empty());
}

void
CaseFile::LogAll()
{
	for (CaseFileList::iterator curCase = s_activeCases.begin();
	     curCase != s_activeCases.end(); curCase++)
		(*curCase)->Log();
}

void
CaseFile::PurgeAll()
{
	/*
	 * Serialize casefiles before deleting them so that they can be reread
	 * and revalidated during BuildCaseFiles.
	 * CaseFiles remove themselves from this list on destruction.
	 */
	while (s_activeCases.size() != 0) {
		CaseFile *casefile = s_activeCases.front();
		casefile->Serialize();
		delete casefile;
	}

}

//- CaseFile Public Methods ----------------------------------------------------
bool
CaseFile::RefreshVdevState()
{
	ZpoolList zpl(ZpoolList::ZpoolByGUID, &m_poolGUID);
	zpool_handle_t *casePool(zpl.empty() ? NULL : zpl.front());
	if (casePool == NULL)
		return (false);

	Vdev vd(casePool, CaseVdev(casePool));
	if (vd.DoesNotExist())
		return (false);

	m_vdevState    = vd.State();
	m_vdevPhysPath = vd.PhysicalPath();
	return (true);
}

bool
CaseFile::ReEvaluate(const string &devPath, const string &physPath, Vdev *vdev)
{
	ZpoolList zpl(ZpoolList::ZpoolByGUID, &m_poolGUID);
	zpool_handle_t *pool(zpl.empty() ? NULL : zpl.front());
	zpool_boot_label_t boot_type;
	uint64_t boot_size;

	if (pool == NULL || !RefreshVdevState()) {
		/*
		 * The pool or vdev for this case file is no longer
		 * part of the configuration.  This can happen
		 * if we process a device arrival notification
		 * before seeing the ZFS configuration change
		 * event.
		 */
		syslog(LOG_INFO,
		       "CaseFile::ReEvaluate(%s,%s) Pool/Vdev unconfigured.  "
		       "Closing\n",
		       PoolGUIDString().c_str(),
		       VdevGUIDString().c_str());
		Close();

		/*
		 * Since this event was not used to close this
		 * case, do not report it as consumed.
		 */
		return (/*consumed*/false);
	}

	if (VdevState() > VDEV_STATE_CANT_OPEN) {
		/*
		 * For now, newly discovered devices only help for
		 * devices that are missing.  In the future, we might
		 * use a newly inserted spare to replace a degraded
		 * or faulted device.
		 */
		syslog(LOG_INFO, "CaseFile::ReEvaluate(%s,%s): Pool/Vdev ignored",
		    PoolGUIDString().c_str(), VdevGUIDString().c_str());
		return (/*consumed*/false);
	}

	if (vdev != NULL
	 && ( vdev->PoolGUID() == m_poolGUID
	   || vdev->PoolGUID() == Guid::InvalidGuid())
	 && vdev->GUID() == m_vdevGUID) {

		zpool_vdev_online(pool, vdev->GUIDString().c_str(),
				  ZFS_ONLINE_CHECKREMOVE | ZFS_ONLINE_UNSPARE,
				  &m_vdevState);
		syslog(LOG_INFO, "Onlined vdev(%s/%s:%s).  State now %s.\n",
		       zpool_get_name(pool), vdev->GUIDString().c_str(),
		       devPath.c_str(),
		       zpool_state_to_name(VdevState(), VDEV_AUX_NONE));

		/*
		 * Check the vdev state post the online action to see
		 * if we can retire this case.
		 */
		CloseIfSolved();

		return (/*consumed*/true);
	}

	/*
	 * If the auto-replace policy is enabled, and we have physical
	 * path information, try a physical path replacement.
	 */
	if (zpool_get_prop_int(pool, ZPOOL_PROP_AUTOREPLACE, NULL) == 0) {
		syslog(LOG_INFO,
		       "CaseFile(%s:%s:%s): AutoReplace not set.  "
		       "Ignoring device insertion.\n",
		       PoolGUIDString().c_str(),
		       VdevGUIDString().c_str(),
		       zpool_state_to_name(VdevState(), VDEV_AUX_NONE));
		return (/*consumed*/false);
	}

	if (PhysicalPath().empty()) {
		syslog(LOG_INFO,
		       "CaseFile(%s:%s:%s): No physical path information.  "
		       "Ignoring device insertion.\n",
		       PoolGUIDString().c_str(),
		       VdevGUIDString().c_str(),
		       zpool_state_to_name(VdevState(), VDEV_AUX_NONE));
		return (/*consumed*/false);
	}

	if (physPath != PhysicalPath()) {
		syslog(LOG_INFO,
		       "CaseFile(%s:%s:%s): Physical path mismatch.  "
		       "Ignoring device insertion.\n",
		       PoolGUIDString().c_str(),
		       VdevGUIDString().c_str(),
		       zpool_state_to_name(VdevState(), VDEV_AUX_NONE));
		return (/*consumed*/false);
	}

	/* Write a label on the newly inserted disk. */
	if (zpool_is_bootable(pool))
		boot_type = ZPOOL_COPY_BOOT_LABEL;
	else
		boot_type = ZPOOL_NO_BOOT_LABEL;
	boot_size = zpool_get_prop_int(pool, ZPOOL_PROP_BOOTSIZE, NULL);
	if (zpool_label_disk(g_zfsHandle, pool, devPath.c_str(),
	    boot_type, boot_size, NULL) != 0) {
		syslog(LOG_ERR,
		       "Replace vdev(%s/%s) by physical path (label): %s: %s\n",
		       zpool_get_name(pool), VdevGUIDString().c_str(),
		       libzfs_error_action(g_zfsHandle),
		       libzfs_error_description(g_zfsHandle));
		return (/*consumed*/false);
	}

	syslog(LOG_INFO, "CaseFile::ReEvaluate(%s/%s): Replacing with %s",
	    PoolGUIDString().c_str(), VdevGUIDString().c_str(),
	    devPath.c_str());
	return (Replace(VDEV_TYPE_DISK, devPath.c_str(), /*isspare*/false));
}

bool
CaseFile::ReEvaluate(const ZfsEvent &event)
{
	bool consumed(false);

	if (event.Value("type") == "misc.fs.zfs.vdev_remove") {
		/*
		 * The Vdev we represent has been removed from the
		 * configuration.  This case is no longer of value.
		 */
		Close();

		return (/*consumed*/true);
	} else if (event.Value("type") == "misc.fs.zfs.pool_destroy") {
		/* This Pool has been destroyed.  Discard the case */
		Close();

		return (/*consumed*/true);
	} else if (event.Value("type") == "misc.fs.zfs.config_sync") {
		RefreshVdevState();
		if (VdevState() < VDEV_STATE_HEALTHY)
			consumed = ActivateSpare();
	}


	if (event.Value("class") == "resource.fs.zfs.removed") {
		bool spare_activated;

		if (!RefreshVdevState()) {
			/*
			 * The pool or vdev for this case file is no longer
			 * part of the configuration.  This can happen
			 * if we process a device arrival notification
			 * before seeing the ZFS configuration change
			 * event.
			 */
			syslog(LOG_INFO,
			       "CaseFile::ReEvaluate(%s,%s) Pool/Vdev "
			       "unconfigured.  Closing\n",
			       PoolGUIDString().c_str(),
			       VdevGUIDString().c_str());
			/*
			 * Close the case now so we won't waste cycles in the
			 * system rescan
			 */
			Close();

			/*
			 * Since this event was not used to close this
			 * case, do not report it as consumed.
			 */
			return (/*consumed*/false);
		}

		/*
		 * Discard any tentative I/O error events for
		 * this case.  They were most likely caused by the
		 * hot-unplug of this device.
		 */
		PurgeTentativeEvents();

		/* Try to activate spares if they are available */
		spare_activated = ActivateSpare();

		/*
		 * Rescan the drives in the system to see if a recent
		 * drive arrival can be used to solve this case.
		 */
		ZfsDaemon::RequestSystemRescan();

		/*
		 * Consume the event if we successfully activated a spare.
		 * Otherwise, leave it in the unconsumed events list so that the
		 * future addition of a spare to this pool might be able to
		 * close the case
		 */
		consumed = spare_activated;
	} else if (event.Value("class") == "resource.fs.zfs.statechange") {
		RefreshVdevState();
		/*
		 * If this vdev is DEGRADED, FAULTED, or UNAVAIL, try to
		 * activate a hotspare.  Otherwise, ignore the event
		 */
		if (VdevState() == VDEV_STATE_FAULTED ||
		    VdevState() == VDEV_STATE_DEGRADED ||
		    VdevState() == VDEV_STATE_CANT_OPEN)
			(void) ActivateSpare();
		consumed = true;
	}
	else if (event.Value("class") == "ereport.fs.zfs.io" ||
	         event.Value("class") == "ereport.fs.zfs.checksum") {

		m_tentativeEvents.push_front(event.DeepCopy());
		RegisterCallout(event);
		consumed = true;
	}

	bool closed(CloseIfSolved());

	return (consumed || closed);
}

/* Find a Vdev containing the vdev with the given GUID */
static nvlist_t*
find_parent(nvlist_t *pool_config, nvlist_t *config, DevdCtl::Guid child_guid)
{
	nvlist_t **vdevChildren;
	int        error;
	unsigned   ch, numChildren;

	error = nvlist_lookup_nvlist_array(config, ZPOOL_CONFIG_CHILDREN,
					   &vdevChildren, &numChildren);

	if (error != 0 || numChildren == 0)
		return (NULL);

	for (ch = 0; ch < numChildren; ch++) {
		nvlist *result;
		Vdev vdev(pool_config, vdevChildren[ch]);

		if (vdev.GUID() == child_guid)
			return (config);

		result = find_parent(pool_config, vdevChildren[ch], child_guid);
		if (result != NULL)
			return (result);
	}

	return (NULL);
}

bool
CaseFile::ActivateSpare() {
	nvlist_t	*config, *nvroot, *parent_config;
	nvlist_t       **spares;
	char		*devPath, *vdev_type;
	const char	*poolname;
	u_int		 nspares, i;
	int		 error;

	ZpoolList zpl(ZpoolList::ZpoolByGUID, &m_poolGUID);
	zpool_handle_t	*zhp(zpl.empty() ? NULL : zpl.front());
	if (zhp == NULL) {
		syslog(LOG_ERR, "CaseFile::ActivateSpare: Could not find pool "
		       "for pool_guid %" PRIu64".", (uint64_t)m_poolGUID);
		return (false);
	}
	poolname = zpool_get_name(zhp);
	config = zpool_get_config(zhp, NULL);
	if (config == NULL) {
		syslog(LOG_ERR, "CaseFile::ActivateSpare: Could not find pool "
		       "config for pool %s", poolname);
		return (false);
	}
	error = nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &nvroot);
	if (error != 0){
		syslog(LOG_ERR, "CaseFile::ActivateSpare: Could not find vdev "
		       "tree for pool %s", poolname);
		return (false);
	}

	parent_config = find_parent(config, nvroot, m_vdevGUID);
	if (parent_config != NULL) {
		char *parent_type;

		/* 
		 * Don't activate spares for members of a "replacing" vdev.
		 * They're already dealt with.  Sparing them will just drag out
		 * the resilver process.
		 */
		error = nvlist_lookup_string(parent_config,
		    ZPOOL_CONFIG_TYPE, &parent_type);
		if (error == 0 && strcmp(parent_type, VDEV_TYPE_REPLACING) == 0)
			return (false);
	}

	nspares = 0;
	nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES, &spares,
				   &nspares);
	if (nspares == 0) {
		/* The pool has no spares configured */
		syslog(LOG_INFO, "CaseFile::ActivateSpare: "
		       "No spares available for pool %s", poolname);
		return (false);
	}
	for (i = 0; i < nspares; i++) {
		uint64_t    *nvlist_array;
		vdev_stat_t *vs;
		uint_t	     nstats;

		if (nvlist_lookup_uint64_array(spares[i],
		    ZPOOL_CONFIG_VDEV_STATS, &nvlist_array, &nstats) != 0) {
			syslog(LOG_ERR, "CaseFile::ActivateSpare: Could not "
			       "find vdev stats for pool %s, spare %d",
			       poolname, i);
			return (false);
		}
		vs = reinterpret_cast<vdev_stat_t *>(nvlist_array);

		if ((vs->vs_aux != VDEV_AUX_SPARED)
		 && (vs->vs_state == VDEV_STATE_HEALTHY)) {
			/* We found a usable spare */
			break;
		}
	}

	if (i == nspares) {
		/* No available spares were found */
		return (false);
	}

	error = nvlist_lookup_string(spares[i], ZPOOL_CONFIG_PATH, &devPath);
	if (error != 0) {
		syslog(LOG_ERR, "CaseFile::ActivateSpare: Cannot determine "
		       "the path of pool %s, spare %d. Error %d",
		       poolname, i, error);
		return (false);
	}

	error = nvlist_lookup_string(spares[i], ZPOOL_CONFIG_TYPE, &vdev_type);
	if (error != 0) {
		syslog(LOG_ERR, "CaseFile::ActivateSpare: Cannot determine "
		       "the vdev type of pool %s, spare %d. Error %d",
		       poolname, i, error);
		return (false);
	}

	return (Replace(vdev_type, devPath, /*isspare*/true));
}

void
CaseFile::RegisterCallout(const Event &event)
{
	timeval now, countdown, elapsed, timestamp, zero, remaining;

	gettimeofday(&now, 0);
	timestamp = event.GetTimestamp();
	timersub(&now, &timestamp, &elapsed);
	timersub(&s_removeGracePeriod, &elapsed, &countdown);
	/*
	 * If countdown is <= zero, Reset the timer to the
	 * smallest positive time value instead
	 */
	timerclear(&zero);
	if (timercmp(&countdown, &zero, <=)) {
		timerclear(&countdown);
		countdown.tv_usec = 1;
	}

	remaining = m_tentativeTimer.TimeRemaining();

	if (!m_tentativeTimer.IsPending()
	 || timercmp(&countdown, &remaining, <))
		m_tentativeTimer.Reset(countdown, OnGracePeriodEnded, this);
}


bool
CaseFile::CloseIfSolved()
{
	if (m_events.empty()
	 && m_tentativeEvents.empty()) {

		/*
		 * We currently do not track or take actions on
		 * devices in the degraded or faulted state.
		 * Once we have support for spare pools, we'll
		 * retain these cases so that any spares added in
		 * the future can be applied to them.
		 */
		switch (VdevState()) {
		case VDEV_STATE_HEALTHY:
			/* No need to keep cases for healthy vdevs */
			Close();
			return (true);
		case VDEV_STATE_REMOVED:
		case VDEV_STATE_CANT_OPEN:
			/*
			 * Keep open.  We may solve it with a newly inserted
			 * device.
			 */
		case VDEV_STATE_FAULTED:
		case VDEV_STATE_DEGRADED:
			/*
			 * Keep open.  We may solve it with the future
			 * addition of a spare to the pool
			 */
		case VDEV_STATE_UNKNOWN:
		case VDEV_STATE_CLOSED:
		case VDEV_STATE_OFFLINE:
			/*
			 * Keep open?  This may not be the correct behavior,
			 * but it's what we've always done
			 */
			;
		}

		/*
		 * Re-serialize the case in order to remove any
		 * previous event data.
		 */
		Serialize();
	}

	return (false);
}

void
CaseFile::Log()
{
	syslog(LOG_INFO, "CaseFile(%s,%s,%s)\n", PoolGUIDString().c_str(),
	       VdevGUIDString().c_str(), PhysicalPath().c_str());
	syslog(LOG_INFO, "\tVdev State = %s\n",
	       zpool_state_to_name(VdevState(), VDEV_AUX_NONE));
	if (m_tentativeEvents.size() != 0) {
		syslog(LOG_INFO, "\t=== Tentative Events ===\n");
		for (EventList::iterator event(m_tentativeEvents.begin());
		     event != m_tentativeEvents.end(); event++)
			(*event)->Log(LOG_INFO);
	}
	if (m_events.size() != 0) {
		syslog(LOG_INFO, "\t=== Events ===\n");
		for (EventList::iterator event(m_events.begin());
		     event != m_events.end(); event++)
			(*event)->Log(LOG_INFO);
	}
}

//- CaseFile Static Protected Methods ------------------------------------------
void
CaseFile::OnGracePeriodEnded(void *arg)
{
	CaseFile &casefile(*static_cast<CaseFile *>(arg));

	casefile.OnGracePeriodEnded();
}

int
CaseFile::DeSerializeSelector(const struct dirent *dirEntry)
{
	uint64_t poolGUID;
	uint64_t vdevGUID;

	if (dirEntry->d_type == DT_REG
	 && sscanf(dirEntry->d_name, "pool_%" PRIu64 "_vdev_%" PRIu64 ".case",
		   &poolGUID, &vdevGUID) == 2)
		return (1);
	return (0);
}

void
CaseFile::DeSerializeFile(const char *fileName)
{
	string	  fullName(s_caseFilePath + '/' + fileName);
	CaseFile *existingCaseFile(NULL);
	CaseFile *caseFile(NULL);

	try {
		uint64_t poolGUID;
		uint64_t vdevGUID;
		nvlist_t *vdevConf;

		if (sscanf(fileName, "pool_%" PRIu64 "_vdev_%" PRIu64 ".case",
		       &poolGUID, &vdevGUID) != 2) {
			throw ZfsdException("CaseFile::DeSerialize: "
			    "Unintelligible CaseFile filename %s.\n", fileName);
		}
		existingCaseFile = Find(Guid(poolGUID), Guid(vdevGUID));
		if (existingCaseFile != NULL) {
			/*
			 * If the vdev is already degraded or faulted,
			 * there's no point in keeping the state around
			 * that we use to put a drive into the degraded
			 * state.  However, if the vdev is simply missing,
			 * preserve the case data in the hopes that it will
			 * return.
			 */
			caseFile = existingCaseFile;
			vdev_state curState(caseFile->VdevState());
			if (curState > VDEV_STATE_CANT_OPEN
			 && curState < VDEV_STATE_HEALTHY) {
				unlink(fileName);
				return;
			}
		} else {
			ZpoolList zpl(ZpoolList::ZpoolByGUID, &poolGUID);
			if (zpl.empty()
			 || (vdevConf = VdevIterator(zpl.front())
						    .Find(vdevGUID)) == NULL) {
				/*
				 * Either the pool no longer exists
				 * or this vdev is no longer a member of
				 * the pool.
				 */
				unlink(fullName.c_str());
				return;
			}

			/*
			 * Any vdev we find that does not have a case file
			 * must be in the healthy state and thus worthy of
			 * continued SERD data tracking.
			 */
			caseFile = new CaseFile(Vdev(zpl.front(), vdevConf));
		}

		ifstream caseStream(fullName.c_str());
		if (!caseStream)
			throw ZfsdException("CaseFile::DeSerialize: Unable to "
					    "read %s.\n", fileName);

		caseFile->DeSerialize(caseStream);
	} catch (const ParseException &exp) {

		exp.Log();
		if (caseFile != existingCaseFile)
			delete caseFile;

		/*
		 * Since we can't parse the file, unlink it so we don't
		 * trip over it again.
		 */
		unlink(fileName);
	} catch (const ZfsdException &zfsException) {

		zfsException.Log();
		if (caseFile != existingCaseFile)
			delete caseFile;
	}
}

//- CaseFile Protected Methods -------------------------------------------------
CaseFile::CaseFile(const Vdev &vdev)
 : m_poolGUID(vdev.PoolGUID()),
   m_vdevGUID(vdev.GUID()),
   m_vdevState(vdev.State()),
   m_vdevPhysPath(vdev.PhysicalPath())
{
	stringstream guidString;

	guidString << m_vdevGUID;
	m_vdevGUIDString = guidString.str();
	guidString.str("");
	guidString << m_poolGUID;
	m_poolGUIDString = guidString.str();

	s_activeCases.push_back(this);

	syslog(LOG_INFO, "Creating new CaseFile:\n");
	Log();
}

CaseFile::~CaseFile()
{
	PurgeEvents();
	PurgeTentativeEvents();
	m_tentativeTimer.Stop();
	s_activeCases.remove(this);
}

void
CaseFile::PurgeEvents()
{
	for (EventList::iterator event(m_events.begin());
	     event != m_events.end(); event++)
		delete *event;

	m_events.clear();
}

void
CaseFile::PurgeTentativeEvents()
{
	for (EventList::iterator event(m_tentativeEvents.begin());
	     event != m_tentativeEvents.end(); event++)
		delete *event;

	m_tentativeEvents.clear();
}

void
CaseFile::SerializeEvList(const EventList events, int fd,
		const char* prefix) const
{
	if (events.empty())
		return;
	for (EventList::const_iterator curEvent = events.begin();
	     curEvent != events.end(); curEvent++) {
		const string &eventString((*curEvent)->GetEventString());

		// TODO: replace many write(2) calls with a single writev(2)
		if (prefix)
			write(fd, prefix, strlen(prefix));
		write(fd, eventString.c_str(), eventString.length());
	}
}

void
CaseFile::Serialize()
{
	stringstream saveFile;

	saveFile << setfill('0')
		 << s_caseFilePath << "/"
		 << "pool_" << PoolGUIDString()
		 << "_vdev_" << VdevGUIDString()
		 << ".case";

	if (m_events.empty() && m_tentativeEvents.empty()) {
		unlink(saveFile.str().c_str());
		return;
	}

	int fd(open(saveFile.str().c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0644));
	if (fd == -1) {
		syslog(LOG_ERR, "CaseFile::Serialize: Unable to open %s.\n",
		       saveFile.str().c_str());
		return;
	}
	SerializeEvList(m_events, fd);
	SerializeEvList(m_tentativeEvents, fd, "tentative ");
	close(fd);
}

/*
 * XXX: This method assumes that events may not contain embedded newlines.  If
 * ever events can contain embedded newlines, then CaseFile must switch
 * serialization formats
 */
void
CaseFile::DeSerialize(ifstream &caseStream)
{
	string	      evString;
	const EventFactory &factory(ZfsDaemon::Get().GetFactory());

	caseStream >> std::noskipws >> std::ws;
	while (caseStream.good()) {
		/*
		 * Outline:
		 * read the beginning of a line and check it for
		 * "tentative".  If found, discard "tentative".
		 * Create a new event
		 * continue
		 */
		EventList* destEvents;
		const string tentFlag("tentative ");
		string line;
		std::stringbuf lineBuf;

		caseStream.get(lineBuf);
		caseStream.ignore();  /*discard the newline character*/
		line = lineBuf.str();
		if (line.compare(0, tentFlag.size(), tentFlag) == 0) {
			/* Discard "tentative" */
			line.erase(0, tentFlag.size());
			destEvents = &m_tentativeEvents;
		} else {
			destEvents = &m_events;
		}
		Event *event(Event::CreateEvent(factory, line));
		if (event != NULL) {
			destEvents->push_back(event);
			RegisterCallout(*event);
		}
	}
}

void
CaseFile::Close()
{
	/*
	 * This case is no longer relevant.  Clean up our
	 * serialization file, and delete the case.
	 */
	syslog(LOG_INFO, "CaseFile(%s,%s) closed - State %s\n",
	       PoolGUIDString().c_str(), VdevGUIDString().c_str(),
	       zpool_state_to_name(VdevState(), VDEV_AUX_NONE));

	/*
	 * Serialization of a Case with no event data, clears the
	 * Serialization data for that event.
	 */
	PurgeEvents();
	Serialize();

	delete this;
}

void
CaseFile::OnGracePeriodEnded()
{
	bool should_fault, should_degrade;
	ZpoolList zpl(ZpoolList::ZpoolByGUID, &m_poolGUID);
	zpool_handle_t *zhp(zpl.empty() ? NULL : zpl.front());

	m_events.splice(m_events.begin(), m_tentativeEvents);
	should_fault = ShouldFault();
	should_degrade = ShouldDegrade();

	if (should_fault || should_degrade) {
		if (zhp == NULL
		 || (VdevIterator(zhp).Find(m_vdevGUID)) == NULL) {
			/*
			 * Either the pool no longer exists
			 * or this vdev is no longer a member of
			 * the pool.
			 */
			Close();
			return;
		}

	}

	/* A fault condition has priority over a degrade condition */
	if (ShouldFault()) {
		/* Fault the vdev and close the case. */
		if (zpool_vdev_fault(zhp, (uint64_t)m_vdevGUID,
				       VDEV_AUX_ERR_EXCEEDED) == 0) {
			syslog(LOG_INFO, "Faulting vdev(%s/%s)",
			       PoolGUIDString().c_str(),
			       VdevGUIDString().c_str());
			Close();
			return;
		}
		else {
			syslog(LOG_ERR, "Fault vdev(%s/%s): %s: %s\n",
			       PoolGUIDString().c_str(),
			       VdevGUIDString().c_str(),
			       libzfs_error_action(g_zfsHandle),
			       libzfs_error_description(g_zfsHandle));
		}
	}
	else if (ShouldDegrade()) {
		/* Degrade the vdev and close the case. */
		if (zpool_vdev_degrade(zhp, (uint64_t)m_vdevGUID,
				       VDEV_AUX_ERR_EXCEEDED) == 0) {
			syslog(LOG_INFO, "Degrading vdev(%s/%s)",
			       PoolGUIDString().c_str(),
			       VdevGUIDString().c_str());
			Close();
			return;
		}
		else {
			syslog(LOG_ERR, "Degrade vdev(%s/%s): %s: %s\n",
			       PoolGUIDString().c_str(),
			       VdevGUIDString().c_str(),
			       libzfs_error_action(g_zfsHandle),
			       libzfs_error_description(g_zfsHandle));
		}
	}
	Serialize();
}

Vdev
CaseFile::BeingReplacedBy(zpool_handle_t *zhp) {
	Vdev vd(zhp, CaseVdev(zhp));
	std::list<Vdev> children;
	std::list<Vdev>::iterator children_it;

	Vdev parent(vd.Parent());
	Vdev replacing(NonexistentVdev);

	/*
	 * To determine whether we are being replaced by another spare that
	 * is still working, then make sure that it is currently spared and
	 * that the spare is either resilvering or healthy.  If any of these
	 * conditions fail, then we are not being replaced by a spare.
	 *
	 * If the spare is healthy, then the case file should be closed very
	 * soon after this check.
	 */
	if (parent.DoesNotExist()
	 || parent.Name(zhp, /*verbose*/false) != "spare")
		return (NonexistentVdev);

	children = parent.Children();
	children_it = children.begin();
	for (;children_it != children.end(); children_it++) {
		Vdev child = *children_it;

		/* Skip our vdev. */
		if (child.GUID() == VdevGUID())
			continue;
		/*
		 * Accept the first child that doesn't match our GUID, or
		 * any resilvering/healthy device if one exists.
		 */
		if (replacing.DoesNotExist() || child.IsResilvering()
		 || child.State() == VDEV_STATE_HEALTHY)
			replacing = child;
	}

	return (replacing);
}

bool
CaseFile::Replace(const char* vdev_type, const char* path, bool isspare) {
	nvlist_t *nvroot, *newvd;
	const char *poolname;
	string oldstr(VdevGUIDString());
	bool retval = true;

	/* Figure out what pool we're working on */
	ZpoolList zpl(ZpoolList::ZpoolByGUID, &m_poolGUID);
	zpool_handle_t *zhp(zpl.empty() ? NULL : zpl.front());
	if (zhp == NULL) {
		syslog(LOG_ERR, "CaseFile::Replace: could not find pool for "
		       "pool_guid %" PRIu64 ".", (uint64_t)m_poolGUID);
		return (false);
	}
	poolname = zpool_get_name(zhp);
	Vdev vd(zhp, CaseVdev(zhp));
	Vdev replaced(BeingReplacedBy(zhp));

	if (isspare && !vd.IsSpare() && !replaced.DoesNotExist()) {
		/* If we are already being replaced by a working spare, pass. */
		if (replaced.IsResilvering()
		 || replaced.State() == VDEV_STATE_HEALTHY) {
			syslog(LOG_INFO, "CaseFile::Replace(%s->%s): already "
			    "replaced", VdevGUIDString().c_str(), path);
			return (/*consumed*/false);
		}
		/*
		 * If we have already been replaced by a spare, but that spare
		 * is broken, we must spare the spare, not the original device.
		 */
		oldstr = replaced.GUIDString();
		syslog(LOG_INFO, "CaseFile::Replace(%s->%s): sparing "
		    "broken spare %s instead", VdevGUIDString().c_str(),
		    path, oldstr.c_str());
	}

	/*
	 * Build a root vdev/leaf vdev configuration suitable for
	 * zpool_vdev_attach. Only enough data for the kernel to find
	 * the device (i.e. type and disk device node path) are needed.
	 */
	nvroot = NULL;
	newvd = NULL;

	if (nvlist_alloc(&nvroot, NV_UNIQUE_NAME, 0) != 0
	 || nvlist_alloc(&newvd, NV_UNIQUE_NAME, 0) != 0) {
		syslog(LOG_ERR, "Replace vdev(%s/%s): Unable to allocate "
		    "configuration data.", poolname, oldstr.c_str());
		if (nvroot != NULL)
			nvlist_free(nvroot);
		return (false);
	}
	if (nvlist_add_string(newvd, ZPOOL_CONFIG_TYPE, vdev_type) != 0
	 || nvlist_add_string(newvd, ZPOOL_CONFIG_PATH, path) != 0
	 || nvlist_add_string(nvroot, ZPOOL_CONFIG_TYPE, VDEV_TYPE_ROOT) != 0
	 || nvlist_add_nvlist_array(nvroot, ZPOOL_CONFIG_CHILDREN,
				    &newvd, 1) != 0) {
		syslog(LOG_ERR, "Replace vdev(%s/%s): Unable to initialize "
		    "configuration data.", poolname, oldstr.c_str());
		nvlist_free(newvd);
		nvlist_free(nvroot);
		return (true);
	}

	/* Data was copied when added to the root vdev. */
	nvlist_free(newvd);

	retval = (zpool_vdev_attach(zhp, oldstr.c_str(), path, nvroot,
	    /*replace*/B_TRUE) == 0);
	if (retval)
		syslog(LOG_INFO, "Replacing vdev(%s/%s) with %s\n",
		    poolname, oldstr.c_str(), path);
	else
		syslog(LOG_ERR, "Replace vdev(%s/%s): %s: %s\n",
		    poolname, oldstr.c_str(), libzfs_error_action(g_zfsHandle),
		    libzfs_error_description(g_zfsHandle));
	nvlist_free(nvroot);

	return (retval);
}

/* Does the argument event refer to a checksum error? */
static bool
IsChecksumEvent(const Event* const event)
{
	return ("ereport.fs.zfs.checksum" == event->Value("type"));
}

/* Does the argument event refer to an IO error? */
static bool
IsIOEvent(const Event* const event)
{
	return ("ereport.fs.zfs.io" == event->Value("type"));
}

bool
CaseFile::ShouldDegrade() const
{
	return (std::count_if(m_events.begin(), m_events.end(),
			      IsChecksumEvent) > ZFS_DEGRADE_IO_COUNT);
}

bool
CaseFile::ShouldFault() const
{
	return (std::count_if(m_events.begin(), m_events.end(),
			      IsIOEvent) > ZFS_DEGRADE_IO_COUNT);
}

nvlist_t *
CaseFile::CaseVdev(zpool_handle_t *zhp) const
{
	return (VdevIterator(zhp).Find(VdevGUID()));
}
