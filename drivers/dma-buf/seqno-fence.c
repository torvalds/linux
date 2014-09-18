/*
 * seqno-fence, using a dma-buf to synchronize fencing
 *
 * Copyright (C) 2012 Texas Instruments
 * Copyright (C) 2012-2014 Canonical Ltd
 * Authors:
 *   Rob Clark <robdclark@gmail.com>
 *   Maarten Lankhorst <maarten.lankhorst@canonical.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <linux/seqno-fence.h>

static const char *seqno_fence_get_driver_name(struct fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);
	return seqno_fence->ops->get_driver_name(fence);
}

static const char *seqno_fence_get_timeline_name(struct fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);
	return seqno_fence->ops->get_timeline_name(fence);
}

static bool seqno_enable_signaling(struct fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);
	return seqno_fence->ops->enable_signaling(fence);
}

static bool seqno_signaled(struct fence *fence)
{
	struct seqno_fence *seqno_fence = to_seqno_fence(fence);
	return seqno_fence->ops->signaled && seqno_fence->ops->signaled(fence);
}

static void seqno_release(struct fence *fence)
{
	struct seqno_fence *f = to_seqno_fence(fence);

	dma_buf_put(f->sync_buf);
	if (f->ops->release)
		f->ops->release(fence);
	else
		fence_free(&f->base);
}

static signed long seqno_wait(struct fence *fence, bool intr, signed long timeout)
{
	struct seqno_fence *f = to_seqno_fence(fence);
	return f->ops->wait(fence, intr, timeout);
}

const struct fence_ops seqno_fence_ops = {
	.get_driver_name = seqno_fence_get_driver_name,
	.get_timeline_name = seqno_fence_get_timeline_name,
	.enable_signaling = seqno_enable_signaling,
	.signaled = seqno_signaled,
	.wait = seqno_wait,
	.release = seqno_release,
};
EXPORT_SYMBOL(seqno_fence_ops);
