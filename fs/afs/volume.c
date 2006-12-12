/* volume.c: AFS volume management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include "volume.h"
#include "vnode.h"
#include "cell.h"
#include "cache.h"
#include "cmservice.h"
#include "fsclient.h"
#include "vlclient.h"
#include "internal.h"

#ifdef __KDEBUG
static const char *afs_voltypes[] = { "R/W", "R/O", "BAK" };
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

/*****************************************************************************/
/*
 * lookup a volume by name
 * - this can be one of the following:
 *	"%[cell:]volume[.]"		R/W volume
 *	"#[cell:]volume[.]"		R/O or R/W volume (rwparent=0),
 *					 or R/W (rwparent=1) volume
 *	"%[cell:]volume.readonly"	R/O volume
 *	"#[cell:]volume.readonly"	R/O volume
 *	"%[cell:]volume.backup"		Backup volume
 *	"#[cell:]volume.backup"		Backup volume
 *
 * The cell name is optional, and defaults to the current cell.
 *
 * See "The Rules of Mount Point Traversal" in Chapter 5 of the AFS SysAdmin
 * Guide
 * - Rule 1: Explicit type suffix forces access of that type or nothing
 *           (no suffix, then use Rule 2 & 3)
 * - Rule 2: If parent volume is R/O, then mount R/O volume by preference, R/W
 *           if not available
 * - Rule 3: If parent volume is R/W, then only mount R/W volume unless
 *           explicitly told otherwise
 */
int afs_volume_lookup(const char *name, struct afs_cell *cell, int rwpath,
		      struct afs_volume **_volume)
{
	struct afs_vlocation *vlocation = NULL;
	struct afs_volume *volume = NULL;
	afs_voltype_t type;
	const char *cellname, *volname, *suffix;
	char srvtmask;
	int force, ret, loop, cellnamesz, volnamesz;

	_enter("%s,,%d,", name, rwpath);

	if (!name || (name[0] != '%' && name[0] != '#') || !name[1]) {
		printk("kAFS: unparsable volume name\n");
		return -EINVAL;
	}

	/* determine the type of volume we're looking for */
	force = 0;
	type = AFSVL_ROVOL;

	if (rwpath || name[0] == '%') {
		type = AFSVL_RWVOL;
		force = 1;
	}

	suffix = strrchr(name, '.');
	if (suffix) {
		if (strcmp(suffix, ".readonly") == 0) {
			type = AFSVL_ROVOL;
			force = 1;
		}
		else if (strcmp(suffix, ".backup") == 0) {
			type = AFSVL_BACKVOL;
			force = 1;
		}
		else if (suffix[1] == 0) {
		}
		else {
			suffix = NULL;
		}
	}

	/* split the cell and volume names */
	name++;
	volname = strchr(name, ':');
	if (volname) {
		cellname = name;
		cellnamesz = volname - name;
		volname++;
	}
	else {
		volname = name;
		cellname = NULL;
		cellnamesz = 0;
	}

	volnamesz = suffix ? suffix - volname : strlen(volname);

	_debug("CELL:%*.*s [%p] VOLUME:%*.*s SUFFIX:%s TYPE:%d%s",
	       cellnamesz, cellnamesz, cellname ?: "", cell,
	       volnamesz, volnamesz, volname, suffix ?: "-",
	       type,
	       force ? " FORCE" : "");

	/* lookup the cell record */
	if (cellname || !cell) {
		ret = afs_cell_lookup(cellname, cellnamesz, &cell);
		if (ret<0) {
			printk("kAFS: unable to lookup cell '%s'\n",
			       cellname ?: "");
			goto error;
		}
	}
	else {
		afs_get_cell(cell);
	}

	/* lookup the volume location record */
	ret = afs_vlocation_lookup(cell, volname, volnamesz, &vlocation);
	if (ret < 0)
		goto error;

	/* make the final decision on the type we want */
	ret = -ENOMEDIUM;
	if (force && !(vlocation->vldb.vidmask & (1 << type)))
		goto error;

	srvtmask = 0;
	for (loop = 0; loop < vlocation->vldb.nservers; loop++)
		srvtmask |= vlocation->vldb.srvtmask[loop];

