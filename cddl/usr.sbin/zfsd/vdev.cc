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
 * \file vdev.cc
 *
 * Implementation of the Vdev class.
 */
#include <syslog.h>
#include <sys/cdefs.h>
#include <sys/fs/zfs.h>

#include <libzfs.h>
/* 
 * Undefine flush, defined by cpufunc.h on sparc64, because it conflicts with
 * C++ flush methods
 */
#undef   flush

#include <list>
#include <map>
#include <string>
#include <sstream>

#include <devdctl/guid.h>
#include <devdctl/event.h>
#include <devdctl/event_factory.h>
#include <devdctl/exception.h>
#include <devdctl/consumer.h>

#include "vdev.h"
#include "vdev_iterator.h"
#include "zfsd.h"
#include "zfsd_exception.h"
#include "zpool_list.h"

__FBSDID("$FreeBSD$");
/*============================ Namespace Control =============================*/
using std::string;
using std::stringstream;

//- Special objects -----------------------------------------------------------
Vdev NonexistentVdev;

//- Vdev Inline Public Methods ------------------------------------------------
/*=========================== Class Implementations ==========================*/
/*----------------------------------- Vdev -----------------------------------*/

/* Special constructor for NonexistentVdev. */
Vdev::Vdev()
 : m_poolConfig(NULL),
   m_config(NULL)
{}

bool
Vdev::VdevLookupPoolGuid()
{
	uint64_t guid;
	if (nvlist_lookup_uint64(m_poolConfig, ZPOOL_CONFIG_POOL_GUID, &guid))
		return (false);
	m_poolGUID = guid;
	return (true);
}

void
Vdev::VdevLookupGuid()
{
	uint64_t guid;
	if (nvlist_lookup_uint64(m_config, ZPOOL_CONFIG_GUID, &guid) != 0)
		throw ZfsdException("Unable to extract vdev GUID "
				    "from vdev config data.");
	m_vdevGUID = guid;
}

Vdev::Vdev(zpool_handle_t *pool, nvlist_t *config)
 : m_poolConfig(zpool_get_config(pool, NULL)),
   m_config(config)
{
	if (!VdevLookupPoolGuid())
		throw ZfsdException("Can't extract pool GUID from handle.");
	VdevLookupGuid();
}

Vdev::Vdev(nvlist_t *poolConfig, nvlist_t *config)
 : m_poolConfig(poolConfig),
   m_config(config)
{
	if (!VdevLookupPoolGuid())
		throw ZfsdException("Can't extract pool GUID from config.");
	VdevLookupGuid();
}

Vdev::Vdev(nvlist_t *labelConfig)
 : m_poolConfig(labelConfig),
   m_config(labelConfig)
{
	/*
	 * Spares do not have a Pool GUID.  Tolerate its absence.
	 * Code accessing this Vdev in a context where the Pool GUID is
	 * required will find it invalid (as it is upon Vdev construction)
	 * and act accordingly.
	 */
	(void) VdevLookupPoolGuid();
	VdevLookupGuid();

	try {
		m_config = VdevIterator(labelConfig).Find(m_vdevGUID);
	} catch (const ZfsdException &exp) {
		/*
		 * When reading a spare's label, it is normal not to find
		 * a list of vdevs
		 */
		m_config = NULL;
	}
}

bool
Vdev::IsSpare() const
{
	uint64_t is_spare(0);

	if (m_config == NULL)
		return (false);

	(void)nvlist_lookup_uint64(m_config, ZPOOL_CONFIG_IS_SPARE, &is_spare);
	return (bool(is_spare));
}

vdev_state
Vdev::State() const
{
	uint64_t    *nvlist_array;
	vdev_stat_t *vs;
	uint_t       vsc;

	if (m_config == NULL) {
		/*
		 * If we couldn't find the list of vdevs, that normally means
		 * that this is an available hotspare.  In that case, we will
		 * presume it to be healthy.  Even if this spare had formerly
		 * been in use, been degraded, and been replaced, the act of
		 * replacement wipes the degraded bit from the label.  So we
		 * have no choice but to presume that it is healthy.
		 */
		return (VDEV_STATE_HEALTHY);
	}

	if (nvlist_lookup_uint64_array(m_config, ZPOOL_CONFIG_VDEV_STATS,
				       &nvlist_array, &vsc) == 0) {
		vs = reinterpret_cast<vdev_stat_t *>(nvlist_array);
		return (static_cast<vdev_state>(vs->vs_state));
	}

	/*
	 * Stats are not available.  This vdev was created from a label.
	 * Synthesize a state based on available data.
	 */
	uint64_t faulted(0);
	uint64_t degraded(0);
	(void)nvlist_lookup_uint64(m_config, ZPOOL_CONFIG_FAULTED, &faulted);
	(void)nvlist_lookup_uint64(m_config, ZPOOL_CONFIG_DEGRADED, &degraded);
	if (faulted)
		return (VDEV_STATE_FAULTED);
	if (degraded)
		return (VDEV_STATE_DEGRADED);
	return (VDEV_STATE_HEALTHY);
}

