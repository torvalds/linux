// SPDX-License-Identifier: GPL-2.0-or-later
/* AFS file locking support
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include "internal.h"

#define AFS_LOCK_GRANTED	0
#define AFS_LOCK_PENDING	1
#define AFS_LOCK_YOUR_TRY	2

struct workqueue_struct *afs_lock_manager;

static void afs_next_locker(struct afs_vyesde *vyesde, int error);
static void afs_fl_copy_lock(struct file_lock *new, struct file_lock *fl);
static void afs_fl_release_private(struct file_lock *fl);

static const struct file_lock_operations afs_lock_ops = {
	.fl_copy_lock		= afs_fl_copy_lock,
	.fl_release_private	= afs_fl_release_private,
};

static inline void afs_set_lock_state(struct afs_vyesde *vyesde, enum afs_lock_state state)
{
	_debug("STATE %u -> %u", vyesde->lock_state, state);
	vyesde->lock_state = state;
}

static atomic_t afs_file_lock_debug_id;

/*
 * if the callback is broken on this vyesde, then the lock may yesw be available
 */
void afs_lock_may_be_available(struct afs_vyesde *vyesde)
{
	_enter("{%llx:%llu}", vyesde->fid.vid, vyesde->fid.vyesde);

	spin_lock(&vyesde->lock);
	if (vyesde->lock_state == AFS_VNODE_LOCK_WAITING_FOR_CB)
		afs_next_locker(vyesde, 0);
	trace_afs_flock_ev(vyesde, NULL, afs_flock_callback_break, 0);
	spin_unlock(&vyesde->lock);
}

/*
 * the lock will time out in 5 minutes unless we extend it, so schedule
 * extension in a bit less than that time
 */
static void afs_schedule_lock_extension(struct afs_vyesde *vyesde)
{
	ktime_t expires_at, yesw, duration;
	u64 duration_j;

	expires_at = ktime_add_ms(vyesde->locked_at, AFS_LOCKWAIT * 1000 / 2);
	yesw = ktime_get_real();
	duration = ktime_sub(expires_at, yesw);
	if (duration <= 0)
		duration_j = 0;
	else
		duration_j = nsecs_to_jiffies(ktime_to_ns(duration));

	queue_delayed_work(afs_lock_manager, &vyesde->lock_work, duration_j);
}

/*
 * In the case of successful completion of a lock operation, record the time
 * the reply appeared and start the lock extension timer.
 */
void afs_lock_op_done(struct afs_call *call)
{
	struct afs_vyesde *vyesde = call->lvyesde;

	if (call->error == 0) {
		spin_lock(&vyesde->lock);
		trace_afs_flock_ev(vyesde, NULL, afs_flock_timestamp, 0);
		vyesde->locked_at = call->reply_time;
		afs_schedule_lock_extension(vyesde);
		spin_unlock(&vyesde->lock);
	}
}

/*
 * grant one or more locks (readlocks are allowed to jump the queue if the
 * first lock in the queue is itself a readlock)
 * - the caller must hold the vyesde lock
 */
static void afs_grant_locks(struct afs_vyesde *vyesde)
{
	struct file_lock *p, *_p;
	bool exclusive = (vyesde->lock_type == AFS_LOCK_WRITE);

	list_for_each_entry_safe(p, _p, &vyesde->pending_locks, fl_u.afs.link) {
		if (!exclusive && p->fl_type == F_WRLCK)
			continue;

		list_move_tail(&p->fl_u.afs.link, &vyesde->granted_locks);
		p->fl_u.afs.state = AFS_LOCK_GRANTED;
		trace_afs_flock_op(vyesde, p, afs_flock_op_grant);
		wake_up(&p->fl_wait);
	}
}

/*
 * If an error is specified, reject every pending lock that matches the
 * authentication and type of the lock we failed to get.  If there are any
 * remaining lockers, try to wake up one of them to have a go.
 */
