/**
 * \file drm_lock.c
 * IOCTLs for locking
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Tue Feb  2 08:37:54 1999 by faith@valinux.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/export.h>
#include <linux/sched/signal.h>

#include <drm/drm.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_print.h>

#include "drm_internal.h"
#include "drm_legacy.h"

static int drm_lock_take(struct drm_lock_data *lock_data, unsigned int context);

/**
 * Take the heavyweight lock.
 *
 * \param lock lock pointer.
 * \param context locking context.
 * \return one if the lock is held, or zero otherwise.
 *
 * Attempt to mark the lock as held by the given context, via the \p cmpxchg instruction.
 */
static
int drm_lock_take(struct drm_lock_data *lock_data,
		  unsigned int context)
{
	unsigned int old, new, prev;
	volatile unsigned int *lock = &lock_data->hw_lock->lock;

	spin_lock_bh(&lock_data->spinlock);
	do {
		old = *lock;
		if (old & _DRM_LOCK_HELD)
			new = old | _DRM_LOCK_CONT;
		else {
			new = context | _DRM_LOCK_HELD |
				((lock_data->user_waiters + lock_data->kernel_waiters > 1) ?
				 _DRM_LOCK_CONT : 0);
		}
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	spin_unlock_bh(&lock_data->spinlock);

	if (_DRM_LOCKING_CONTEXT(old) == context) {
		if (old & _DRM_LOCK_HELD) {
			if (context != DRM_KERNEL_CONTEXT) {
				DRM_ERROR("%d holds heavyweight lock\n",
					  context);
			}
			return 0;
		}
	}

	if ((_DRM_LOCKING_CONTEXT(new)) == context && (new & _DRM_LOCK_HELD)) {
		/* Have lock */
		return 1;
	}
	return 0;
}

/**
 * This takes a lock forcibly and hands it to context.	Should ONLY be used
 * inside *_unlock to give lock to kernel before calling *_dma_schedule.
 *
 * \param dev DRM device.
 * \param lock lock pointer.
 * \param context locking context.
 * \return always one.
 *
 * Resets the lock file pointer.
 * Marks the lock as held by the given context, via the \p cmpxchg instruction.
 */
static int drm_lock_transfer(struct drm_lock_data *lock_data,
			     unsigned int context)
{
	unsigned int old, new, prev;
	volatile unsigned int *lock = &lock_data->hw_lock->lock;

	lock_data->file_priv = NULL;
	do {
		old = *lock;
		new = context | _DRM_LOCK_HELD;
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	return 1;
}

static int drm_legacy_lock_free(struct drm_lock_data *lock_data,
				unsigned int context)
{
	unsigned int old, new, prev;
	volatile unsigned int *lock = &lock_data->hw_lock->lock;

	spin_lock_bh(&lock_data->spinlock);
	if (lock_data->kernel_waiters != 0) {
		drm_lock_transfer(lock_data, 0);
		lock_data->idle_has_lock = 1;
		spin_unlock_bh(&lock_data->spinlock);
		return 1;
	}
	spin_unlock_bh(&lock_data->spinlock);

	do {
		old = *lock;
		new = _DRM_LOCKING_CONTEXT(old);
		prev = cmpxchg(lock, old, new);
	} while (prev != old);

	if (_DRM_LOCK_IS_HELD(old) && _DRM_LOCKING_CONTEXT(old) != context) {
		DRM_ERROR("%d freed heavyweight lock held by %d\n",
			  context, _DRM_LOCKING_CONTEXT(old));
		return 1;
	}
	wake_up_interruptible(&lock_data->lock_queue);
	return 0;
}

/**
 * Lock ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Add the current task to the lock wait queue, and attempt to take to lock.
 */
int drm_legacy_lock(struct drm_device *dev, void *data,
		    struct drm_file *file_priv)
{
	DECLARE_WAITQUEUE(entry, current);
	struct drm_lock *lock = data;
	struct drm_master *master = file_priv->master;
	int ret = 0;

	if (!drm_core_check_feature(dev, DRIVER_LEGACY))
		return -EOPNOTSUPP;

	++file_priv->lock_count;

	if (lock->context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  task_pid_nr(current), lock->context);
		return -EINVAL;
	}

	DRM_DEBUG("%d (pid %d) requests lock (0x%08x), flags = 0x%08x\n",
		  lock->context, task_pid_nr(current),
		  master->lock.hw_lock ? master->lock.hw_lock->lock : -1,
		  lock->flags);

	add_wait_queue(&master->lock.lock_queue, &entry);
	spin_lock_bh(&master->lock.spinlock);
	master->lock.user_waiters++;
	spin_unlock_bh(&master->lock.spinlock);

	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		if (!master->lock.hw_lock) {
			/* Device has been unregistered */
			send_sig(SIGTERM, current, 0);
			ret = -EINTR;
			break;
		}
		if (drm_lock_take(&master->lock, lock->context)) {
			master->lock.file_priv = file_priv;
			master->lock.lock_time = jiffies;
			break;	/* Got lock */
		}

		/* Contention */
		mutex_unlock(&drm_global_mutex);
		schedule();
		mutex_lock(&drm_global_mutex);
		if (signal_pending(current)) {
			ret = -EINTR;
			break;
		}
	}
	spin_lock_bh(&master->lock.spinlock);
	master->lock.user_waiters--;
	spin_unlock_bh(&master->lock.spinlock);
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&master->lock.lock_queue, &entry);

	DRM_DEBUG("%d %s\n", lock->context,
		  ret ? "interrupted" : "has lock");
	if (ret) return ret;

	/* don't set the block all signals on the master process for now 
	 * really probably not the correct answer but lets us debug xkb
 	 * xserver for now */
	if (!drm_is_current_master(file_priv)) {
		dev->sigdata.context = lock->context;
		dev->sigdata.lock = master->lock.hw_lock;
	}

	if (dev->driver->dma_quiescent && (lock->flags & _DRM_LOCK_QUIESCENT))
	{
		if (dev->driver->dma_quiescent(dev)) {
			DRM_DEBUG("%d waiting for DMA quiescent\n",
				  lock->context);
			return -EBUSY;
		}
	}

	return 0;
}

