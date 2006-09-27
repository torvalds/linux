/* vlocation.c: volume location management
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
#include "cell.h"
#include "cmservice.h"
#include "fsclient.h"
#include "vlclient.h"
#include "kafstimod.h"
#include <rxrpc/connection.h>
#include "internal.h"

#define AFS_VLDB_TIMEOUT HZ*1000

static void afs_vlocation_update_timer(struct afs_timer *timer);
static void afs_vlocation_update_attend(struct afs_async_op *op);
static void afs_vlocation_update_discard(struct afs_async_op *op);
static void __afs_put_vlocation(struct afs_vlocation *vlocation);

static void __afs_vlocation_timeout(struct afs_timer *timer)
{
	struct afs_vlocation *vlocation =
		list_entry(timer, struct afs_vlocation, timeout);

	_debug("VL TIMEOUT [%s{u=%d}]",
	       vlocation->vldb.name, atomic_read(&vlocation->usage));

	afs_vlocation_do_timeout(vlocation);
}

static const struct afs_timer_ops afs_vlocation_timer_ops = {
	.timed_out	= __afs_vlocation_timeout,
};

static const struct afs_timer_ops afs_vlocation_update_timer_ops = {
	.timed_out	= afs_vlocation_update_timer,
};

static const struct afs_async_op_ops afs_vlocation_update_op_ops = {
	.attend		= afs_vlocation_update_attend,
	.discard	= afs_vlocation_update_discard,
};

static LIST_HEAD(afs_vlocation_update_pendq);	/* queue of VLs awaiting update */
static struct afs_vlocation *afs_vlocation_update;	/* VL currently being updated */
static DEFINE_SPINLOCK(afs_vlocation_update_lock); /* lock guarding update queue */

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

/*****************************************************************************/
/*
 * iterate through the VL servers in a cell until one of them admits knowing
 * about the volume in question
 * - caller must have cell->vl_sem write-locked
 */
static int afs_vlocation_access_vl_by_name(struct afs_vlocation *vlocation,
					   const char *name,
					   unsigned namesz,
					   struct afs_cache_vlocation *vldb)
{
	struct afs_server *server = NULL;
	struct afs_cell *cell = vlocation->cell;
	int count, ret;

	_enter("%s,%*.*s,%u", cell->name, namesz, namesz, name, namesz);

	ret = -ENOMEDIUM;
	for (count = cell->vl_naddrs; count > 0; count--) {
		_debug("CellServ[%hu]: %08x",
		       cell->vl_curr_svix,
		       cell->vl_addrs[cell->vl_curr_svix].s_addr);

		/* try and create a server */
		ret = afs_server_lookup(cell,
					&cell->vl_addrs[cell->vl_curr_svix],
					&server);
		switch (ret) {
		case 0:
			break;
		case -ENOMEM:
		case -ENONET:
			goto out;
		default:
			goto rotate;
		}

		/* attempt to access the VL server */
		ret = afs_rxvl_get_entry_by_name(server, name, namesz, vldb);
		switch (ret) {
		case 0:
			afs_put_server(server);
			goto out;
		case -ENOMEM:
		case -ENONET:
		case -ENETUNREACH:
		case -EHOSTUNREACH:
		case -ECONNREFUSED:
			down_write(&server->sem);
			if (server->vlserver) {
				rxrpc_put_connection(server->vlserver);
				server->vlserver = NULL;
			}
			up_write(&server->sem);
			afs_put_server(server);
			if (ret == -ENOMEM || ret == -ENONET)
				goto out;
			goto rotate;
		case -ENOMEDIUM:
			afs_put_server(server);
			goto out;
		default:
			afs_put_server(server);
			ret = -ENOMEDIUM;
			goto rotate;
		}

		/* rotate the server records upon lookup failure */
	rotate:
		cell->vl_curr_svix++;
		cell->vl_curr_svix %= cell->vl_naddrs;
	}

 out:
	_leave(" = %d", ret);
	return ret;

} /* end afs_vlocation_access_vl_by_name() */

/*****************************************************************************/
/*
 * iterate through the VL servers in a cell until one of them admits knowing
 * about the volume in question
 * - caller must have cell->vl_sem write-locked
 */