static void afs_next_locker(struct afs_vyesde *vyesde, int error)
{
	struct file_lock *p, *_p, *next = NULL;
	struct key *key = vyesde->lock_key;
	unsigned int fl_type = F_RDLCK;

	_enter("");

	if (vyesde->lock_type == AFS_LOCK_WRITE)
		fl_type = F_WRLCK;

	list_for_each_entry_safe(p, _p, &vyesde->pending_locks, fl_u.afs.link) {
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

	vyesde->lock_key = NULL;
	key_put(key);

	if (next) {
		afs_set_lock_state(vyesde, AFS_VNODE_LOCK_SETTING);
		next->fl_u.afs.state = AFS_LOCK_YOUR_TRY;
		trace_afs_flock_op(vyesde, next, afs_flock_op_wake);
		wake_up(&next->fl_wait);
	} else {
		afs_set_lock_state(vyesde, AFS_VNODE_LOCK_NONE);
		trace_afs_flock_ev(vyesde, NULL, afs_flock_yes_lockers, 0);
	}

	_leave("");
}

/*
 * Kill off all waiters in the the pending lock queue due to the vyesde being
 * deleted.
 */
static void afs_kill_lockers_eyesent(struct afs_vyesde *vyesde)
{
	struct file_lock *p;

	afs_set_lock_state(vyesde, AFS_VNODE_LOCK_DELETED);

	while (!list_empty(&vyesde->pending_locks)) {
		p = list_entry(vyesde->pending_locks.next,
			       struct file_lock, fl_u.afs.link);
		list_del_init(&p->fl_u.afs.link);
		p->fl_u.afs.state = -ENOENT;
		wake_up(&p->fl_wait);
	}

	key_put(vyesde->lock_key);
	vyesde->lock_key = NULL;
}

/*
 * Get a lock on a file
 */
static int afs_set_lock(struct afs_vyesde *vyesde, struct key *key,
			afs_lock_type_t type)
{
	struct afs_status_cb *scb;
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%llx:%llu.%u},%x,%u",
	       vyesde->volume->name,
	       vyesde->fid.vid,
	       vyesde->fid.vyesde,
	       vyesde->fid.unique,
	       key_serial(key), type);

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	ret = -ERESTARTSYS;
	if (afs_begin_vyesde_operation(&fc, vyesde, key, true)) {
		while (afs_select_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(vyesde);
			afs_fs_set_lock(&fc, type, scb);
		}

		afs_check_for_remote_deletion(&fc, vyesde);
		afs_vyesde_commit_status(&fc, vyesde, fc.cb_break, NULL, scb);
		ret = afs_end_vyesde_operation(&fc);
	}

	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Extend a lock on a file
 */
static int afs_extend_lock(struct afs_vyesde *vyesde, struct key *key)
{
	struct afs_status_cb *scb;
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%llx:%llu.%u},%x",
	       vyesde->volume->name,
	       vyesde->fid.vid,
	       vyesde->fid.vyesde,
	       vyesde->fid.unique,
	       key_serial(key));

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	ret = -ERESTARTSYS;
	if (afs_begin_vyesde_operation(&fc, vyesde, key, false)) {
		while (afs_select_current_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(vyesde);
			afs_fs_extend_lock(&fc, scb);
		}

		afs_check_for_remote_deletion(&fc, vyesde);
		afs_vyesde_commit_status(&fc, vyesde, fc.cb_break, NULL, scb);
		ret = afs_end_vyesde_operation(&fc);
	}

	kfree(scb);
	_leave(" = %d", ret);
	return ret;
}

/*
 * Release a lock on a file
 */
