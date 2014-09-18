/* AFS volume location management
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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/sched.h>
#include "internal.h"

static unsigned afs_vlocation_timeout = 10;	/* volume location timeout in seconds */
static unsigned afs_vlocation_update_timeout = 10 * 60;

static void afs_vlocation_reaper(struct work_struct *);
static void afs_vlocation_updater(struct work_struct *);

static LIST_HEAD(afs_vlocation_updates);
static LIST_HEAD(afs_vlocation_graveyard);
static DEFINE_SPINLOCK(afs_vlocation_updates_lock);
static DEFINE_SPINLOCK(afs_vlocation_graveyard_lock);
static DECLARE_DELAYED_WORK(afs_vlocation_reap, afs_vlocation_reaper);
static DECLARE_DELAYED_WORK(afs_vlocation_update, afs_vlocation_updater);
static struct workqueue_struct *afs_vlocation_update_worker;

/*
 * iterate through the VL servers in a cell until one of them admits knowing
 * about the volume in question
 */
static int afs_vlocation_access_vl_by_name(struct afs_vlocation *vl,
					   struct key *key,
					   struct afs_cache_vlocation *vldb)
{
	struct afs_cell *cell = vl->cell;
	struct in_addr addr;
	int count, ret;

	_enter("%s,%s", cell->name, vl->vldb.name);

	down_write(&vl->cell->vl_sem);
	ret = -ENOMEDIUM;
	for (count = cell->vl_naddrs; count > 0; count--) {
		addr = cell->vl_addrs[cell->vl_curr_svix];

		_debug("CellServ[%hu]: %08x", cell->vl_curr_svix, addr.s_addr);

		/* attempt to access the VL server */
		ret = afs_vl_get_entry_by_name(&addr, key, vl->vldb.name, vldb,
					       &afs_sync_call);
		switch (ret) {
		case 0:
			goto out;
		case -ENOMEM:
		case -ENONET:
		case -ENETUNREACH:
		case -EHOSTUNREACH:
		case -ECONNREFUSED:
			if (ret == -ENOMEM || ret == -ENONET)
				goto out;
			goto rotate;
		case -ENOMEDIUM:
		case -EKEYREJECTED:
		case -EKEYEXPIRED:
			goto out;
		default:
			ret = -EIO;
			goto rotate;
		}

		/* rotate the server records upon lookup failure */
	rotate:
		cell->vl_curr_svix++;
		cell->vl_curr_svix %= cell->vl_naddrs;
	}

out:
	up_write(&vl->cell->vl_sem);
	_leave(" = %d", ret);
	return ret;
}

/*
 * iterate through the VL servers in a cell until one of them admits knowing
 * about the volume in question
 */
static int afs_vlocation_access_vl_by_id(struct afs_vlocation *vl,
					 struct key *key,
					 afs_volid_t volid,
					 afs_voltype_t voltype,
					 struct afs_cache_vlocation *vldb)
{
	struct afs_cell *cell = vl->cell;
	struct in_addr addr;
	int count, ret;

	_enter("%s,%x,%d,", cell->name, volid, voltype);

	down_write(&vl->cell->vl_sem);
	ret = -ENOMEDIUM;
	for (count = cell->vl_naddrs; count > 0; count--) {
		addr = cell->vl_addrs[cell->vl_curr_svix];

		_debug("CellServ[%hu]: %08x", cell->vl_curr_svix, addr.s_addr);

		/* attempt to access the VL server */
		ret = afs_vl_get_entry_by_id(&addr, key, volid, voltype, vldb,
					     &afs_sync_call);
		switch (ret) {
		case 0:
			goto out;
		case -ENOMEM:
		case -ENONET:
		case -ENETUNREACH:
		case -EHOSTUNREACH:
		case -ECONNREFUSED:
			if (ret == -ENOMEM || ret == -ENONET)
				goto out;
			goto rotate;
		case -EBUSY:
			vl->upd_busy_cnt++;
			if (vl->upd_busy_cnt <= 3) {
				if (vl->upd_busy_cnt > 1) {
					/* second+ BUSY - sleep a little bit */
					set_current_state(TASK_UNINTERRUPTIBLE);
					schedule_timeout(1);
					__set_current_state(TASK_RUNNING);
				}
				continue;
			}
			break;
		case -ENOMEDIUM:
			vl->upd_rej_cnt++;
			goto rotate;
		default:
			ret = -EIO;
			goto rotate;
		}

		/* rotate the server records upon lookup failure */
	rotate:
		cell->vl_curr_svix++;
		cell->vl_curr_svix %= cell->vl_naddrs;
		vl->upd_busy_cnt = 0;
	}

out:
	if (ret < 0 && vl->upd_rej_cnt > 0) {
		printk(KERN_NOTICE "kAFS:"
		       " Active volume no longer valid '%s'\n",
		       vl->vldb.name);
		vl->valid = 0;
		ret = -ENOMEDIUM;
	}