	if (force) {
		if (!(srvtmask & (1 << type)))
			goto error;
	}
	else if (srvtmask & AFS_VOL_VTM_RO) {
		type = AFSVL_ROVOL;
	}
	else if (srvtmask & AFS_VOL_VTM_RW) {
		type = AFSVL_RWVOL;
	}
	else {
		goto error;
	}

	down_write(&cell->vl_sem);

	/* is the volume already active? */
	if (vlocation->vols[type]) {
		/* yes - re-use it */
		volume = vlocation->vols[type];
		afs_get_volume(volume);
		goto success;
	}

	/* create a new volume record */
	_debug("creating new volume record");

	ret = -ENOMEM;
	volume = kzalloc(sizeof(struct afs_volume), GFP_KERNEL);
	if (!volume)
		goto error_up;

	atomic_set(&volume->usage, 1);
	volume->type		= type;
	volume->type_force	= force;
	volume->cell		= cell;
	volume->vid		= vlocation->vldb.vid[type];

	init_rwsem(&volume->server_sem);

	/* look up all the applicable server records */
	for (loop = 0; loop < 8; loop++) {
		if (vlocation->vldb.srvtmask[loop] & (1 << volume->type)) {
			ret = afs_server_lookup(
				volume->cell,
				&vlocation->vldb.servers[loop],
				&volume->servers[volume->nservers]);
			if (ret < 0)
				goto error_discard;

			volume->nservers++;
		}
	}

	/* attach the cache and volume location */
#ifdef AFS_CACHING_SUPPORT
	cachefs_acquire_cookie(vlocation->cache,
			       &afs_vnode_cache_index_def,
			       volume,
			       &volume->cache);
#endif

	afs_get_vlocation(vlocation);
	volume->vlocation = vlocation;

	vlocation->vols[type] = volume;

 success:
	_debug("kAFS selected %s volume %08x",
	       afs_voltypes[volume->type], volume->vid);
	*_volume = volume;
	ret = 0;

	/* clean up */
 error_up:
	up_write(&cell->vl_sem);
 error:
	afs_put_vlocation(vlocation);
	afs_put_cell(cell);

	_leave(" = %d (%p)", ret, volume);
	return ret;

 error_discard:
	up_write(&cell->vl_sem);

	for (loop = volume->nservers - 1; loop >= 0; loop--)
		afs_put_server(volume->servers[loop]);

	kfree(volume);
	goto error;
} /* end afs_volume_lookup() */

/*****************************************************************************/
/*
 * destroy a volume record
 */
void afs_put_volume(struct afs_volume *volume)
{
	struct afs_vlocation *vlocation;
	int loop;

	if (!volume)
		return;

	_enter("%p", volume);

	vlocation = volume->vlocation;

	/* sanity check */
	BUG_ON(atomic_read(&volume->usage) <= 0);

	/* to prevent a race, the decrement and the dequeue must be effectively
	 * atomic */
	down_write(&vlocation->cell->vl_sem);

	if (likely(!atomic_dec_and_test(&volume->usage))) {
		up_write(&vlocation->cell->vl_sem);
		_leave("");
		return;
	}

	vlocation->vols[volume->type] = NULL;

	up_write(&vlocation->cell->vl_sem);

	/* finish cleaning up the volume */
#ifdef AFS_CACHING_SUPPORT
	cachefs_relinquish_cookie(volume->cache, 0);
#endif
	afs_put_vlocation(vlocation);

	for (loop = volume->nservers - 1; loop >= 0; loop--)
		afs_put_server(volume->servers[loop]);

	kfree(volume);

	_leave(" [destroyed]");
} /* end afs_put_volume() */

/*****************************************************************************/
/*
 * pick a server to use to try accessing this volume
 * - returns with an elevated usage count on the server chosen
 */
int afs_volume_pick_fileserver(struct afs_volume *volume,
			       struct afs_server **_server)
{
	struct afs_server *server;
	int ret, state, loop;

	_enter("%s", volume->vlocation->vldb.name);

	down_read(&volume->server_sem);

	/* handle the no-server case */
	if (volume->nservers == 0) {
		ret = volume->rjservers ? -ENOMEDIUM : -ESTALE;
		up_read(&volume->server_sem);
		_leave(" = %d [no servers]", ret);
		return ret;
	}

