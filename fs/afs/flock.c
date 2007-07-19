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

#include <linux/smp_lock.h>
#include "internal.h"

#define AFS_LOCK_GRANTED	0
#define AFS_LOCK_PENDING	1

static void afs_fl_copy_lock(struct file_lock *new, struct file_lock *fl);
static void afs_fl_release_private(struct file_lock *fl);

static struct workqueue_struct *afs_lock_manager;

static struct file_lock_operations afs_lock_ops = {
	.fl_copy_lock		= afs_fl_copy_lock,
	.fl_release_private	= afs_fl_release_private,
};

/*
 * initialise the lock manager thread if it isn't already running
 */
static int afs_init_lock_manager(void)
{
	if (!afs_lock_manager) {
		afs_lock_manager = create_singlethread_workqueue("kafs_lockd");
		if (!afs_lock_manager)
			return -ENOMEM;
	}
	return 0;
}

/*
 * destroy the lock manager thread if it's running
 */
void __exit afs_kill_lock_manager(void)
{
	if (afs_lock_manager)
		destroy_workqueue(afs_lock_manager);
}

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
 * do work for a lock, including:
 * - probing for a lock we're waiting on but didn't get immediately
 * - extending a lock that's close to timing out
 */
void afs_lock_work(struct work_struct *work)
{
	struct afs_vnode *vnode =
		container_of(work, struct afs_vnode, lock_work.work);
	struct file_lock *fl;
	afs_lock_type_t type;
	struct key *key;
	int ret;

	_enter("{%x:%u}", vnode->fid.vid, vnode->fid.vnode);

	spin_lock(&vnode->lock);

	if (test_bit(AFS_VNODE_UNLOCKING, &vnode->flags)) {
		_debug("unlock");
		spin_unlock(&vnode->lock);

		/* attempt to release the server lock; if it fails, we just
		 * wait 5 minutes and it'll time out anyway */
		ret = afs_vnode_release_lock(vnode, vnode->unlock_key);
		if (ret < 0)
			printk(KERN_WARNING "AFS:"
			       " Failed to release lock on {%x:%x} error %d\n",
			       vnode->fid.vid, vnode->fid.vnode, ret);

		spin_lock(&vnode->lock);
		key_put(vnode->unlock_key);
		vnode->unlock_key = NULL;
		clear_bit(AFS_VNODE_UNLOCKING, &vnode->flags);
	}

	/* if we've got a lock, then it must be time to extend that lock as AFS
	 * locks time out after 5 minutes */
	if (!list_empty(&vnode->granted_locks)) {
		_debug("extend");

		if (test_and_set_bit(AFS_VNODE_LOCKING, &vnode->flags))
			BUG();
		fl = list_entry(vnode->granted_locks.next,
				struct file_lock, fl_u.afs.link);
		key = key_get(fl->fl_file->private_data);
		spin_unlock(&vnode->lock);

		ret = afs_vnode_extend_lock(vnode, key);
		clear_bit(AFS_VNODE_LOCKING, &vnode->flags);
		key_put(key);
		switch (ret) {
		case 0:
			afs_schedule_lock_extension(vnode);
			break;
		default:
			/* ummm... we failed to extend the lock - retry
			 * extension shortly */
			printk(KERN_WARNING "AFS:"
			       " Failed to extend lock on {%x:%x} error %d\n",
			       vnode->fid.vid, vnode->fid.vnode, ret);
			queue_delayed_work(afs_lock_manager, &vnode->lock_work,
					   HZ * 10);
			break;
		}
		_leave(" [extend]");
		return;
	}

	/* if we don't have a granted lock, then we must've been called back by
	 * the server, and so if might be possible to get a lock we're
	 * currently waiting for */
	if (!list_empty(&vnode->pending_locks)) {
		_debug("get");

		if (test_and_set_bit(AFS_VNODE_LOCKING, &vnode->flags))
			BUG();
		fl = list_entry(vnode->pending_locks.next,
				struct file_lock, fl_u.afs.link);
		key = key_get(fl->fl_file->private_data);
		type = (fl->fl_type == F_RDLCK) ?
			AFS_LOCK_READ : AFS_LOCK_WRITE;
		spin_unlock(&vnode->lock);

		ret = afs_vnode_set_lock(vnode, key, type);
		clear_bit(AFS_VNODE_LOCKING, &vnode->flags);
		switch (ret) {
		case -EWOULDBLOCK:
			_debug("blocked");
			break;
		case 0:
			_debug("acquired");
			if (type == AFS_LOCK_READ)
				set_bit(AFS_VNODE_READLOCKED, &vnode->flags);
			else
				set_bit(AFS_VNODE_WRITELOCKED, &vnode->flags);
			ret = AFS_LOCK_GRANTED;
		default:
			spin_lock(&vnode->lock);
			/* the pending lock may have been withdrawn due to a
			 * signal */
			if (list_entry(vnode->pending_locks.next,
				       struct file_lock, fl_u.afs.link) == fl) {
				fl->fl_u.afs.state = ret;
				if (ret == AFS_LOCK_GRANTED)
					list_move_tail(&fl->fl_u.afs.link,
						       &vnode->granted_locks);
				else
					list_del_init(&fl->fl_u.afs.link);
				wake_up(&fl->fl_wait);
				spin_unlock(&vnode->lock);
			} else {
				_debug("withdrawn");
				clear_bit(AFS_VNODE_READLOCKED, &vnode->flags);
				clear_bit(AFS_VNODE_WRITELOCKED, &vnode->flags);
				spin_unlock(&vnode->lock);
				afs_vnode_release_lock(vnode, key);
				if (!list_empty(&vnode->pending_locks))
					afs_lock_may_be_available(vnode);
			}
			break;
		}
		key_put(key);
		_leave(" [pend]");
		return;
	}

	/* looks like the lock request was withdrawn on a signal */
	spin_unlock(&vnode->lock);
	_leave(" [no locks]");
}

