// SPDX-License-Identifier: GPL-2.0-or-later
/* vnode and volume validity verification.
 *
 * Copyright (C) 2023 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include "internal.h"

/*
 * mark the data attached to an inode as obsolete due to a write on the server
 * - might also want to ditch all the outstanding writes and dirty pages
 */
static void afs_zap_data(struct afs_vnode *vnode)
{
	_enter("{%llx:%llu}", vnode->fid.vid, vnode->fid.vnode);

	afs_invalidate_cache(vnode, 0);

	/* nuke all the non-dirty pages that aren't locked, mapped or being
	 * written back in a regular file and completely discard the pages in a
	 * directory or symlink */
	if (S_ISREG(vnode->netfs.inode.i_mode))
		invalidate_remote_inode(&vnode->netfs.inode);
	else
		invalidate_inode_pages2(vnode->netfs.inode.i_mapping);
}

/*
 * Check to see if we have a server currently serving this volume and that it
 * hasn't been reinitialised or dropped from the list.
 */
static bool afs_check_server_good(struct afs_vnode *vnode)
{
	struct afs_server_list *slist;
	struct afs_server *server;
	bool good;
	int i;

	if (vnode->cb_fs_s_break == atomic_read(&vnode->volume->cell->fs_s_break))
		return true;

	rcu_read_lock();

	slist = rcu_dereference(vnode->volume->servers);
	for (i = 0; i < slist->nr_servers; i++) {
		server = slist->servers[i].server;
		if (server == vnode->cb_server) {
			good = (vnode->cb_s_break == server->cb_s_break);
			rcu_read_unlock();
			return good;
		}
	}

	rcu_read_unlock();
	return false;
}

/*
 * Check the validity of a vnode/inode.
 */
bool afs_check_validity(struct afs_vnode *vnode)
{
	enum afs_cb_break_reason need_clear = afs_cb_break_no_break;
	time64_t now = ktime_get_real_seconds();
	unsigned int cb_break;
	int seq;

	do {
		seq = read_seqbegin(&vnode->cb_lock);
		cb_break = vnode->cb_break;

		if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
			if (vnode->cb_v_break != vnode->volume->cb_v_break)
				need_clear = afs_cb_break_for_v_break;
			else if (!afs_check_server_good(vnode))
				need_clear = afs_cb_break_for_s_reinit;
			else if (test_bit(AFS_VNODE_ZAP_DATA, &vnode->flags))
				need_clear = afs_cb_break_for_zap;
			else if (vnode->cb_expires_at - 10 <= now)
				need_clear = afs_cb_break_for_lapsed;
		} else if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
			;
		} else {
			need_clear = afs_cb_break_no_promise;
		}

	} while (read_seqretry(&vnode->cb_lock, seq));

	if (need_clear == afs_cb_break_no_break)
		return true;

	write_seqlock(&vnode->cb_lock);
	if (need_clear == afs_cb_break_no_promise)
		vnode->cb_v_break = vnode->volume->cb_v_break;
	else if (cb_break == vnode->cb_break)
		__afs_break_callback(vnode, need_clear);
	else
		trace_afs_cb_miss(&vnode->fid, need_clear);
	write_sequnlock(&vnode->cb_lock);
	return false;
}

/*
 * Returns true if the pagecache is still valid.  Does not sleep.
 */
bool afs_pagecache_valid(struct afs_vnode *vnode)
{
	if (unlikely(test_bit(AFS_VNODE_DELETED, &vnode->flags))) {
		if (vnode->netfs.inode.i_nlink)
			clear_nlink(&vnode->netfs.inode);
		return true;
	}

	if (test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags) &&
	    afs_check_validity(vnode))
		return true;

	return false;
}

/*
 * validate a vnode/inode
 * - there are several things we need to check
 *   - parent dir data changes (rm, rmdir, rename, mkdir, create, link,
 *     symlink)
 *   - parent dir metadata changed (security changes)
 *   - dentry data changed (write, truncate)
 *   - dentry metadata changed (security changes)
 */
int afs_validate(struct afs_vnode *vnode, struct key *key)
{
	int ret;

	_enter("{v={%llx:%llu} fl=%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, vnode->flags,
	       key_serial(key));

	if (afs_pagecache_valid(vnode))
		goto valid;

	down_write(&vnode->validate_lock);

	/* if the promise has expired, we need to check the server again to get
	 * a new promise - note that if the (parent) directory's metadata was
	 * changed then the security may be different and we may no longer have
	 * access */
	if (!test_bit(AFS_VNODE_CB_PROMISED, &vnode->flags)) {
		_debug("not promised");
		ret = afs_fetch_status(vnode, key, false, NULL);
		if (ret < 0) {
			if (ret == -ENOENT) {
				set_bit(AFS_VNODE_DELETED, &vnode->flags);
				ret = -ESTALE;
			}
			goto error_unlock;
		}
		_debug("new promise [fl=%lx]", vnode->flags);
	}

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		_debug("file already deleted");
		ret = -ESTALE;
		goto error_unlock;
	}

	/* if the vnode's data version number changed then its contents are
	 * different */
	if (test_and_clear_bit(AFS_VNODE_ZAP_DATA, &vnode->flags))
		afs_zap_data(vnode);
	up_write(&vnode->validate_lock);
valid:
	_leave(" = 0");
	return 0;

error_unlock:
	up_write(&vnode->validate_lock);
	_leave(" = %d", ret);
	return ret;
}
