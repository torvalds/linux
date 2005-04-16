/* drm_dma.c -- DMA IOCTL and function support -*- linux-c -*-
 * Created: Fri Mar 19 14:30:16 1999 by faith@valinux.com
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
 */


/* Gamma-specific code pulled from drm_dma.h:
 */

void DRM(clear_next_buffer)(drm_device_t *dev)
{
	drm_device_dma_t *dma = dev->dma;

	dma->next_buffer = NULL;
	if (dma->next_queue && !DRM_BUFCOUNT(&dma->next_queue->waitlist)) {
		wake_up_interruptible(&dma->next_queue->flush_queue);
	}
	dma->next_queue	 = NULL;
}

int DRM(select_queue)(drm_device_t *dev, void (*wrapper)(unsigned long))
{
	int	   i;
	int	   candidate = -1;
	int	   j	     = jiffies;

	if (!dev) {
		DRM_ERROR("No device\n");
		return -1;
	}
	if (!dev->queuelist || !dev->queuelist[DRM_KERNEL_CONTEXT]) {
				/* This only happens between the time the
				   interrupt is initialized and the time
				   the queues are initialized. */
		return -1;
	}

				/* Doing "while locked" DMA? */
	if (DRM_WAITCOUNT(dev, DRM_KERNEL_CONTEXT)) {
		return DRM_KERNEL_CONTEXT;
	}

				/* If there are buffers on the last_context
				   queue, and we have not been executing
				   this context very long, continue to
				   execute this context. */
	if (dev->last_switch <= j
	    && dev->last_switch + DRM_TIME_SLICE > j
	    && DRM_WAITCOUNT(dev, dev->last_context)) {
		return dev->last_context;
	}

				/* Otherwise, find a candidate */
	for (i = dev->last_checked + 1; i < dev->queue_count; i++) {
		if (DRM_WAITCOUNT(dev, i)) {
			candidate = dev->last_checked = i;
			break;
		}
	}

	if (candidate < 0) {
		for (i = 0; i < dev->queue_count; i++) {
			if (DRM_WAITCOUNT(dev, i)) {
				candidate = dev->last_checked = i;
				break;
			}
		}
	}

	if (wrapper
	    && candidate >= 0
	    && candidate != dev->last_context
	    && dev->last_switch <= j
	    && dev->last_switch + DRM_TIME_SLICE > j) {
		if (dev->timer.expires != dev->last_switch + DRM_TIME_SLICE) {
			del_timer(&dev->timer);
			dev->timer.function = wrapper;
			dev->timer.data	    = (unsigned long)dev;
			dev->timer.expires  = dev->last_switch+DRM_TIME_SLICE;
			add_timer(&dev->timer);
		}
		return -1;
	}

	return candidate;
}


