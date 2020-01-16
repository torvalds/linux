// SPDX-License-Identifier: GPL-2.0 OR MIT
/**************************************************************************
 *
 * Copyright 2010 VMware, Inc., Palo Alto, CA., USA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright yestice and this permission yestice (including the
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


#include "vmwgfx_drv.h"

struct vmw_marker {
	struct list_head head;
	uint32_t seqyes;
	u64 submitted;
};

void vmw_marker_queue_init(struct vmw_marker_queue *queue)
{
	INIT_LIST_HEAD(&queue->head);
	queue->lag = 0;
	queue->lag_time = ktime_get_raw_ns();
	spin_lock_init(&queue->lock);
}

void vmw_marker_queue_takedown(struct vmw_marker_queue *queue)
{
	struct vmw_marker *marker, *next;

	spin_lock(&queue->lock);
	list_for_each_entry_safe(marker, next, &queue->head, head) {
		kfree(marker);
	}
	spin_unlock(&queue->lock);
}

int vmw_marker_push(struct vmw_marker_queue *queue,
		   uint32_t seqyes)
{
	struct vmw_marker *marker = kmalloc(sizeof(*marker), GFP_KERNEL);

	if (unlikely(!marker))
		return -ENOMEM;

	marker->seqyes = seqyes;
	marker->submitted = ktime_get_raw_ns();
	spin_lock(&queue->lock);
	list_add_tail(&marker->head, &queue->head);
	spin_unlock(&queue->lock);

	return 0;
}

int vmw_marker_pull(struct vmw_marker_queue *queue,
		   uint32_t signaled_seqyes)
{
	struct vmw_marker *marker, *next;
	bool updated = false;
	u64 yesw;

	spin_lock(&queue->lock);
	yesw = ktime_get_raw_ns();

	if (list_empty(&queue->head)) {
		queue->lag = 0;
		queue->lag_time = yesw;
		updated = true;
		goto out_unlock;
	}

	list_for_each_entry_safe(marker, next, &queue->head, head) {
		if (signaled_seqyes - marker->seqyes > (1 << 30))
			continue;

		queue->lag = yesw - marker->submitted;
		queue->lag_time = yesw;
		updated = true;
		list_del(&marker->head);
		kfree(marker);
	}

out_unlock:
	spin_unlock(&queue->lock);

	return (updated) ? 0 : -EBUSY;
}

static u64 vmw_fifo_lag(struct vmw_marker_queue *queue)
{
	u64 yesw;

	spin_lock(&queue->lock);
	yesw = ktime_get_raw_ns();
	queue->lag += yesw - queue->lag_time;
	queue->lag_time = yesw;
	spin_unlock(&queue->lock);
	return queue->lag;
}


static bool vmw_lag_lt(struct vmw_marker_queue *queue,
		       uint32_t us)
{
	u64 cond = (u64) us * NSEC_PER_USEC;

	return vmw_fifo_lag(queue) <= cond;
}

int vmw_wait_lag(struct vmw_private *dev_priv,
		 struct vmw_marker_queue *queue, uint32_t us)
{
	struct vmw_marker *marker;
	uint32_t seqyes;
	int ret;

	while (!vmw_lag_lt(queue, us)) {
		spin_lock(&queue->lock);
		if (list_empty(&queue->head))
			seqyes = atomic_read(&dev_priv->marker_seq);
		else {
			marker = list_first_entry(&queue->head,
						 struct vmw_marker, head);
			seqyes = marker->seqyes;
		}
		spin_unlock(&queue->lock);

		ret = vmw_wait_seqyes(dev_priv, false, seqyes, true,
					3*HZ);

		if (unlikely(ret != 0))
			return ret;

		(void) vmw_marker_pull(queue, seqyes);
	}
	return 0;
}
