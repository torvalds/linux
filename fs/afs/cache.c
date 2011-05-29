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
static uint16_t afs_cell_cache_get_aux(const void *cookie_netfs_data,
				       void *buffer, uint16_t buflen);
static enum fscache_checkaux afs_cell_cache_check_aux(void *cookie_netfs_data,
						      const void *buffer,
						      uint16_t buflen);

static uint16_t afs_vlocation_cache_get_key(const void *cookie_netfs_data,
					    void *buffer, uint16_t buflen);
static uint16_t afs_vlocation_cache_get_aux(const void *cookie_netfs_data,
					    void *buffer, uint16_t buflen);
static enum fscache_checkaux afs_vlocation_cache_check_aux(
	void *cookie_netfs_data, const void *buffer, uint16_t buflen);

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
static void afs_vnode_cache_now_uncached(void *cookie_netfs_data);

struct fscache_netfs afs_cache_netfs = {
	.name			= "afs",
	.version		= 0,
};

struct fscache_cookie_def afs_cell_cache_index_def = {
	.name		= "AFS.cell",
	.type		= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key	= afs_cell_cache_get_key,
	.get_aux	= afs_cell_cache_get_aux,
	.check_aux	= afs_cell_cache_check_aux,
};

struct fscache_cookie_def afs_vlocation_cache_index_def = {
	.name			= "AFS.vldb",
	.type			= FSCACHE_COOKIE_TYPE_INDEX,
	.get_key		= afs_vlocation_cache_get_key,
	.get_aux		= afs_vlocation_cache_get_aux,
	.check_aux		= afs_vlocation_cache_check_aux,
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
	.now_uncached		= afs_vnode_cache_now_uncached,
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

/*
 * provide new auxiliary cache data
 */
static uint16_t afs_cell_cache_get_aux(const void *cookie_netfs_data,
				       void *buffer, uint16_t bufmax)
{
	const struct afs_cell *cell = cookie_netfs_data;
	uint16_t dlen;

	_enter("%p,%p,%u", cell, buffer, bufmax);

	dlen = cell->vl_naddrs * sizeof(cell->vl_addrs[0]);
	dlen = min(dlen, bufmax);
	dlen &= ~(sizeof(cell->vl_addrs[0]) - 1);

	memcpy(buffer, cell->vl_addrs, dlen);
	return dlen;
}

/*
 * check that the auxiliary data indicates that the entry is still valid
 */
static enum fscache_checkaux afs_cell_cache_check_aux(void *cookie_netfs_data,
						      const void *buffer,
						      uint16_t buflen)
{
	_leave(" = OKAY");
	return FSCACHE_CHECKAUX_OKAY;
}

/*****************************************************************************/
/*
 * set the key for the index entry
 */
static uint16_t afs_vlocation_cache_get_key(const void *cookie_netfs_data,
					    void *buffer, uint16_t bufmax)
{
	const struct afs_vlocation *vlocation = cookie_netfs_data;
	uint16_t klen;

	_enter("{%s},%p,%u", vlocation->vldb.name, buffer, bufmax);

	klen = strnlen(vlocation->vldb.name, sizeof(vlocation->vldb.name));
	if (klen > bufmax)
		return 0;

	memcpy(buffer, vlocation->vldb.name, klen);

	_leave(" = %u", klen);
	return klen;
}

/*
 * provide new auxiliary cache data
 */
static uint16_t afs_vlocation_cache_get_aux(const void *cookie_netfs_data,
					    void *buffer, uint16_t bufmax)
{
	const struct afs_vlocation *vlocation = cookie_netfs_data;
	uint16_t dlen;

	_enter("{%s},%p,%u", vlocation->vldb.name, buffer, bufmax);

	dlen = sizeof(struct afs_cache_vlocation);
	dlen -= offsetof(struct afs_cache_vlocation, nservers);
	if (dlen > bufmax)
		return 0;

	memcpy(buffer, (uint8_t *)&vlocation->vldb.nservers, dlen);

	_leave(" = %u", dlen);
	return dlen;
}

/*
 * check that the auxiliary data indicates that the entry is still valid
 */
static
enum fscache_checkaux afs_vlocation_cache_check_aux(void *cookie_netfs_data,
						    const void *buffer,
						    uint16_t buflen)
{
	const struct afs_cache_vlocation *cvldb;
	struct afs_vlocation *vlocation = cookie_netfs_data;
	uint16_t dlen;

	_enter("{%s},%p,%u", vlocation->vldb.name, buffer, buflen);

	/* check the size of the data is what we're expecting */
	dlen = sizeof(struct afs_cache_vlocation);
	dlen -= offsetof(struct afs_cache_vlocation, nservers);
	if (dlen != buflen)
		return FSCACHE_CHECKAUX_OBSOLETE;

	cvldb = container_of(buffer, struct afs_cache_vlocation, nservers);

	/* if what's on disk is more valid than what's in memory, then use the
	 * VL record from the cache */
	if (!vlocation->valid || vlocation->vldb.rtime == cvldb->rtime) {
		memcpy((uint8_t *)&vlocation->vldb.nservers, buffer, dlen);
		vlocation->valid = 1;
		_leave(" = SUCCESS [c->m]");
		return FSCACHE_CHECKAUX_OKAY;
	}

	/* need to update the cache if the cached info differs */
	if (memcmp(&vlocation->vldb, buffer, dlen) != 0) {
		/* delete if the volume IDs for this name differ */
		if (memcmp(&vlocation->vldb.vid, &cvldb->vid,
			   sizeof(cvldb->vid)) != 0
		    ) {
			_leave(" = OBSOLETE");
			return FSCACHE_CHECKAUX_OBSOLETE;
		}

		_leave(" = UPDATE");
		return FSCACHE_CHECKAUX_NEEDS_UPDATE;
	}

	_leave(" = OKAY");
	return FSCACHE_CHECKAUX_OKAY;
}

/*****************************************************************************/
/*
 * set the key for the volume index entry
 */
static uint16_t afs_volume_cache_get_key(const void *cookie_netfs_data,
					void *buffer, uint16_t bufmax)
{
	const struct afs_volume *volume = cookie_netfs_data;
	uint16_t klen;

	_enter("{%u},%p,%u", volume->type, buffer, bufmax);

	klen = sizeof(volume->type);
	if (klen > bufmax)
		return 0;

	memcpy(buffer, &volume->type, sizeof(volume->type));

	_leave(" = %u", klen);
	return klen;

}

/*****************************************************************************/
/*
 * set the key for the index entry
 */
static uint16_t afs_vnode_cache_get_key(const void *cookie_netfs_data,
					void *buffer, uint16_t bufmax)
{
	const struct afs_vnode *vnode = cookie_netfs_data;
	uint16_t klen;

	_enter("{%x,%x,%llx},%p,%u",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version,
	       buffer, bufmax);

	klen = sizeof(vnode->fid.vnode);
	if (klen > bufmax)
		return 0;

	memcpy(buffer, &vnode->fid.vnode, sizeof(vnode->fid.vnode));

	_leave(" = %u", klen);
	return klen;
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

/*
 * provide new auxiliary cache data
 */
static uint16_t afs_vnode_cache_get_aux(const void *cookie_netfs_data,
					void *buffer, uint16_t bufmax)
{
	const struct afs_vnode *vnode = cookie_netfs_data;
	uint16_t dlen;

	_enter("{%x,%x,%Lx},%p,%u",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version,
	       buffer, bufmax);

	dlen = sizeof(vnode->fid.unique) + sizeof(vnode->status.data_version);
	if (dlen > bufmax)
		return 0;

	memcpy(buffer, &vnode->fid.unique, sizeof(vnode->fid.unique));
	buffer += sizeof(vnode->fid.unique);
	memcpy(buffer, &vnode->status.data_version,
	       sizeof(vnode->status.data_version));

	_leave(" = %u", dlen);
	return dlen;
}

/*
 * check that the auxiliary data indicates that the entry is still valid
 */
static enum fscache_checkaux afs_vnode_cache_check_aux(void *cookie_netfs_data,
						       const void *buffer,
						       uint16_t buflen)
{
	struct afs_vnode *vnode = cookie_netfs_data;
	uint16_t dlen;

	_enter("{%x,%x,%llx},%p,%u",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version,
	       buffer, buflen);

	/* check the size of the data is what we're expecting */
	dlen = sizeof(vnode->fid.unique) + sizeof(vnode->status.data_version);
	if (dlen != buflen) {
		_leave(" = OBSOLETE [len %hx != %hx]", dlen, buflen);
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	if (memcmp(buffer,
		   &vnode->fid.unique,
		   sizeof(vnode->fid.unique)
		   ) != 0) {
		unsigned unique;

		memcpy(&unique, buffer, sizeof(unique));

		_leave(" = OBSOLETE [uniq %x != %x]",
		       unique, vnode->fid.unique);
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	if (memcmp(buffer + sizeof(vnode->fid.unique),
		   &vnode->status.data_version,
		   sizeof(vnode->status.data_version)
		   ) != 0) {
		afs_dataversion_t version;

		memcpy(&version, buffer + sizeof(vnode->fid.unique),
		       sizeof(version));

		_leave(" = OBSOLETE [vers %llx != %llx]",
		       version, vnode->status.data_version);
		return FSCACHE_CHECKAUX_OBSOLETE;
	}

	_leave(" = SUCCESS");
	return FSCACHE_CHECKAUX_OKAY;
}

/*
 * indication the cookie is no longer uncached
 * - this function is called when the backing store currently caching a cookie
 *   is removed
 * - the netfs should use this to clean up any markers indicating cached pages
 * - this is mandatory for any object that may have data
 */
static void afs_vnode_cache_now_uncached(void *cookie_netfs_data)
{
	struct afs_vnode *vnode = cookie_netfs_data;
	struct pagevec pvec;
	pgoff_t first;
	int loop, nr_pages;

	_enter("{%x,%x,%Lx}",
	       vnode->fid.vnode, vnode->fid.unique, vnode->status.data_version);

	pagevec_init(&pvec, 0);
	first = 0;

	for (;;) {
		/* grab a bunch of pages to clean */
		nr_pages = pagevec_lookup(&pvec, vnode->vfs_inode.i_mapping,
					  first,
					  PAGEVEC_SIZE - pagevec_count(&pvec));
		if (!nr_pages)
			break;

		for (loop = 0; loop < nr_pages; loop++)
			ClearPageFsCache(pvec.pages[loop]);

		first = pvec.pages[nr_pages - 1]->index + 1;

		pvec.nr = nr_pages;
		pagevec_release(&pvec);
		cond_resched();
	}

	_leave("");
}
