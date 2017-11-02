/* AFS caching stuff
 *
 * Copyright (C) 2008 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include "internal.h"

static uint16_t afs_cell_cache_get_key(const void *cookie_netfs_data,
				       void *buffer, uint16_t buflen);
static uint16_t afs_volume_cache_get_key(const void *cookie_netfs_data,
					 void *buffer, uint16_t buflen);

static uint16_t afs_vnode_cache_get_key(const void *cookie_netfs_data,
					void *buffer, uint16_t buflen);
static void afs_vnode_cache_get_attr(const void *cookie_netfs_data,
				     uint64_t *size);
static uint16_t afs_vnode_cache_get_aux(const void *cookie_netfs_data,
					void *buffer, uint16_t buflen);
static enum fscache_checkaux afs_vnode_cache_check_aux(void *cookie_netfs_data,
						       const void *buffer,
						       uint16_t buflen);

struct fscache_netfs afs_cache_netfs = {
	.name			= "afs",
	.version		= 1,
};

struct fscache_cookie_def afs_cell_cache_index_def = {
	.name		= "AFS.cell",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key	= afs_cell_cache_get_key,
};

struct fscache_cookie_def afs_volume_cache_index_def = {
	.name		= "AFS.volume",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key	= afs_volume_cache_get_key,
};

struct fscache_cookie_def afs_vnode_cache_index_def = {
	.name			= "AFS.vnode",
	.type			= FSCACHE_COOKIE_TYPE_DATAFILE,
	.get_key		= afs_vnode_cache_get_key,
	.get_attr		= afs_vnode_cache_get_attr,
	.get_aux		= afs_vnode_cache_get_aux,
	.check_aux		= afs_vnode_cache_check_aux,
};

/*
 * set the key for the index entry
 */
static uint16_t afs_cell_cache_get_key(const void *cookie_netfs_data,
				       void *buffer, uint16_t bufmax)
{
	const struct afs_cell *cell = cookie_netfs_data;
	uint16_t klen;

	_enter("%p,%p,%u", cell, buffer, bufmax);

	klen = strlen(cell->name);
	if (klen > bufmax)
		return 0;

	memcpy(buffer, cell->name, klen);
	return klen;
}

/*****************************************************************************/
/*
 * set the key for the volume index entry
 */
static uint16_t afs_volume_cache_get_key(const void *cookie_netfs_data,
					 void *buffer, uint16_t bufmax)
{
	const struct afs_volume *volume = cookie_netfs_data;
	struct {
		u64 volid;
	} __packed key;

	_enter("{%u},%p,%u", volume->type, buffer, bufmax);

	if (bufmax < sizeof(key))
		return 0;

	key.volid = volume->vid;
	memcpy(buffer, &key, sizeof(key));
	return sizeof(key);
}

/*****************************************************************************/
/*
 * set the key for the index entry
 */
static uint16_t afs_vnode_cache_get_key(const void *cookie_netfs_data,
					void *buffer, uint16_t bufmax)
{
	const struct afs_vnode *vnode = cookie_netfs_data;
	struct {
		u32 vnode_id[3];
	} __packed key;

	_enter("{%x,%x,%llx},%p,%u",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version,
	       buffer, bufmax);

	/* Allow for a 96-bit key */
	memset(&key, 0, sizeof(key));
	key.vnode_id[0] = vnode->fid.vnode;
	key.vnode_id[1] = 0;
	key.vnode_id[2] = 0;

	if (sizeof(key) > bufmax)
		return 0;

	memcpy(buffer, &key, sizeof(key));
	return sizeof(key);
}

/*
 * provide updated file attributes
 */
static void afs_vnode_cache_get_attr(const void *cookie_netfs_data,
				     uint64_t *size)
{
	const struct afs_vnode *vnode = cookie_netfs_data;

	_enter("{%x,%x,%llx},",
	       vnode->fid.vnode, vnode->fid.unique,
	       vnode->status.data_version);

	*size = vnode->status.size;
}

struct afs_vnode_cache_aux {
	u64 data_version;
	u32 fid_unique;
} __packed;

/*
 * provide new auxiliary cache data
 */
static uint16_t afs_vnode_cache_get_aux(const void *cookie_netfs_data,
					void *buffer, uint16_t bufmax)
{
	const struct afs_vnode *vnode = cookie_netfs_data;
	struct afs_vnode_cache_aux aux;

	_enter("{%x,%x,%Lx},%p,%u",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version,
	       buffer, bufmax);

	memset(&aux, 0, sizeof(aux));
	aux.data_version = vnode->status.data_version;
	aux.fid_unique = vnode->fid.unique;

	if (bufmax < sizeof(aux))
		return 0;

	memcpy(buffer, &aux, sizeof(aux));
	return sizeof(aux);
}

/*
 * check that the auxiliary data indicates that the entry is still valid
 */
static enum fscache_checkaux afs_vnode_cache_check_aux(void *cookie_netfs_data,
						       const void *buffer,
						       uint16_t buflen)
{
	struct afs_vnode *vnode = cookie_netfs_data;
	struct afs_vnode_cache_aux aux;

	_enter("{%x,%x,%llx},%p,%u",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version,
	       buffer, buflen);

	memcpy(&aux, buffer, sizeof(aux));

	/* check the size of the data is what we're expecting */
	if (buflen != sizeof(aux)) {
		_leave(" = OBSOLETE [len %hx != %zx]", buflen, sizeof(aux));
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	if (vnode->fid.unique != aux.fid_unique) {
		_leave(" = OBSOLETE [uniq %x != %x]",
		       aux.fid_unique, vnode->fid.unique);
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