int DRM(dma_enqueue)(struct file *filp, drm_dma_t *d)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;
	int		  i;
	drm_queue_t	  *q;
	drm_buf_t	  *buf;
	int		  idx;
	int		  while_locked = 0;
	drm_device_dma_t  *dma = dev->dma;
	int		  *ind;
	int		  err;
	DECLARE_WAITQUEUE(entry, current);

	DRM_DEBUG("%d\n", d->send_count);

	if (d->flags & _DRM_DMA_WHILE_LOCKED) {
		int context = dev->lock.hw_lock->lock;

		if (!_DRM_LOCK_IS_HELD(context)) {
			DRM_ERROR("No lock held during \"while locked\""
				  " request\n");
			return -EINVAL;
		}
		if (d->context != _DRM_LOCKING_CONTEXT(context)
		    && _DRM_LOCKING_CONTEXT(context) != DRM_KERNEL_CONTEXT) {
			DRM_ERROR("Lock held by %d while %d makes"
				  " \"while locked\" request\n",
				  _DRM_LOCKING_CONTEXT(context),
				  d->context);
			return -EINVAL;
		}
		q = dev->queuelist[DRM_KERNEL_CONTEXT];
		while_locked = 1;
	} else {
		q = dev->queuelist[d->context];
	}


	atomic_inc(&q->use_count);
	if (atomic_read(&q->block_write)) {
		add_wait_queue(&q->write_queue, &entry);
		atomic_inc(&q->block_count);
		for (;;) {
			current->state = TASK_INTERRUPTIBLE;
			if (!atomic_read(&q->block_write)) break;
			schedule();
			if (signal_pending(current)) {
				atomic_dec(&q->use_count);
				remove_wait_queue(&q->write_queue, &entry);
				return -EINTR;
			}
		}
		atomic_dec(&q->block_count);
		current->state = TASK_RUNNING;
		remove_wait_queue(&q->write_queue, &entry);
	}

	ind = DRM(alloc)(d->send_count * sizeof(int), DRM_MEM_DRIVER);
	if (!ind)
		return -ENOMEM;

	if (copy_from_user(ind, d->send_indices, d->send_count * sizeof(int))) {
		err = -EFAULT;
                goto out;
	}

	err = -EINVAL;
	for (i = 0; i < d->send_count; i++) {
		idx = ind[i];
		if (idx < 0 || idx >= dma->buf_count) {
			DRM_ERROR("Index %d (of %d max)\n",
				  ind[i], dma->buf_count - 1);
			goto out;
		}
		buf = dma->buflist[ idx ];
		if (buf->filp != filp) {
			DRM_ERROR("Process %d using buffer not owned\n",
				  current->pid);
			goto out;
		}
		if (buf->list != DRM_LIST_NONE) {
			DRM_ERROR("Process %d using buffer %d on list %d\n",
				  current->pid, buf->idx, buf->list);
			goto out;
		}
		buf->used	  = ind[i];
		buf->while_locked = while_locked;
		buf->context	  = d->context;
		if (!buf->used) {
			DRM_ERROR("Queueing 0 length buffer\n");
		}
		if (buf->pending) {
			DRM_ERROR("Queueing pending buffer:"
				  " buffer %d, offset %d\n",
				  ind[i], i);
			goto out;
		}
		if (buf->waiting) {
			DRM_ERROR("Queueing waiting buffer:"
				  " buffer %d, offset %d\n",
				  ind[i], i);
			goto out;
		}
		buf->waiting = 1;
		if (atomic_read(&q->use_count) == 1
		    || atomic_read(&q->finalization)) {
			DRM(free_buffer)(dev, buf);
		} else {
			DRM(waitlist_put)(&q->waitlist, buf);
			atomic_inc(&q->total_queued);
		}
	}
	atomic_dec(&q->use_count);

	return 0;

out:
	DRM(free)(ind, d->send_count * sizeof(int), DRM_MEM_DRIVER);
	atomic_dec(&q->use_count);
	return err;
}

static int DRM(dma_get_buffers_of_order)(struct file *filp, drm_dma_t *d,
					 int order)
{
	drm_file_t    *priv   = filp->private_data;
	drm_device_t  *dev    = priv->dev;
	int		  i;
	drm_buf_t	  *buf;
	drm_device_dma_t  *dma = dev->dma;

	for (i = d->granted_count; i < d->request_count; i++) {
		buf = DRM(freelist_get)(&dma->bufs[order].freelist,
					d->flags & _DRM_DMA_WAIT);
		if (!buf) break;
		if (buf->pending || buf->waiting) {
			DRM_ERROR("Free buffer %d in use: filp %p (w%d, p%d)\n",
				  buf->idx,
				  buf->filp,
				  buf->waiting,
				  buf->pending);
		}
		buf->filp     = filp;
		if (copy_to_user(&d->request_indices[i],
				 &buf->idx,
				 sizeof(buf->idx)))
			return -EFAULT;

		if (copy_to_user(&d->request_sizes[i],
				 &buf->total,
				 sizeof(buf->total)))
			return -EFAULT;

		++d->granted_count;
	}
	return 0;
}


int DRM(dma_get_buffers)(struct file *filp, drm_dma_t *dma)
{
	int		  order;
	int		  retcode = 0;
	int		  tmp_order;

	order = DRM(order)(dma->request_size);

	dma->granted_count = 0;
	retcode		   = DRM(dma_get_buffers_of_order)(filp, dma, order);

	if (dma->granted_count < dma->request_count
	    && (dma->flags & _DRM_DMA_SMALLER_OK)) {
		for (tmp_order = order - 1;
		     !retcode
			     && dma->granted_count < dma->request_count
			     && tmp_order >= DRM_MIN_ORDER;
		     --tmp_order) {

			retcode = DRM(dma_get_buffers_of_order)(filp, dma,
								tmp_order);
		}
	}

	if (dma->granted_count < dma->request_count
	    && (dma->flags & _DRM_DMA_LARGER_OK)) {
		for (tmp_order = order + 1;
		     !retcode
			     && dma->granted_count < dma->request_count
			     && tmp_order <= DRM_MAX_ORDER;
		     ++tmp_order) {

			retcode = DRM(dma_get_buffers_of_order)(filp, dma,
								tmp_order);
		}
	}
	return 0;
}

