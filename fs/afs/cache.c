// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS caching stuff
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/sched.h>
#include "internal.h"

static enum fscache_checkaux afs_vnode_cache_check_aux(void *cookie_netfs_data,
						       const void *buffer,
						       uint16_t buflen,
						       loff_t object_size);

struct fscache_netfs afs_cache_netfs = {
	.name			= "afs",
	.version		= 2,
};

struct fscache_cookie_def afs_cell_cache_index_def = {
	.name		= "AFS.cell",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
};

struct fscache_cookie_def afs_volume_cache_index_def = {
	.name		= "AFS.volume",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
};

struct fscache_cookie_def afs_vnode_cache_index_def = {
	.name		= "AFS.vnode",
	.type		= FSCACHE_COOKIE_TYPE_DATAFILE,
	.check_aux	= afs_vnode_cache_check_aux,
};

/*
 * check that the auxiliary data indicates that the entry is still valid
 */
static enum fscache_checkaux afs_vnode_cache_check_aux(void *cookie_netfs_data,
						       const void *buffer,
						       uint16_t buflen,
						       loff_t object_size)
{
	struct afs_vnode *vnode = cookie_netfs_data;
	struct afs_vnode_cache_aux aux;

	_enter("{%llx,%x,%llx},%p,%u",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version,
	       buffer, buflen);

	memcpy(&aux, buffer, sizeof(aux));

	/* check the size of the data is what we're expecting */
	if (buflen != sizeof(aux)) {
		_leave(" = OBSOLETE [len %hx != %zx]", buflen, sizeof(aux));
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	if (vnode->status.data_version != aux.data_version) {
		_leave(" = OBSOLETE [vers %llx != %llx]",
		       aux.data_version, vnode->status.data_version);
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	_leave(" = SUCCESS");
	return FSCACHE_CHECKAUX_OKAY;
}