static int afs_release_lock(struct afs_vyesde *vyesde, struct key *key)
{
	struct afs_status_cb *scb;
	struct afs_fs_cursor fc;
	int ret;

	_enter("%s{%llx:%llu.%u},%x",
	       vyesde->volume->name,
	       vyesde->fid.vid,
	       vyesde->fid.vyesde,
	       vyesde->fid.unique,
	       key_serial(key));

	scb = kzalloc(sizeof(struct afs_status_cb), GFP_KERNEL);
	if (!scb)
		return -ENOMEM;

	ret = -ERESTARTSYS;
	if (afs_begin_vyesde_operation(&fc, vyesde, key, false)) {
		while (afs_select_current_fileserver(&fc)) {
			fc.cb_break = afs_calc_vyesde_cb_break(vyesde);
			afs_fs_release_lock(&fc, scb);
		}

		afs_check_for_remote_deletion(&fc, vyesde);
		afs_vyesde_commit_status(&fc, vyesde, fc.cb_break, NULL, scb);
		ret = afs_end_vyesde_operation(&fc);
	}

	kfree(scb);
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
	struct afs_vyesde *vyesde =
		container_of(work, struct afs_vyesde, lock_work.work);
	struct key *key;
	int ret;

	_enter("{%llx:%llu}", vyesde->fid.vid, vyesde->fid.vyesde);

	spin_lock(&vyesde->lock);

again:
	_debug("wstate %u for %p", vyesde->lock_state, vyesde);
	switch (vyesde->lock_state) {
	case AFS_VNODE_LOCK_NEED_UNLOCK:
		afs_set_lock_state(vyesde, AFS_VNODE_LOCK_UNLOCKING);
		trace_afs_flock_ev(vyesde, NULL, afs_flock_work_unlocking, 0);
		spin_unlock(&vyesde->lock);

		/* attempt to release the server lock; if it fails, we just
		 * wait 5 minutes and it'll expire anyway */
		ret = afs_release_lock(vyesde, vyesde->lock_key);
		if (ret < 0 && vyesde->lock_state != AFS_VNODE_LOCK_DELETED) {
			trace_afs_flock_ev(vyesde, NULL, afs_flock_release_fail,
					   ret);
			printk(KERN_WARNING "AFS:"
			       " Failed to release lock on {%llx:%llx} error %d\n",
			       vyesde->fid.vid, vyesde->fid.vyesde, ret);
		}

		spin_lock(&vyesde->lock);
		if (ret == -ENOENT)
			afs_kill_lockers_eyesent(vyesde);
		else
			afs_next_locker(vyesde, 0);
		spin_unlock(&vyesde->lock);
		return;

	/* If we've already got a lock, then it must be time to extend that
	 * lock as AFS locks time out after 5 minutes.
	 */
	case AFS_VNODE_LOCK_GRANTED:
		_debug("extend");

		ASSERT(!list_empty(&vyesde->granted_locks));

		key = key_get(vyesde->lock_key);
		afs_set_lock_state(vyesde, AFS_VNODE_LOCK_EXTENDING);
		trace_afs_flock_ev(vyesde, NULL, afs_flock_work_extending, 0);
		spin_unlock(&vyesde->lock);

		ret = afs_extend_lock(vyesde, key); /* RPC */
		key_put(key);

		if (ret < 0) {
			trace_afs_flock_ev(vyesde, NULL, afs_flock_extend_fail,
					   ret);
			pr_warn("AFS: Failed to extend lock on {%llx:%llx} error %d\n",
				vyesde->fid.vid, vyesde->fid.vyesde, ret);
		}

		spin_lock(&vyesde->lock);

		if (ret == -ENOENT) {
			afs_kill_lockers_eyesent(vyesde);
			spin_unlock(&vyesde->lock);
			return;
		}

		if (vyesde->lock_state != AFS_VNODE_LOCK_EXTENDING)
			goto again;
		afs_set_lock_state(vyesde, AFS_VNODE_LOCK_GRANTED);

		if (ret != 0)
			queue_delayed_work(afs_lock_manager, &vyesde->lock_work,
					   HZ * 10);
		spin_unlock(&vyesde->lock);
		_leave(" [ext]");
		return;

	/* If we're waiting for a callback to indicate lock release, we can't
	 * actually rely on this, so need to recheck at regular intervals.  The
	 * problem is that the server might yest yestify us if the lock just
	 * expires (say because a client died) rather than being explicitly
	 * released.
	 */
	case AFS_VNODE_LOCK_WAITING_FOR_CB:
		_debug("retry");
		afs_next_locker(vyesde, 0);
		spin_unlock(&vyesde->lock);
		return;

	case AFS_VNODE_LOCK_DELETED:
		afs_kill_lockers_eyesent(vyesde);
		spin_unlock(&vyesde->lock);
		return;

		/* Fall through */
	default:
		/* Looks like a lock request was withdrawn. */
		spin_unlock(&vyesde->lock);
		_leave(" [yes]");
		return;
	}
}

/*
 * pass responsibility for the unlocking of a vyesde on the server to the
 * manager thread, lest a pending signal in the calling thread interrupt
 * AF_RXRPC
 * - the caller must hold the vyesde lock
 */
