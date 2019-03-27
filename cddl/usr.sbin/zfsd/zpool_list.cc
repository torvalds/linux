/*-
 * Copyright (c) 2011, 2012, 2013 Spectra Logic Corporation
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
 * \file zpool_list.cc
 *
 * Implementation of the ZpoolList class.
 */
#include <sys/cdefs.h>
#include <sys/fs/zfs.h>

#include <stdint.h>

#include <libzfs.h>

#include <list>
#include <map>
#include <string>

#include <devdctl/guid.h>
#include <devdctl/event.h>
#include <devdctl/event_factory.h>
#include <devdctl/exception.h>
#include <devdctl/consumer.h>

#include "vdev.h"
#include "vdev_iterator.h"
#include "zpool_list.h"
#include "zfsd.h"

/*============================ Namespace Control =============================*/
using DevdCtl::Guid;

/*=========================== Class Implementations ==========================*/
/*--------------------------------- ZpoolList --------------------------------*/
bool
ZpoolList::ZpoolAll(zpool_handle_t *pool, nvlist_t *poolConfig, void *cbArg)
{
	return (true);
}

bool
ZpoolList::ZpoolByGUID(zpool_handle_t *pool, nvlist_t *poolConfig,
			   void *cbArg)
{
	Guid *desiredPoolGUID(static_cast<Guid *>(cbArg));
	uint64_t poolGUID;

	/* We are only intested in the pool that matches our pool GUID. */
	return (nvlist_lookup_uint64(poolConfig, ZPOOL_CONFIG_POOL_GUID,
				     &poolGUID) == 0
	     && poolGUID == (uint64_t)*desiredPoolGUID);
}

bool
ZpoolList::ZpoolByName(zpool_handle_t *pool, nvlist_t *poolConfig, void *cbArg)
{
	const string &desiredPoolName(*static_cast<const string *>(cbArg));

	/* We are only intested in the pool that matches our pool GUID. */
	return (desiredPoolName == zpool_get_name(pool));
}

int
ZpoolList::LoadIterator(zpool_handle_t *pool, void *data)
{
	ZpoolList *zpl(reinterpret_cast<ZpoolList *>(data));
	nvlist_t  *poolConfig(zpool_get_config(pool, NULL));

	if (zpl->m_filter(pool, poolConfig, zpl->m_filterArg))
		zpl->push_back(pool);
	else
		zpool_close(pool);
	return (0);
}

ZpoolList::ZpoolList(PoolFilter_t *filter, void * filterArg)
 : m_filter(filter),
   m_filterArg(filterArg)
{
	zpool_iter(g_zfsHandle, LoadIterator, this);
}

ZpoolList::~ZpoolList()
{
	for (iterator it(begin()); it != end(); it++)
		zpool_close(*it);

	clear();
}