	up_write(&vl->cell->vl_sem);
	_leave(" = %d", ret);
	return ret;
}

/*
 * allocate a volume location record
 */
static struct afs_vlocation *afs_vlocation_alloc(struct afs_cell *cell,
						 const char *name,
						 size_t namesz)
{
	struct afs_vlocation *vl;

	vl = kzalloc(sizeof(struct afs_vlocation), GFP_KERNEL);
	if (vl) {
		vl->cell = cell;
		vl->state = AFS_VL_NEW;
		atomic_set(&vl->usage, 1);
		INIT_LIST_HEAD(&vl->link);
		INIT_LIST_HEAD(&vl->grave);
		INIT_LIST_HEAD(&vl->update);
		init_waitqueue_head(&vl->waitq);
		spin_lock_init(&vl->lock);
		memcpy(vl->vldb.name, name, namesz);
	}

	_leave(" = %p", vl);
	return vl;
}

/*
 * update record if we found it in the cache
 */
static int afs_vlocation_update_record(struct afs_vlocation *vl,
				       struct key *key,
				       struct afs_cache_vlocation *vldb)
{
	afs_voltype_t voltype;
	afs_volid_t vid;
	int ret;

	/* try to look up a cached volume in the cell VL databases by ID */
	_debug("Locally Cached: %s %02x { %08x(%x) %08x(%x) %08x(%x) }",
	       vl->vldb.name,
	       vl->vldb.vidmask,
	       ntohl(vl->vldb.servers[0].s_addr),
	       vl->vldb.srvtmask[0],
	       ntohl(vl->vldb.servers[1].s_addr),
	       vl->vldb.srvtmask[1],
	       ntohl(vl->vldb.servers[2].s_addr),
	       vl->vldb.srvtmask[2]);

	_debug("Vids: %08x %08x %08x",
	       vl->vldb.vid[0],
	       vl->vldb.vid[1],
	       vl->vldb.vid[2]);

	if (vl->vldb.vidmask & AFS_VOL_VTM_RW) {
		vid = vl->vldb.vid[0];
		voltype = AFSVL_RWVOL;
	} else if (vl->vldb.vidmask & AFS_VOL_VTM_RO) {
		vid = vl->vldb.vid[1];
		voltype = AFSVL_ROVOL;
	} else if (vl->vldb.vidmask & AFS_VOL_VTM_BAK) {
		vid = vl->vldb.vid[2];
		voltype = AFSVL_BACKVOL;
	} else {
		BUG();
		vid = 0;
		voltype = 0;
	}

	/* contact the server to make sure the volume is still available
	 * - TODO: need to handle disconnected operation here
	 */
	ret = afs_vlocation_access_vl_by_id(vl, key, vid, voltype, vldb);
	switch (ret) {
		/* net error */
	default:
		printk(KERN_WARNING "kAFS:"
		       " failed to update volume '%s' (%x) up in '%s': %d\n",
		       vl->vldb.name, vid, vl->cell->name, ret);
		_leave(" = %d", ret);
		return ret;

		/* pulled from local cache into memory */
	case 0:
		_leave(" = 0");
		return 0;

		/* uh oh... looks like the volume got deleted */
	case -ENOMEDIUM:
		printk(KERN_ERR "kAFS:"
		       " volume '%s' (%x) does not exist '%s'\n",
		       vl->vldb.name, vid, vl->cell->name);

		/* TODO: make existing record unavailable */
		_leave(" = %d", ret);
		return ret;
	}
}

/*
 * apply the update to a VL record
 */
static void afs_vlocation_apply_update(struct afs_vlocation *vl,
				       struct afs_cache_vlocation *vldb)
{
	_debug("Done VL Lookup: %s %02x { %08x(%x) %08x(%x) %08x(%x) }",
	       vldb->name, vldb->vidmask,
	       ntohl(vldb->servers[0].s_addr), vldb->srvtmask[0],
	       ntohl(vldb->servers[1].s_addr), vldb->srvtmask[1],
	       ntohl(vldb->servers[2].s_addr), vldb->srvtmask[2]);

	_debug("Vids: %08x %08x %08x",
	       vldb->vid[0], vldb->vid[1], vldb->vid[2]);

