/* vnode.c: AFS vnode management
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
#include "vnode.h"
#include "internal.h"

static void afs_vnode_cb_timed_out(struct afs_timer *timer);

struct afs_timer_ops afs_vnode_cb_timed_out_ops = {
	.timed_out	= afs_vnode_cb_timed_out,
};

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

/*****************************************************************************/
/*
 * handle a callback timing out
 * TODO: retain a ref to vnode struct for an outstanding callback timeout
 */
static void afs_vnode_cb_timed_out(struct afs_timer *timer)
{
	struct afs_server *oldserver;
	struct afs_vnode *vnode;

	vnode = list_entry(timer, struct afs_vnode, cb_timeout);

	_enter("%p", vnode);

	/* set the changed flag in the vnode and release the server */
	spin_lock(&vnode->lock);

	oldserver = xchg(&vnode->cb_server, NULL);
	if (oldserver) {
		vnode->flags |= AFS_VNODE_CHANGED;

		spin_lock(&afs_cb_hash_lock);
		list_del_init(&vnode->cb_hash_link);
		spin_unlock(&afs_cb_hash_lock);

		spin_lock(&oldserver->cb_lock);
		list_del_init(&vnode->cb_link);
		spin_unlock(&oldserver->cb_lock);
	}

	spin_unlock(&vnode->lock);

	afs_put_server(oldserver);

	_leave("");
} /* end afs_vnode_cb_timed_out() */

/*****************************************************************************/
/*
 * finish off updating the recorded status of a file
 * - starts callback expiry timer
 * - adds to server's callback list
 */
static void afs_vnode_finalise_status_update(struct afs_vnode *vnode,
					     struct afs_server *server,
					     int ret)
{
	struct afs_server *oldserver = NULL;

	_enter("%p,%p,%d", vnode, server, ret);

	spin_lock(&vnode->lock);

	vnode->flags &= ~AFS_VNODE_CHANGED;

	if (ret == 0) {
		/* adjust the callback timeout appropriately */
		afs_kafstimod_add_timer(&vnode->cb_timeout,
					vnode->cb_expiry * HZ);

		spin_lock(&afs_cb_hash_lock);
		list_del(&vnode->cb_hash_link);
		list_add_tail(&vnode->cb_hash_link,
			      &afs_cb_hash(server, &vnode->fid));
		spin_unlock(&afs_cb_hash_lock);

		/* swap ref to old callback server with that for new callback
		 * server */
		oldserver = xchg(&vnode->cb_server, server);
		if (oldserver != server) {
			if (oldserver) {
				spin_lock(&oldserver->cb_lock);
				list_del_init(&vnode->cb_link);
				spin_unlock(&oldserver->cb_lock);
			}

			afs_get_server(server);
			spin_lock(&server->cb_lock);
			list_add_tail(&vnode->cb_link, &server->cb_promises);
			spin_unlock(&server->cb_lock);
		}
		else {
			/* same server */
			oldserver = NULL;
		}
	}
	else if (ret == -ENOENT) {
		/* the file was deleted - clear the callback timeout */
		oldserver = xchg(&vnode->cb_server, NULL);
		afs_kafstimod_del_timer(&vnode->cb_timeout);

		_debug("got NOENT from server - marking file deleted");
		vnode->flags |= AFS_VNODE_DELETED;
	}

	vnode->update_cnt--;

	spin_unlock(&vnode->lock);

	wake_up_all(&vnode->update_waitq);

	afs_put_server(oldserver);

	_leave("");

} /* end afs_vnode_finalise_status_update() */

/*****************************************************************************/
/*
 * fetch file status from the volume
 * - don't issue a fetch if:
 *   - the changed bit is not set and there's a valid callback
 *   - there are any outstanding ops that will fetch the status
 * - TODO implement local caching
 */