static void afs_defer_unlock(struct afs_vyesde *vyesde)
{
	_enter("%u", vyesde->lock_state);

	if (list_empty(&vyesde->granted_locks) &&
	    (vyesde->lock_state == AFS_VNODE_LOCK_GRANTED ||
	     vyesde->lock_state == AFS_VNODE_LOCK_EXTENDING)) {
		cancel_delayed_work(&vyesde->lock_work);

		afs_set_lock_state(vyesde, AFS_VNODE_LOCK_NEED_UNLOCK);
		trace_afs_flock_ev(vyesde, NULL, afs_flock_defer_unlock, 0);
		queue_delayed_work(afs_lock_manager, &vyesde->lock_work, 0);
	}
}

/*
 * Check that our view of the file metadata is up to date and check to see
 * whether we think that we have a locking permit.
 */
static int afs_do_setlk_check(struct afs_vyesde *vyesde, struct key *key,
			      enum afs_flock_mode mode, afs_lock_type_t type)
{
	afs_access_t access;
	int ret;

	/* Make sure we've got a callback on this file and that our view of the
	 * data version is up to date.
	 */
	ret = afs_validate(vyesde, key);
	if (ret < 0)
		return ret;

	/* Check the permission set to see if we're actually going to be
	 * allowed to get a lock on this file.
	 */
	ret = afs_check_permit(vyesde, key, &access);
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
	} else {
		if (!(access & (AFS_ACE_INSERT | AFS_ACE_WRITE)))
			return -EACCES;
	}

	return 0;
}

/*
 * request a lock on a file on the server
 */
