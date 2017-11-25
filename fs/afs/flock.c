/* AFS file locking support
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "internal.h"

#define AFS_LOCK_GRANTED	0
#define AFS_LOCK_PENDING	1

struct workqueue_struct *afs_lock_manager;

static void afs_fl_copy_lock(struct file_lock *new, struct file_lock *fl);
static void afs_fl_release_private(struct file_lock *fl);

static const struct file_lock_operations afs_lock_ops = {
	.fl_copy_lock		= afs_fl_copy_lock,
	.fl_release_private	= afs_fl_release_private,
};

/*
 * if the callback is broken on this vnode, then the lock may now be available
 */
void afs_lock_may_be_available(struct afs_vnode *vnode)
{
	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	queue_delayed_work(afs_lock_manager, &vnode->lock_work, 0);
}

/*
 * the lock will time out in 5 minutes unless we extend it, so schedule
 * extension in a bit less than that time
 */
static void afs_schedule_lock_extension(struct afs_vnode *vnode)
{
	queue_delayed_work(afs_lock_manager, &vnode->lock_work,
			   AFS_LOCKWAIT * HZ / 2);
}

/*
 * grant one or more locks (readlocks are allowed to jump the queue if the
 * first lock in the queue is itself a readlock)
 * - the caller must hold the vnode lock
 */
static void afs_grant_locks(struct afs_vnode *vnode, struct file_lock *fl)
{
	struct file_lock *p, *_p;

	list_move_tail(&fl->fl_u.afs.link, &vnode->granted_locks);
	if (fl->fl_type == F_RDLCK) {
		list_for_each_entry_safe(p, _p, &vnode->pending_locks,
					 fl_u.afs.link) {
			if (p->fl_type == F_RDLCK) {
				p->fl_u.afs.state = AFS_LOCK_GRANTED;
				list_move_tail(&p->fl_u.afs.link,
					       &vnode->granted_locks);
				wake_up(&p->fl_wait);
			}
		}
	}
}

/*
 * Get a lock on a file
 */
static int afs_set_lock(struct afs_vnode *vnode, struct key *key,
			afs_lock_type_t type)
{
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%x:%u.%u},%x,%u",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key), type);

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = vnode->cb_break + vnode->cb_s_break;
			afs_fs_set_lock(&fc, type);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * Extend a lock on a file
 */
static int afs_extend_lock(struct afs_vnode *vnode, struct key *key)
{
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%x:%u.%u},%x",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_current_fileserver(&fc)) {
			fc.cb_break = vnode->cb_break + vnode->cb_s_break;
			afs_fs_extend_lock(&fc);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * Release a lock on a file
 */
static int afs_release_lock(struct afs_vnode *vnode, struct key *key)
{
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%x:%u.%u},%x",
	       vnode->volume->name,
	       vnode->fid.vid,
	       vnode->fid.vnode,
	       vnode->fid.unique,
	       key_serial(key));

	ret = -ERESTARTSYS;
	if (afs_begin_vnode_operation(&fc, vnode, key)) {
		while (afs_select_current_fileserver(&fc)) {
			fc.cb_break = vnode->cb_break + vnode->cb_s_break;
			afs_fs_release_lock(&fc);
		}

		afs_check_for_remote_deletion(&fc, fc.vnode);
		afs_vnode_commit_status(&fc, vnode, fc.cb_break);
		ret = afs_end_vnode_operation(&fc);
	}

	_leave(" = %d", ret);
	return ret;
}

/*
 * do work for a lock, including:
 * - probing for a lock we're waiting on but didn't get immediately
 * - extending a lock that's close to timing out
 */
