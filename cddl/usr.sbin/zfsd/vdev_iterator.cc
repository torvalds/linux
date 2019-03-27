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
 * \file vdev_iterator.cc
 *
 * Implementation of the VdevIterator class.
 */
#include <sys/cdefs.h>
#include <sys/fs/zfs.h>

#include <stdint.h>
#include <syslog.h>

#include <libzfs.h>

#include <list>
#include <string>

#include <devdctl/exception.h>
#include <devdctl/guid.h>

#include "vdev.h"
#include "vdev_iterator.h"
#include "zfsd_exception.h"

/*============================ Namespace Control =============================*/
using DevdCtl::Guid;

/*=========================== Class Implementations ==========================*/
/*------------------------------- VdevIterator -------------------------------*/
VdevIterator::VdevIterator(zpool_handle_t *pool)
 : m_poolConfig(zpool_get_config(pool, NULL))
{
	Reset();
}

VdevIterator::VdevIterator(nvlist_t *poolConfig)
 : m_poolConfig(poolConfig)
{
	Reset();
}

void
VdevIterator::Reset()
{
	nvlist_t  *rootVdev;
	nvlist	  **cache_child;
	int	   result;
	uint_t   cache_children;

	result = nvlist_lookup_nvlist(m_poolConfig,
				      ZPOOL_CONFIG_VDEV_TREE,
				      &rootVdev);
	if (result != 0)
		throw ZfsdException(m_poolConfig, "Unable to extract "
				    "ZPOOL_CONFIG_VDEV_TREE from pool.");
	m_vdevQueue.assign(1, rootVdev);
	result = nvlist_lookup_nvlist_array(rootVdev,
				      	    ZPOOL_CONFIG_L2CACHE,
				      	    &cache_child,
					    &cache_children);
	if (result == 0)
		for (uint_t c = 0; c < cache_children; c++)
			m_vdevQueue.push_back(cache_child[c]);
}

nvlist_t *
VdevIterator::Next()
{
	nvlist_t *vdevConfig;

	if (m_vdevQueue.empty())
		return (NULL);

	for (;;) {
		nvlist_t **vdevChildren;
		int        result;
		u_int      numChildren;

		vdevConfig = m_vdevQueue.front();
		m_vdevQueue.pop_front();

		/* Expand non-leaf vdevs. */
		result = nvlist_lookup_nvlist_array(vdevConfig,
						    ZPOOL_CONFIG_CHILDREN,
						   &vdevChildren, &numChildren);
		if (result != 0) {
			/* leaf vdev */
			break;
		}

		/*
		 * Insert children at the head of the queue to effect a
		 * depth first traversal of the tree.
		 */
		m_vdevQueue.insert(m_vdevQueue.begin(), vdevChildren,
				   vdevChildren + numChildren);
	}

	return (vdevConfig);
}

void
VdevIterator::Each(VdevCallback_t *callBack, void *callBackArg)
{
	nvlist_t *vdevConfig;

	Reset();
	while ((vdevConfig = Next()) != NULL) {
		Vdev vdev(m_poolConfig, vdevConfig);

		if (callBack(vdev, callBackArg))
			break;
	}
}

nvlist_t *
VdevIterator::Find(Guid vdevGUID)
{
	nvlist_t *vdevConfig;

	Reset();
	while ((vdevConfig = Next()) != NULL) {
		Vdev vdev(m_poolConfig, vdevConfig);

		if (vdev.GUID() == vdevGUID)
			return (vdevConfig);
	}
	return (NULL);
}