	if (strcmp(vldb->name, vl->vldb.name) != 0)
		printk(KERN_NOTICE "kAFS:"
		       " name of volume '%s' changed to '%s' on server\n",
		       vl->vldb.name, vldb->name);

	vl->vldb = *vldb;

#ifdef CONFIG_AFS_FSCACHE
	fscache_update_cookie(vl->cache);
#endif
}

/*
 * fill in a volume location record, consulting the cache and the VL server
 * both
 */
static int afs_vlocation_fill_in_record(struct afs_vlocation *vl,
					struct key *key)
{
	struct afs_cache_vlocation vldb;
	int ret;

	_enter("");

	ASSERTCMP(vl->valid, ==, 0);

	memset(&vldb, 0, sizeof(vldb));

	/* see if we have an in-cache copy (will set vl->valid if there is) */
#ifdef CONFIG_AFS_FSCACHE
	vl->cache = fscache_acquire_cookie(vl->cell->cache,
					   &afs_vlocation_cache_index_def, vl,
					   true);
#endif

	if (vl->valid) {
		/* try to update a known volume in the cell VL databases by
		 * ID as the name may have changed */
		_debug("found in cache");
		ret = afs_vlocation_update_record(vl, key, &vldb);
	} else {
		/* try to look up an unknown volume in the cell VL databases by
		 * name */
		ret = afs_vlocation_access_vl_by_name(vl, key, &vldb);
		if (ret < 0) {
			printk("kAFS: failed to locate '%s' in cell '%s'\n",
			       vl->vldb.name, vl->cell->name);
			return ret;
		}
	}

	afs_vlocation_apply_update(vl, &vldb);
	_leave(" = 0");
	return 0;
}

/*
 * queue a vlocation record for updates
 */
static void afs_vlocation_queue_for_updates(struct afs_vlocation *vl)
{
	struct afs_vlocation *xvl;

	/* wait at least 10 minutes before updating... */
	vl->update_at = get_seconds() + afs_vlocation_update_timeout;

	spin_lock(&afs_vlocation_updates_lock);

	if (!list_empty(&afs_vlocation_updates)) {
		/* ... but wait at least 1 second more than the newest record
		 * already queued so that we don't spam the VL server suddenly
		 * with lots of requests
		 */
		xvl = list_entry(afs_vlocation_updates.prev,
				 struct afs_vlocation, update);
		if (vl->update_at <= xvl->update_at)
			vl->update_at = xvl->update_at + 1;
	} else {
		queue_delayed_work(afs_vlocation_update_worker,
				   &afs_vlocation_update,
				   afs_vlocation_update_timeout * HZ);
	}

	list_add_tail(&vl->update, &afs_vlocation_updates);
	spin_unlock(&afs_vlocation_updates_lock);
}

/*
 * lookup volume location
 * - iterate through the VL servers in a cell until one of them admits knowing
 *   about the volume in question
 * - lookup in the local cache if not able to find on the VL server
 * - insert/update in the local cache if did get a VL response
 */
