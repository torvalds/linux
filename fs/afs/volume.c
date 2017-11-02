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
#include <linux/slab.h>
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
	vlocation = afs_vlocation_lookup(params->net, params->cell, params->key,
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
#ifdef CONFIG_AFS_FSCACHE
	volume->cache = fscache_acquire_cookie(volume->cell->cache,
					       &afs_volume_cache_index_def,
					       volume, true);
#endif
	afs_get_vlocation(vlocation);
	volume->vlocation = vlocation;

	vlocation->vols[volume->type] = volume;

success:
	_debug("kAFS selected %s volume %08x",
	       afs_voltypes[volume->type], volume->vid);
	up_write(&params->cell->vl_sem);
	afs_put_vlocation(params->net, vlocation);
	_leave(" = %p", volume);
	return volume;

	/* clean up */
error_up:
	up_write(&params->cell->vl_sem);
error:
	afs_put_vlocation(params->net, vlocation);
	_leave(" = %d", ret);
	return ERR_PTR(ret);

error_discard:
	up_write(&params->cell->vl_sem);

	for (loop = volume->nservers - 1; loop >= 0; loop--) {
		afs_put_cb_interest(params->net, volume->cb_interests[loop]);
		afs_put_server(params->net, volume->servers[loop]);
	}

	kfree(volume);
	goto error;
}

/*
 * destroy a volume record
 */
void afs_put_volume(struct afs_cell *cell, struct afs_volume *volume)
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
	down_write(&cell->vl_sem);

	if (likely(!atomic_dec_and_test(&volume->usage))) {
		up_write(&vlocation->cell->vl_sem);
		_leave("");
		return;
	}

	vlocation->vols[volume->type] = NULL;

	up_write(&cell->vl_sem);

	/* finish cleaning up the volume */
#ifdef CONFIG_AFS_FSCACHE
	fscache_relinquish_cookie(volume->cache, 0);
#endif
	afs_put_vlocation(cell->net, vlocation);

	for (loop = volume->nservers - 1; loop >= 0; loop--) {
		afs_put_cb_interest(cell->net, volume->cb_interests[loop]);
		afs_put_server(cell->net, volume->servers[loop]);
	}

	kfree(volume);

	_leave(" [destroyed]");
}
