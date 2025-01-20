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
 * Data validation is managed through a number of mechanisms from the server:
 *
 *  (1) On first contact with a server (such as if it has just been rebooted),
 *      the server sends us a CB.InitCallBackState* request.
 *
 *  (2) On a RW volume, in response to certain vnode (inode)-accessing RPC
 *      calls, the server maintains a time-limited per-vnode promise that it
 *      will send us a CB.CallBack request if a third party alters the vnodes
 *      accessed.
 *
 *      Note that a vnode-level callbacks may also be sent for other reasons,
 *      such as filelock release.
 *
 *  (3) On a RO (or Backup) volume, in response to certain vnode-accessing RPC
 *      calls, each server maintains a time-limited per-volume promise that it
 *      will send us a CB.CallBack request if the RO volume is updated to a
 *      snapshot of the RW volume ("vos release").  This is an atomic event
 *      that cuts over all instances of the RO volume across multiple servers
 *      simultaneously.
 *
 *	Note that a volume-level callbacks may also be sent for other reasons,
 *	such as the volumeserver taking over control of the volume from the
 *	fileserver.
 *
 *	Note also that each server maintains an independent time limit on an
 *	independent callback.
 *
 *  (4) Certain RPC calls include a volume information record "VolSync" in
 *      their reply.  This contains a creation date for the volume that should
 *      remain unchanged for a RW volume (but will be changed if the volume is
 *      restored from backup) or will be bumped to the time of snapshotting
 *      when a RO volume is released.
 *
 * In order to track this events, the following are provided:
 *
 *	->cb_v_break.  A counter of events that might mean that the contents of
 *	a volume have been altered since we last checked a vnode.
 *
 *	->cb_v_check.  A counter of the number of events that we've sent a
 *	query to the server for.  Everything's up to date if this equals
 *	cb_v_break.
 *
 *	->cb_scrub.  A counter of the number of regression events for which we
 *	have to completely wipe the cache.
 *
 *	->cb_ro_snapshot.  A counter of the number of times that we've
 *      recognised that a RO volume has been updated.
 *
 *	->cb_break.  A counter of events that might mean that the contents of a
 *      vnode have been altered.
 *
 *	->cb_expires_at.  The time at which the callback promise expires or
 *      AFS_NO_CB_PROMISE if we have no promise.
 *
 * The way we manage things is:
 *
 *  (1) When a volume-level CB.CallBack occurs, we increment ->cb_v_break on
 *      the volume and reset ->cb_expires_at (ie. set AFS_NO_CB_PROMISE) on the
 *      volume and volume's server record.
 *
 *  (2) When a CB.InitCallBackState occurs, we treat this as a volume-level
 *	callback break on all the volumes that have been using that volume
 *	(ie. increment ->cb_v_break and reset ->cb_expires_at).
 *
 *  (3) When a vnode-level CB.CallBack occurs, we increment ->cb_break on the
 *	vnode and reset its ->cb_expires_at.  If the vnode is mmapped, we also
 *	dispatch a work item to unmap all PTEs to the vnode's pagecache to
 *	force reentry to the filesystem for revalidation.
 *
 *  (4) When entering the filesystem, we call afs_validate() to check the
 *	validity of a vnode.  This first checks to see if ->cb_v_check and
 *	->cb_v_break match, and if they don't, we lock volume->cb_check_lock
 *	exclusively and perform an FS.FetchStatus on the vnode.
 *
 *	After checking the volume, we check the vnode.  If there's a mismatch
 *	between the volume counters and the vnode's mirrors of those counters,
 *	we lock vnode->validate_lock and issue an FS.FetchStatus on the vnode.
 *
 *  (5) When the reply from FS.FetchStatus arrives, the VolSync record is
 *      parsed:
 *
 *	(A) If the Creation timestamp has changed on a RW volume or regressed
 *	    on a RO volume, we try to increment ->cb_scrub; if it advances on a
 *	    RO volume, we assume "vos release" happened and try to increment
 *	    ->cb_ro_snapshot.
 *
 *      (B) If the Update timestamp has regressed, we try to increment
 *	    ->cb_scrub.
 *
 *      Note that in both of these cases, we only do the increment if we can
 *      cmpxchg the value of the timestamp from the value we noted before the
 *      op.  This tries to prevent parallel ops from fighting one another.
 *
 *	volume->cb_v_check is then set to ->cb_v_break.
 *
 *  (6) The AFSCallBack record included in the FS.FetchStatus reply is also
 *	parsed and used to set the promise in ->cb_expires_at for the vnode,
 *	the volume and the volume's server record.
 *
 *  (7) If ->cb_scrub is seen to have advanced, we invalidate the pagecache for
 *      the vnode.
 */

/*
 * Check the validity of a vnode/inode and its parent volume.
 */