static int afs_vlocation_access_vl_by_id(struct afs_vlocation *vlocation,
					 afs_volid_t volid,
					 afs_voltype_t voltype,
					 struct afs_cache_vlocation *vldb)
{
	struct afs_server *server = NULL;
	struct afs_cell *cell = vlocation->cell;
	int count, ret;

	_enter("%s,%x,%d,", cell->name, volid, voltype);

	ret = -ENOMEDIUM;
	for (count = cell->vl_naddrs; count > 0; count--) {
		_debug("CellServ[%hu]: %08x",
		       cell->vl_curr_svix,
		       cell->vl_addrs[cell->vl_curr_svix].s_addr);

		/* try and create a server */
		ret = afs_server_lookup(cell,
					&cell->vl_addrs[cell->vl_curr_svix],
					&server);
		switch (ret) {
		case 0:
			break;
		case -ENOMEM:
		case -ENONET:
			goto out;
		default:
			goto rotate;
		}

		/* attempt to access the VL server */
		ret = afs_rxvl_get_entry_by_id(server, volid, voltype, vldb);
		switch (ret) {
		case 0:
			afs_put_server(server);
			goto out;
		case -ENOMEM:
		case -ENONET:
		case -ENETUNREACH:
		case -EHOSTUNREACH:
		case -ECONNREFUSED:
			down_write(&server->sem);
			if (server->vlserver) {
				rxrpc_put_connection(server->vlserver);
				server->vlserver = NULL;
			}
			up_write(&server->sem);
			afs_put_server(server);
			if (ret == -ENOMEM || ret == -ENONET)
				goto out;
			goto rotate;
		case -ENOMEDIUM:
			afs_put_server(server);
			goto out;
		default:
			afs_put_server(server);
			ret = -ENOMEDIUM;
			goto rotate;
		}

		/* rotate the server records upon lookup failure */
	rotate:
		cell->vl_curr_svix++;
		cell->vl_curr_svix %= cell->vl_naddrs;
	}

 out:
	_leave(" = %d", ret);
	return ret;

} /* end afs_vlocation_access_vl_by_id() */

/*****************************************************************************/
/*
 * lookup volume location
 * - caller must have cell->vol_sem write-locked
 * - iterate through the VL servers in a cell until one of them admits knowing
 *   about the volume in question
 * - lookup in the local cache if not able to find on the VL server
 * - insert/update in the local cache if did get a VL response
 */
int afs_vlocation_lookup(struct afs_cell *cell,
			 const char *name,
			 unsigned namesz,
			 struct afs_vlocation **_vlocation)
{
	struct afs_cache_vlocation vldb;
	struct afs_vlocation *vlocation;
	afs_voltype_t voltype;
	afs_volid_t vid;
	int active = 0, ret;

	_enter("{%s},%*.*s,%u,", cell->name, namesz, namesz, name, namesz);

	if (namesz > sizeof(vlocation->vldb.name)) {
		_leave(" = -ENAMETOOLONG");
		return -ENAMETOOLONG;
	}

	/* search the cell's active list first */
	list_for_each_entry(vlocation, &cell->vl_list, link) {
		if (namesz < sizeof(vlocation->vldb.name) &&
		    vlocation->vldb.name[namesz] != '\0')
			continue;

		if (memcmp(vlocation->vldb.name, name, namesz) == 0)
			goto found_in_memory;
	}

	/* search the cell's graveyard list second */
	spin_lock(&cell->vl_gylock);
	list_for_each_entry(vlocation, &cell->vl_graveyard, link) {
		if (namesz < sizeof(vlocation->vldb.name) &&
		    vlocation->vldb.name[namesz] != '\0')
			continue;

		if (memcmp(vlocation->vldb.name, name, namesz) == 0)
			goto found_in_graveyard;
	}
	spin_unlock(&cell->vl_gylock);

	/* not in the cell's in-memory lists - create a new record */
	vlocation = kzalloc(sizeof(struct afs_vlocation), GFP_KERNEL);
	if (!vlocation)
		return -ENOMEM;

