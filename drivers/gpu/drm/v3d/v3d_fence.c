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

	switch (f->queue) {
	case V3D_BIN:
		return "v3d-bin";
	case V3D_RENDER:
		return "v3d-render";
	case V3D_TFU:
		return "v3d-tfu";
	case V3D_CSD:
		return "v3d-csd";
	default:
		return NULL;
	}
}

const struct dma_fence_ops v3d_fence_ops = {
	.get_driver_name = v3d_fence_get_driver_name,
	.get_timeline_name = v3d_fence_get_timeline_name,
};
