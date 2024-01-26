// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS volume management
 *
 * Copyright (C) 2002, 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include "internal.h"

static unsigned __read_mostly afs_volume_record_life = 60 * 60;

static void afs_destroy_volume(struct work_struct *work);

/*
 * Insert a volume into a cell.  If there's an existing volume record, that is
 * returned instead with a ref held.
 */
static struct afs_volume *afs_insert_volume_into_cell(struct afs_cell *cell,
						      struct afs_volume *volume)
{
	struct afs_volume *p;
	struct rb_node *parent = NULL, **pp;

	write_seqlock(&cell->volume_lock);

	pp = &cell->volumes.rb_node;
	while (*pp) {
		parent = *pp;
		p = rb_entry(parent, struct afs_volume, cell_node);
		if (p->vid < volume->vid) {
			pp = &(*pp)->rb_left;
		} else if (p->vid > volume->vid) {
			pp = &(*pp)->rb_right;
		} else {
			if (afs_try_get_volume(p, afs_volume_trace_get_cell_insert)) {
				volume = p;
				goto found;
			}

			set_bit(AFS_VOLUME_RM_TREE, &volume->flags);
			rb_replace_node_rcu(&p->cell_node, &volume->cell_node, &cell->volumes);
		}
	}

	rb_link_node_rcu(&volume->cell_node, parent, pp);
	rb_insert_color(&volume->cell_node, &cell->volumes);
	hlist_add_head_rcu(&volume->proc_link, &cell->proc_volumes);

found:
	write_sequnlock(&cell->volume_lock);
	return volume;

}

static void afs_remove_volume_from_cell(struct afs_volume *volume)
{
	struct afs_cell *cell = volume->cell;

	if (!hlist_unhashed(&volume->proc_link)) {
		trace_afs_volume(volume->vid, refcount_read(&cell->ref),
				 afs_volume_trace_remove);
		write_seqlock(&cell->volume_lock);
		hlist_del_rcu(&volume->proc_link);
		if (!test_and_set_bit(AFS_VOLUME_RM_TREE, &volume->flags))
			rb_erase(&volume->cell_node, &cell->volumes);
		write_sequnlock(&cell->volume_lock);
	}
}

/*
 * Allocate a volume record and load it up from a vldb record.
 */
static struct afs_volume *afs_alloc_volume(struct afs_fs_context *params,
					   struct afs_vldb_entry *vldb,
					   struct afs_server_list **_slist)
{
	struct afs_server_list *slist;
	struct afs_volume *volume;
	int ret = -ENOMEM, i;

	volume = kzalloc(sizeof(struct afs_volume), GFP_KERNEL);
	if (!volume)
		goto error_0;

	volume->vid		= vldb->vid[params->type];
	volume->update_at	= ktime_get_real_seconds() + afs_volume_record_life;
	volume->cell		= afs_get_cell(params->cell, afs_cell_trace_get_vol);
	volume->type		= params->type;
	volume->type_force	= params->force;
	volume->name_len	= vldb->name_len;
	volume->creation_time	= TIME64_MIN;
	volume->update_time	= TIME64_MIN;

	refcount_set(&volume->ref, 1);
	INIT_HLIST_NODE(&volume->proc_link);
	INIT_WORK(&volume->destructor, afs_destroy_volume);
	rwlock_init(&volume->servers_lock);
	mutex_init(&volume->volsync_lock);
	mutex_init(&volume->cb_check_lock);
	rwlock_init(&volume->cb_v_break_lock);
	INIT_LIST_HEAD(&volume->open_mmaps);
	init_rwsem(&volume->open_mmaps_lock);
	memcpy(volume->name, vldb->name, vldb->name_len + 1);

	for (i = 0; i < AFS_MAXTYPES; i++)
		volume->vids[i] = vldb->vid[i];

	slist = afs_alloc_server_list(volume, params->key, vldb);
	if (IS_ERR(slist)) {
		ret = PTR_ERR(slist);
		goto error_1;
	}

	*_slist = slist;
	rcu_assign_pointer(volume->servers, slist);
	trace_afs_volume(volume->vid, 1, afs_volume_trace_alloc);
	return volume;

error_1:
	afs_put_cell(volume->cell, afs_cell_trace_put_vol);
	kfree(volume);
error_0:
	return ERR_PTR(ret);
}

/*
 * Look up or allocate a volume record.
 */
static struct afs_volume *afs_lookup_volume(struct afs_fs_context *params,
					    struct afs_vldb_entry *vldb)
{
	struct afs_server_list *slist;
	struct afs_volume *candidate, *volume;

	candidate = afs_alloc_volume(params, vldb, &slist);
	if (IS_ERR(candidate))
		return candidate;

	volume = afs_insert_volume_into_cell(params->cell, candidate);
	if (volume == candidate)
		afs_attach_volume_to_servers(volume, slist);
	else
		afs_put_volume(candidate, afs_volume_trace_put_cell_dup);
	return volume;
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

	volume = afs_lookup_volume(params, vldb);

error:
	kfree(vldb);
	return volume;
}

/*
 * Destroy a volume record
 */
