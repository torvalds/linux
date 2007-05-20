/* AFS volume management
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
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
#include <linux/sched.h>
#include "internal.h"

static const char *afs_voltypes[] = { "R/W", "R/O", "BAK" };

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
struct afs_volume *afs_volume_lookup(struct afs_mount_params *params)
{
	struct afs_vlocation *vlocation = NULL;
	struct afs_volume *volume = NULL;
	struct afs_server *server = NULL;
	char srvtmask;
	int ret, loop;

	_enter("{%*.*s,%d}",
	       params->volnamesz, params->volnamesz, params->volname, params->rwpath);

	/* lookup the volume location record */
	vlocation = afs_vlocation_lookup(params->cell, params->key,
					 params->volname, params->volnamesz);
	if (IS_ERR(vlocation)) {
		ret = PTR_ERR(vlocation);
		vlocation = NULL;
		goto error;
	}

	/* make the final decision on the type we want */
	ret = -ENOMEDIUM;
	if (params->force && !(vlocation->vldb.vidmask & (1 << params->type)))
		goto error;

	srvtmask = 0;
	for (loop = 0; loop < vlocation->vldb.nservers; loop++)
		srvtmask |= vlocation->vldb.srvtmask[loop];

	if (params->force) {
		if (!(srvtmask & (1 << params->type)))
			goto error;
	} else if (srvtmask & AFS_VOL_VTM_RO) {
		params->type = AFSVL_ROVOL;
	} else if (srvtmask & AFS_VOL_VTM_RW) {
		params->type = AFSVL_RWVOL;
	} else {
		goto error;
	}

	down_write(&params->cell->vl_sem);

	/* is the volume already active? */
	if (vlocation->vols[params->type]) {
		/* yes - re-use it */
		volume = vlocation->vols[params->type];
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
	volume->type		= params->type;
	volume->type_force	= params->force;
	volume->cell		= params->cell;
	volume->vid		= vlocation->vldb.vid[params->type];

	init_rwsem(&volume->server_sem);

	/* look up all the applicable server records */
	for (loop = 0; loop < 8; loop++) {
		if (vlocation->vldb.srvtmask[loop] & (1 << volume->type)) {
			server = afs_lookup_server(
			       volume->cell, &vlocation->vldb.servers[loop]);
			if (IS_ERR(server)) {
				ret = PTR_ERR(server);
				goto error_discard;
			}

			volume->servers[volume->nservers] = server;
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

	vlocation->vols[volume->type] = volume;

success:
	_debug("kAFS selected %s volume %08x",
	       afs_voltypes[volume->type], volume->vid);
	up_write(&params->cell->vl_sem);
	afs_put_vlocation(vlocation);
	_leave(" = %p", volume);
	return volume;

	/* clean up */
error_up:
	up_write(&params->cell->vl_sem);
error:
	afs_put_vlocation(vlocation);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

error_discard:
	up_write(&params->cell->vl_sem);

	for (loop = volume->nservers - 1; loop >= 0; loop--)
		afs_put_server(volume->servers[loop]);

	kfree(volume);
	goto error;
}

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

	ASSERTCMP(atomic_read(&volume->usage), >, 0);

	vlocation = volume->vlocation;

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
}

/*
 * pick a server to use to try accessing this volume
 * - returns with an elevated usage count on the server chosen
 */
struct afs_server *afs_volume_pick_fileserver(struct afs_vnode *vnode)
{
	struct afs_volume *volume = vnode->volume;
	struct afs_server *server;
	int ret, state, loop;

	_enter("%s", volume->vlocation->vldb.name);

	/* stick with the server we're already using if we can */
	if (vnode->server && vnode->server->fs_state == 0) {
		afs_get_server(vnode->server);
		_leave(" = %p [current]", vnode->server);
		return vnode->server;
	}

	down_read(&volume->server_sem);

	/* handle the no-server case */
	if (volume->nservers == 0) {
		ret = volume->rjservers ? -ENOMEDIUM : -ESTALE;
		up_read(&volume->server_sem);
		_leave(" = %d [no servers]", ret);
		return ERR_PTR(ret);
	}

	/* basically, just search the list for the first live server and use
	 * that */
	ret = 0;
	for (loop = 0; loop < volume->nservers; loop++) {
		server = volume->servers[loop];
		state = server->fs_state;

		_debug("consider %d [%d]", loop, state);

		switch (state) {
			/* found an apparently healthy server */
		case 0:
			afs_get_server(server);
			up_read(&volume->server_sem);
			_leave(" = %p (picked %08x)",
			       server, ntohl(server->addr.s_addr));
			return server;

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
	return ERR_PTR(ret);
}

/*
 * release a server after use
 * - releases the ref on the server struct that was acquired by picking
 * - records result of using a particular server to access a volume
 * - return 0 to try again, 1 if okay or to issue error
 * - the caller must release the server struct if result was 0
 */
int afs_volume_release_fileserver(struct afs_vnode *vnode,
				  struct afs_server *server,
				  int result)
{
	struct afs_volume *volume = vnode->volume;
	unsigned loop;

	_enter("%s,%08x,%d",
	       volume->vlocation->vldb.name, ntohl(server->addr.s_addr),
	       result);

	switch (result) {
		/* success */
	case 0:
		server->fs_act_jif = jiffies;
		server->fs_state = 0;
		_leave("");
		return 1;

		/* the fileserver denied all knowledge of the volume */
	case -ENOMEDIUM:
		server->fs_act_jif = jiffies;
		down_write(&volume->server_sem);

		/* firstly, find where the server is in the active list (if it
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
	case -ETIME:
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
		/* tell the caller to accept the result */
		afs_put_server(server);
		_leave(" [local failure]");
		return 1;
	}

	/* tell the caller to loop around and try the next server */
try_next_server_upw:
	up_write(&volume->server_sem);
try_next_server:
	afs_put_server(server);
	_leave(" [try next server]");
	return 0;
}