	atomic_set(&vlocation->usage, 1);
	INIT_LIST_HEAD(&vlocation->link);
	rwlock_init(&vlocation->lock);
	memcpy(vlocation->vldb.name, name, namesz);

	afs_timer_init(&vlocation->timeout, &afs_vlocation_timer_ops);
	afs_timer_init(&vlocation->upd_timer, &afs_vlocation_update_timer_ops);
	afs_async_op_init(&vlocation->upd_op, &afs_vlocation_update_op_ops);

	afs_get_cell(cell);
	vlocation->cell = cell;

	list_add_tail(&vlocation->link, &cell->vl_list);

#ifdef AFS_CACHING_SUPPORT
	/* we want to store it in the cache, plus it might already be
	 * encached */
	cachefs_acquire_cookie(cell->cache,
			       &afs_volume_cache_index_def,
			       vlocation,
			       &vlocation->cache);

	if (vlocation->valid)
		goto found_in_cache;
#endif

	/* try to look up an unknown volume in the cell VL databases by name */
	ret = afs_vlocation_access_vl_by_name(vlocation, name, namesz, &vldb);
	if (ret < 0) {
		printk("kAFS: failed to locate '%*.*s' in cell '%s'\n",
		       namesz, namesz, name, cell->name);
		goto error;
	}

	goto found_on_vlserver;

 found_in_graveyard:
	/* found in the graveyard - resurrect */
	_debug("found in graveyard");
	atomic_inc(&vlocation->usage);
	list_move_tail(&vlocation->link, &cell->vl_list);
	spin_unlock(&cell->vl_gylock);

	afs_kafstimod_del_timer(&vlocation->timeout);
	goto active;

 found_in_memory:
	/* found in memory - check to see if it's active */
	_debug("found in memory");
	atomic_inc(&vlocation->usage);

 active:
	active = 1;

#ifdef AFS_CACHING_SUPPORT
 found_in_cache:
#endif
	/* try to look up a cached volume in the cell VL databases by ID */
	_debug("found in cache");

	_debug("Locally Cached: %s %02x { %08x(%x) %08x(%x) %08x(%x) }",
	       vlocation->vldb.name,
	       vlocation->vldb.vidmask,
	       ntohl(vlocation->vldb.servers[0].s_addr),
	       vlocation->vldb.srvtmask[0],
	       ntohl(vlocation->vldb.servers[1].s_addr),
	       vlocation->vldb.srvtmask[1],
	       ntohl(vlocation->vldb.servers[2].s_addr),
	       vlocation->vldb.srvtmask[2]
	       );

	_debug("Vids: %08x %08x %08x",
	       vlocation->vldb.vid[0],
	       vlocation->vldb.vid[1],
	       vlocation->vldb.vid[2]);

	if (vlocation->vldb.vidmask & AFS_VOL_VTM_RW) {
		vid = vlocation->vldb.vid[0];
		voltype = AFSVL_RWVOL;
	}
	else if (vlocation->vldb.vidmask & AFS_VOL_VTM_RO) {
		vid = vlocation->vldb.vid[1];
		voltype = AFSVL_ROVOL;
	}
	else if (vlocation->vldb.vidmask & AFS_VOL_VTM_BAK) {
		vid = vlocation->vldb.vid[2];
		voltype = AFSVL_BACKVOL;
	}
	else {
		BUG();
		vid = 0;
		voltype = 0;
	}

	ret = afs_vlocation_access_vl_by_id(vlocation, vid, voltype, &vldb);
	switch (ret) {
		/* net error */
	default:
		printk("kAFS: failed to volume '%*.*s' (%x) up in '%s': %d\n",
		       namesz, namesz, name, vid, cell->name, ret);
		goto error;

		/* pulled from local cache into memory */
	case 0:
		goto found_on_vlserver;

		/* uh oh... looks like the volume got deleted */
	case -ENOMEDIUM:
		printk("kAFS: volume '%*.*s' (%x) does not exist '%s'\n",
		       namesz, namesz, name, vid, cell->name);

		/* TODO: make existing record unavailable */
		goto error;
	}

