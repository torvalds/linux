/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/dma-fence.h>

#include "msm_drv.h"
#include "msm_fence.h"


struct msm_fence_context *
msm_fence_context_alloc(struct drm_device *dev, const char *name)
{
	struct msm_fence_context *fctx;

	fctx = kzalloc(sizeof(*fctx), GFP_KERNEL);
	if (!fctx)
		return ERR_PTR(-ENOMEM);

	fctx->dev = dev;
	fctx->name = name;
	fctx->context = dma_fence_context_alloc(1);
	init_waitqueue_head(&fctx->event);
	spin_lock_init(&fctx->spinlock);

	return fctx;
}

void msm_fence_context_free(struct msm_fence_context *fctx)
{
	kfree(fctx);
}

static inline bool fence_completed(struct msm_fence_context *fctx, uint32_t fence)
{
	return (int32_t)(fctx->completed_fence - fence) >= 0;
}

/* legacy path for WAIT_FENCE ioctl: */
int msm_wait_fence(struct msm_fence_context *fctx, uint32_t fence,
		ktime_t *timeout, bool interruptible)
{
	int ret;

	if (fence > fctx->last_fence) {
		DRM_ERROR("%s: waiting on invalid fence: %u (of %u)\n",
				fctx->name, fence, fctx->last_fence);
		return -EINVAL;
	}

	if (!timeout) {
		/* no-wait: */
		ret = fence_completed(fctx, fence) ? 0 : -EBUSY;
	} else {
		unsigned long remaining_jiffies = timeout_to_jiffies(timeout);

		if (interruptible)
			ret = wait_event_interruptible_timeout(fctx->event,
				fence_completed(fctx, fence),
				remaining_jiffies);
		else
			ret = wait_event_timeout(fctx->event,
				fence_completed(fctx, fence),
				remaining_jiffies);

		if (ret == 0) {
			DBG("timeout waiting for fence: %u (completed: %u)",
					fence, fctx->completed_fence);
			ret = -ETIMEDOUT;
		} else if (ret != -ERESTARTSYS) {
			ret = 0;
		}
	}

	return ret;
}

/* called from workqueue */
void msm_update_fence(struct msm_fence_context *fctx, uint32_t fence)
{
	spin_lock(&fctx->spinlock);
	fctx->completed_fence = max(fence, fctx->completed_fence);
	spin_unlock(&fctx->spinlock);

	wake_up_all(&fctx->event);
}

struct msm_fence {
	struct msm_fence_context *fctx;
	struct dma_fence base;
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

static bool msm_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static bool msm_fence_signaled(struct dma_fence *fence)
{
	struct msm_fence *f = to_msm_fence(fence);
	return fence_completed(f->fctx, f->base.seqno);
}

static void msm_fence_release(struct dma_fence *fence)
{
	struct msm_fence *f = to_msm_fence(fence);
	kfree_rcu(f, base.rcu);
}

static const struct dma_fence_ops msm_fence_ops = {
	.get_driver_name = msm_fence_get_driver_name,
	.get_timeline_name = msm_fence_get_timeline_name,
	.enable_signaling = msm_fence_enable_signaling,
	.signaled = msm_fence_signaled,
	.wait = dma_fence_default_wait,
	.release = msm_fence_release,
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