static int afs_do_setlk(struct file *file, struct file_lock *fl)
{
	struct iyesde *iyesde = locks_iyesde(file);
	struct afs_vyesde *vyesde = AFS_FS_I(iyesde);
	enum afs_flock_mode mode = AFS_FS_S(iyesde->i_sb)->flock_mode;
	afs_lock_type_t type;
	struct key *key = afs_file_key(file);
	bool partial, yes_server_lock = false;
	int ret;

	if (mode == afs_flock_mode_unset)
		mode = afs_flock_mode_openafs;

	_enter("{%llx:%llu},%llu-%llu,%u,%u",
	       vyesde->fid.vid, vyesde->fid.vyesde,
	       fl->fl_start, fl->fl_end, fl->fl_type, mode);

	fl->fl_ops = &afs_lock_ops;
	INIT_LIST_HEAD(&fl->fl_u.afs.link);
	fl->fl_u.afs.state = AFS_LOCK_PENDING;

	partial = (fl->fl_start != 0 || fl->fl_end != OFFSET_MAX);
	type = (fl->fl_type == F_RDLCK) ? AFS_LOCK_READ : AFS_LOCK_WRITE;
	if (mode == afs_flock_mode_write && partial)
		type = AFS_LOCK_WRITE;

	ret = afs_do_setlk_check(vyesde, key, mode, type);
	if (ret < 0)
		return ret;

	trace_afs_flock_op(vyesde, fl, afs_flock_op_set_lock);

	/* AFS3 protocol only supports full-file locks and doesn't provide any
	 * method of upgrade/downgrade, so we need to emulate for partial-file
	 * locks.
	 *
	 * The OpenAFS client only gets a server lock for a full-file lock and
	 * keeps partial-file locks local.  Allow this behaviour to be emulated
	 * (as the default).
	 */
	if (mode == afs_flock_mode_local ||
	    (partial && mode == afs_flock_mode_openafs)) {
		yes_server_lock = true;
		goto skip_server_lock;
	}

	spin_lock(&vyesde->lock);
	list_add_tail(&fl->fl_u.afs.link, &vyesde->pending_locks);

	ret = -ENOENT;
	if (vyesde->lock_state == AFS_VNODE_LOCK_DELETED)
		goto error_unlock;

	/* If we've already got a lock on the server then try to move to having
	 * the VFS grant the requested lock.  Note that this means that other
	 * clients may get starved out.
	 */
	_debug("try %u", vyesde->lock_state);
	if (vyesde->lock_state == AFS_VNODE_LOCK_GRANTED) {
		if (type == AFS_LOCK_READ) {
			_debug("instant readlock");
			list_move_tail(&fl->fl_u.afs.link, &vyesde->granted_locks);
			fl->fl_u.afs.state = AFS_LOCK_GRANTED;
			goto vyesde_is_locked_u;
		}

		if (vyesde->lock_type == AFS_LOCK_WRITE) {
			_debug("instant writelock");
			list_move_tail(&fl->fl_u.afs.link, &vyesde->granted_locks);
			fl->fl_u.afs.state = AFS_LOCK_GRANTED;
			goto vyesde_is_locked_u;
		}
	}

	if (vyesde->lock_state == AFS_VNODE_LOCK_NONE &&
	    !(fl->fl_flags & FL_SLEEP)) {
		ret = -EAGAIN;
		if (type == AFS_LOCK_READ) {
			if (vyesde->status.lock_count == -1)
				goto lock_is_contended; /* Write locked */
		} else {
			if (vyesde->status.lock_count != 0)
				goto lock_is_contended; /* Locked */
		}
	}

	if (vyesde->lock_state != AFS_VNODE_LOCK_NONE)
		goto need_to_wait;

try_to_lock:
	/* We don't have a lock on this vyesde and we aren't currently waiting
	 * for one either, so ask the server for a lock.
	 *
	 * Note that we need to be careful if we get interrupted by a signal
	 * after dispatching the request as we may still get the lock, even
	 * though we don't wait for the reply (it's yest too bad a problem - the
	 * lock will expire in 5 mins anyway).
	 */
	trace_afs_flock_ev(vyesde, fl, afs_flock_try_to_lock, 0);
	vyesde->lock_key = key_get(key);
	vyesde->lock_type = type;
	afs_set_lock_state(vyesde, AFS_VNODE_LOCK_SETTING);
	spin_unlock(&vyesde->lock);

	ret = afs_set_lock(vyesde, key, type); /* RPC */

	spin_lock(&vyesde->lock);
	switch (ret) {
	case -EKEYREJECTED:
	case -EKEYEXPIRED:
	case -EKEYREVOKED:
	case -EPERM:
	case -EACCES:
		fl->fl_u.afs.state = ret;
		trace_afs_flock_ev(vyesde, fl, afs_flock_fail_perm, ret);
		list_del_init(&fl->fl_u.afs.link);
		afs_next_locker(vyesde, ret);
		goto error_unlock;

	case -ENOENT:
		fl->fl_u.afs.state = ret;
		trace_afs_flock_ev(vyesde, fl, afs_flock_fail_other, ret);
		list_del_init(&fl->fl_u.afs.link);
		afs_kill_lockers_eyesent(vyesde);
		goto error_unlock;

	default:
		fl->fl_u.afs.state = ret;
		trace_afs_flock_ev(vyesde, fl, afs_flock_fail_other, ret);
		list_del_init(&fl->fl_u.afs.link);
		afs_next_locker(vyesde, 0);
		goto error_unlock;

	case -EWOULDBLOCK:
		/* The server doesn't have a lock-waiting queue, so the client
		 * will have to retry.  The server will break the outstanding
		 * callbacks on a file when a lock is released.
		 */
		ASSERT(list_empty(&vyesde->granted_locks));
		ASSERTCMP(vyesde->pending_locks.next, ==, &fl->fl_u.afs.link);
		goto lock_is_contended;

	case 0:
		afs_set_lock_state(vyesde, AFS_VNODE_LOCK_GRANTED);
		trace_afs_flock_ev(vyesde, fl, afs_flock_acquired, type);
		afs_grant_locks(vyesde);
		goto vyesde_is_locked_u;
	}

vyesde_is_locked_u:
	spin_unlock(&vyesde->lock);
vyesde_is_locked:
	/* the lock has been granted by the server... */
	ASSERTCMP(fl->fl_u.afs.state, ==, AFS_LOCK_GRANTED);

skip_server_lock:
	/* ... but the VFS still needs to distribute access on this client. */
	trace_afs_flock_ev(vyesde, fl, afs_flock_vfs_locking, 0);
	ret = locks_lock_file_wait(file, fl);
	trace_afs_flock_ev(vyesde, fl, afs_flock_vfs_lock, ret);
	if (ret < 0)
		goto vfs_rejected_lock;

	/* Again, make sure we've got a callback on this file and, again, make
	 * sure that our view of the data version is up to date (we igyesre
	 * errors incurred here and deal with the consequences elsewhere).
	 */
	afs_validate(vyesde, key);
	_leave(" = 0");
	return 0;

lock_is_contended:
	if (!(fl->fl_flags & FL_SLEEP)) {
		list_del_init(&fl->fl_u.afs.link);
		afs_next_locker(vyesde, 0);
		ret = -EAGAIN;
		goto error_unlock;
	}

	afs_set_lock_state(vyesde, AFS_VNODE_LOCK_WAITING_FOR_CB);
	trace_afs_flock_ev(vyesde, fl, afs_flock_would_block, ret);
	queue_delayed_work(afs_lock_manager, &vyesde->lock_work, HZ * 5);

need_to_wait:
	/* We're going to have to wait.  Either this client doesn't have a lock
	 * on the server yet and we need to wait for a callback to occur, or
	 * the client does have a lock on the server, but it's shared and we
	 * need an exclusive lock.
	 */
	spin_unlock(&vyesde->lock);

	trace_afs_flock_ev(vyesde, fl, afs_flock_waiting, 0);
	ret = wait_event_interruptible(fl->fl_wait,
				       fl->fl_u.afs.state != AFS_LOCK_PENDING);
	trace_afs_flock_ev(vyesde, fl, afs_flock_waited, ret);

	if (fl->fl_u.afs.state >= 0 && fl->fl_u.afs.state != AFS_LOCK_GRANTED) {
		spin_lock(&vyesde->lock);

		switch (fl->fl_u.afs.state) {
		case AFS_LOCK_YOUR_TRY:
			fl->fl_u.afs.state = AFS_LOCK_PENDING;
			goto try_to_lock;
		case AFS_LOCK_PENDING:
			if (ret > 0) {
				/* We need to retry the lock.  We may yest be
				 * yestified by the server if it just expired
				 * rather than being released.
				 */
				ASSERTCMP(vyesde->lock_state, ==, AFS_VNODE_LOCK_WAITING_FOR_CB);
				afs_set_lock_state(vyesde, AFS_VNODE_LOCK_SETTING);
				fl->fl_u.afs.state = AFS_LOCK_PENDING;
				goto try_to_lock;
			}
			goto error_unlock;
		case AFS_LOCK_GRANTED:
		default:
			break;
		}

		spin_unlock(&vyesde->lock);
	}

	if (fl->fl_u.afs.state == AFS_LOCK_GRANTED)
		goto vyesde_is_locked;
	ret = fl->fl_u.afs.state;
	goto error;

vfs_rejected_lock:
	/* The VFS rejected the lock we just obtained, so we have to discard
	 * what we just got.  We defer this to the lock manager work item to
	 * deal with.
	 */
	_debug("vfs refused %d", ret);
	if (yes_server_lock)
		goto error;
	spin_lock(&vyesde->lock);
	list_del_init(&fl->fl_u.afs.link);
	afs_defer_unlock(vyesde);

error_unlock:
	spin_unlock(&vyesde->lock);
error:
	_leave(" = %d", ret);
	return ret;
}