/*
 * pass responsibility for the unlocking of a vnode on the server to the
 * manager thread, lest a pending signal in the calling thread interrupt
 * AF_RXRPC
 * - the caller must hold the vnode lock
 */
static void afs_defer_unlock(struct afs_vnode *vnode, struct key *key)
{
	cancel_delayed_work(&vnode->lock_work);
	if (!test_and_clear_bit(AFS_VNODE_READLOCKED, &vnode->flags) &&
	    !test_and_clear_bit(AFS_VNODE_WRITELOCKED, &vnode->flags))
		BUG();
	if (test_and_set_bit(AFS_VNODE_UNLOCKING, &vnode->flags))
		BUG();
	vnode->unlock_key = key_get(key);
	afs_lock_may_be_available(vnode);
}

/*
 * request a lock on a file on the server
 */
static int afs_do_setlk(struct file *file, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(file->f_mapping->host);
	afs_lock_type_t type;
	struct key *key = file->private_data;
	int ret;

	_enter("{%x:%u},%u", vnode->fid.vid, vnode->fid.vnode, fl->fl_type);

	/* only whole-file locks are supported */
	if (fl->fl_start != 0 || fl->fl_end != OFFSET_MAX)
		return -EINVAL;

	ret = afs_init_lock_manager();
	if (ret < 0)
		return ret;

	fl->fl_ops = &afs_lock_ops;
	INIT_LIST_HEAD(&fl->fl_u.afs.link);
	fl->fl_u.afs.state = AFS_LOCK_PENDING;

	type = (fl->fl_type == F_RDLCK) ? AFS_LOCK_READ : AFS_LOCK_WRITE;

	lock_kernel();

	/* make sure we've got a callback on this file and that our view of the
	 * data version is up to date */
	ret = afs_vnode_fetch_status(vnode, NULL, key);
	if (ret < 0)
		goto error;

	if (vnode->status.lock_count != 0 && !(fl->fl_flags & FL_SLEEP)) {
		ret = -EAGAIN;
		goto error;
	}

	spin_lock(&vnode->lock);

	if (list_empty(&vnode->pending_locks)) {
		/* if there's no-one else with a lock on this vnode, then we
		 * need to ask the server for a lock */
		if (list_empty(&vnode->granted_locks)) {
			_debug("not locked");
			ASSERTCMP(vnode->flags &
				  ((1 << AFS_VNODE_LOCKING) |
				   (1 << AFS_VNODE_READLOCKED) |
				   (1 << AFS_VNODE_WRITELOCKED)), ==, 0);
			list_add_tail(&fl->fl_u.afs.link, &vnode->pending_locks);
			set_bit(AFS_VNODE_LOCKING, &vnode->flags);
			spin_unlock(&vnode->lock);

			ret = afs_vnode_set_lock(vnode, key, type);
			clear_bit(AFS_VNODE_LOCKING, &vnode->flags);
			switch (ret) {
			case 0:
				goto acquired_server_lock;
			case -EWOULDBLOCK:
				spin_lock(&vnode->lock);
				ASSERT(list_empty(&vnode->granted_locks));
				ASSERTCMP(vnode->pending_locks.next, ==,
					  &fl->fl_u.afs.link);
				goto wait;
			default:
				spin_lock(&vnode->lock);
				list_del_init(&fl->fl_u.afs.link);
				spin_unlock(&vnode->lock);
				goto error;
			}
		}

		/* if we've already got a readlock on the server and no waiting
		 * writelocks, then we might be able to instantly grant another
		 * readlock */
		if (type == AFS_LOCK_READ &&
		    vnode->flags & (1 << AFS_VNODE_READLOCKED)) {
			_debug("instant readlock");
			ASSERTCMP(vnode->flags &
				  ((1 << AFS_VNODE_LOCKING) |
				   (1 << AFS_VNODE_WRITELOCKED)), ==, 0);
			ASSERT(!list_empty(&vnode->granted_locks));
			goto sharing_existing_lock;
		}
	}

	/* otherwise, we need to wait for a local lock to become available */
	_debug("wait local");
	list_add_tail(&fl->fl_u.afs.link, &vnode->pending_locks);
wait:
	if (!(fl->fl_flags & FL_SLEEP)) {
		_debug("noblock");
		ret = -EAGAIN;
		goto abort_attempt;
	}
	spin_unlock(&vnode->lock);

	/* now we need to sleep and wait for the lock manager thread to get the
	 * lock from the server */
	_debug("sleep");
	ret = wait_event_interruptible(fl->fl_wait,
				       fl->fl_u.afs.state <= AFS_LOCK_GRANTED);
	if (fl->fl_u.afs.state <= AFS_LOCK_GRANTED) {
		ret = fl->fl_u.afs.state;
		if (ret < 0)
			goto error;
		spin_lock(&vnode->lock);
		goto given_lock;
	}

	/* we were interrupted, but someone may still be in the throes of
	 * giving us the lock */
	_debug("intr");
	ASSERTCMP(ret, ==, -ERESTARTSYS);

	spin_lock(&vnode->lock);
	if (fl->fl_u.afs.state <= AFS_LOCK_GRANTED) {
		ret = fl->fl_u.afs.state;
		if (ret < 0) {
			spin_unlock(&vnode->lock);
			goto error;
		}
		goto given_lock;
	}

abort_attempt:
	/* we aren't going to get the lock, either because we're unwilling to
	 * wait, or because some signal happened */
	_debug("abort");
	if (list_empty(&vnode->granted_locks) &&
	    vnode->pending_locks.next == &fl->fl_u.afs.link) {
		if (vnode->pending_locks.prev != &fl->fl_u.afs.link) {
			/* kick the next pending lock into having a go */
			list_del_init(&fl->fl_u.afs.link);
			afs_lock_may_be_available(vnode);
		}
	} else {
		list_del_init(&fl->fl_u.afs.link);
	}
	spin_unlock(&vnode->lock);
	goto error;

acquired_server_lock:
	/* we've acquired a server lock, but it needs to be renewed after 5
	 * mins */
	spin_lock(&vnode->lock);
	afs_schedule_lock_extension(vnode);
	if (type == AFS_LOCK_READ)
		set_bit(AFS_VNODE_READLOCKED, &vnode->flags);
	else
		set_bit(AFS_VNODE_WRITELOCKED, &vnode->flags);
sharing_existing_lock:
	/* the lock has been granted as far as we're concerned... */
	fl->fl_u.afs.state = AFS_LOCK_GRANTED;
	list_move_tail(&fl->fl_u.afs.link, &vnode->granted_locks);
given_lock:
	/* ... but we do still need to get the VFS's blessing */
	ASSERT(!(vnode->flags & (1 << AFS_VNODE_LOCKING)));
	ASSERT((vnode->flags & ((1 << AFS_VNODE_READLOCKED) |
				(1 << AFS_VNODE_WRITELOCKED))) != 0);
	ret = posix_lock_file(file, fl, NULL);
	if (ret < 0)
		goto vfs_rejected_lock;
	spin_unlock(&vnode->lock);

	/* again, make sure we've got a callback on this file and, again, make
	 * sure that our view of the data version is up to date (we ignore
	 * errors incurred here and deal with the consequences elsewhere) */
	afs_vnode_fetch_status(vnode, NULL, key);

error:
	unlock_kernel();
	_leave(" = %d", ret);
	return ret;

vfs_rejected_lock:
	/* the VFS rejected the lock we just obtained, so we have to discard
	 * what we just got */
	_debug("vfs refused %d", ret);
	list_del_init(&fl->fl_u.afs.link);
	if (list_empty(&vnode->granted_locks))
		afs_defer_unlock(vnode, key);
	spin_unlock(&vnode->lock);
	goto abort_attempt;
}