 found_on_vlserver:
	_debug("Done VL Lookup: %*.*s %02x { %08x(%x) %08x(%x) %08x(%x) }",
	       namesz, namesz, name,
	       vldb.vidmask,
	       ntohl(vldb.servers[0].s_addr), vldb.srvtmask[0],
	       ntohl(vldb.servers[1].s_addr), vldb.srvtmask[1],
	       ntohl(vldb.servers[2].s_addr), vldb.srvtmask[2]
	       );

	_debug("Vids: %08x %08x %08x", vldb.vid[0], vldb.vid[1], vldb.vid[2]);

	if ((namesz < sizeof(vlocation->vldb.name) &&
	     vlocation->vldb.name[namesz] != '\0') ||
	    memcmp(vldb.name, name, namesz) != 0)
		printk("kAFS: name of volume '%*.*s' changed to '%s' on server\n",
		       namesz, namesz, name, vldb.name);

	memcpy(&vlocation->vldb, &vldb, sizeof(vlocation->vldb));

	afs_kafstimod_add_timer(&vlocation->upd_timer, 10 * HZ);

#ifdef AFS_CACHING_SUPPORT
	/* update volume entry in local cache */
	cachefs_update_cookie(vlocation->cache);
#endif

	*_vlocation = vlocation;
	_leave(" = 0 (%p)",vlocation);
	return 0;

 error:
	if (vlocation) {
		if (active) {
			__afs_put_vlocation(vlocation);
		}
		else {
			list_del(&vlocation->link);
#ifdef AFS_CACHING_SUPPORT
			cachefs_relinquish_cookie(vlocation->cache, 0);
#endif
			afs_put_cell(vlocation->cell);
			kfree(vlocation);
		}
	}

	_leave(" = %d", ret);
	return ret;
} /* end afs_vlocation_lookup() */

/*****************************************************************************/
/*
 * finish using a volume location record
 * - caller must have cell->vol_sem write-locked
 */
static void __afs_put_vlocation(struct afs_vlocation *vlocation)
{
	struct afs_cell *cell;

	if (!vlocation)
		return;

	_enter("%s", vlocation->vldb.name);

	cell = vlocation->cell;

	/* sanity check */
	BUG_ON(atomic_read(&vlocation->usage) <= 0);

	spin_lock(&cell->vl_gylock);
	if (likely(!atomic_dec_and_test(&vlocation->usage))) {
		spin_unlock(&cell->vl_gylock);
		_leave("");
		return;
	}

	/* move to graveyard queue */
	list_move_tail(&vlocation->link,&cell->vl_graveyard);

	/* remove from pending timeout queue (refcounted if actually being
	 * updated) */
	list_del_init(&vlocation->upd_op.link);

	/* time out in 10 secs */
	afs_kafstimod_del_timer(&vlocation->upd_timer);
	afs_kafstimod_add_timer(&vlocation->timeout, 10 * HZ);

	spin_unlock(&cell->vl_gylock);

	_leave(" [killed]");
} /* end __afs_put_vlocation() */

/*****************************************************************************/
/*
 * finish using a volume location record
 */
void afs_put_vlocation(struct afs_vlocation *vlocation)
{
	if (vlocation) {
		struct afs_cell *cell = vlocation->cell;

		down_write(&cell->vl_sem);
		__afs_put_vlocation(vlocation);
		up_write(&cell->vl_sem);
	}
} /* end afs_put_vlocation() */

/*****************************************************************************/
/*
 * timeout vlocation record
 * - removes from the cell's graveyard if the usage count is zero
 */
void afs_vlocation_do_timeout(struct afs_vlocation *vlocation)
{
	struct afs_cell *cell;

	_enter("%s", vlocation->vldb.name);

	cell = vlocation->cell;

	BUG_ON(atomic_read(&vlocation->usage) < 0);

	/* remove from graveyard if still dead */
	spin_lock(&cell->vl_gylock);
	if (atomic_read(&vlocation->usage) == 0)
		list_del_init(&vlocation->link);
	else
		vlocation = NULL;
	spin_unlock(&cell->vl_gylock);

	if (!vlocation) {
		_leave("");
		return; /* resurrected */
	}

	/* we can now destroy it properly */
#ifdef AFS_CACHING_SUPPORT
	cachefs_relinquish_cookie(vlocation->cache, 0);
#endif
	afs_put_cell(cell);

	kfree(vlocation);

	_leave(" [destroyed]");
} /* end afs_vlocation_do_timeout() */

