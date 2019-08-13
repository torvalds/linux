/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_FENCE_H__
#define __MSM_FENCE_H__

#include "msm_drv.h"

struct msm_fence_context {
	struct drm_device *dev;
	char name[32];
	unsigned context;
	/* last_fence == completed_fence --> no pending work */
	uint32_t last_fence;          /* last assigned fence */
	uint32_t completed_fence;     /* last completed fence */
	wait_queue_head_t event;
	spinlock_t spinlock;
};

struct msm_fence_context * msm_fence_context_alloc(struct drm_device *dev,
		const char *name);
void msm_fence_context_free(struct msm_fence_context *fctx);

int msm_wait_fence(struct msm_fence_context *fctx, uint32_t fence,
		ktime_t *timeout, bool interruptible);
void msm_update_fence(struct msm_fence_context *fctx, uint32_t fence);

struct dma_fence * msm_fence_alloc(struct msm_fence_context *fctx);

#endif
