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
 * See if the server we've just talked to is currently excluded.
 */
static bool __afs_is_server_excluded(struct afs_operation *op, struct afs_volume *volume)
{
	const struct afs_server_entry *se;
	const struct afs_server_list *slist;
	bool is_excluded = true;
	int i;

	rcu_read_lock();

	slist = rcu_dereference(volume->servers);
	for (i = 0; i < slist->nr_servers; i++) {
		se = &slist->servers[i];
		if (op->server == se->server) {
			is_excluded = test_bit(AFS_SE_EXCLUDED, &se->flags);
			break;
		}
	}

	rcu_read_unlock();
	return is_excluded;
}

/*
 * Update the volume's server list when the creation time changes and see if
 * the server we've just talked to is currently excluded.
 */
static int afs_is_server_excluded(struct afs_operation *op, struct afs_volume *volume)
{
	int ret;

	if (__afs_is_server_excluded(op, volume))
		return 1;

	set_bit(AFS_VOLUME_NEEDS_UPDATE, &volume->flags);
	ret = afs_check_volume_status(op->volume, op);
	if (ret < 0)
		return ret;

	return __afs_is_server_excluded(op, volume);
}

/*
 * Handle a change to the volume creation time in the VolSync record.
 */
static int afs_update_volume_creation_time(struct afs_operation *op, struct afs_volume *volume)
{
	unsigned int snap;
	time64_t cur = volume->creation_time;
	time64_t old = op->pre_volsync.creation;
	time64_t new = op->volsync.creation;
	int ret;

	_enter("%llx,%llx,%llx->%llx", volume->vid, cur, old, new);

	if (cur == TIME64_MIN) {
		volume->creation_time = new;
		return 0;
	}

	if (new == cur)
		return 0;

	/* Try to advance the creation timestamp from what we had before the
	 * operation to what we got back from the server.  This should
	 * hopefully ensure that in a race between multiple operations only one
	 * of them will do this.
	 */
	if (cur != old)
		return 0;

	/* If the creation time changes in an unexpected way, we need to scrub
	 * our caches.  For a RW vol, this will only change if the volume is
	 * restored from a backup; for a RO/Backup vol, this will advance when
	 * the volume is updated to a new snapshot (eg. "vos release").
	 */
	if (volume->type == AFSVL_RWVOL)
		goto regressed;
	if (volume->type == AFSVL_BACKVOL) {
		if (new < old)
			goto regressed;
		goto advance;
	}

	/* We have an RO volume, we need to query the VL server and look at the
	 * server flags to see if RW->RO replication is in progress.
	 */
	ret = afs_is_server_excluded(op, volume);
	if (ret < 0)
		return ret;
	if (ret > 0) {
		snap = atomic_read(&volume->cb_ro_snapshot);
		trace_afs_cb_v_break(volume->vid, snap, afs_cb_break_volume_excluded);
		return ret;
	}

advance:
	snap = atomic_inc_return(&volume->cb_ro_snapshot);
	trace_afs_cb_v_break(volume->vid, snap, afs_cb_break_for_vos_release);
	volume->creation_time = new;
	return 0;

regressed:
	atomic_inc(&volume->cb_scrub);
	trace_afs_cb_v_break(volume->vid, 0, afs_cb_break_for_creation_regress);
	volume->creation_time = new;
	return 0;
}

/*
 * Handle a change to the volume update time in the VolSync record.
 */
static void afs_update_volume_update_time(struct afs_operation *op, struct afs_volume *volume)
{
	enum afs_cb_break_reason reason = afs_cb_break_no_break;
	time64_t cur = volume->update_time;
	time64_t old = op->pre_volsync.update;
	time64_t new = op->volsync.update;

	_enter("%llx,%llx,%llx->%llx", volume->vid, cur, old, new);

	if (cur == TIME64_MIN) {
		volume->update_time = new;
		return;
	}

	if (new == cur)
		return;

	/* If the volume update time changes in an unexpected way, we need to
	 * scrub our caches.  For a RW vol, this will advance on every
	 * modification op; for a RO/Backup vol, this will advance when the
	 * volume is updated to a new snapshot (eg. "vos release").
	 */
	if (new < old)
		reason = afs_cb_break_for_update_regress;

	/* Try to advance the update timestamp from what we had before the
	 * operation to what we got back from the server.  This should
	 * hopefully ensure that in a race between multiple operations only one
	 * of them will do this.
	 */
	if (cur == old) {
		if (reason == afs_cb_break_for_update_regress) {
			atomic_inc(&volume->cb_scrub);
			trace_afs_cb_v_break(volume->vid, 0, reason);
		}
		volume->update_time = new;
	}
}

static int afs_update_volume_times(struct afs_operation *op, struct afs_volume *volume)
{
	int ret = 0;

	if (likely(op->volsync.creation == volume->creation_time &&
		   op->volsync.update == volume->update_time))
		return 0;

	mutex_lock(&volume->volsync_lock);
	if (op->volsync.creation != volume->creation_time) {
		ret = afs_update_volume_creation_time(op, volume);
		if (ret < 0)
			goto out;
	}
	if (op->volsync.update != volume->update_time)
		afs_update_volume_update_time(op, volume);
out:
	mutex_unlock(&volume->volsync_lock);
	return ret;
}

/*
 * Update the state of a volume.  Returns 1 to redo the operation from the start.
 */
int afs_update_volume_state(struct afs_operation *op)
{
	struct afs_volume *volume = op->volume;
	int ret;

	_enter("%llx", op->volume->vid);

	if (op->volsync.creation != TIME64_MIN || op->volsync.update != TIME64_MIN) {
		ret = afs_update_volume_times(op, volume);
		if (ret != 0) {
			_leave(" = %d", ret);
			return ret;
		}
	}

	return 0;
}

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
			if (vnode->cb_v_break != atomic_read(&vnode->volume->cb_v_break))
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
		vnode->cb_v_break = atomic_read(&vnode->volume->cb_v_break);
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