void afs_lock_work(struct work_struct *work)
{
	struct afs_vnode *vnode =
		container_of(work, struct afs_vnode, lock_work.work);
	struct file_lock *fl, *next;
	afs_lock_type_t type;
	struct key *key;
	int ret;

	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	spin_lock(&vnode->lock);

again:
	_debug("wstate %u for %p", vnode->lock_state, vnode);
	switch (vnode->lock_state) {
	case AFS_VNODE_LOCK_NEED_UNLOCK:
		_debug("unlock");
		vnode->lock_state = AFS_VNODE_LOCK_UNLOCKING;
		spin_unlock(&vnode->lock);

		/* attempt to release the server lock; if it fails, we just
		 * wait 5 minutes and it'll expire anyway */
		ret = afs_release_lock(vnode, vnode->lock_key);
		if (ret < 0)
			printk(KERN_WARNING "AFS:"
			       " Failed to release lock on {%x:%x} error %d\n",
			       vnode->fid.vid, vnode->fid.vnode, ret);

		spin_lock(&vnode->lock);
		key_put(vnode->lock_key);
		vnode->lock_key = NULL;
		vnode->lock_state = AFS_VNODE_LOCK_NONE;

		if (list_empty(&vnode->pending_locks)) {
			spin_unlock(&vnode->lock);
			return;
		}

		/* The new front of the queue now owns the state variables. */
		next = list_entry(vnode->pending_locks.next,
				  struct file_lock, fl_u.afs.link);
		vnode->lock_key = afs_file_key(next->fl_file);
		vnode->lock_type = (next->fl_type == F_RDLCK) ? AFS_LOCK_READ : AFS_LOCK_WRITE;
		vnode->lock_state = AFS_VNODE_LOCK_WAITING_FOR_CB;
		goto again;

	/* If we've already got a lock, then it must be time to extend that
	 * lock as AFS locks time out after 5 minutes.
	 */
	case AFS_VNODE_LOCK_GRANTED:
		_debug("extend");

		ASSERT(!list_empty(&vnode->granted_locks));

		key = key_get(vnode->lock_key);
		vnode->lock_state = AFS_VNODE_LOCK_EXTENDING;
		spin_unlock(&vnode->lock);

		ret = afs_extend_lock(vnode, key); /* RPC */
		key_put(key);

		if (ret < 0)
			pr_warning("AFS: Failed to extend lock on {%x:%x} error %d\n",
				   vnode->fid.vid, vnode->fid.vnode, ret);

		spin_lock(&vnode->lock);

		if (vnode->lock_state != AFS_VNODE_LOCK_EXTENDING)
			goto again;
		vnode->lock_state = AFS_VNODE_LOCK_GRANTED;

		if (ret == 0)
			afs_schedule_lock_extension(vnode);
		else
			queue_delayed_work(afs_lock_manager, &vnode->lock_work,
					   HZ * 10);
		spin_unlock(&vnode->lock);
		_leave(" [ext]");
		return;

		/* If we don't have a granted lock, then we must've been called
		 * back by the server, and so if might be possible to get a
		 * lock we're currently waiting for.
		 */
	case AFS_VNODE_LOCK_WAITING_FOR_CB:
		_debug("get");

		key = key_get(vnode->lock_key);
		type = vnode->lock_type;
		vnode->lock_state = AFS_VNODE_LOCK_SETTING;
		spin_unlock(&vnode->lock);

		ret = afs_set_lock(vnode, key, type); /* RPC */
		key_put(key);

		spin_lock(&vnode->lock);
		switch (ret) {
		case -EWOULDBLOCK:
			_debug("blocked");
			break;
		case 0:
			_debug("acquired");
			vnode->lock_state = AFS_VNODE_LOCK_GRANTED;
			/* Fall through */
		default:
			/* Pass the lock or the error onto the first locker in
			 * the list - if they're looking for this type of lock.
			 * If they're not, we assume that whoever asked for it
			 * took a signal.
			 */
			if (list_empty(&vnode->pending_locks)) {
				_debug("withdrawn");
				vnode->lock_state = AFS_VNODE_LOCK_NEED_UNLOCK;
				goto again;
			}

			fl = list_entry(vnode->pending_locks.next,
					struct file_lock, fl_u.afs.link);
			type = (fl->fl_type == F_RDLCK) ? AFS_LOCK_READ : AFS_LOCK_WRITE;
			if (vnode->lock_type != type) {
				_debug("changed");
				vnode->lock_state = AFS_VNODE_LOCK_NEED_UNLOCK;
				goto again;
			}

			fl->fl_u.afs.state = ret;
			if (ret == 0)
				afs_grant_locks(vnode, fl);
			else
				list_del_init(&fl->fl_u.afs.link);
			wake_up(&fl->fl_wait);
			spin_unlock(&vnode->lock);
			_leave(" [granted]");
			return;
		}

	default:
		/* Looks like a lock request was withdrawn. */
		spin_unlock(&vnode->lock);
		_leave(" [no]");
		return;
	}
}

/*
 * pass responsibility for the unlocking of a vnode on the server to the
 * manager thread, lest a pending signal in the calling thread interrupt
 * AF_RXRPC
 * - the caller must hold the vnode lock
 */