/*
 * unlock on a file on the server
 */
static int afs_do_unlk(struct file *file, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(file->f_mapping->host);
	struct key *key = file->private_data;
	int ret;

	_enter("{%x:%u},%u", vnode->fid.vid, vnode->fid.vnode, fl->fl_type);

	/* only whole-file unlocks are supported */
	if (fl->fl_start != 0 || fl->fl_end != OFFSET_MAX)
		return -EINVAL;

	fl->fl_ops = &afs_lock_ops;
	INIT_LIST_HEAD(&fl->fl_u.afs.link);
	fl->fl_u.afs.state = AFS_LOCK_PENDING;

	spin_lock(&vnode->lock);
	ret = posix_lock_file(file, fl, NULL);
	if (ret < 0) {
		spin_unlock(&vnode->lock);
		_leave(" = %d [vfs]", ret);
		return ret;
	}

	/* discard the server lock only if all granted locks are gone */
	if (list_empty(&vnode->granted_locks))
		afs_defer_unlock(vnode, key);
	spin_unlock(&vnode->lock);
	_leave(" = 0");
	return 0;
}

/*
 * return information about a lock we currently hold, if indeed we hold one
 */
static int afs_do_getlk(struct file *file, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(file->f_mapping->host);
	struct key *key = file->private_data;
	int ret, lock_count;

	_enter("");

	fl->fl_type = F_UNLCK;

	mutex_lock(&vnode->vfs_inode.i_mutex);

	/* check local lock records first */
	ret = 0;
	posix_test_lock(file, fl);
	if (fl->fl_type == F_UNLCK) {
		/* no local locks; consult the server */
		ret = afs_vnode_fetch_status(vnode, NULL, key);
		if (ret < 0)
			goto error;
		lock_count = vnode->status.lock_count;
		if (lock_count) {
			if (lock_count > 0)
				fl->fl_type = F_RDLCK;
			else
				fl->fl_type = F_WRLCK;
			fl->fl_start = 0;
			fl->fl_end = OFFSET_MAX;
		}
	}

error:
	mutex_unlock(&vnode->vfs_inode.i_mutex);
	_leave(" = %d [%hd]", ret, fl->fl_type);
	return ret;
}