/*****************************************************************************/
/*
 * send an update operation to the currently selected server
 */
static int afs_vlocation_update_begin(struct afs_vlocation *vlocation)
{
	afs_voltype_t voltype;
	afs_volid_t vid;
	int ret;

	_enter("%s{ufs=%u ucs=%u}",
	       vlocation->vldb.name,
	       vlocation->upd_first_svix,
	       vlocation->upd_curr_svix);

	/* try to look up a cached volume in the cell VL databases by ID */
	if (vlocation->vldb.vidmask & AFS_VOL_VTM_RW) {
		vid = vlocation->vldb.vid[0];
		voltype = AFSVL_RWVOL;
	}
	else if (vlocation->vldb.vidmask & AFS_VOL_VTM_RO) {
		vid = vlocation->vldb.vid[1];
		voltype = AFSVL_ROVOL;
	}
	else if (vlocation->vldb.vidmask & AFS_VOL_VTM_BAK) {
		vid = vlocation->vldb.vid[2];
		voltype = AFSVL_BACKVOL;
	}
	else {
		BUG();
		vid = 0;
		voltype = 0;
	}

	/* contact the chosen server */
	ret = afs_server_lookup(
		vlocation->cell,
		&vlocation->cell->vl_addrs[vlocation->upd_curr_svix],
		&vlocation->upd_op.server);

	switch (ret) {
	case 0:
		break;
	case -ENOMEM:
	case -ENONET:
	default:
		_leave(" = %d", ret);
		return ret;
	}

	/* initiate the update operation */
	ret = afs_rxvl_get_entry_by_id_async(&vlocation->upd_op, vid, voltype);
	if (ret < 0) {
		_leave(" = %d", ret);
		return ret;
	}

	_leave(" = %d", ret);
	return ret;
} /* end afs_vlocation_update_begin() */

/*****************************************************************************/
/*
 * abandon updating a VL record
 * - does not restart the update timer
 */
static void afs_vlocation_update_abandon(struct afs_vlocation *vlocation,
					 afs_vlocation_upd_t state,
					 int ret)
{
	_enter("%s,%u", vlocation->vldb.name, state);

	if (ret < 0)
		printk("kAFS: Abandoning VL update '%s': %d\n",
		       vlocation->vldb.name, ret);

	/* discard the server record */
	afs_put_server(vlocation->upd_op.server);
	vlocation->upd_op.server = NULL;

	spin_lock(&afs_vlocation_update_lock);
	afs_vlocation_update = NULL;
	vlocation->upd_state = state;

	/* TODO: start updating next VL record on pending list */

	spin_unlock(&afs_vlocation_update_lock);

	_leave("");
} /* end afs_vlocation_update_abandon() */

/*****************************************************************************/
/*
 * handle periodic update timeouts and busy retry timeouts
 * - called from kafstimod
 */
