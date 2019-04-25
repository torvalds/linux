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
#define AFS_LOCK_YOUR_TRY	2

struct workqueue_struct *afs_lock_manager;

static void afs_next_locker(struct afs_vnode *vnode, int error);
static void afs_fl_copy_lock(struct file_lock *new, struct file_lock *fl);
static void afs_fl_release_private(struct file_lock *fl);

static const struct file_lock_operations afs_lock_ops = {
	.fl_copy_lock		= afs_fl_copy_lock,
	.fl_release_private	= afs_fl_release_private,
};

static inline void afs_set_lock_state(struct afs_vnode *vnode, enum afs_lock_state state)
{
	_debug("STATE %u -> %u", vnode->lock_state, state);
	vnode->lock_state = state;
}

/*
 * if the callback is broken on this vnode, then the lock may now be available
 */
void afs_lock_may_be_available(struct afs_vnode *vnode)
{
	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	if (vnode->lock_state != AFS_VNODE_LOCK_WAITING_FOR_CB)
		return;

	spin_lock(&vnode->lock);
	if (vnode->lock_state == AFS_VNODE_LOCK_WAITING_FOR_CB)
		afs_next_locker(vnode, 0);
	spin_unlock(&vnode->lock);
}

/*
 * the lock will time out in 5 minutes unless we extend it, so schedule
 * extension in a bit less than that time
 */
static void __maybe_unused afs_schedule_lock_extension(struct afs_vnode *vnode)
{
	queue_delayed_work(afs_lock_manager, &vnode->lock_work,
			   AFS_LOCKWAIT * HZ / 2);
}

/*
 * grant one or more locks (readlocks are allowed to jump the queue if the
 * first lock in the queue is itself a readlock)
 * - the caller must hold the vnode lock
 */
static void afs_grant_locks(struct afs_vnode *vnode)
{
	struct file_lock *p, *_p;
	bool exclusive = (vnode->lock_type == AFS_LOCK_WRITE);

	list_for_each_entry_safe(p, _p, &vnode->pending_locks, fl_u.afs.link) {
		if (!exclusive && p->fl_type == F_WRLCK)
			continue;

		list_move_tail(&p->fl_u.afs.link, &vnode->granted_locks);
		p->fl_u.afs.state = AFS_LOCK_GRANTED;
		wake_up(&p->fl_wait);
	}
}

/*
 * If an error is specified, reject every pending lock that matches the
 * authentication and type of the lock we failed to get.  If there are any
 * remaining lockers, try to wake up one of them to have a go.
 */