static void afs_destroy_volume(struct work_struct *work)
{
	struct afs_volume *volume = container_of(work, struct afs_volume, destructor);
	struct afs_server_list *slist = rcu_access_pointer(volume->servers);

	_enter("%p", volume);

#ifdef CONFIG_AFS_FSCACHE
	ASSERTCMP(volume->cache, ==, NULL);
#endif

	afs_detach_volume_from_servers(volume, slist);
	afs_remove_volume_from_cell(volume);
	afs_put_serverlist(volume->cell->net, slist);
	afs_put_cell(volume->cell, afs_cell_trace_put_vol);
	trace_afs_volume(volume->vid, refcount_read(&volume->ref),
			 afs_volume_trace_free);
	kfree_rcu(volume, rcu);

	_leave(" [destroyed]");
}

/*
 * Try to get a reference on a volume record.
 */
bool afs_try_get_volume(struct afs_volume *volume, enum afs_volume_trace reason)
{
	int r;

	if (__refcount_inc_not_zero(&volume->ref, &r)) {
		trace_afs_volume(volume->vid, r + 1, reason);
		return true;
	}
	return false;
}

/*
 * Get a reference on a volume record.
 */
struct afs_volume *afs_get_volume(struct afs_volume *volume,
				  enum afs_volume_trace reason)
{
	if (volume) {
		int r;

		__refcount_inc(&volume->ref, &r);
		trace_afs_volume(volume->vid, r + 1, reason);
	}
	return volume;
}


/*
 * Drop a reference on a volume record.
 */
void afs_put_volume(struct afs_volume *volume, enum afs_volume_trace reason)
{
	if (volume) {
		afs_volid_t vid = volume->vid;
		bool zero;
		int r;

		zero = __refcount_dec_and_test(&volume->ref, &r);
		trace_afs_volume(vid, r - 1, reason);
		if (zero)
			schedule_work(&volume->destructor);
	}
}

/*
 * Activate a volume.
 */
int afs_activate_volume(struct afs_volume *volume)
{
#ifdef CONFIG_AFS_FSCACHE
	struct fscache_volume *vcookie;
	char *name;

	name = kasprintf(GFP_KERNEL, "afs,%s,%llx",
			 volume->cell->name, volume->vid);
	if (!name)
		return -ENOMEM;

	vcookie = fscache_acquire_volume(name, NULL, NULL, 0);
	if (IS_ERR(vcookie)) {
		if (vcookie != ERR_PTR(-EBUSY)) {
			kfree(name);
			return PTR_ERR(vcookie);
		}
		pr_err("AFS: Cache volume key already in use (%s)\n", name);
		vcookie = NULL;
	}
	volume->cache = vcookie;
	kfree(name);
#endif
	return 0;
}

/*
 * Deactivate a volume.
 */
void afs_deactivate_volume(struct afs_volume *volume)
{
	_enter("%s", volume->name);

#ifdef CONFIG_AFS_FSCACHE
	fscache_relinquish_volume(volume->cache, NULL,
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
	new = afs_alloc_server_list(volume, key, vldb);
	if (IS_ERR(new)) {
		ret = PTR_ERR(new);
		goto error_vldb;
	}

	write_lock(&volume->servers_lock);

	discard = new;
	old = rcu_dereference_protected(volume->servers,
					lockdep_is_held(&volume->servers_lock));
	if (afs_annotate_server_list(new, old)) {
		new->seq = volume->servers_seq + 1;
		rcu_assign_pointer(volume->servers, new);
		smp_wmb();
		volume->servers_seq++;
		discard = old;
	}

	/* Check more often if replication is ongoing. */
	if (new->ro_replicating)
		volume->update_at = ktime_get_real_seconds() + 10 * 60;
	else
		volume->update_at = ktime_get_real_seconds() + afs_volume_record_life;
	write_unlock(&volume->servers_lock);

	if (discard == old)
		afs_reattach_volume_to_servers(volume, new, old);
	afs_put_serverlist(volume->cell->net, discard);
	ret = 0;
error_vldb:
	kfree(vldb);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * Make sure the volume record is up to date.
 */
int afs_check_volume_status(struct afs_volume *volume, struct afs_operation *op)
{
	int ret, retries = 0;

	_enter("");

retry:
	if (test_bit(AFS_VOLUME_WAIT, &volume->flags))
		goto wait;
	if (volume->update_at <= ktime_get_real_seconds() ||
	    test_bit(AFS_VOLUME_NEEDS_UPDATE, &volume->flags))
		goto update;
	_leave(" = 0");
	return 0;

update:
	if (!test_and_set_bit_lock(AFS_VOLUME_UPDATING, &volume->flags)) {
		clear_bit(AFS_VOLUME_NEEDS_UPDATE, &volume->flags);
		ret = afs_update_volume_status(volume, op->key);
		if (ret < 0)
			set_bit(AFS_VOLUME_NEEDS_UPDATE, &volume->flags);
		clear_bit_unlock(AFS_VOLUME_WAIT, &volume->flags);
		clear_bit_unlock(AFS_VOLUME_UPDATING, &volume->flags);
		wake_up_bit(&volume->flags, AFS_VOLUME_WAIT);
		_leave(" = %d", ret);
		return ret;
	}

wait:
	if (!test_bit(AFS_VOLUME_WAIT, &volume->flags)) {
		_leave(" = 0 [no wait]");
		return 0;
	}

	ret = wait_on_bit(&volume->flags, AFS_VOLUME_WAIT,
			  (op->flags & AFS_OPERATION_UNINTR) ?
			  TASK_UNINTERRUPTIBLE : TASK_INTERRUPTIBLE);
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