/*
 * unlock on a file on the server
 */
static int afs_do_unlk(struct file *file, struct file_lock *fl)
{
	struct afs_vyesde *vyesde = AFS_FS_I(locks_iyesde(file));
	int ret;

	_enter("{%llx:%llu},%u", vyesde->fid.vid, vyesde->fid.vyesde, fl->fl_type);

	trace_afs_flock_op(vyesde, fl, afs_flock_op_unlock);

	/* Flush all pending writes before doing anything with locks. */
	vfs_fsync(file, 0);

	ret = locks_lock_file_wait(file, fl);
	_leave(" = %d [%u]", ret, vyesde->lock_state);
	return ret;
}

/*
 * return information about a lock we currently hold, if indeed we hold one
 */
static int afs_do_getlk(struct file *file, struct file_lock *fl)
{
	struct afs_vyesde *vyesde = AFS_FS_I(locks_iyesde(file));
	struct key *key = afs_file_key(file);
	int ret, lock_count;

	_enter("");

	if (vyesde->lock_state == AFS_VNODE_LOCK_DELETED)
		return -ENOENT;

	fl->fl_type = F_UNLCK;

	/* check local lock records first */
	posix_test_lock(file, fl);
	if (fl->fl_type == F_UNLCK) {
		/* yes local locks; consult the server */
		ret = afs_fetch_status(vyesde, key, false, NULL);
		if (ret < 0)
			goto error;

		lock_count = READ_ONCE(vyesde->status.lock_count);
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
	struct afs_vyesde *vyesde = AFS_FS_I(locks_iyesde(file));
	enum afs_flock_operation op;
	int ret;

	_enter("{%llx:%llu},%d,{t=%x,fl=%x,r=%Ld:%Ld}",
	       vyesde->fid.vid, vyesde->fid.vyesde, cmd,
	       fl->fl_type, fl->fl_flags,
	       (long long) fl->fl_start, (long long) fl->fl_end);

	/* AFS doesn't support mandatory locks */
	if (__mandatory_lock(&vyesde->vfs_iyesde) && fl->fl_type != F_UNLCK)
		return -ENOLCK;

	if (IS_GETLK(cmd))
		return afs_do_getlk(file, fl);

	fl->fl_u.afs.debug_id = atomic_inc_return(&afs_file_lock_debug_id);
	trace_afs_flock_op(vyesde, fl, afs_flock_op_lock);

	if (fl->fl_type == F_UNLCK)
		ret = afs_do_unlk(file, fl);
	else
		ret = afs_do_setlk(file, fl);

	switch (ret) {
	case 0:		op = afs_flock_op_return_ok; break;
	case -EAGAIN:	op = afs_flock_op_return_eagain; break;
	case -EDEADLK:	op = afs_flock_op_return_edeadlk; break;
	default:	op = afs_flock_op_return_error; break;
	}
	trace_afs_flock_op(vyesde, fl, op);
	return ret;
}

/*
 * manage FLOCK locks on a file
 */
int afs_flock(struct file *file, int cmd, struct file_lock *fl)
{
	struct afs_vyesde *vyesde = AFS_FS_I(locks_iyesde(file));
	enum afs_flock_operation op;
	int ret;

	_enter("{%llx:%llu},%d,{t=%x,fl=%x}",
	       vyesde->fid.vid, vyesde->fid.vyesde, cmd,
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

	fl->fl_u.afs.debug_id = atomic_inc_return(&afs_file_lock_debug_id);
	trace_afs_flock_op(vyesde, fl, afs_flock_op_flock);

	/* we're simulating flock() locks using posix locks on the server */
	if (fl->fl_type == F_UNLCK)
		ret = afs_do_unlk(file, fl);
	else
		ret = afs_do_setlk(file, fl);

	switch (ret) {
	case 0:		op = afs_flock_op_return_ok; break;
	case -EAGAIN:	op = afs_flock_op_return_eagain; break;
	case -EDEADLK:	op = afs_flock_op_return_edeadlk; break;
	default:	op = afs_flock_op_return_error; break;
	}
	trace_afs_flock_op(vyesde, fl, op);
	return ret;
}

/*
 * the POSIX lock management core VFS code copies the lock record and adds the
 * copy into its own list, so we need to add that copy to the vyesde's lock
 * queue in the same place as the original (which will be deleted shortly
 * after)
 */
static void afs_fl_copy_lock(struct file_lock *new, struct file_lock *fl)
{
	struct afs_vyesde *vyesde = AFS_FS_I(locks_iyesde(fl->fl_file));

	_enter("");

	new->fl_u.afs.debug_id = atomic_inc_return(&afs_file_lock_debug_id);

	spin_lock(&vyesde->lock);
	trace_afs_flock_op(vyesde, new, afs_flock_op_copy_lock);
	list_add(&new->fl_u.afs.link, &fl->fl_u.afs.link);
	spin_unlock(&vyesde->lock);
}

/*
 * need to remove this lock from the vyesde queue when it's removed from the
 * VFS's list
 */
static void afs_fl_release_private(struct file_lock *fl)
{
	struct afs_vyesde *vyesde = AFS_FS_I(locks_iyesde(fl->fl_file));

	_enter("");

	spin_lock(&vyesde->lock);

	trace_afs_flock_op(vyesde, fl, afs_flock_op_release_lock);
	list_del_init(&fl->fl_u.afs.link);
	if (list_empty(&vyesde->granted_locks))
		afs_defer_unlock(vyesde);

	_debug("state %u for %p", vyesde->lock_state, vyesde);
	spin_unlock(&vyesde->lock);
}