static void afs_next_locker(struct afs_vnode *vnode, int error)
{
	struct file_lock *p, *_p, *next = NULL;
	struct key *key = vnode->lock_key;
	unsigned int fl_type = F_RDLCK;

	_enter("");

	if (vnode->lock_type == AFS_LOCK_WRITE)
		fl_type = F_WRLCK;

	list_for_each_entry_safe(p, _p, &vnode->pending_locks, fl_u.afs.link) {
		if (error &&
		    p->fl_type == fl_type &&
		    afs_file_key(p->fl_file) == key) {
			list_del_init(&p->fl_u.afs.link);
			p->fl_u.afs.state = error;
			wake_up(&p->fl_wait);
		}

		/* Select the next locker to hand off to. */
		if (next &&
		    (next->fl_type == F_WRLCK || p->fl_type == F_RDLCK))
			continue;
		next = p;
	}

	vnode->lock_key = NULL;
	key_put(key);

	if (next) {
		afs_set_lock_state(vnode, AFS_VNODE_LOCK_SETTING);
		next->fl_u.afs.state = AFS_LOCK_YOUR_TRY;
		wake_up(&next->fl_wait);
	} else {
		afs_set_lock_state(vnode, AFS_VNODE_LOCK_NONE);
	}

	_leave("");
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
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
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
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
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
			fc.cb_break = afs_calc_vnode_cb_break(vnode);
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
	struct key *key;
	int ret;

	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	spin_lock(&vnode->lock);

again:
	_debug("wstate %u for %p", vnode->lock_state, vnode);
	switch (vnode->lock_state) {
	case AFS_VNODE_LOCK_NEED_UNLOCK:
		_debug("unlock");
		afs_set_lock_state(vnode, AFS_VNODE_LOCK_UNLOCKING);
		spin_unlock(&vnode->lock);

		/* attempt to release the server lock; if it fails, we just
		 * wait 5 minutes and it'll expire anyway */
		ret = afs_release_lock(vnode, vnode->lock_key);
		if (ret < 0)
			printk(KERN_WARNING "AFS:"
			       " Failed to release lock on {%x:%x} error %d\n",
			       vnode->fid.vid, vnode->fid.vnode, ret);

		spin_lock(&vnode->lock);
		afs_next_locker(vnode, 0);
		spin_unlock(&vnode->lock);
		return;

	/* If we've already got a lock, then it must be time to extend that
	 * lock as AFS locks time out after 5 minutes.
	 */
	case AFS_VNODE_LOCK_GRANTED:
		_debug("extend");

		ASSERT(!list_empty(&vnode->granted_locks));

		key = key_get(vnode->lock_key);
		afs_set_lock_state(vnode, AFS_VNODE_LOCK_EXTENDING);
		spin_unlock(&vnode->lock);

		ret = afs_extend_lock(vnode, key); /* RPC */
		key_put(key);

		if (ret < 0)
			pr_warning("AFS: Failed to extend lock on {%x:%x} error %d\n",
				   vnode->fid.vid, vnode->fid.vnode, ret);

		spin_lock(&vnode->lock);

		if (vnode->lock_state != AFS_VNODE_LOCK_EXTENDING)
			goto again;
		afs_set_lock_state(vnode, AFS_VNODE_LOCK_GRANTED);

		if (ret != 0)
			queue_delayed_work(afs_lock_manager, &vnode->lock_work,
					   HZ * 10);
		spin_unlock(&vnode->lock);
		_leave(" [ext]");
		return;

	/* If we're waiting for a callback to indicate lock release, we can't
	 * actually rely on this, so need to recheck at regular intervals.  The
	 * problem is that the server might not notify us if the lock just
	 * expires (say because a client died) rather than being explicitly
	 * released.
	 */
	case AFS_VNODE_LOCK_WAITING_FOR_CB:
		_debug("retry");
		afs_next_locker(vnode, 0);
		spin_unlock(&vnode->lock);
		return;

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
	_enter("%u", vnode->lock_state);

	if (list_empty(&vnode->granted_locks) &&
	    (vnode->lock_state == AFS_VNODE_LOCK_GRANTED ||
	     vnode->lock_state == AFS_VNODE_LOCK_EXTENDING)) {
		cancel_delayed_work(&vnode->lock_work);

		afs_set_lock_state(vnode, AFS_VNODE_LOCK_NEED_UNLOCK);
		queue_delayed_work(afs_lock_manager, &vnode->lock_work, 0);
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

	fl->fl_ops = &afs_lock_ops;
	INIT_LIST_HEAD(&fl->fl_u.afs.link);
	fl->fl_u.afs.state = AFS_LOCK_PENDING;

	type = (fl->fl_type == F_RDLCK) ? AFS_LOCK_READ : AFS_LOCK_WRITE;

	ret = afs_do_setlk_check(vnode, key, type, fl->fl_flags & FL_SLEEP);
	if (ret < 0)
		return ret;

	spin_lock(&vnode->lock);
	list_add_tail(&fl->fl_u.afs.link, &vnode->pending_locks);

	/* If we've already got a lock on the server then try to move to having
	 * the VFS grant the requested lock.  Note that this means that other
	 * clients may get starved out.
	 */
	_debug("try %u", vnode->lock_state);
	if (vnode->lock_state == AFS_VNODE_LOCK_GRANTED) {
		if (type == AFS_LOCK_READ) {
			_debug("instant readlock");
			list_move_tail(&fl->fl_u.afs.link, &vnode->granted_locks);
			fl->fl_u.afs.state = AFS_LOCK_GRANTED;
			goto vnode_is_locked_u;
		}

		if (vnode->lock_type == AFS_LOCK_WRITE) {
			_debug("instant writelock");
			list_move_tail(&fl->fl_u.afs.link, &vnode->granted_locks);
			fl->fl_u.afs.state = AFS_LOCK_GRANTED;
			goto vnode_is_locked_u;
		}
	}

	if (vnode->lock_state != AFS_VNODE_LOCK_NONE)
		goto need_to_wait;

try_to_lock:
	/* We don't have a lock on this vnode and we aren't currently waiting
	 * for one either, so ask the server for a lock.
	 *
	 * Note that we need to be careful if we get interrupted by a signal
	 * after dispatching the request as we may still get the lock, even
	 * though we don't wait for the reply (it's not too bad a problem - the
	 * lock will expire in 5 mins anyway).
	 */
	_debug("not locked");
	vnode->lock_key = key_get(key);
	vnode->lock_type = type;
	afs_set_lock_state(vnode, AFS_VNODE_LOCK_SETTING);
	spin_unlock(&vnode->lock);

	ret = afs_set_lock(vnode, key, type); /* RPC */

	spin_lock(&vnode->lock);
	switch (ret) {
	case -EKEYREJECTED:
	case -EKEYEXPIRED:
	case -EKEYREVOKED:
	case -EPERM:
	case -EACCES:
		fl->fl_u.afs.state = ret;
		list_del_init(&fl->fl_u.afs.link);
		afs_next_locker(vnode, ret);
		goto error_unlock;

	default:
		fl->fl_u.afs.state = ret;
		list_del_init(&fl->fl_u.afs.link);
		afs_next_locker(vnode, 0);
		goto error_unlock;

	case -EWOULDBLOCK:
		/* The server doesn't have a lock-waiting queue, so the client
		 * will have to retry.  The server will break the outstanding
		 * callbacks on a file when a lock is released.
		 */
		_debug("would block");
		ASSERT(list_empty(&vnode->granted_locks));
		ASSERTCMP(vnode->pending_locks.next, ==, &fl->fl_u.afs.link);
		goto lock_is_contended;

	case 0:
		_debug("acquired");
		afs_set_lock_state(vnode, AFS_VNODE_LOCK_GRANTED);
		afs_grant_locks(vnode);
		goto vnode_is_locked_u;
	}

vnode_is_locked_u:
	spin_unlock(&vnode->lock);
vnode_is_locked:
	/* the lock has been granted by the server... */
	ASSERTCMP(fl->fl_u.afs.state, ==, AFS_LOCK_GRANTED);

	/* ... but the VFS still needs to distribute access on this client. */
	ret = locks_lock_file_wait(file, fl);
	if (ret < 0)
		goto vfs_rejected_lock;

	/* Again, make sure we've got a callback on this file and, again, make
	 * sure that our view of the data version is up to date (we ignore
	 * errors incurred here and deal with the consequences elsewhere).
	 */
	afs_validate(vnode, key);
	_leave(" = 0");
	return 0;

lock_is_contended:
	if (!(fl->fl_flags & FL_SLEEP)) {
		list_del_init(&fl->fl_u.afs.link);
		afs_next_locker(vnode, 0);
		ret = -EAGAIN;
		goto error_unlock;
	}

	afs_set_lock_state(vnode, AFS_VNODE_LOCK_WAITING_FOR_CB);
	queue_delayed_work(afs_lock_manager, &vnode->lock_work, HZ * 5);

need_to_wait:
	/* We're going to have to wait.  Either this client doesn't have a lock
	 * on the server yet and we need to wait for a callback to occur, or
	 * the client does have a lock on the server, but it's shared and we
	 * need an exclusive lock.
	 */
	spin_unlock(&vnode->lock);

	_debug("sleep");
	ret = wait_event_interruptible(fl->fl_wait,
				       fl->fl_u.afs.state != AFS_LOCK_PENDING);
	_debug("wait = %d", ret);

	if (fl->fl_u.afs.state >= 0 && fl->fl_u.afs.state != AFS_LOCK_GRANTED) {
		spin_lock(&vnode->lock);

		switch (fl->fl_u.afs.state) {
		case AFS_LOCK_YOUR_TRY:
			fl->fl_u.afs.state = AFS_LOCK_PENDING;
			goto try_to_lock;
		case AFS_LOCK_PENDING:
			if (ret > 0) {
				/* We need to retry the lock.  We may not be
				 * notified by the server if it just expired
				 * rather than being released.
				 */
				ASSERTCMP(vnode->lock_state, ==, AFS_VNODE_LOCK_WAITING_FOR_CB);
				afs_set_lock_state(vnode, AFS_VNODE_LOCK_SETTING);
				fl->fl_u.afs.state = AFS_LOCK_PENDING;
				goto try_to_lock;
			}
			goto error_unlock;
		case AFS_LOCK_GRANTED:
		default:
			break;
		}

		spin_unlock(&vnode->lock);
	}

	if (fl->fl_u.afs.state == AFS_LOCK_GRANTED)
		goto vnode_is_locked;
	ret = fl->fl_u.afs.state;
	goto error;

vfs_rejected_lock:
	/* The VFS rejected the lock we just obtained, so we have to discard
	 * what we just got.  We defer this to the lock manager work item to
	 * deal with.
	 */
	_debug("vfs refused %d", ret);
	spin_lock(&vnode->lock);
	list_del_init(&fl->fl_u.afs.link);
	afs_defer_unlock(vnode);

error_unlock:
	spin_unlock(&vnode->lock);
error:
	_leave(" = %d", ret);
	return ret;
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

	ret = locks_lock_file_wait(file, fl);
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
		ret = afs_fetch_status(vnode, key, false);
		if (ret < 0)
			goto error;

		lock_count = READ_ONCE(vnode->status.lock_count);
		if (lock_count != 0) {
			if (lock_count > 0)
				fl->fl_type = F_RDLCK;
			else
				fl->fl_type = F_WRLCK;
			fl->fl_start = 0;
			fl->fl_end = OFFSET_MAX;
			fl->fl_pid = 0;
		}
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

	list_del_init(&fl->fl_u.afs.link);
	if (list_empty(&vnode->granted_locks))
		afs_defer_unlock(vnode);

	_debug("state %u for %p", vnode->lock_state, vnode);
	spin_unlock(&vnode->lock);
}