static void afs_defer_unlock(struct afs_vnode *vnode)
{
	_enter("");

	if (vnode->lock_state == AFS_VNODE_LOCK_GRANTED ||
	    vnode->lock_state == AFS_VNODE_LOCK_EXTENDING) {
		cancel_delayed_work(&vnode->lock_work);

		vnode->lock_state = AFS_VNODE_LOCK_NEED_UNLOCK;
		afs_lock_may_be_available(vnode);
	}
}

/*
 * Check that our view of the file metadata is up to date and check to see
 * whether we think that we have a locking permit.
 */
static int afs_do_setlk_check(struct afs_vnode *vnode, struct key *key,
			      afs_lock_type_t type, bool can_sleep)
{
	afs_access_t access;
	int ret;

	/* Make sure we've got a callback on this file and that our view of the
	 * data version is up to date.
	 */
	ret = afs_validate(vnode, key);
	if (ret < 0)
		return ret;

	/* Check the permission set to see if we're actually going to be
	 * allowed to get a lock on this file.
	 */
	ret = afs_check_permit(vnode, key, &access);
	if (ret < 0)
		return ret;

	/* At a rough estimation, you need LOCK, WRITE or INSERT perm to
	 * read-lock a file and WRITE or INSERT perm to write-lock a file.
	 *
	 * We can't rely on the server to do this for us since if we want to
	 * share a read lock that we already have, we won't go the server.
	 */
	if (type == AFS_LOCK_READ) {
		if (!(access & (AFS_ACE_INSERT | AFS_ACE_WRITE | AFS_ACE_LOCK)))
			return -EACCES;
		if (vnode->status.lock_count == -1 && !can_sleep)
			return -EAGAIN; /* Write locked */
	} else {
		if (!(access & (AFS_ACE_INSERT | AFS_ACE_WRITE)))
			return -EACCES;
		if (vnode->status.lock_count != 0 && !can_sleep)
			return -EAGAIN; /* Locked */
	}

	return 0;
}

/*
 * Remove the front runner from the pending queue.
 * - The caller must hold vnode->lock.
 */
static void afs_dequeue_lock(struct afs_vnode *vnode, struct file_lock *fl)
{
	struct file_lock *next;

	_enter("");

	/* ->lock_type, ->lock_key and ->lock_state only belong to this
	 * file_lock if we're at the front of the pending queue or if we have
	 * the lock granted or if the lock_state is NEED_UNLOCK or UNLOCKING.
	 */
	if (vnode->granted_locks.next == &fl->fl_u.afs.link &&
	    vnode->granted_locks.prev == &fl->fl_u.afs.link) {
		list_del_init(&fl->fl_u.afs.link);
		afs_defer_unlock(vnode);
		return;
	}

	if (!list_empty(&vnode->granted_locks) ||
	    vnode->pending_locks.next != &fl->fl_u.afs.link) {
		list_del_init(&fl->fl_u.afs.link);
		return;
	}

	list_del_init(&fl->fl_u.afs.link);
	key_put(vnode->lock_key);
	vnode->lock_key = NULL;
	vnode->lock_state = AFS_VNODE_LOCK_NONE;

	if (list_empty(&vnode->pending_locks))
		return;

	/* The new front of the queue now owns the state variables. */
	next = list_entry(vnode->pending_locks.next,
			  struct file_lock, fl_u.afs.link);
	vnode->lock_key = afs_file_key(next->fl_file);
	vnode->lock_type = (next->fl_type == F_RDLCK) ? AFS_LOCK_READ : AFS_LOCK_WRITE;
	vnode->lock_state = AFS_VNODE_LOCK_WAITING_FOR_CB;
	afs_lock_may_be_available(vnode);
}

/*
 * request a lock on a file on the server
 */
