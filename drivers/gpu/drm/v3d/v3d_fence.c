// SPDX-License-Identifier: GPL-2.0+
/* Copyright (C) 2017-2018 Broadcom */

#include "v3d_drv.h"

struct dma_fence *v3d_fence_create(struct v3d_dev *v3d, enum v3d_queue queue)
{
	struct v3d_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->dev = &v3d->drm;
	fence->queue = queue;
	fence->seqno = ++v3d->queue[queue].emit_seqno;
	dma_fence_init(&fence->base, &v3d_fence_ops, &v3d->job_lock,
		       v3d->queue[queue].fence_context, fence->seqno);

	return &fence->base;
}

static const char *v3d_fence_get_driver_name(struct dma_fence *fence)
{
	return "v3d";
}

static const char *v3d_fence_get_timeline_name(struct dma_fence *fence)
{
	struct v3d_fence *f = to_v3d_fence(fence);

	if (f->queue == V3D_BIN)
		return "v3d-bin";
	else
		return "v3d-render";
}

static bool v3d_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static bool v3d_fence_signaled(struct dma_fence *fence)
{
	struct v3d_fence *f = to_v3d_fence(fence);
	struct v3d_dev *v3d = to_v3d_dev(f->dev);

	return v3d->queue[f->queue].finished_seqno >= f->seqno;
}

const struct dma_fence_ops v3d_fence_ops = {
	.get_driver_name = v3d_fence_get_driver_name,
	.get_timeline_name = v3d_fence_get_timeline_name,
	.enable_signaling = v3d_fence_enable_signaling,
	.signaled = v3d_fence_signaled,
	.wait = dma_fence_default_wait,
	.release = dma_fence_free,
};