/**
 * Unlock ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Transfer and free the lock.
 */
int drm_legacy_unlock(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_lock *lock = data;
	struct drm_master *master = file_priv->master;

	if (!drm_core_check_feature(dev, DRIVER_LEGACY))
		return -EOPNOTSUPP;

	if (lock->context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  task_pid_nr(current), lock->context);
		return -EINVAL;
	}

	if (drm_legacy_lock_free(&master->lock, lock->context)) {
		/* FIXME: Should really bail out here. */
	}

	return 0;
}

/**
 * This function returns immediately and takes the hw lock
 * with the kernel context if it is free, otherwise it gets the highest priority when and if
 * it is eventually released.
 *
 * This guarantees that the kernel will _eventually_ have the lock _unless_ it is held
 * by a blocked process. (In the latter case an explicit wait for the hardware lock would cause
 * a deadlock, which is why the "idlelock" was invented).
 *
 * This should be sufficient to wait for GPU idle without
 * having to worry about starvation.
 */

void drm_legacy_idlelock_take(struct drm_lock_data *lock_data)
{
	int ret;

	spin_lock_bh(&lock_data->spinlock);
	lock_data->kernel_waiters++;
	if (!lock_data->idle_has_lock) {

		spin_unlock_bh(&lock_data->spinlock);
		ret = drm_lock_take(lock_data, DRM_KERNEL_CONTEXT);
		spin_lock_bh(&lock_data->spinlock);

		if (ret == 1)
			lock_data->idle_has_lock = 1;
	}
	spin_unlock_bh(&lock_data->spinlock);
}
EXPORT_SYMBOL(drm_legacy_idlelock_take);

void drm_legacy_idlelock_release(struct drm_lock_data *lock_data)
{
	unsigned int old, prev;
	volatile unsigned int *lock = &lock_data->hw_lock->lock;

	spin_lock_bh(&lock_data->spinlock);
	if (--lock_data->kernel_waiters == 0) {
		if (lock_data->idle_has_lock) {
			do {
				old = *lock;
				prev = cmpxchg(lock, old, DRM_KERNEL_CONTEXT);
			} while (prev != old);
			wake_up_interruptible(&lock_data->lock_queue);
			lock_data->idle_has_lock = 0;
		}
	}
	spin_unlock_bh(&lock_data->spinlock);
}
EXPORT_SYMBOL(drm_legacy_idlelock_release);

static int drm_legacy_i_have_hw_lock(struct drm_device *dev,
				     struct drm_file *file_priv)
{
	struct drm_master *master = file_priv->master;
	return (file_priv->lock_count && master->lock.hw_lock &&
		_DRM_LOCK_IS_HELD(master->lock.hw_lock->lock) &&
		master->lock.file_priv == file_priv);
}

void drm_legacy_lock_release(struct drm_device *dev, struct file *filp)
{
	struct drm_file *file_priv = filp->private_data;

	/* if the master has gone away we can't do anything with the lock */
	if (!dev->master)
		return;

	if (drm_legacy_i_have_hw_lock(dev, file_priv)) {
		DRM_DEBUG("File %p released, freeing lock for context %d\n",
			  filp, _DRM_LOCKING_CONTEXT(file_priv->master->lock.hw_lock->lock));
		drm_legacy_lock_free(&file_priv->master->lock,
				     _DRM_LOCKING_CONTEXT(file_priv->master->lock.hw_lock->lock));
	}
}

void drm_legacy_lock_master_cleanup(struct drm_device *dev, struct drm_master *master)
{
	if (!drm_core_check_feature(dev, DRIVER_LEGACY))
		return;

	/*
	 * Since the master is disappearing, so is the
	 * possibility to lock.
	 */	mutex_lock(&dev->struct_mutex);
	if (master->lock.hw_lock) {
		if (dev->sigdata.lock == master->lock.hw_lock)
			dev->sigdata.lock = NULL;
		master->lock.hw_lock = NULL;
		master->lock.file_priv = NULL;
		wake_up_interruptible_all(&master->lock.lock_queue);
	}
	mutex_unlock(&dev->struct_mutex);
}