static int afs_do_setlk(struct file *file, struct file_lock *fl)
{
	struct inode *inode = locks_inode(file);
	struct afs_vnode *vnode = AFS_FS_I(inode);
	afs_lock_type_t type;
	struct key *key = afs_file_key(file);
	int ret;

	_enter("{%x:%u},%u", vnode->fid.vid, vnode->fid.vnode, fl->fl_type);

	/* only whole-file locks are supported */
	if (fl->fl_start != 0 || fl->fl_end != OFFSET_MAX)
		return -EINVAL;

	fl->fl_ops = &afs_lock_ops;
	INIT_LIST_HEAD(&fl->fl_u.afs.link);
	fl->fl_u.afs.state = AFS_LOCK_PENDING;

	type = (fl->fl_type == F_RDLCK) ? AFS_LOCK_READ : AFS_LOCK_WRITE;

	ret = afs_do_setlk_check(vnode, key, type, fl->fl_flags & FL_SLEEP);
	if (ret < 0)
		return ret;

	spin_lock(&vnode->lock);

	/* If we've already got a readlock on the server then we instantly
	 * grant another readlock, irrespective of whether there are any
	 * pending writelocks.
	 */
	if (type == AFS_LOCK_READ &&
	    vnode->lock_state == AFS_VNODE_LOCK_GRANTED &&
	    vnode->lock_type == AFS_LOCK_READ) {
		_debug("instant readlock");
		ASSERT(!list_empty(&vnode->granted_locks));
		goto share_existing_lock;
	}

	list_add_tail(&fl->fl_u.afs.link, &vnode->pending_locks);

	if (vnode->lock_state != AFS_VNODE_LOCK_NONE)
		goto need_to_wait;

	/* We don't have a lock on this vnode and we aren't currently waiting
	 * for one either, so ask the server for a lock.
	 *
	 * Note that we need to be careful if we get interrupted by a signal
	 * after dispatching the request as we may still get the lock, even
	 * though we don't wait for the reply (it's not too bad a problem - the
	 * lock will expire in 10 mins anyway).
	 */
	_debug("not locked");
	vnode->lock_key = key_get(key);
	vnode->lock_type = type;
	vnode->lock_state = AFS_VNODE_LOCK_SETTING;
	spin_unlock(&vnode->lock);

	ret = afs_set_lock(vnode, key, type); /* RPC */

	spin_lock(&vnode->lock);
	switch (ret) {
	default:
		goto abort_attempt;

	case -EWOULDBLOCK:
		/* The server doesn't have a lock-waiting queue, so the client
		 * will have to retry.  The server will break the outstanding
		 * callbacks on a file when a lock is released.
		 */
		_debug("would block");
		ASSERT(list_empty(&vnode->granted_locks));
		ASSERTCMP(vnode->pending_locks.next, ==, &fl->fl_u.afs.link);
		vnode->lock_state = AFS_VNODE_LOCK_WAITING_FOR_CB;
		goto need_to_wait;

	case 0:
		_debug("acquired");
		break;
	}

	/* we've acquired a server lock, but it needs to be renewed after 5
	 * mins */
	vnode->lock_state = AFS_VNODE_LOCK_GRANTED;
	afs_schedule_lock_extension(vnode);

share_existing_lock:
	/* the lock has been granted as far as we're concerned... */
	fl->fl_u.afs.state = AFS_LOCK_GRANTED;
	list_move_tail(&fl->fl_u.afs.link, &vnode->granted_locks);

given_lock:
	/* ... but we do still need to get the VFS's blessing */
	spin_unlock(&vnode->lock);

	ret = posix_lock_file(file, fl, NULL);
	if (ret < 0)
		goto vfs_rejected_lock;

	/* Again, make sure we've got a callback on this file and, again, make
	 * sure that our view of the data version is up to date (we ignore
	 * errors incurred here and deal with the consequences elsewhere).
	 */
	afs_validate(vnode, key);
	_leave(" = 0");
	return 0;

need_to_wait:
	/* We're going to have to wait.  Either this client doesn't have a lock
	 * on the server yet and we need to wait for a callback to occur, or
	 * the client does have a lock on the server, but it belongs to some
	 * other process(es) and is incompatible with the lock we want.
	 */
	ret = -EAGAIN;
	if (fl->fl_flags & FL_SLEEP) {
		spin_unlock(&vnode->lock);

		_debug("sleep");
		ret = wait_event_interruptible(fl->fl_wait,
					       fl->fl_u.afs.state != AFS_LOCK_PENDING);

		spin_lock(&vnode->lock);
	}

	if (fl->fl_u.afs.state == AFS_LOCK_GRANTED)
		goto given_lock;
	if (fl->fl_u.afs.state < 0)
		ret = fl->fl_u.afs.state;

abort_attempt:
	/* we aren't going to get the lock, either because we're unwilling to
	 * wait, or because some signal happened */
	_debug("abort");
	afs_dequeue_lock(vnode, fl);

error_unlock:
	spin_unlock(&vnode->lock);
	_leave(" = %d", ret);
	return ret;

vfs_rejected_lock:
	/* The VFS rejected the lock we just obtained, so we have to discard
	 * what we just got.  We defer this to the lock manager work item to
	 * deal with.
	 */
	_debug("vfs refused %d", ret);
	spin_lock(&vnode->lock);
	list_del_init(&fl->fl_u.afs.link);
	if (list_empty(&vnode->granted_locks))
		afs_defer_unlock(vnode);
	goto error_unlock;
}

