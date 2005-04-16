/* lock.c -- IOCTLs for locking -*- linux-c -*-
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
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */


/* Gamma-specific code extracted from drm_lock.h:
 */
static int DRM(flush_queue)(drm_device_t *dev, int context)
{
	DECLARE_WAITQUEUE(entry, current);
	int		  ret	= 0;
	drm_queue_t	  *q	= dev->queuelist[context];

	DRM_DEBUG("\n");

	atomic_inc(&q->use_count);
	if (atomic_read(&q->use_count) > 1) {
		atomic_inc(&q->block_write);
		add_wait_queue(&q->flush_queue, &entry);
		atomic_inc(&q->block_count);
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
			if (!DRM_BUFCOUNT(&q->waitlist)) break;
			schedule();
			if (signal_pending(current)) {
				ret = -EINTR; /* Can't restart */
				break;
			}
		}
		atomic_dec(&q->block_count);
		current->state = TASK_RUNNING;
		remove_wait_queue(&q->flush_queue, &entry);
	}
	atomic_dec(&q->use_count);

				/* NOTE: block_write is still incremented!
				   Use drm_flush_unlock_queue to decrement. */
	return ret;
}

static int DRM(flush_unblock_queue)(drm_device_t *dev, int context)
{
	drm_queue_t	  *q	= dev->queuelist[context];

	DRM_DEBUG("\n");

	atomic_inc(&q->use_count);
	if (atomic_read(&q->use_count) > 1) {
		if (atomic_read(&q->block_write)) {
			atomic_dec(&q->block_write);
			wake_up_interruptible(&q->write_queue);
		}
	}
	atomic_dec(&q->use_count);
	return 0;
}

int DRM(flush_block_and_flush)(drm_device_t *dev, int context,
			       drm_lock_flags_t flags)
{
	int ret = 0;
	int i;

	DRM_DEBUG("\n");

	if (flags & _DRM_LOCK_FLUSH) {
		ret = DRM(flush_queue)(dev, DRM_KERNEL_CONTEXT);
		if (!ret) ret = DRM(flush_queue)(dev, context);
	}
	if (flags & _DRM_LOCK_FLUSH_ALL) {
		for (i = 0; !ret && i < dev->queue_count; i++) {
			ret = DRM(flush_queue)(dev, i);
		}
	}
	return ret;
}

int DRM(flush_unblock)(drm_device_t *dev, int context, drm_lock_flags_t flags)
{
	int ret = 0;
	int i;

	DRM_DEBUG("\n");

	if (flags & _DRM_LOCK_FLUSH) {
		ret = DRM(flush_unblock_queue)(dev, DRM_KERNEL_CONTEXT);
		if (!ret) ret = DRM(flush_unblock_queue)(dev, context);
	}
	if (flags & _DRM_LOCK_FLUSH_ALL) {
		for (i = 0; !ret && i < dev->queue_count; i++) {
			ret = DRM(flush_unblock_queue)(dev, i);
		}
	}

	return ret;
}

int DRM(finish)(struct inode *inode, struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	drm_file_t	  *priv	  = filp->private_data;
	drm_device_t	  *dev	  = priv->dev;
	int		  ret	  = 0;
	drm_lock_t	  lock;

	DRM_DEBUG("\n");

	if (copy_from_user(&lock, (drm_lock_t __user *)arg, sizeof(lock)))
		return -EFAULT;
	ret = DRM(flush_block_and_flush)(dev, lock.context, lock.flags);
	DRM(flush_unblock)(dev, lock.context, lock.flags);
	return ret;
}
