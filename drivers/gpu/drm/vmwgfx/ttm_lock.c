/* SPDX-License-Identifier: GPL-2.0 OR MIT */
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

#include <linux/atomic.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>
#include "ttm_lock.h"
#include "ttm_object.h"

#define TTM_WRITE_LOCK_PENDING    (1 << 0)
#define TTM_VT_LOCK_PENDING       (1 << 1)
#define TTM_SUSPEND_LOCK_PENDING  (1 << 2)
#define TTM_VT_LOCK               (1 << 3)
#define TTM_SUSPEND_LOCK          (1 << 4)

void ttm_lock_init(struct ttm_lock *lock)
{
	spin_lock_init(&lock->lock);
	init_waitqueue_head(&lock->queue);
	lock->rw = 0;
	lock->flags = 0;
}

void ttm_read_unlock(struct ttm_lock *lock)
{
	spin_lock(&lock->lock);
	if (--lock->rw == 0)
		wake_up_all(&lock->queue);
	spin_unlock(&lock->lock);
}

static bool __ttm_read_lock(struct ttm_lock *lock)
{
	bool locked = false;

	spin_lock(&lock->lock);
	if (lock->rw >= 0 && lock->flags == 0) {
		++lock->rw;
		locked = true;
	}
	spin_unlock(&lock->lock);
	return locked;
}

int ttm_read_lock(struct ttm_lock *lock, bool interruptible)
{
	int ret = 0;

	if (interruptible)
		ret = wait_event_interruptible(lock->queue,
					       __ttm_read_lock(lock));
	else
		wait_event(lock->queue, __ttm_read_lock(lock));
	return ret;
}

static bool __ttm_read_trylock(struct ttm_lock *lock, bool *locked)
{
	bool block = true;

	*locked = false;

	spin_lock(&lock->lock);
	if (lock->rw >= 0 && lock->flags == 0) {
		++lock->rw;
		block = false;
		*locked = true;
	} else if (lock->flags == 0) {
		block = false;
	}
	spin_unlock(&lock->lock);

	return !block;
}

int ttm_read_trylock(struct ttm_lock *lock, bool interruptible)
{
	int ret = 0;
	bool locked;

	if (interruptible)
		ret = wait_event_interruptible
			(lock->queue, __ttm_read_trylock(lock, &locked));
	else
		wait_event(lock->queue, __ttm_read_trylock(lock, &locked));

	if (unlikely(ret != 0)) {
		BUG_ON(locked);
		return ret;
	}

	return (locked) ? 0 : -EBUSY;
}

void ttm_write_unlock(struct ttm_lock *lock)
{
	spin_lock(&lock->lock);
	lock->rw = 0;
	wake_up_all(&lock->queue);
	spin_unlock(&lock->lock);
}

static bool __ttm_write_lock(struct ttm_lock *lock)
{
	bool locked = false;

	spin_lock(&lock->lock);
	if (lock->rw == 0 && ((lock->flags & ~TTM_WRITE_LOCK_PENDING) == 0)) {
		lock->rw = -1;
		lock->flags &= ~TTM_WRITE_LOCK_PENDING;
		locked = true;
	} else {
		lock->flags |= TTM_WRITE_LOCK_PENDING;
	}
	spin_unlock(&lock->lock);
	return locked;
}

int ttm_write_lock(struct ttm_lock *lock, bool interruptible)
{
	int ret = 0;

	if (interruptible) {
		ret = wait_event_interruptible(lock->queue,
					       __ttm_write_lock(lock));
		if (unlikely(ret != 0)) {
			spin_lock(&lock->lock);
			lock->flags &= ~TTM_WRITE_LOCK_PENDING;
			wake_up_all(&lock->queue);
			spin_unlock(&lock->lock);
		}
	} else
		wait_event(lock->queue, __ttm_write_lock(lock));

	return ret;
}

void ttm_suspend_unlock(struct ttm_lock *lock)
{
	spin_lock(&lock->lock);
	lock->flags &= ~TTM_SUSPEND_LOCK;
	wake_up_all(&lock->queue);
	spin_unlock(&lock->lock);
}

static bool __ttm_suspend_lock(struct ttm_lock *lock)
{
	bool locked = false;

	spin_lock(&lock->lock);
	if (lock->rw == 0) {
		lock->flags &= ~TTM_SUSPEND_LOCK_PENDING;
		lock->flags |= TTM_SUSPEND_LOCK;
		locked = true;
	} else {
		lock->flags |= TTM_SUSPEND_LOCK_PENDING;
	}
	spin_unlock(&lock->lock);
	return locked;
}

void ttm_suspend_lock(struct ttm_lock *lock)
{
	wait_event(lock->queue, __ttm_suspend_lock(lock));
}