static void afs_vlocation_update_timer(struct afs_timer *timer)
{
	struct afs_vlocation *vlocation =
		list_entry(timer, struct afs_vlocation, upd_timer);
	int ret;

	_enter("%s", vlocation->vldb.name);

	/* only update if not in the graveyard (defend against putting too) */
	spin_lock(&vlocation->cell->vl_gylock);

	if (!atomic_read(&vlocation->usage))
		goto out_unlock1;

	spin_lock(&afs_vlocation_update_lock);

	/* if we were woken up due to EBUSY sleep then restart immediately if
	 * possible or else jump to front of pending queue */
	if (vlocation->upd_state == AFS_VLUPD_BUSYSLEEP) {
		if (afs_vlocation_update) {
			list_add(&vlocation->upd_op.link,
				 &afs_vlocation_update_pendq);
		}
		else {
			afs_get_vlocation(vlocation);
			afs_vlocation_update = vlocation;
			vlocation->upd_state = AFS_VLUPD_INPROGRESS;
		}
		goto out_unlock2;
	}

	/* put on pending queue if there's already another update in progress */
	if (afs_vlocation_update) {
		vlocation->upd_state = AFS_VLUPD_PENDING;
		list_add_tail(&vlocation->upd_op.link,
			      &afs_vlocation_update_pendq);
		goto out_unlock2;
	}

	/* hold a ref on it while actually updating */
	afs_get_vlocation(vlocation);
	afs_vlocation_update = vlocation;
	vlocation->upd_state = AFS_VLUPD_INPROGRESS;

	spin_unlock(&afs_vlocation_update_lock);
	spin_unlock(&vlocation->cell->vl_gylock);

	/* okay... we can start the update */
	_debug("BEGIN VL UPDATE [%s]", vlocation->vldb.name);
	vlocation->upd_first_svix = vlocation->cell->vl_curr_svix;
	vlocation->upd_curr_svix = vlocation->upd_first_svix;
	vlocation->upd_rej_cnt = 0;
	vlocation->upd_busy_cnt = 0;

	ret = afs_vlocation_update_begin(vlocation);
	if (ret < 0) {
		afs_vlocation_update_abandon(vlocation, AFS_VLUPD_SLEEP, ret);
		afs_kafstimod_add_timer(&vlocation->upd_timer,
					AFS_VLDB_TIMEOUT);
		afs_put_vlocation(vlocation);
	}

	_leave("");
	return;

 out_unlock2:
	spin_unlock(&afs_vlocation_update_lock);
 out_unlock1:
	spin_unlock(&vlocation->cell->vl_gylock);
	_leave("");
	return;

} /* end afs_vlocation_update_timer() */

/*****************************************************************************/
/*
 * attend to an update operation upon which an event happened
 * - called in kafsasyncd context
 */
