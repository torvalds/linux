// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS volume management
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include "internal.h"

unsigned __read_mostly afs_volume_gc_delay = 10;
unsigned __read_mostly afs_volume_record_life = 60 * 60;

static const char *const afs_voltypes[] = { "R/W", "R/O", "BAK" };

/*
 * Allocate a volume record and load it up from a vldb record.
 */
static struct afs_volume *afs_alloc_volume(struct afs_fs_context *params,
					   struct afs_vldb_entry *vldb,
					   unsigned long type_mask)
{
	struct afs_server_list *slist;
	struct afs_volume *volume;
	int ret = -ENOMEM, nr_servers = 0, i;

	for (i = 0; i < vldb->nr_servers; i++)
		if (vldb->fs_mask[i] & type_mask)
			nr_servers++;

	volume = kzalloc(sizeof(struct afs_volume), GFP_KERNEL);
	if (!volume)
		goto error_0;

	volume->vid		= vldb->vid[params->type];
	volume->update_at	= ktime_get_real_seconds() + afs_volume_record_life;
	volume->cell		= afs_get_cell(params->cell);
	volume->type		= params->type;
	volume->type_force	= params->force;
	volume->name_len	= vldb->name_len;

	atomic_set(&volume->usage, 1);
	INIT_LIST_HEAD(&volume->proc_link);
	rwlock_init(&volume->servers_lock);
	rwlock_init(&volume->cb_v_break_lock);
	memcpy(volume->name, vldb->name, vldb->name_len + 1);

	slist = afs_alloc_server_list(params->cell, params->key, vldb, type_mask);
	if (IS_ERR(slist)) {
		ret = PTR_ERR(slist);
		goto error_1;
	}

	refcount_set(&slist->usage, 1);
	volume->servers = slist;
	return volume;

error_1:
	afs_put_cell(params->net, volume->cell);
	kfree(volume);
error_0:
	return ERR_PTR(ret);
}

/*
 * Look up a VLDB record for a volume.
 */
static struct afs_vldb_entry *afs_vl_lookup_vldb(struct afs_cell *cell,
						 struct key *key,
						 const char *volname,
						 size_t volnamesz)
{
	struct afs_vldb_entry *vldb = ERR_PTR(-EDESTADDRREQ);
	struct afs_vl_cursor vc;
	int ret;

	if (!afs_begin_vlserver_operation(&vc, cell, key))
		return ERR_PTR(-ERESTARTSYS);

	while (afs_select_vlserver(&vc)) {
		vldb = afs_vl_get_entry_by_name_u(&vc, volname, volnamesz);
	}

	ret = afs_end_vlserver_operation(&vc);
	return ret < 0 ? ERR_PTR(ret) : vldb;
}

/*
 * Look up a volume in the VL server and create a candidate volume record for
 * it.
 *
 * The volume name can be one of the following:
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
struct afs_volume *afs_create_volume(struct afs_fs_context *params)
{
	struct afs_vldb_entry *vldb;
	struct afs_volume *volume;
	unsigned long type_mask = 1UL << params->type;

	vldb = afs_vl_lookup_vldb(params->cell, params->key,
				  params->volname, params->volnamesz);
	if (IS_ERR(vldb))
		return ERR_CAST(vldb);

	if (test_bit(AFS_VLDB_QUERY_ERROR, &vldb->flags)) {
		volume = ERR_PTR(vldb->error);
		goto error;
	}

	/* Make the final decision on the type we want */
	volume = ERR_PTR(-ENOMEDIUM);
	if (params->force) {
		if (!(vldb->flags & type_mask))
			goto error;
	} else if (test_bit(AFS_VLDB_HAS_RO, &vldb->flags)) {
		params->type = AFSVL_ROVOL;
	} else if (test_bit(AFS_VLDB_HAS_RW, &vldb->flags)) {
		params->type = AFSVL_RWVOL;
	} else {
		goto error;
	}

	type_mask = 1UL << params->type;
	volume = afs_alloc_volume(params, vldb, type_mask);

error:
	kfree(vldb);
	return volume;
}

/*
 * Destroy a volume record
 */
static void afs_destroy_volume(struct afs_net *net, struct afs_volume *volume)
{
	_enter("%p", volume);

#ifdef CONFIG_AFS_FSCACHE
	ASSERTCMP(volume->cache, ==, NULL);
#endif

	afs_put_serverlist(net, volume->servers);
	afs_put_cell(net, volume->cell);
	kfree(volume);

	_leave(" [destroyed]");
}

/*
 * Drop a reference on a volume record.
 */
void afs_put_volume(struct afs_cell *cell, struct afs_volume *volume)
{
	if (volume) {
		_enter("%s", volume->name);

		if (atomic_dec_and_test(&volume->usage))
			afs_destroy_volume(cell->net, volume);
	}
}