struct afs_vlocation *afs_vlocation_lookup(struct afs_cell *cell,
					   struct key *key,
					   const char *name,
					   size_t namesz)
{
	struct afs_vlocation *vl;
	int ret;

	_enter("{%s},{%x},%*.*s,%zu",
	       cell->name, key_serial(key),
	       (int) namesz, (int) namesz, name, namesz);

	if (namesz >= sizeof(vl->vldb.name)) {
		_leave(" = -ENAMETOOLONG");
		return ERR_PTR(-ENAMETOOLONG);
	}

	/* see if we have an in-memory copy first */
	down_write(&cell->vl_sem);
	spin_lock(&cell->vl_lock);
	list_for_each_entry(vl, &cell->vl_list, link) {
		if (vl->vldb.name[namesz] != '\0')
			continue;
		if (memcmp(vl->vldb.name, name, namesz) == 0)
			goto found_in_memory;
	}
	spin_unlock(&cell->vl_lock);

	/* not in the cell's in-memory lists - create a new record */
	vl = afs_vlocation_alloc(cell, name, namesz);
	if (!vl) {
		up_write(&cell->vl_sem);
		return ERR_PTR(-ENOMEM);
	}

	afs_get_cell(cell);

	list_add_tail(&vl->link, &cell->vl_list);
	vl->state = AFS_VL_CREATING;
	up_write(&cell->vl_sem);

fill_in_record:
	ret = afs_vlocation_fill_in_record(vl, key);
	if (ret < 0)
		goto error_abandon;
	spin_lock(&vl->lock);
	vl->state = AFS_VL_VALID;
	spin_unlock(&vl->lock);
	wake_up(&vl->waitq);

	/* update volume entry in local cache */
#ifdef CONFIG_AFS_FSCACHE
	fscache_update_cookie(vl->cache);
#endif

	/* schedule for regular updates */
	afs_vlocation_queue_for_updates(vl);
	goto success;

found_in_memory:
	/* found in memory */
	_debug("found in memory");
	atomic_inc(&vl->usage);
	spin_unlock(&cell->vl_lock);
	if (!list_empty(&vl->grave)) {
		spin_lock(&afs_vlocation_graveyard_lock);
		list_del_init(&vl->grave);
		spin_unlock(&afs_vlocation_graveyard_lock);
	}
	up_write(&cell->vl_sem);

	/* see if it was an abandoned record that we might try filling in */
	spin_lock(&vl->lock);
	while (vl->state != AFS_VL_VALID) {
		afs_vlocation_state_t state = vl->state;

		_debug("invalid [state %d]", state);

		if (state == AFS_VL_NEW || state == AFS_VL_NO_VOLUME) {
			vl->state = AFS_VL_CREATING;
			spin_unlock(&vl->lock);
			goto fill_in_record;
		}

		/* must now wait for creation or update by someone else to
		 * complete */
		_debug("wait");

		spin_unlock(&vl->lock);
		ret = wait_event_interruptible(vl->waitq,
					       vl->state == AFS_VL_NEW ||
					       vl->state == AFS_VL_VALID ||
					       vl->state == AFS_VL_NO_VOLUME);
		if (ret < 0)
			goto error;
		spin_lock(&vl->lock);
	}
	spin_unlock(&vl->lock);

success:
	_leave(" = %p", vl);
	return vl;

error_abandon:
	spin_lock(&vl->lock);
	vl->state = AFS_VL_NEW;
	spin_unlock(&vl->lock);
	wake_up(&vl->waitq);
error:
	ASSERT(vl != NULL);
	afs_put_vlocation(vl);
	_leave(" = %d", ret);
	return ERR_PTR(ret);
}

/*
 * finish using a volume location record
 */
void afs_put_vlocation(struct afs_vlocation *vl)
{
	if (!vl)
		return;

	_enter("%s", vl->vldb.name);

	ASSERTCMP(atomic_read(&vl->usage), >, 0);

	if (likely(!atomic_dec_and_test(&vl->usage))) {
		_leave("");
		return;
	}

	spin_lock(&afs_vlocation_graveyard_lock);
	if (atomic_read(&vl->usage) == 0) {
		_debug("buried");
		list_move_tail(&vl->grave, &afs_vlocation_graveyard);
		vl->time_of_death = get_seconds();
		queue_delayed_work(afs_wq, &afs_vlocation_reap,
				   afs_vlocation_timeout * HZ);

		/* suspend updates on this record */
		if (!list_empty(&vl->update)) {
			spin_lock(&afs_vlocation_updates_lock);
			list_del_init(&vl->update);
			spin_unlock(&afs_vlocation_updates_lock);
		}
	}
	spin_unlock(&afs_vlocation_graveyard_lock);
	_leave(" [killed?]");
}

/*
 * destroy a dead volume location record
 */
static void afs_vlocation_destroy(struct afs_vlocation *vl)
{
	_enter("%p", vl);

#ifdef CONFIG_AFS_FSCACHE
	fscache_relinquish_cookie(vl->cache, 0);
#endif
	afs_put_cell(vl->cell);
	kfree(vl);
}

/*
 * reap dead volume location records
 */
static void afs_vlocation_reaper(struct work_struct *work)
{
	LIST_HEAD(corpses);
	struct afs_vlocation *vl;
	unsigned long delay, expiry;
	time_t now;

	_enter("");

	now = get_seconds();
	spin_lock(&afs_vlocation_graveyard_lock);

	while (!list_empty(&afs_vlocation_graveyard)) {
		vl = list_entry(afs_vlocation_graveyard.next,
				struct afs_vlocation, grave);

		_debug("check %p", vl);

		/* the queue is ordered most dead first */
		expiry = vl->time_of_death + afs_vlocation_timeout;
		if (expiry > now) {
			delay = (expiry - now) * HZ;
			_debug("delay %lu", delay);
			mod_delayed_work(afs_wq, &afs_vlocation_reap, delay);
			break;
		}

		spin_lock(&vl->cell->vl_lock);
		if (atomic_read(&vl->usage) > 0) {
			_debug("no reap");
			list_del_init(&vl->grave);
		} else {
			_debug("reap");
			list_move_tail(&vl->grave, &corpses);
			list_del_init(&vl->link);
		}
		spin_unlock(&vl->cell->vl_lock);
	}

	spin_unlock(&afs_vlocation_graveyard_lock);

	/* now reap the corpses we've extracted */
	while (!list_empty(&corpses)) {
		vl = list_entry(corpses.next, struct afs_vlocation, grave);
		list_del(&vl->grave);
		afs_vlocation_destroy(vl);
	}

	_leave("");
}