static void afs_vlocation_update_attend(struct afs_async_op *op)
{
	struct afs_cache_vlocation vldb;
	struct afs_vlocation *vlocation =
		list_entry(op, struct afs_vlocation, upd_op);
	unsigned tmp;
	int ret;

	_enter("%s", vlocation->vldb.name);

	ret = afs_rxvl_get_entry_by_id_async2(op, &vldb);
	switch (ret) {
	case -EAGAIN:
		_leave(" [unfinished]");
		return;

	case 0:
		_debug("END VL UPDATE: %d\n", ret);
		vlocation->valid = 1;

		_debug("Done VL Lookup: %02x { %08x(%x) %08x(%x) %08x(%x) }",
		       vldb.vidmask,
		       ntohl(vldb.servers[0].s_addr), vldb.srvtmask[0],
		       ntohl(vldb.servers[1].s_addr), vldb.srvtmask[1],
		       ntohl(vldb.servers[2].s_addr), vldb.srvtmask[2]
		       );

		_debug("Vids: %08x %08x %08x",
		       vldb.vid[0], vldb.vid[1], vldb.vid[2]);

		afs_vlocation_update_abandon(vlocation, AFS_VLUPD_SLEEP, 0);

		down_write(&vlocation->cell->vl_sem);

		/* actually update the cache */
		if (strncmp(vldb.name, vlocation->vldb.name,
			    sizeof(vlocation->vldb.name)) != 0)
			printk("kAFS: name of volume '%s'"
			       " changed to '%s' on server\n",
			       vlocation->vldb.name, vldb.name);

		memcpy(&vlocation->vldb, &vldb, sizeof(vlocation->vldb));

#if 0
		/* TODO update volume entry in local cache */
#endif

		up_write(&vlocation->cell->vl_sem);

		if (ret < 0)
			printk("kAFS: failed to update local cache: %d\n", ret);

		afs_kafstimod_add_timer(&vlocation->upd_timer,
					AFS_VLDB_TIMEOUT);
		afs_put_vlocation(vlocation);
		_leave(" [found]");
		return;

	case -ENOMEDIUM:
		vlocation->upd_rej_cnt++;
		goto try_next;

		/* the server is locked - retry in a very short while */
	case -EBUSY:
		vlocation->upd_busy_cnt++;
		if (vlocation->upd_busy_cnt > 3)
			goto try_next; /* too many retries */

		afs_vlocation_update_abandon(vlocation,
					     AFS_VLUPD_BUSYSLEEP, 0);
		afs_kafstimod_add_timer(&vlocation->upd_timer, HZ / 2);
		afs_put_vlocation(vlocation);
		_leave(" [busy]");
		return;

	case -ENETUNREACH:
	case -EHOSTUNREACH:
	case -ECONNREFUSED:
	case -EREMOTEIO:
		/* record bad vlserver info in the cell too
		 * - TODO: use down_write_trylock() if available
		 */
		if (vlocation->upd_curr_svix == vlocation->cell->vl_curr_svix)
			vlocation->cell->vl_curr_svix =
				vlocation->cell->vl_curr_svix %
				vlocation->cell->vl_naddrs;

	case -EBADRQC:
	case -EINVAL:
	case -EACCES:
	case -EBADMSG:
		goto try_next;

	default:
		goto abandon;
	}

	/* try contacting the next server */
 try_next:
	vlocation->upd_busy_cnt = 0;

	/* discard the server record */
	afs_put_server(vlocation->upd_op.server);
	vlocation->upd_op.server = NULL;

	tmp = vlocation->cell->vl_naddrs;
	if (tmp == 0)
		goto abandon;

	vlocation->upd_curr_svix++;
	if (vlocation->upd_curr_svix >= tmp)
		vlocation->upd_curr_svix = 0;
	if (vlocation->upd_first_svix >= tmp)
		vlocation->upd_first_svix = tmp - 1;

	/* move to the next server */
	if (vlocation->upd_curr_svix != vlocation->upd_first_svix) {
		afs_vlocation_update_begin(vlocation);
		_leave(" [next]");
		return;
	}

	/* run out of servers to try - was the volume rejected? */
	if (vlocation->upd_rej_cnt > 0) {
		printk("kAFS: Active volume no longer valid '%s'\n",
		       vlocation->vldb.name);
		vlocation->valid = 0;
		afs_vlocation_update_abandon(vlocation, AFS_VLUPD_SLEEP, 0);
		afs_kafstimod_add_timer(&vlocation->upd_timer,
					AFS_VLDB_TIMEOUT);
		afs_put_vlocation(vlocation);
		_leave(" [invalidated]");
		return;
	}

	/* abandon the update */
 abandon:
	afs_vlocation_update_abandon(vlocation, AFS_VLUPD_SLEEP, ret);
	afs_kafstimod_add_timer(&vlocation->upd_timer, HZ * 10);
	afs_put_vlocation(vlocation);
	_leave(" [abandoned]");

} /* end afs_vlocation_update_attend() */

/*****************************************************************************/
/*
 * deal with an update operation being discarded
 * - called in kafsasyncd context when it's dying due to rmmod
 * - the call has already been aborted and put()'d
 */
static void afs_vlocation_update_discard(struct afs_async_op *op)
{
	struct afs_vlocation *vlocation =
		list_entry(op, struct afs_vlocation, upd_op);

	_enter("%s", vlocation->vldb.name);

	afs_put_server(op->server);
	op->server = NULL;

	afs_put_vlocation(vlocation);

	_leave("");
} /* end afs_vlocation_update_discard() */

/*****************************************************************************/
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
		}
		/* need to update cache if cached info differs */
		else if (memcmp(&vlocation->vldb, vldb, sizeof(*vldb)) != 0) {
			/* delete if VIDs for this name differ */
			if (memcmp(&vlocation->vldb.vid,
				   &vldb->vid,
				   sizeof(vldb->vid)) != 0) {
				_leave(" = DELETE");
				return CACHEFS_MATCH_SUCCESS_DELETE;
			}

			_leave(" = UPDATE");
			return CACHEFS_MATCH_SUCCESS_UPDATE;
		}
		else {
			_leave(" = SUCCESS");
			return CACHEFS_MATCH_SUCCESS;
		}
	}

	_leave(" = FAILED");
	return CACHEFS_MATCH_FAILED;
} /* end afs_vlocation_cache_match() */
#endif

/*****************************************************************************/
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

} /* end afs_vlocation_cache_update() */
#endif
