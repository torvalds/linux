/* AFS caching stuff
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_cell_cache_match(void *target,
						const void *entry);
static void afs_cell_cache_update(void *source, void *entry);

struct cachefs_index_def afs_cache_cell_index_def = {
	.name			= "cell_ix",
	.data_size		= sizeof(struct afs_cache_cell),
	.keys[0]		= { CACHEFS_INDEX_KEYS_ASCIIZ, 64 },
	.match			= afs_cell_cache_match,
	.update			= afs_cell_cache_update,
};
#endif

/*
 * match a cell record obtained from the cache
 */
#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_cell_cache_match(void *target,
						const void *entry)
{
	const struct afs_cache_cell *ccell = entry;
	struct afs_cell *cell = target;

	_enter("{%s},{%s}", ccell->name, cell->name);

	if (strncmp(ccell->name, cell->name, sizeof(ccell->name)) == 0) {
		_leave(" = SUCCESS");
		return CACHEFS_MATCH_SUCCESS;
	}

	_leave(" = FAILED");
	return CACHEFS_MATCH_FAILED;
}
#endif

/*
 * update a cell record in the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_cell_cache_update(void *source, void *entry)
{
	struct afs_cache_cell *ccell = entry;
	struct afs_cell *cell = source;

	_enter("%p,%p", source, entry);

	strncpy(ccell->name, cell->name, sizeof(ccell->name));

	memcpy(ccell->vl_servers,
	       cell->vl_addrs,
	       min(sizeof(ccell->vl_servers), sizeof(cell->vl_addrs)));

}
#endif

#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_vlocation_cache_match(void *target,
						     const void *entry);
static void afs_vlocation_cache_update(void *source, void *entry);

struct cachefs_index_def afs_vlocation_cache_index_def = {
	.name		= "vldb",
	.data_size	= sizeof(struct afs_cache_vlocation),
	.keys[0]	= { CACHEFS_INDEX_KEYS_ASCIIZ, 64 },
	.match		= afs_vlocation_cache_match,
	.update		= afs_vlocation_cache_update,
};
#endif

/*
 * match a VLDB record stored in the cache
 * - may also load target from entry
 */
#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_vlocation_cache_match(void *target,
						     const void *entry)
{
	const struct afs_cache_vlocation *vldb = entry;
	struct afs_vlocation *vlocation = target;

	_enter("{%s},{%s}", vlocation->vldb.name, vldb->name);

	if (strncmp(vlocation->vldb.name, vldb->name, sizeof(vldb->name)) == 0
	    ) {
		if (!vlocation->valid ||
		    vlocation->vldb.rtime == vldb->rtime
		    ) {
			vlocation->vldb = *vldb;
			vlocation->valid = 1;
			_leave(" = SUCCESS [c->m]");
			return CACHEFS_MATCH_SUCCESS;
		} else if (memcmp(&vlocation->vldb, vldb, sizeof(*vldb)) != 0) {
			/* delete if VIDs for this name differ */
			if (memcmp(&vlocation->vldb.vid,
				   &vldb->vid,
				   sizeof(vldb->vid)) != 0) {
				_leave(" = DELETE");
				return CACHEFS_MATCH_SUCCESS_DELETE;
			}

			_leave(" = UPDATE");
			return CACHEFS_MATCH_SUCCESS_UPDATE;
		} else {
			_leave(" = SUCCESS");
			return CACHEFS_MATCH_SUCCESS;
		}
	}

	_leave(" = FAILED");
	return CACHEFS_MATCH_FAILED;
}
#endif

/*
 * update a VLDB record stored in the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_vlocation_cache_update(void *source, void *entry)
{
	struct afs_cache_vlocation *vldb = entry;
	struct afs_vlocation *vlocation = source;

	_enter("");

	*vldb = vlocation->vldb;
}
#endif

#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_volume_cache_match(void *target,
						  const void *entry);
static void afs_volume_cache_update(void *source, void *entry);

struct cachefs_index_def afs_volume_cache_index_def = {
	.name		= "volume",
	.data_size	= sizeof(struct afs_cache_vhash),
	.keys[0]	= { CACHEFS_INDEX_KEYS_BIN, 1 },
	.keys[1]	= { CACHEFS_INDEX_KEYS_BIN, 1 },
	.match		= afs_volume_cache_match,
	.update		= afs_volume_cache_update,
};
#endif

/*
 * match a volume hash record stored in the cache
 */
#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_volume_cache_match(void *target,
						  const void *entry)
{
	const struct afs_cache_vhash *vhash = entry;
	struct afs_volume *volume = target;

	_enter("{%u},{%u}", volume->type, vhash->vtype);

	if (volume->type == vhash->vtype) {
		_leave(" = SUCCESS");
		return CACHEFS_MATCH_SUCCESS;
	}

	_leave(" = FAILED");
	return CACHEFS_MATCH_FAILED;
}
#endif

/*
 * update a volume hash record stored in the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_volume_cache_update(void *source, void *entry)
{
	struct afs_cache_vhash *vhash = entry;
	struct afs_volume *volume = source;

	_enter("");

	vhash->vtype = volume->type;
}
#endif

#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_vnode_cache_match(void *target,
						 const void *entry);
static void afs_vnode_cache_update(void *source, void *entry);

struct cachefs_index_def afs_vnode_cache_index_def = {
	.name		= "vnode",
	.data_size	= sizeof(struct afs_cache_vnode),
	.keys[0]	= { CACHEFS_INDEX_KEYS_BIN, 4 },
	.match		= afs_vnode_cache_match,
	.update		= afs_vnode_cache_update,
};
#endif

/*
 * match a vnode record stored in the cache
 */
#ifdef AFS_CACHING_SUPPORT
static cachefs_match_val_t afs_vnode_cache_match(void *target,
						 const void *entry)
{
	const struct afs_cache_vnode *cvnode = entry;
	struct afs_vnode *vnode = target;

	_enter("{%x,%x,%Lx},{%x,%x,%Lx}",
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       vnode->status.version,
	       cvnode->vnode_id,
	       cvnode->vnode_unique,
	       cvnode->data_version);

	if (vnode->fid.vnode != cvnode->vnode_id) {
		_leave(" = FAILED");
		return CACHEFS_MATCH_FAILED;
	}

	if (vnode->fid.unique != cvnode->vnode_unique ||
	    vnode->status.version != cvnode->data_version) {
		_leave(" = DELETE");
		return CACHEFS_MATCH_SUCCESS_DELETE;
	}

	_leave(" = SUCCESS");
	return CACHEFS_MATCH_SUCCESS;
}
#endif

/*
 * update a vnode record stored in the cache
 */
#ifdef AFS_CACHING_SUPPORT
static void afs_vnode_cache_update(void *source, void *entry)
{
	struct afs_cache_vnode *cvnode = entry;
	struct afs_vnode *vnode = source;

	_enter("");

	cvnode->vnode_id	= vnode->fid.vnode;
	cvnode->vnode_unique	= vnode->fid.unique;
	cvnode->data_version	= vnode->status.version;
}
#endif
