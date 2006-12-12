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

#include "drmP.h"

static int drm_lock_transfer(drm_device_t * dev,
			     __volatile__ unsigned int *lock,
			     unsigned int context);
static int drm_notifier(void *priv);

/**
 * Lock ioctl.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Add the current task to the lock wait queue, and attempt to take to lock.
 */
int drm_lock(struct inode *inode, struct file *filp,
	     unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	DECLARE_WAITQUEUE(entry, current);
	drm_lock_t lock;
	int ret = 0;

	++priv->lock_count;

	if (copy_from_user(&lock, (drm_lock_t __user *) arg, sizeof(lock)))
		return -EFAULT;

	if (lock.context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  current->pid, lock.context);
		return -EINVAL;
	}

	DRM_DEBUG("%d (pid %d) requests lock (0x%08x), flags = 0x%08x\n",
		  lock.context, current->pid,
		  dev->lock.hw_lock->lock, lock.flags);

	if (drm_core_check_feature(dev, DRIVER_DMA_QUEUE))
		if (lock.context < 0)
			return -EINVAL;

	add_wait_queue(&dev->lock.lock_queue, &entry);
	for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		if (!dev->lock.hw_lock) {
			/* Device has been unregistered */
			ret = -EINTR;
			break;
		}
		if (drm_lock_take(&dev->lock.hw_lock->lock, lock.context)) {
			dev->lock.filp = filp;
			dev->lock.lock_time = jiffies;
			atomic_inc(&dev->counts[_DRM_STAT_LOCKS]);
			break;	/* Got lock */
		}

		/* Contention */
		schedule();
		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&dev->lock.lock_queue, &entry);

	DRM_DEBUG("%d %s\n", lock.context, ret ? "interrupted" : "has lock");
	if (ret)
		return ret;

	sigemptyset(&dev->sigmask);
	sigaddset(&dev->sigmask, SIGSTOP);
	sigaddset(&dev->sigmask, SIGTSTP);
	sigaddset(&dev->sigmask, SIGTTIN);
	sigaddset(&dev->sigmask, SIGTTOU);
	dev->sigdata.context = lock.context;
	dev->sigdata.lock = dev->lock.hw_lock;
	block_all_signals(drm_notifier, &dev->sigdata, &dev->sigmask);

	if (dev->driver->dma_ready && (lock.flags & _DRM_LOCK_READY))
		dev->driver->dma_ready(dev);

	if (dev->driver->dma_quiescent && (lock.flags & _DRM_LOCK_QUIESCENT)) {
		if (dev->driver->dma_quiescent(dev)) {
			DRM_DEBUG("%d waiting for DMA quiescent\n", lock.context);
			return DRM_ERR(EBUSY);
		}
	}

	/* dev->driver->kernel_context_switch isn't used by any of the x86
	 *  drivers but is used by the Sparc driver.
	 */
	if (dev->driver->kernel_context_switch &&
	    dev->last_context != lock.context) {
		dev->driver->kernel_context_switch(dev, dev->last_context,
						   lock.context);
	}
	return 0;
}

/**
 * Unlock ioctl.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Transfer and free the lock.
 */
int drm_unlock(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_lock_t lock;
	unsigned long irqflags;

	if (copy_from_user(&lock, (drm_lock_t __user *) arg, sizeof(lock)))
		return -EFAULT;

	if (lock.context == DRM_KERNEL_CONTEXT) {
		DRM_ERROR("Process %d using kernel context %d\n",
			  current->pid, lock.context);
		return -EINVAL;
	}

	spin_lock_irqsave(&dev->tasklet_lock, irqflags);

	if (dev->locked_tasklet_func) {
		dev->locked_tasklet_func(dev);

		dev->locked_tasklet_func = NULL;
	}

	spin_unlock_irqrestore(&dev->tasklet_lock, irqflags);

	atomic_inc(&dev->counts[_DRM_STAT_UNLOCKS]);

	/* kernel_context_switch isn't used by any of the x86 drm
	 * modules but is required by the Sparc driver.
	 */
	if (dev->driver->kernel_context_switch_unlock)
		dev->driver->kernel_context_switch_unlock(dev, &lock);
	else {
		drm_lock_transfer(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT);

		if (drm_lock_free(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("\n");
		}
	}

	unblock_all_signals();
	return 0;
}

/**
 * Take the heavyweight lock.
 *
 * \param lock lock pointer.
 * \param context locking context.
 * \return one if the lock is held, or zero otherwise.
 *
 * Attempt to mark the lock as held by the given context, via the \p cmpxchg instruction.
 */
int drm_lock_take(__volatile__ unsigned int *lock, unsigned int context)
{
	unsigned int old, new, prev;

	do {
		old = *lock;
		if (old & _DRM_LOCK_HELD)
			new = old | _DRM_LOCK_CONT;
		else
			new = context | _DRM_LOCK_HELD;
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	if (_DRM_LOCKING_CONTEXT(old) == context) {
		if (old & _DRM_LOCK_HELD) {
			if (context != DRM_KERNEL_CONTEXT) {
				DRM_ERROR("%d holds heavyweight lock\n",
					  context);
			}
			return 0;
		}
	}
	if (new == (context | _DRM_LOCK_HELD)) {
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
static int drm_lock_transfer(drm_device_t * dev,
			     __volatile__ unsigned int *lock,
			     unsigned int context)
{
	unsigned int old, new, prev;

	dev->lock.filp = NULL;
	do {
		old = *lock;
		new = context | _DRM_LOCK_HELD;
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	return 1;
}

/**
 * Free lock.
 *
 * \param dev DRM device.
 * \param lock lock.
 * \param context context.
 *
 * Resets the lock file pointer.
 * Marks the lock as not held, via the \p cmpxchg instruction. Wakes any task
 * waiting on the lock queue.
 */
int drm_lock_free(drm_device_t * dev,
		  __volatile__ unsigned int *lock, unsigned int context)
{
	unsigned int old, new, prev;

	dev->lock.filp = NULL;
	do {
		old = *lock;
		new = 0;
		prev = cmpxchg(lock, old, new);
	} while (prev != old);
	if (_DRM_LOCK_IS_HELD(old) && _DRM_LOCKING_CONTEXT(old) != context) {
		DRM_ERROR("%d freed heavyweight lock held by %d\n",
			  context, _DRM_LOCKING_CONTEXT(old));
		return 1;
	}
	wake_up_interruptible(&dev->lock.lock_queue);
	return 0;
}

/**
 * If we get here, it means that the process has called DRM_IOCTL_LOCK
 * without calling DRM_IOCTL_UNLOCK.
 *
 * If the lock is not held, then let the signal proceed as usual.  If the lock
 * is held, then set the contended flag and keep the signal blocked.
 *
 * \param priv pointer to a drm_sigdata structure.
 * \return one if the signal should be delivered normally, or zero if the
 * signal should be blocked.
 */
static int drm_notifier(void *priv)
{
	drm_sigdata_t *s = (drm_sigdata_t *) priv;
	unsigned int old, new, prev;

	/* Allow signal delivery if lock isn't held */
	if (!s->lock || !_DRM_LOCK_IS_HELD(s->lock->lock)
	    || _DRM_LOCKING_CONTEXT(s->lock->lock) != s->context)
		return 1;

	/* Otherwise, set flag to force call to
	   drmUnlock */
	do {
		old = s->lock->lock;
		new = old | _DRM_LOCK_CONT;
		prev = cmpxchg(&s->lock->lock, old, new);
	} while (prev != old);
	return 0;
}