/*
 * Activate a volume.
 */
void afs_activate_volume(struct afs_volume *volume)
{
#ifdef CONFIG_AFS_FSCACHE
	volume->cache = fscache_acquire_cookie(volume->cell->cache,
					       &afs_volume_cache_index_def,
					       &volume->vid, sizeof(volume->vid),
					       NULL, 0,
					       volume, 0, true);
#endif

	write_lock(&volume->cell->proc_lock);
	list_add_tail(&volume->proc_link, &volume->cell->proc_volumes);
	write_unlock(&volume->cell->proc_lock);
}

/*
 * Deactivate a volume.
 */
void afs_deactivate_volume(struct afs_volume *volume)
{
	_enter("%s", volume->name);

	write_lock(&volume->cell->proc_lock);
	list_del_init(&volume->proc_link);
	write_unlock(&volume->cell->proc_lock);

#ifdef CONFIG_AFS_FSCACHE
	fscache_relinquish_cookie(volume->cache, NULL,
				  test_bit(AFS_VOLUME_DELETED, &volume->flags));
	volume->cache = NULL;
#endif

	_leave("");
}

/*
 * Query the VL service to update the volume status.
 */
static int afs_update_volume_status(struct afs_volume *volume, struct key *key)
{
	struct afs_server_list *new, *old, *discard;
	struct afs_vldb_entry *vldb;
	char idbuf[16];
	int ret, idsz;

	_enter("");

	/* We look up an ID by passing it as a decimal string in the
	 * operation's name parameter.
	 */
	idsz = sprintf(idbuf, "%llu", volume->vid);

	vldb = afs_vl_lookup_vldb(volume->cell, key, idbuf, idsz);
	if (IS_ERR(vldb)) {
		ret = PTR_ERR(vldb);
		goto error;
	}

	/* See if the volume got renamed. */
	if (vldb->name_len != volume->name_len ||
	    memcmp(vldb->name, volume->name, vldb->name_len) != 0) {
		/* TODO: Use RCU'd string. */
		memcpy(volume->name, vldb->name, AFS_MAXVOLNAME);
		volume->name_len = vldb->name_len;
	}

	/* See if the volume's server list got updated. */
	new = afs_alloc_server_list(volume->cell, key,
				    vldb, (1 << volume->type));
	if (IS_ERR(new)) {
		ret = PTR_ERR(new);
		goto error_vldb;
	}

	write_lock(&volume->servers_lock);

	discard = new;
	old = volume->servers;
	if (afs_annotate_server_list(new, old)) {
		new->seq = volume->servers_seq + 1;
		volume->servers = new;
		smp_wmb();
		volume->servers_seq++;
		discard = old;
	}

	volume->update_at = ktime_get_real_seconds() + afs_volume_record_life;
	clear_bit(AFS_VOLUME_NEEDS_UPDATE, &volume->flags);
	write_unlock(&volume->servers_lock);
	ret = 0;

	afs_put_serverlist(volume->cell->net, discard);
error_vldb:
	kfree(vldb);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Make sure the volume record is up to date.
 */
int afs_check_volume_status(struct afs_volume *volume, struct key *key)
{
	time64_t now = ktime_get_real_seconds();
	int ret, retries = 0;

	_enter("");

	if (volume->update_at <= now)
		set_bit(AFS_VOLUME_NEEDS_UPDATE, &volume->flags);

retry:
	if (!test_bit(AFS_VOLUME_NEEDS_UPDATE, &volume->flags) &&
	    !test_bit(AFS_VOLUME_WAIT, &volume->flags)) {
		_leave(" = 0");
		return 0;
	}

	if (!test_and_set_bit_lock(AFS_VOLUME_UPDATING, &volume->flags)) {
		ret = afs_update_volume_status(volume, key);
		clear_bit_unlock(AFS_VOLUME_WAIT, &volume->flags);
		clear_bit_unlock(AFS_VOLUME_UPDATING, &volume->flags);
		wake_up_bit(&volume->flags, AFS_VOLUME_WAIT);
		_leave(" = %d", ret);
		return ret;
	}

	if (!test_bit(AFS_VOLUME_WAIT, &volume->flags)) {
		_leave(" = 0 [no wait]");
		return 0;
	}

	ret = wait_on_bit(&volume->flags, AFS_VOLUME_WAIT, TASK_INTERRUPTIBLE);
	if (ret == -ERESTARTSYS) {
		_leave(" = %d", ret);
		return ret;
	}

	retries++;
	if (retries == 4) {
		_leave(" = -ESTALE");
		return -ESTALE;
	}
	goto retry;
}
