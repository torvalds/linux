// SPDX-License-Identifier: GPL-2.0-only
/*
 * seqno-fence, using a dma-buf to synchronize fencing
 *
 * Copyright (C) 2012 Texas Instruments
 * Copyright (C) 2012-2014 Canonical Ltd
 * Authors:
 *   Rob Clark <robdclark@gmail.com>
 *   Maarten Lankhorst <maarten.lankhorst@canonical.com>
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/seqno-fence.h>

static const char *seqno_fence_get_driver_name(struct dma_fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);

	return seqno_fence->ops->get_driver_name(fence);
}

static const char *seqno_fence_get_timeline_name(struct dma_fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);

	return seqno_fence->ops->get_timeline_name(fence);
}

static bool seqno_enable_signaling(struct dma_fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);

	return seqno_fence->ops->enable_signaling(fence);
}

static bool seqno_signaled(struct dma_fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);

	return seqno_fence->ops->signaled && seqno_fence->ops->signaled(fence);
}

static void seqno_release(struct dma_fence *fence)
{
	struct seqno_fence *f = to_seqno_fence(fence);

	dma_buf_put(f->sync_buf);
	if (f->ops->release)
		f->ops->release(fence);
	else
		dma_fence_free(&f->base);
}

static signed long seqno_wait(struct dma_fence *fence, bool intr,
			      signed long timeout)
{
	struct seqno_fence *f = to_seqno_fence(fence);

	return f->ops->wait(fence, intr, timeout);
}

const struct dma_fence_ops seqno_fence_ops = {
	.get_driver_name = seqno_fence_get_driver_name,
	.get_timeline_name = seqno_fence_get_timeline_name,
	.enable_signaling = seqno_enable_signaling,
	.signaled = seqno_signaled,
	.wait = seqno_wait,
	.release = seqno_release,
};
EXPORT_SYMBOL(seqno_fence_ops);