/*
 * manage POSIX locks on a file
 */
int afs_lock(struct file *file, int cmd, struct file_lock *fl)
{
	struct afs_vnode *vnode = AFS_FS_I(file->f_dentry->d_inode);

	_enter("{%x:%u},%d,{t=%x,fl=%x,r=%Ld:%Ld}",
	       vnode->fid.vid, vnode->fid.vnode, cmd,
	       fl->fl_type, fl->fl_flags,
	       (long long) fl->fl_start, (long long) fl->fl_end);

	/* AFS doesn't support mandatory locks */
	if ((vnode->vfs_inode.i_mode & (S_ISGID | S_IXGRP)) == S_ISGID &&
	    fl->fl_type != F_UNLCK)
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
	struct afs_vnode *vnode = AFS_FS_I(file->f_dentry->d_inode);

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
	fl->fl_owner = (fl_owner_t) file;
	fl->fl_start = 0;
	fl->fl_end = OFFSET_MAX;

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
	_enter("");

	list_add(&new->fl_u.afs.link, &fl->fl_u.afs.link);
}

/*
 * need to remove this lock from the vnode queue when it's removed from the
 * VFS's list
 */
static void afs_fl_release_private(struct file_lock *fl)
{
	_enter("");

	list_del_init(&fl->fl_u.afs.link);
}
