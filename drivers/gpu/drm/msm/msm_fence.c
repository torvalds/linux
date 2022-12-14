// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/dma-fence.h>

#include "msm_drv.h"
#include "msm_fence.h"


struct msm_fence_context *
msm_fence_context_alloc(struct drm_device *dev, volatile uint32_t *fenceptr,
		const char *name)
{
	struct msm_fence_context *fctx;
	static int index = 0;

	fctx = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return ERR_PTR(-ENOMEM);

	fctx->dev = dev;
	strncpy(fctx->name, name, sizeof(fctx->name));
	fctx->context = dma_fence_context_alloc(1);
	fctx->index = index++;
	fctx->fenceptr = fenceptr;
	spin_lock_init(&fctx->spinlock);

	/*
	 * Start out close to the 32b fence rollover point, so we can
	 * catch bugs with fence comparisons.
	 */
	fctx->last_fence = 0xffffff00;
	fctx->completed_fence = fctx->last_fence;
	*fctx->fenceptr = fctx->last_fence;

	return fctx;
}

void msm_fence_context_free(struct msm_fence_context *fctx)
{
	kfree(fctx);
}

bool msm_fence_completed(struct msm_fence_context *fctx, uint32_t fence)
{
	/*
	 * Note: Check completed_fence first, as fenceptr is in a write-combine
	 * mapping, so it will be more expensive to read.
	 */
	return (int32_t)(fctx->completed_fence - fence) >= 0 ||
		(int32_t)(*fctx->fenceptr - fence) >= 0;
}

/* called from irq handler and workqueue (in recover path) */
void msm_update_fence(struct msm_fence_context *fctx, uint32_t fence)
{
	unsigned long flags;

	spin_lock_irqsave(&fctx->spinlock, flags);
	if (fence_after(fence, fctx->completed_fence))
		fctx->completed_fence = fence;
	spin_unlock_irqrestore(&fctx->spinlock, flags);
}

struct msm_fence {
	struct dma_fence base;
	struct msm_fence_context *fctx;
};

static inline struct msm_fence *to_msm_fence(struct dma_fence *fence)
{
	return container_of(fence, struct msm_fence, base);
}

static const char *msm_fence_get_driver_name(struct dma_fence *fence)
{
	return "msm";
}

static const char *msm_fence_get_timeline_name(struct dma_fence *fence)
{
	struct msm_fence *f = to_msm_fence(fence);
	return f->fctx->name;
}

static bool msm_fence_signaled(struct dma_fence *fence)
{
	struct msm_fence *f = to_msm_fence(fence);
	return msm_fence_completed(f->fctx, f->base.seqno);
}

static const struct dma_fence_ops msm_fence_ops = {
	.get_driver_name = msm_fence_get_driver_name,
	.get_timeline_name = msm_fence_get_timeline_name,
	.signaled = msm_fence_signaled,
};

struct dma_fence *
msm_fence_alloc(struct msm_fence_context *fctx)
{
	struct msm_fence *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return ERR_PTR(-ENOMEM);

	f->fctx = fctx;

	dma_fence_init(&f->base, &msm_fence_ops, &fctx->spinlock,
		       fctx->context, ++fctx->last_fence);

	return &f->base;
}