bool afs_check_validity(const struct afs_vnode *vnode)
{
	const struct afs_volume *volume = vnode->volume;
	enum afs_vnode_invalid_trace trace = afs_vnode_valid_trace;
	time64_t cb_expires_at = atomic64_read(&vnode->cb_expires_at);
	time64_t deadline = ktime_get_real_seconds() + 10;

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags))
		return true;

	if (atomic_read(&volume->cb_v_check) != atomic_read(&volume->cb_v_break))
		trace = afs_vnode_invalid_trace_cb_v_break;
	else if (cb_expires_at == AFS_NO_CB_PROMISE)
		trace = afs_vnode_invalid_trace_no_cb_promise;
	else if (cb_expires_at <= deadline)
		trace = afs_vnode_invalid_trace_expired;
	else if (volume->cb_expires_at <= deadline)
		trace = afs_vnode_invalid_trace_vol_expired;
	else if (vnode->cb_ro_snapshot != atomic_read(&volume->cb_ro_snapshot))
		trace = afs_vnode_invalid_trace_cb_ro_snapshot;
	else if (vnode->cb_scrub != atomic_read(&volume->cb_scrub))
		trace = afs_vnode_invalid_trace_cb_scrub;
	else if (test_bit(AFS_VNODE_ZAP_DATA, &vnode->flags))
		trace = afs_vnode_invalid_trace_zap_data;
	else
		return true;
	trace_afs_vnode_invalid(vnode, trace);
	return false;
}

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
 * Update the state of a volume, including recording the expiration time of the
 * callback promise.  Returns 1 to redo the operation from the start.
 */
int afs_update_volume_state(struct afs_operation *op)
{
	struct afs_server_list *slist = op->server_list;
	struct afs_server_entry *se = &slist->servers[op->server_index];
	struct afs_callback *cb = &op->file[0].scb.callback;
	struct afs_volume *volume = op->volume;
	unsigned int cb_v_break = atomic_read(&volume->cb_v_break);
	unsigned int cb_v_check = atomic_read(&volume->cb_v_check);
	int ret;

	_enter("%llx", op->volume->vid);

	if (op->volsync.creation != TIME64_MIN || op->volsync.update != TIME64_MIN) {
		ret = afs_update_volume_times(op, volume);
		if (ret != 0) {
			_leave(" = %d", ret);
			return ret;
		}
	}

	if (op->cb_v_break == cb_v_break &&
	    (op->file[0].scb.have_cb || op->file[1].scb.have_cb)) {
		time64_t expires_at = cb->expires_at;

		if (!op->file[0].scb.have_cb)
			expires_at = op->file[1].scb.callback.expires_at;

		se->cb_expires_at = expires_at;
		volume->cb_expires_at = expires_at;
	}
	if (cb_v_check < op->cb_v_break)
		atomic_cmpxchg(&volume->cb_v_check, cb_v_check, op->cb_v_break);
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
		filemap_invalidate_inode(&vnode->netfs.inode, true, 0, LLONG_MAX);
	else
		filemap_invalidate_inode(&vnode->netfs.inode, false, 0, LLONG_MAX);
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
	struct afs_volume *volume = vnode->volume;
	unsigned int cb_ro_snapshot, cb_scrub;
	time64_t deadline = ktime_get_real_seconds() + 10;
	bool zap = false, locked_vol = false;
	int ret;

	_enter("{v={%llx:%llu} fl=%lx},%x",
	       vnode->fid.vid, vnode->fid.vnode, vnode->flags,
	       key_serial(key));

	if (afs_check_validity(vnode))
		return test_bit(AFS_VNODE_DELETED, &vnode->flags) ? -ESTALE : 0;

	ret = down_write_killable(&vnode->validate_lock);
	if (ret < 0)
		goto error;

	if (test_bit(AFS_VNODE_DELETED, &vnode->flags)) {
		ret = -ESTALE;
		goto error_unlock;
	}

	/* Validate a volume after the v_break has changed or the volume
	 * callback expired.  We only want to do this once per volume per
	 * v_break change.  The actual work will be done when parsing the
	 * status fetch reply.
	 */
	if (volume->cb_expires_at <= deadline ||
	    atomic_read(&volume->cb_v_check) != atomic_read(&volume->cb_v_break)) {
		ret = mutex_lock_interruptible(&volume->cb_check_lock);
		if (ret < 0)
			goto error_unlock;
		locked_vol = true;
	}

	cb_ro_snapshot = atomic_read(&volume->cb_ro_snapshot);
	cb_scrub = atomic_read(&volume->cb_scrub);
	if (vnode->cb_ro_snapshot != cb_ro_snapshot ||
	    vnode->cb_scrub	  != cb_scrub)
		unmap_mapping_pages(vnode->netfs.inode.i_mapping, 0, 0, false);

	if (vnode->cb_ro_snapshot != cb_ro_snapshot ||
	    vnode->cb_scrub	  != cb_scrub ||
	    volume->cb_expires_at <= deadline ||
	    atomic_read(&volume->cb_v_check) != atomic_read(&volume->cb_v_break) ||
	    atomic64_read(&vnode->cb_expires_at) <= deadline
	    ) {
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

	/* We can drop the volume lock now as. */
	if (locked_vol) {
		mutex_unlock(&volume->cb_check_lock);
		locked_vol = false;
	}

	cb_ro_snapshot = atomic_read(&volume->cb_ro_snapshot);
	cb_scrub = atomic_read(&volume->cb_scrub);
	_debug("vnode inval %x==%x %x==%x",
	       vnode->cb_ro_snapshot, cb_ro_snapshot,
	       vnode->cb_scrub, cb_scrub);
	if (vnode->cb_scrub != cb_scrub)
		zap = true;
	vnode->cb_ro_snapshot = cb_ro_snapshot;
	vnode->cb_scrub = cb_scrub;

	/* if the vnode's data version number changed then its contents are
	 * different */
	zap |= test_and_clear_bit(AFS_VNODE_ZAP_DATA, &vnode->flags);
	if (zap)
		afs_zap_data(vnode);
	up_write(&vnode->validate_lock);
	_leave(" = 0");
	return 0;

error_unlock:
	if (locked_vol)
		mutex_unlock(&volume->cb_check_lock);
	up_write(&vnode->validate_lock);
error:
	_leave(" = %d", ret);
	return ret;
}