/*
 * initialise the VL update process
 */
int __init afs_vlocation_update_init(void)
{
	afs_vlocation_update_worker =
		create_singlethread_workqueue("kafs_vlupdated");
	return afs_vlocation_update_worker ? 0 : -ENOMEM;
}

/*
 * discard all the volume location records for rmmod
 */
void afs_vlocation_purge(void)
{
	afs_vlocation_timeout = 0;

	spin_lock(&afs_vlocation_updates_lock);
	list_del_init(&afs_vlocation_updates);
	spin_unlock(&afs_vlocation_updates_lock);
	mod_delayed_work(afs_vlocation_update_worker, &afs_vlocation_update, 0);
	destroy_workqueue(afs_vlocation_update_worker);

	mod_delayed_work(afs_wq, &afs_vlocation_reap, 0);
}

/*
 * update a volume location
 */
static void afs_vlocation_updater(struct work_struct *work)
{
	struct afs_cache_vlocation vldb;
	struct afs_vlocation *vl, *xvl;
	time_t now;
	long timeout;
	int ret;

	_enter("");

	now = get_seconds();

	/* find a record to update */
	spin_lock(&afs_vlocation_updates_lock);
	for (;;) {
		if (list_empty(&afs_vlocation_updates)) {
			spin_unlock(&afs_vlocation_updates_lock);
			_leave(" [nothing]");
			return;
		}

		vl = list_entry(afs_vlocation_updates.next,
				struct afs_vlocation, update);
		if (atomic_read(&vl->usage) > 0)
			break;
		list_del_init(&vl->update);
	}

	timeout = vl->update_at - now;
	if (timeout > 0) {
		queue_delayed_work(afs_vlocation_update_worker,
				   &afs_vlocation_update, timeout * HZ);
		spin_unlock(&afs_vlocation_updates_lock);
		_leave(" [nothing]");
		return;
	}

	list_del_init(&vl->update);
	atomic_inc(&vl->usage);
	spin_unlock(&afs_vlocation_updates_lock);

	/* we can now perform the update */
	_debug("update %s", vl->vldb.name);
	vl->state = AFS_VL_UPDATING;
	vl->upd_rej_cnt = 0;
	vl->upd_busy_cnt = 0;

	ret = afs_vlocation_update_record(vl, NULL, &vldb);
	spin_lock(&vl->lock);
	switch (ret) {
	case 0:
		afs_vlocation_apply_update(vl, &vldb);
		vl->state = AFS_VL_VALID;
		break;
	case -ENOMEDIUM:
		vl->state = AFS_VL_VOLUME_DELETED;
		break;
	default:
		vl->state = AFS_VL_UNCERTAIN;
		break;
	}
	spin_unlock(&vl->lock);
	wake_up(&vl->waitq);

	/* and then reschedule */
	_debug("reschedule");
	vl->update_at = get_seconds() + afs_vlocation_update_timeout;

	spin_lock(&afs_vlocation_updates_lock);

	if (!list_empty(&afs_vlocation_updates)) {
		/* next update in 10 minutes, but wait at least 1 second more
		 * than the newest record already queued so that we don't spam
		 * the VL server suddenly with lots of requests
		 */
		xvl = list_entry(afs_vlocation_updates.prev,
				 struct afs_vlocation, update);
		if (vl->update_at <= xvl->update_at)
			vl->update_at = xvl->update_at + 1;
		xvl = list_entry(afs_vlocation_updates.next,
				 struct afs_vlocation, update);
		timeout = xvl->update_at - now;
		if (timeout < 0)
			timeout = 0;
	} else {
		timeout = afs_vlocation_update_timeout;
	}

	ASSERT(list_empty(&vl->update));

	list_add_tail(&vl->update, &afs_vlocation_updates);

	_debug("timeout %ld", timeout);
	queue_delayed_work(afs_vlocation_update_worker,
			   &afs_vlocation_update, timeout * HZ);
	spin_unlock(&afs_vlocation_updates_lock);
	afs_put_vlocation(vl);
}