int afs_vnode_fetch_status(struct afs_vnode *vnode)
{
	struct afs_server *server;
	int ret;

	DECLARE_WAITQUEUE(myself, current);

	_enter("%s,{%u,%u,%u}",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid, vnode->fid.vnode, vnode->fid.unique);

	if (!(vnode->flags & AFS_VNODE_CHANGED) && vnode->cb_server) {
		_leave(" [unchanged]");
		return 0;
	}

	if (vnode->flags & AFS_VNODE_DELETED) {
		_leave(" [deleted]");
		return -ENOENT;
	}

	spin_lock(&vnode->lock);

	if (!(vnode->flags & AFS_VNODE_CHANGED)) {
		spin_unlock(&vnode->lock);
		_leave(" [unchanged]");
		return 0;
	}

	if (vnode->update_cnt > 0) {
		/* someone else started a fetch */
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&vnode->update_waitq, &myself);

		/* wait for the status to be updated */
		for (;;) {
			if (!(vnode->flags & AFS_VNODE_CHANGED))
				break;
			if (vnode->flags & AFS_VNODE_DELETED)
				break;

			/* it got updated and invalidated all before we saw
			 * it */
			if (vnode->update_cnt == 0) {
				remove_wait_queue(&vnode->update_waitq,
						  &myself);
				set_current_state(TASK_RUNNING);
				goto get_anyway;
			}

			spin_unlock(&vnode->lock);

			schedule();
			set_current_state(TASK_UNINTERRUPTIBLE);

			spin_lock(&vnode->lock);
		}

		remove_wait_queue(&vnode->update_waitq, &myself);
		spin_unlock(&vnode->lock);
		set_current_state(TASK_RUNNING);

		return vnode->flags & AFS_VNODE_DELETED ? -ENOENT : 0;
	}

 get_anyway:
	/* okay... we're going to have to initiate the op */
	vnode->update_cnt++;

	spin_unlock(&vnode->lock);

	/* merge AFS status fetches and clear outstanding callback on this
	 * vnode */
	do {
		/* pick a server to query */
		ret = afs_volume_pick_fileserver(vnode->volume, &server);
		if (ret<0)
			return ret;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_rxfs_fetch_file_status(server, vnode, NULL);

	} while (!afs_volume_release_fileserver(vnode->volume, server, ret));

	/* adjust the flags */
	afs_vnode_finalise_status_update(vnode, server, ret);

	_leave(" = %d", ret);
	return ret;
} /* end afs_vnode_fetch_status() */

/*****************************************************************************/
/*
 * fetch file data from the volume
 * - TODO implement caching and server failover
 */
int afs_vnode_fetch_data(struct afs_vnode *vnode,
			 struct afs_rxfs_fetch_descriptor *desc)
{
	struct afs_server *server;
	int ret;

	_enter("%s,{%u,%u,%u}",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique);

	/* this op will fetch the status */
	spin_lock(&vnode->lock);
	vnode->update_cnt++;
	spin_unlock(&vnode->lock);

	/* merge in AFS status fetches and clear outstanding callback on this
	 * vnode */
	do {
		/* pick a server to query */
		ret = afs_volume_pick_fileserver(vnode->volume, &server);
		if (ret < 0)
			return ret;

		_debug("USING SERVER: %08x\n", ntohl(server->addr.s_addr));

		ret = afs_rxfs_fetch_file_data(server, vnode, desc, NULL);

	} while (!afs_volume_release_fileserver(vnode->volume, server, ret));

	/* adjust the flags */
	afs_vnode_finalise_status_update(vnode, server, ret);

	_leave(" = %d", ret);
	return ret;

} /* end afs_vnode_fetch_data() */

/*****************************************************************************/
/*
 * break any outstanding callback on a vnode
 * - only relevent to server that issued it
 */
int afs_vnode_give_up_callback(struct afs_vnode *vnode)
{
	struct afs_server *server;
	int ret;

	_enter("%s,{%u,%u,%u}",
	       vnode->volume->vlocation->vldb.name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique);

	spin_lock(&afs_cb_hash_lock);
	list_del_init(&vnode->cb_hash_link);
	spin_unlock(&afs_cb_hash_lock);

	/* set the changed flag in the vnode and release the server */
	spin_lock(&vnode->lock);

	afs_kafstimod_del_timer(&vnode->cb_timeout);

	server = xchg(&vnode->cb_server, NULL);
	if (server) {
		vnode->flags |= AFS_VNODE_CHANGED;

		spin_lock(&server->cb_lock);
		list_del_init(&vnode->cb_link);
		spin_unlock(&server->cb_lock);
	}

	spin_unlock(&vnode->lock);

	ret = 0;
	if (server) {
		ret = afs_rxfs_give_up_callback(server, vnode);
		afs_put_server(server);
	}

	_leave(" = %d", ret);
	return ret;
} /* end afs_vnode_give_up_callback() */

/*****************************************************************************/
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
} /* end afs_vnode_cache_match() */
#endif

/*****************************************************************************/
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

} /* end afs_vnode_cache_update() */
#endif
