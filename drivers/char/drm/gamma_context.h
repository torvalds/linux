/* drm_context.h -- IOCTLs for generic contexts -*- linux-c -*-
 * Created: Fri Nov 24 18:31:37 2000 by gareth@valinux.com
 *
 * Copyright 1999, 2000 Precision Insight, Inc., Cedar Park, Texas.
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
 * ChangeLog:
 *  2001-11-16	Torsten Duwe <duwe@caldera.de>
 *		added context constructor/destructor hooks,
 *		needed by SiS driver's memory management.
 */

/* ================================================================
 * Old-style context support -- only used by gamma.  
 */


/* The drm_read and drm_write_string code (especially that which manages
   the circular buffer), is based on Alessandro Rubini's LINUX DEVICE
   DRIVERS (Cambridge: O'Reilly, 1998), pages 111-113. */

ssize_t gamma_fops_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;
	int	      left;
	int	      avail;
	int	      send;
	int	      cur;

	DRM_DEBUG("%p, %p\n", dev->buf_rp, dev->buf_wp);

	while (dev->buf_rp == dev->buf_wp) {
		DRM_DEBUG("  sleeping\n");
		if (filp->f_flags & O_NONBLOCK) {
			return -EAGAIN;
		}
		interruptible_sleep_on(&dev->buf_readers);
		if (signal_pending(current)) {
			DRM_DEBUG("  interrupted\n");
			return -ERESTARTSYS;
		}
		DRM_DEBUG("  awake\n");
	}

	left  = (dev->buf_rp + DRM_BSZ - dev->buf_wp) % DRM_BSZ;
	avail = DRM_BSZ - left;
	send  = DRM_MIN(avail, count);

	while (send) {
		if (dev->buf_wp > dev->buf_rp) {
			cur = DRM_MIN(send, dev->buf_wp - dev->buf_rp);
		} else {
			cur = DRM_MIN(send, dev->buf_end - dev->buf_rp);
		}
		if (copy_to_user(buf, dev->buf_rp, cur))
			return -EFAULT;
		dev->buf_rp += cur;
		if (dev->buf_rp == dev->buf_end) dev->buf_rp = dev->buf;
		send -= cur;
	}

	wake_up_interruptible(&dev->buf_writers);
	return DRM_MIN(avail, count);
}


/* In an incredibly convoluted setup, the kernel module actually calls
 * back into the X server to perform context switches on behalf of the
 * 3d clients.
 */
int DRM(write_string)(drm_device_t *dev, const char *s)
{
	int left   = (dev->buf_rp + DRM_BSZ - dev->buf_wp) % DRM_BSZ;
	int send   = strlen(s);
	int count;

	DRM_DEBUG("%d left, %d to send (%p, %p)\n",
		  left, send, dev->buf_rp, dev->buf_wp);

	if (left == 1 || dev->buf_wp != dev->buf_rp) {
		DRM_ERROR("Buffer not empty (%d left, wp = %p, rp = %p)\n",
			  left,
			  dev->buf_wp,
			  dev->buf_rp);
	}

	while (send) {
		if (dev->buf_wp >= dev->buf_rp) {
			count = DRM_MIN(send, dev->buf_end - dev->buf_wp);
			if (count == left) --count; /* Leave a hole */
		} else {
			count = DRM_MIN(send, dev->buf_rp - dev->buf_wp - 1);
		}
		strncpy(dev->buf_wp, s, count);
		dev->buf_wp += count;
		if (dev->buf_wp == dev->buf_end) dev->buf_wp = dev->buf;
		send -= count;
	}

	if (dev->buf_async) kill_fasync(&dev->buf_async, SIGIO, POLL_IN);

	DRM_DEBUG("waking\n");
	wake_up_interruptible(&dev->buf_readers);
	return 0;
}

unsigned int gamma_fops_poll(struct file *filp, struct poll_table_struct *wait)
{
	drm_file_t   *priv = filp->private_data;
	drm_device_t *dev  = priv->dev;

	poll_wait(filp, &dev->buf_readers, wait);
	if (dev->buf_wp != dev->buf_rp) return POLLIN | POLLRDNORM;
	return 0;
}

int DRM(context_switch)(drm_device_t *dev, int old, int new)
{
	char	    buf[64];
	drm_queue_t *q;

	if (test_and_set_bit(0, &dev->context_flag)) {
		DRM_ERROR("Reentering -- FIXME\n");
		return -EBUSY;
	}

	DRM_DEBUG("Context switch from %d to %d\n", old, new);

	if (new >= dev->queue_count) {
		clear_bit(0, &dev->context_flag);
		return -EINVAL;
	}

	if (new == dev->last_context) {
		clear_bit(0, &dev->context_flag);
		return 0;
	}

	q = dev->queuelist[new];
	atomic_inc(&q->use_count);
	if (atomic_read(&q->use_count) == 1) {
		atomic_dec(&q->use_count);
		clear_bit(0, &dev->context_flag);
		return -EINVAL;
	}

	/* This causes the X server to wake up & do a bunch of hardware
	 * interaction to actually effect the context switch.
	 */
	sprintf(buf, "C %d %d\n", old, new);
	DRM(write_string)(dev, buf);

	atomic_dec(&q->use_count);

	return 0;
}

