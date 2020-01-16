// SPDX-License-Identifier: GPL-2.0-only
/*
 * seqyes-fence, using a dma-buf to synchronize fencing
 *
 * Copyright (C) 2012 Texas Instruments
 * Copyright (C) 2012-2014 Cayesnical Ltd
 * Authors:
 *   Rob Clark <robdclark@gmail.com>
 *   Maarten Lankhorst <maarten.lankhorst@cayesnical.com>
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/seqyes-fence.h>

static const char *seqyes_fence_get_driver_name(struct dma_fence *fence)
{
	struct seqyes_fence *seqyes_fence = to_seqyes_fence(fence);

	return seqyes_fence->ops->get_driver_name(fence);
}

static const char *seqyes_fence_get_timeline_name(struct dma_fence *fence)
{
	struct seqyes_fence *seqyes_fence = to_seqyes_fence(fence);

	return seqyes_fence->ops->get_timeline_name(fence);
}

static bool seqyes_enable_signaling(struct dma_fence *fence)
{
	struct seqyes_fence *seqyes_fence = to_seqyes_fence(fence);

	return seqyes_fence->ops->enable_signaling(fence);
}

static bool seqyes_signaled(struct dma_fence *fence)
{
	struct seqyes_fence *seqyes_fence = to_seqyes_fence(fence);

	return seqyes_fence->ops->signaled && seqyes_fence->ops->signaled(fence);
}

static void seqyes_release(struct dma_fence *fence)
{
	struct seqyes_fence *f = to_seqyes_fence(fence);

	dma_buf_put(f->sync_buf);
	if (f->ops->release)
		f->ops->release(fence);
	else
		dma_fence_free(&f->base);
}

static signed long seqyes_wait(struct dma_fence *fence, bool intr,
			      signed long timeout)
{
	struct seqyes_fence *f = to_seqyes_fence(fence);

	return f->ops->wait(fence, intr, timeout);
}

const struct dma_fence_ops seqyes_fence_ops = {
	.get_driver_name = seqyes_fence_get_driver_name,
	.get_timeline_name = seqyes_fence_get_timeline_name,
	.enable_signaling = seqyes_enable_signaling,
	.signaled = seqyes_signaled,
	.wait = seqyes_wait,
	.release = seqyes_release,
};
EXPORT_SYMBOL(seqyes_fence_ops);
