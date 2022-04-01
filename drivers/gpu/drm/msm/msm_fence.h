/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_FENCE_H__
#define __MSM_FENCE_H__

#include "msm_drv.h"

/**
 * struct msm_fence_context - fence context for gpu
 *
 * Each ringbuffer has a single fence context, with the GPU writing an
 * incrementing fence seqno at the end of each submit
 */
struct msm_fence_context {
	struct drm_device *dev;
	/** name: human readable name for fence timeline */
	char name[32];
	/** context: see dma_fence_context_alloc() */
	unsigned context;

	/**
	 * last_fence:
	 *
	 * Last assigned fence, incremented each time a fence is created
	 * on this fence context.  If last_fence == completed_fence,
	 * there is no remaining pending work
	 */
	uint32_t last_fence;

	/**
	 * completed_fence:
	 *
	 * The last completed fence, updated from the CPU after interrupt
	 * from GPU
	 */
	uint32_t completed_fence;

	/**
	 * fenceptr:
	 *
	 * The address that the GPU directly writes with completed fence
	 * seqno.  This can be ahead of completed_fence.  We can peek at
	 * this to see if a fence has already signaled but the CPU hasn't
	 * gotten around to handling the irq and updating completed_fence
	 */
	volatile uint32_t *fenceptr;

	spinlock_t spinlock;
};

struct msm_fence_context * msm_fence_context_alloc(struct drm_device *dev,
		volatile uint32_t *fenceptr, const char *name);
void msm_fence_context_free(struct msm_fence_context *fctx);

void msm_update_fence(struct msm_fence_context *fctx, uint32_t fence);

struct dma_fence * msm_fence_alloc(struct msm_fence_context *fctx);

static inline bool
fence_before(uint32_t a, uint32_t b)
{
   return (int32_t)(a - b) < 0;
}

static inline bool
fence_after(uint32_t a, uint32_t b)
{
   return (int32_t)(a - b) > 0;
}

#endif
