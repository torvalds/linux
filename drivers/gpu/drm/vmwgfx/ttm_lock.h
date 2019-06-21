/**************************************************************************
 *
 * Copyright (c) 2007-2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */

/** @file ttm_lock.h
 * This file implements a simple replacement for the buffer manager use
 * of the DRM heavyweight hardware lock.
 * The lock is a read-write lock. Taking it in read mode and write mode
 * is relatively fast, and intended for in-kernel use only.
 *
 * The vt mode is used only when there is a need to block all
 * user-space processes from validating buffers.
 * It's allowed to leave kernel space with the vt lock held.
 * If a user-space process dies while having the vt-lock,
 * it will be released during the file descriptor release. The vt lock
 * excludes write lock and read lock.
 *
 * The suspend mode is used to lock out all TTM users when preparing for
 * and executing suspend operations.
 *
 */

#ifndef _TTM_LOCK_H_
#define _TTM_LOCK_H_

#include <linux/wait.h>
#include <linux/atomic.h>

#include "ttm_object.h"

/**
 * struct ttm_lock
 *
 * @base: ttm base object used solely to release the lock if the client
 * holding the lock dies.
 * @queue: Queue for processes waiting for lock change-of-status.
 * @lock: Spinlock protecting some lock members.
 * @rw: Read-write lock counter. Protected by @lock.
 * @flags: Lock state. Protected by @lock.
 */

struct ttm_lock {
	struct ttm_base_object base;
	wait_queue_head_t queue;
	spinlock_t lock;
	int32_t rw;
	uint32_t flags;
};


/**
 * ttm_lock_init
 *
 * @lock: Pointer to a struct ttm_lock
 * Initializes the lock.
 */
extern void ttm_lock_init(struct ttm_lock *lock);

/**
 * ttm_read_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Releases a read lock.
 */
extern void ttm_read_unlock(struct ttm_lock *lock);

/**
 * ttm_read_lock
 *
 * @lock: Pointer to a struct ttm_lock
 * @interruptible: Interruptible sleeping while waiting for a lock.
 *
 * Takes the lock in read mode.
 * Returns:
 * -ERESTARTSYS If interrupted by a signal and interruptible is true.
 */
extern int ttm_read_lock(struct ttm_lock *lock, bool interruptible);

/**
 * ttm_read_trylock
 *
 * @lock: Pointer to a struct ttm_lock
 * @interruptible: Interruptible sleeping while waiting for a lock.
 *
 * Tries to take the lock in read mode. If the lock is already held
 * in write mode, the function will return -EBUSY. If the lock is held
 * in vt or suspend mode, the function will sleep until these modes
 * are unlocked.
 *
 * Returns:
 * -EBUSY The lock was already held in write mode.
 * -ERESTARTSYS If interrupted by a signal and interruptible is true.
 */
extern int ttm_read_trylock(struct ttm_lock *lock, bool interruptible);

/**
 * ttm_write_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Releases a write lock.
 */
extern void ttm_write_unlock(struct ttm_lock *lock);

/**
 * ttm_write_lock
 *
 * @lock: Pointer to a struct ttm_lock
 * @interruptible: Interruptible sleeping while waiting for a lock.
 *
 * Takes the lock in write mode.
 * Returns:
 * -ERESTARTSYS If interrupted by a signal and interruptible is true.
 */
extern int ttm_write_lock(struct ttm_lock *lock, bool interruptible);

/**
 * ttm_lock_downgrade
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Downgrades a write lock to a read lock.
 */
extern void ttm_lock_downgrade(struct ttm_lock *lock);

/**
 * ttm_suspend_lock
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Takes the lock in suspend mode. Excludes read and write mode.
 */
extern void ttm_suspend_lock(struct ttm_lock *lock);

/**
 * ttm_suspend_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Releases a suspend lock
 */
extern void ttm_suspend_unlock(struct ttm_lock *lock);

/**
 * ttm_vt_lock
 *
 * @lock: Pointer to a struct ttm_lock
 * @interruptible: Interruptible sleeping while waiting for a lock.
 * @tfile: Pointer to a struct ttm_object_file to register the lock with.
 *
 * Takes the lock in vt mode.
 * Returns:
 * -ERESTARTSYS If interrupted by a signal and interruptible is true.
 * -ENOMEM: Out of memory when locking.
 */
extern int ttm_vt_lock(struct ttm_lock *lock, bool interruptible,
		       struct ttm_object_file *tfile);

/**
 * ttm_vt_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Releases a vt lock.
 * Returns:
 * -EINVAL If the lock was not held.
 */
extern int ttm_vt_unlock(struct ttm_lock *lock);

/**
 * ttm_write_unlock
 *
 * @lock: Pointer to a struct ttm_lock
 *
 * Releases a write lock.
 */
extern void ttm_write_unlock(struct ttm_lock *lock);

/**
 * ttm_write_lock
 *
 * @lock: Pointer to a struct ttm_lock
 * @interruptible: Interruptible sleeping while waiting for a lock.
 *
 * Takes the lock in write mode.
 * Returns:
 * -ERESTARTSYS If interrupted by a signal and interruptible is true.
 */
extern int ttm_write_lock(struct ttm_lock *lock, bool interruptible);

#endif