int DRM(context_switch_complete)(drm_device_t *dev, int new)
{
	drm_device_dma_t *dma = dev->dma;

	dev->last_context = new;  /* PRE/POST: This is the _only_ writer. */
	dev->last_switch  = jiffies;

	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock)) {
		DRM_ERROR("Lock isn't held after context switch\n");
	}

	if (!dma || !(dma->next_buffer && dma->next_buffer->while_locked)) {
		if (DRM(lock_free)(dev, &dev->lock.hw_lock->lock,
				  DRM_KERNEL_CONTEXT)) {
			DRM_ERROR("Cannot free lock\n");
		}
	}

	clear_bit(0, &dev->context_flag);
	wake_up_interruptible(&dev->context_wait);

	return 0;
}

static int DRM(init_queue)(drm_device_t *dev, drm_queue_t *q, drm_ctx_t *ctx)
{
	DRM_DEBUG("\n");

	if (atomic_read(&q->use_count) != 1
	    || atomic_read(&q->finalization)
	    || atomic_read(&q->block_count)) {
		DRM_ERROR("New queue is already in use: u%d f%d b%d\n",
			  atomic_read(&q->use_count),
			  atomic_read(&q->finalization),
			  atomic_read(&q->block_count));
	}

	atomic_set(&q->finalization,  0);
	atomic_set(&q->block_count,   0);
	atomic_set(&q->block_read,    0);
	atomic_set(&q->block_write,   0);
	atomic_set(&q->total_queued,  0);
	atomic_set(&q->total_flushed, 0);
	atomic_set(&q->total_locks,   0);

	init_waitqueue_head(&q->write_queue);
	init_waitqueue_head(&q->read_queue);
	init_waitqueue_head(&q->flush_queue);

	q->flags = ctx->flags;

	DRM(waitlist_create)(&q->waitlist, dev->dma->buf_count);

	return 0;
}


/* drm_alloc_queue:
PRE: 1) dev->queuelist[0..dev->queue_count] is allocated and will not
	disappear (so all deallocation must be done after IOCTLs are off)
     2) dev->queue_count < dev->queue_slots
     3) dev->queuelist[i].use_count == 0 and
	dev->queuelist[i].finalization == 0 if i not in use
POST: 1) dev->queuelist[i].use_count == 1
      2) dev->queue_count < dev->queue_slots */

static int DRM(alloc_queue)(drm_device_t *dev)
{
	int	    i;
	drm_queue_t *queue;
	int	    oldslots;
	int	    newslots;
				/* Check for a free queue */
	for (i = 0; i < dev->queue_count; i++) {
		atomic_inc(&dev->queuelist[i]->use_count);
		if (atomic_read(&dev->queuelist[i]->use_count) == 1
		    && !atomic_read(&dev->queuelist[i]->finalization)) {
			DRM_DEBUG("%d (free)\n", i);
			return i;
		}
		atomic_dec(&dev->queuelist[i]->use_count);
	}
				/* Allocate a new queue */
	down(&dev->struct_sem);

	queue = DRM(alloc)(sizeof(*queue), DRM_MEM_QUEUES);
	memset(queue, 0, sizeof(*queue));
	atomic_set(&queue->use_count, 1);

	++dev->queue_count;
	if (dev->queue_count >= dev->queue_slots) {
		oldslots = dev->queue_slots * sizeof(*dev->queuelist);
		if (!dev->queue_slots) dev->queue_slots = 1;
		dev->queue_slots *= 2;
		newslots = dev->queue_slots * sizeof(*dev->queuelist);

		dev->queuelist = DRM(realloc)(dev->queuelist,
					      oldslots,
					      newslots,
					      DRM_MEM_QUEUES);
		if (!dev->queuelist) {
			up(&dev->struct_sem);
			DRM_DEBUG("out of memory\n");
			return -ENOMEM;
		}
	}
	dev->queuelist[dev->queue_count-1] = queue;

	up(&dev->struct_sem);
	DRM_DEBUG("%d (new)\n", dev->queue_count - 1);
	return dev->queue_count - 1;
}

int DRM(resctx)(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	drm_ctx_res_t __user *argp = (void __user *)arg;
	drm_ctx_res_t	res;
	drm_ctx_t	ctx;
	int		i;

	DRM_DEBUG("%d\n", DRM_RESERVED_CONTEXTS);
	if (copy_from_user(&res, argp, sizeof(res)))
		return -EFAULT;
	if (res.count >= DRM_RESERVED_CONTEXTS) {
		memset(&ctx, 0, sizeof(ctx));
		for (i = 0; i < DRM_RESERVED_CONTEXTS; i++) {
			ctx.handle = i;
			if (copy_to_user(&res.contexts[i],
					 &i,
					 sizeof(i)))
				return -EFAULT;
		}
	}
	res.count = DRM_RESERVED_CONTEXTS;
	if (copy_to_user(argp, &res, sizeof(res)))
		return -EFAULT;
	return 0;
}