std::list<Vdev>
Vdev::Children()
{
	nvlist_t **vdevChildren;
	int result;
	u_int numChildren;
	std::list<Vdev> children;

	if (m_poolConfig == NULL || m_config == NULL)
		return (children);

	result = nvlist_lookup_nvlist_array(m_config,
	    ZPOOL_CONFIG_CHILDREN, &vdevChildren, &numChildren);
	if (result != 0)
		return (children);

	for (u_int c = 0;c < numChildren; c++)
		children.push_back(Vdev(m_poolConfig, vdevChildren[c]));

	return (children);
}

Vdev
Vdev::RootVdev()
{
	nvlist_t *rootVdev;

	if (m_poolConfig == NULL)
		return (NonexistentVdev);

	if (nvlist_lookup_nvlist(m_poolConfig, ZPOOL_CONFIG_VDEV_TREE,
	    &rootVdev) != 0)
		return (NonexistentVdev);
	return (Vdev(m_poolConfig, rootVdev));
}

/*
 * Find our parent.  This requires doing a traversal of the config; we can't
 * cache it as leaf vdevs may change their pool config location (spare,
 * replacing, mirror, etc).
 */
Vdev
Vdev::Parent()
{
	std::list<Vdev> to_examine;
	std::list<Vdev> children;
	std::list<Vdev>::iterator children_it;

	to_examine.push_back(RootVdev());
	for (;;) {
		if (to_examine.empty())
			return (NonexistentVdev);
		Vdev vd = to_examine.front();
		if (vd.DoesNotExist())
			return (NonexistentVdev);
		to_examine.pop_front();
		children = vd.Children();
		children_it = children.begin();
		for (;children_it != children.end(); children_it++) {
			Vdev child = *children_it;

			if (child.GUID() == GUID())
				return (vd);
			to_examine.push_front(child);
		}
	}
}

bool
Vdev::IsAvailableSpare() const
{
	/* If we have a pool guid, we cannot be an available spare. */
	if (PoolGUID())
		return (false);

	return (true);
}

bool
Vdev::IsSpare()
{
	uint64_t spare;
	if (nvlist_lookup_uint64(m_config, ZPOOL_CONFIG_IS_SPARE, &spare) != 0)
		return (false);
	return (spare != 0);
}

bool
Vdev::IsActiveSpare() const
{
	vdev_stat_t *vs;
	uint_t c;

	if (m_poolConfig == NULL)
		return (false);

	(void) nvlist_lookup_uint64_array(m_config, ZPOOL_CONFIG_VDEV_STATS,
	    reinterpret_cast<uint64_t **>(&vs), &c);
	if (vs == NULL || vs->vs_aux != VDEV_AUX_SPARED)
		return (false);
	return (true);
}

bool
Vdev::IsResilvering() const
{
	pool_scan_stat_t *ps = NULL;
	uint_t c;

	if (State() != VDEV_STATE_HEALTHY)
		return (false);

	(void) nvlist_lookup_uint64_array(m_config, ZPOOL_CONFIG_SCAN_STATS,
	    reinterpret_cast<uint64_t **>(&ps), &c);
	if (ps == NULL || ps->pss_func != POOL_SCAN_RESILVER)
		return (false);
	return (true);
}

string
Vdev::GUIDString() const
{
	stringstream vdevGUIDString;

	vdevGUIDString << GUID();
	return (vdevGUIDString.str());
}

string
Vdev::Name(zpool_handle_t *zhp, bool verbose) const
{
	return (zpool_vdev_name(g_zfsHandle, zhp, m_config,
	    verbose ? B_TRUE : B_FALSE));
}

string
Vdev::Path() const
{
	char *path(NULL);

	if ((m_config != NULL)
	    && (nvlist_lookup_string(m_config, ZPOOL_CONFIG_PATH, &path) == 0))
		return (path);

	return ("");
}

string
Vdev::PhysicalPath() const
{
	char *path(NULL);

	if ((m_config != NULL) && (nvlist_lookup_string(m_config,
				    ZPOOL_CONFIG_PHYS_PATH, &path) == 0))
		return (path);

	return ("");
}