	/* basically, just search the list for the first live server and use
	 * that */
	ret = 0;
	for (loop = 0; loop < volume->nservers; loop++) {
		server = volume->servers[loop];
		state = server->fs_state;

		switch (state) {
			/* found an apparently healthy server */
		case 0:
			afs_get_server(server);
			up_read(&volume->server_sem);
			*_server = server;
			_leave(" = 0 (picked %08x)",
			       ntohl(server->addr.s_addr));
			return 0;

		case -ENETUNREACH:
			if (ret == 0)
				ret = state;
			break;

		case -EHOSTUNREACH:
			if (ret == 0 ||
			    ret == -ENETUNREACH)
				ret = state;
			break;

		case -ECONNREFUSED:
			if (ret == 0 ||
			    ret == -ENETUNREACH ||
			    ret == -EHOSTUNREACH)
				ret = state;
			break;

		default:
		case -EREMOTEIO:
			if (ret == 0 ||
			    ret == -ENETUNREACH ||
			    ret == -EHOSTUNREACH ||
			    ret == -ECONNREFUSED)
				ret = state;
			break;
		}
	}

	/* no available servers
	 * - TODO: handle the no active servers case better
	 */
	up_read(&volume->server_sem);
	_leave(" = %d", ret);
	return ret;
} /* end afs_volume_pick_fileserver() */

/*****************************************************************************/
/*
 * release a server after use
 * - releases the ref on the server struct that was acquired by picking
 * - records result of using a particular server to access a volume
 * - return 0 to try again, 1 if okay or to issue error
 */
int afs_volume_release_fileserver(struct afs_volume *volume,
				  struct afs_server *server,
				  int result)
{
	unsigned loop;

	_enter("%s,%08x,%d",
	       volume->vlocation->vldb.name, ntohl(server->addr.s_addr),
	       result);

	switch (result) {
		/* success */
	case 0:
		server->fs_act_jif = jiffies;
		break;

		/* the fileserver denied all knowledge of the volume */
	case -ENOMEDIUM:
		server->fs_act_jif = jiffies;
		down_write(&volume->server_sem);

		/* first, find where the server is in the active list (if it
		 * is) */
		for (loop = 0; loop < volume->nservers; loop++)
			if (volume->servers[loop] == server)
				goto present;

		/* no longer there - may have been discarded by another op */
		goto try_next_server_upw;

	present:
		volume->nservers--;
		memmove(&volume->servers[loop],
			&volume->servers[loop + 1],
			sizeof(volume->servers[loop]) *
			(volume->nservers - loop));
		volume->servers[volume->nservers] = NULL;
		afs_put_server(server);
		volume->rjservers++;

		if (volume->nservers > 0)
			/* another server might acknowledge its existence */
			goto try_next_server_upw;

		/* handle the case where all the fileservers have rejected the
		 * volume
		 * - TODO: try asking the fileservers for volume information
		 * - TODO: contact the VL server again to see if the volume is
		 *         no longer registered
		 */
		up_write(&volume->server_sem);
		afs_put_server(server);
		_leave(" [completely rejected]");
		return 1;

		/* problem reaching the server */
	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -ECONNREFUSED:
	case -ETIMEDOUT:
	case -EREMOTEIO:
		/* mark the server as dead
		 * TODO: vary dead timeout depending on error
		 */
		spin_lock(&server->fs_lock);
		if (!server->fs_state) {
			server->fs_dead_jif = jiffies + HZ * 10;
			server->fs_state = result;
			printk("kAFS: SERVER DEAD state=%d\n", result);
		}
		spin_unlock(&server->fs_lock);
		goto try_next_server;

		/* miscellaneous error */
	default:
		server->fs_act_jif = jiffies;
	case -ENOMEM:
	case -ENONET:
		break;
	}

	/* tell the caller to accept the result */
	afs_put_server(server);
	_leave("");
	return 1;

	/* tell the caller to loop around and try the next server */
 try_next_server_upw:
	up_write(&volume->server_sem);
 try_next_server:
	afs_put_server(server);
	_leave(" [try next server]");
	return 0;

} /* end afs_volume_release_fileserver() */

/*****************************************************************************/
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
} /* end afs_volume_cache_match() */
#endif

/*****************************************************************************/
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

} /* end afs_volume_cache_update() */
#endif