int DRM(addctx)(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	ctx;
	drm_ctx_t	__user *argp = (void __user *)arg;

	if (copy_from_user(&ctx, argp, sizeof(ctx)))
		return -EFAULT;
	if ((ctx.handle = DRM(alloc_queue)(dev)) == DRM_KERNEL_CONTEXT) {
				/* Init kernel's context and get a new one. */
		DRM(init_queue)(dev, dev->queuelist[ctx.handle], &ctx);
		ctx.handle = DRM(alloc_queue)(dev);
	}
	DRM(init_queue)(dev, dev->queuelist[ctx.handle], &ctx);
	DRM_DEBUG("%d\n", ctx.handle);
	if (copy_to_user(argp, &ctx, sizeof(ctx)))
		return -EFAULT;
	return 0;
}

int DRM(modctx)(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	ctx;
	drm_queue_t	*q;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;

	DRM_DEBUG("%d\n", ctx.handle);

	if (ctx.handle < 0 || ctx.handle >= dev->queue_count) return -EINVAL;
	q = dev->queuelist[ctx.handle];

	atomic_inc(&q->use_count);
	if (atomic_read(&q->use_count) == 1) {
				/* No longer in use */
		atomic_dec(&q->use_count);
		return -EINVAL;
	}

	if (DRM_BUFCOUNT(&q->waitlist)) {
		atomic_dec(&q->use_count);
		return -EBUSY;
	}

	q->flags = ctx.flags;

	atomic_dec(&q->use_count);
	return 0;
}

int DRM(getctx)(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	__user *argp = (void __user *)arg;
	drm_ctx_t	ctx;
	drm_queue_t	*q;

	if (copy_from_user(&ctx, argp, sizeof(ctx)))
		return -EFAULT;

	DRM_DEBUG("%d\n", ctx.handle);

	if (ctx.handle >= dev->queue_count) return -EINVAL;
	q = dev->queuelist[ctx.handle];

	atomic_inc(&q->use_count);
	if (atomic_read(&q->use_count) == 1) {
				/* No longer in use */
		atomic_dec(&q->use_count);
		return -EINVAL;
	}

	ctx.flags = q->flags;
	atomic_dec(&q->use_count);

	if (copy_to_user(argp, &ctx, sizeof(ctx)))
		return -EFAULT;

	return 0;
}

int DRM(switchctx)(struct inode *inode, struct file *filp,
		   unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	ctx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;
	DRM_DEBUG("%d\n", ctx.handle);
	return DRM(context_switch)(dev, dev->last_context, ctx.handle);
}

int DRM(newctx)(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	ctx;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;
	DRM_DEBUG("%d\n", ctx.handle);
	DRM(context_switch_complete)(dev, ctx.handle);

	return 0;
}

int DRM(rmctx)(struct inode *inode, struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
	drm_file_t	*priv	= filp->private_data;
	drm_device_t	*dev	= priv->dev;
	drm_ctx_t	ctx;
	drm_queue_t	*q;
	drm_buf_t	*buf;

	if (copy_from_user(&ctx, (drm_ctx_t __user *)arg, sizeof(ctx)))
		return -EFAULT;
	DRM_DEBUG("%d\n", ctx.handle);

	if (ctx.handle >= dev->queue_count) return -EINVAL;
	q = dev->queuelist[ctx.handle];

	atomic_inc(&q->use_count);
	if (atomic_read(&q->use_count) == 1) {
				/* No longer in use */
		atomic_dec(&q->use_count);
		return -EINVAL;
	}

	atomic_inc(&q->finalization); /* Mark queue in finalization state */
	atomic_sub(2, &q->use_count); /* Mark queue as unused (pending
					 finalization) */

	while (test_and_set_bit(0, &dev->interrupt_flag)) {
		schedule();
		if (signal_pending(current)) {
			clear_bit(0, &dev->interrupt_flag);
			return -EINTR;
		}
	}
				/* Remove queued buffers */
	while ((buf = DRM(waitlist_get)(&q->waitlist))) {
		DRM(free_buffer)(dev, buf);
	}
	clear_bit(0, &dev->interrupt_flag);

				/* Wakeup blocked processes */
	wake_up_interruptible(&q->read_queue);
	wake_up_interruptible(&q->write_queue);
	wake_up_interruptible(&q->flush_queue);

				/* Finalization over.  Queue is made
				   available when both use_count and
				   finalization become 0, which won't
				   happen until all the waiting processes
				   stop waiting. */
	atomic_dec(&q->finalization);
	return 0;
}

