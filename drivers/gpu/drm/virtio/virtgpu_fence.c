/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <trace/events/dma_fence.h>

#include "virtgpu_drv.h"

static const char *virtio_get_driver_name(struct dma_fence *f)
{
	return "virtio_gpu";
}

static const char *virtio_get_timeline_name(struct dma_fence *f)
{
	return "controlq";
}

bool virtio_fence_signaled(struct dma_fence *f)
{
	struct virtio_gpu_fence *fence = to_virtio_fence(f);

	if (WARN_ON_ONCE(fence->f.seqno == 0))
		/* leaked fence outside driver before completing
		 * initialization with virtio_gpu_fence_emit */
		return false;
	if (atomic64_read(&fence->drv->last_seq) >= fence->f.seqno)
		return true;
	return false;
}

static void virtio_fence_value_str(struct dma_fence *f, char *str, int size)
{
	snprintf(str, size, "%llu", f->seqno);
}

static void virtio_timeline_value_str(struct dma_fence *f, char *str, int size)
{
	struct virtio_gpu_fence *fence = to_virtio_fence(f);

	snprintf(str, size, "%llu", (u64)atomic64_read(&fence->drv->last_seq));
}

static const struct dma_fence_ops virtio_fence_ops = {
	.get_driver_name     = virtio_get_driver_name,
	.get_timeline_name   = virtio_get_timeline_name,
	.signaled            = virtio_fence_signaled,
	.fence_value_str     = virtio_fence_value_str,
	.timeline_value_str  = virtio_timeline_value_str,
};

struct virtio_gpu_fence *virtio_gpu_fence_alloc(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
	struct virtio_gpu_fence *fence = kzalloc(sizeof(struct virtio_gpu_fence),
							GFP_KERNEL);
	if (!fence)
		return fence;

	fence->drv = drv;

	/* This only partially initializes the fence because the seqno is
	 * unknown yet.  The fence must not be used outside of the driver
	 * until virtio_gpu_fence_emit is called.
	 */
	dma_fence_init(&fence->f, &virtio_fence_ops, &drv->lock, drv->context, 0);

	return fence;
}

void virtio_gpu_fence_emit(struct virtio_gpu_device *vgdev,
			  struct virtio_gpu_ctrl_hdr *cmd_hdr,
			  struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
	unsigned long irq_flags;

	spin_lock_irqsave(&drv->lock, irq_flags);
	fence->f.seqno = ++drv->sync_seq;
	dma_fence_get(&fence->f);
	list_add_tail(&fence->node, &drv->fences);
	spin_unlock_irqrestore(&drv->lock, irq_flags);

	trace_dma_fence_emit(&fence->f);

	cmd_hdr->flags |= cpu_to_le32(VIRTIO_GPU_FLAG_FENCE);
	cmd_hdr->fence_id = cpu_to_le64(fence->f.seqno);
}

void virtio_gpu_fence_event_process(struct virtio_gpu_device *vgdev,
				    u64 last_seq)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
	struct virtio_gpu_fence *fence, *tmp;
	unsigned long irq_flags;

	spin_lock_irqsave(&drv->lock, irq_flags);
	atomic64_set(&vgdev->fence_drv.last_seq, last_seq);
	list_for_each_entry_safe(fence, tmp, &drv->fences, node) {
		if (last_seq < fence->f.seqno)
			continue;
		dma_fence_signal_locked(&fence->f);
		list_del(&fence->node);
		dma_fence_put(&fence->f);
	}
	spin_unlock_irqrestore(&drv->lock, irq_flags);
}