/*
 * unlock on a file on the server
 */
static int afs_do_unlk(struct file *file, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(locks_inode(file));
	int ret;

	_enter("{%x:%u},%u", vnode->fid.vid, vnode->fid.vnode, fl->fl_type);

	/* Flush all pending writes before doing anything with locks. */
	vfs_fsync(file, 0);

	/* only whole-file unlocks are supported */
	if (fl->fl_start != 0 || fl->fl_end != OFFSET_MAX)
		return -EINVAL;

	ret = posix_lock_file(file, fl, NULL);
	_leave(" = %d [%u]", ret, vnode->lock_state);
	return ret;
}

/*
 * return information about a lock we currently hold, if indeed we hold one
 */
static int afs_do_getlk(struct file *file, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(locks_inode(file));
	struct key *key = afs_file_key(file);
	int ret, lock_count;

	_enter("");

	fl->fl_type = F_UNLCK;

	/* check local lock records first */
	posix_test_lock(file, fl);
	if (fl->fl_type == F_UNLCK) {
		/* no local locks; consult the server */
		ret = afs_fetch_status(vnode, key);
		if (ret < 0)
			goto error;

		lock_count = READ_ONCE(vnode->status.lock_count);
		if (lock_count > 0)
			fl->fl_type = F_RDLCK;
		else
			fl->fl_type = F_WRLCK;
		fl->fl_start = 0;
		fl->fl_end = OFFSET_MAX;
	}

	ret = 0;
error:
	_leave(" = %d [%hd]", ret, fl->fl_type);
	return ret;
}

/*
 * manage POSIX locks on a file
 */
int afs_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(locks_inode(file));

	_enter("{%x:%u},%d,{t=%x,fl=%x,r=%Ld:%Ld}",
	       vnode->fid.vid, vnode->fid.vnode, cmd,
	       fl->fl_type, fl->fl_flags,
	       (long long) fl->fl_start, (long long) fl->fl_end);

	/* AFS doesn't support mandatory locks */
	if (__mandatory_lock(&vnode->vfs_inode) && fl->fl_type != F_UNLCK)
		return -ENOLCK;

	if (IS_GETLK(cmd))
		return afs_do_getlk(file, fl);
	if (fl->fl_type == F_UNLCK)
		return afs_do_unlk(file, fl);
	return afs_do_setlk(file, fl);
}

/*
 * manage FLOCK locks on a file
 */
int afs_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(locks_inode(file));

	_enter("{%x:%u},%d,{t=%x,fl=%x}",
	       vnode->fid.vid, vnode->fid.vnode, cmd,
	       fl->fl_type, fl->fl_flags);

	/*
	 * No BSD flocks over NFS allowed.
	 * Note: we could try to fake a POSIX lock request here by
	 * using ((u32) filp | 0x80000000) or some such as the pid.
	 * Not sure whether that would be unique, though, or whether
	 * that would break in other places.
	 */
	if (!(fl->fl_flags & FL_FLOCK))
		return -ENOLCK;

	/* we're simulating flock() locks using posix locks on the server */
	if (fl->fl_type == F_UNLCK)
		return afs_do_unlk(file, fl);
	return afs_do_setlk(file, fl);
}

/*
 * the POSIX lock management core VFS code copies the lock record and adds the
 * copy into its own list, so we need to add that copy to the vnode's lock
 * queue in the same place as the original (which will be deleted shortly
 * after)
 */
static void afs_fl_copy_lock(struct file_lock *new, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(locks_inode(fl->fl_file));

	_enter("");

	spin_lock(&vnode->lock);
	list_add(&new->fl_u.afs.link, &fl->fl_u.afs.link);
	spin_unlock(&vnode->lock);
}

/*
 * need to remove this lock from the vnode queue when it's removed from the
 * VFS's list
 */
static void afs_fl_release_private(struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(locks_inode(fl->fl_file));

	_enter("");

	spin_lock(&vnode->lock);
	afs_dequeue_lock(vnode, fl);
	_debug("state %u for %p", vnode->lock_state, vnode);
	spin_unlock(&vnode->lock);
}
