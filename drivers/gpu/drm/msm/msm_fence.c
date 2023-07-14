// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013-2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#include <linux/dma-fence.h>

#include "msm_drv.h"
#include "msm_fence.h"
#include "msm_gpu.h"

static struct msm_gpu *fctx2gpu(struct msm_fence_context *fctx)
{
	struct msm_drm_private *priv = fctx->dev->dev_private;
	return priv->gpu;
}

static enum hrtimer_restart deadline_timer(struct hrtimer *t)
{
	struct msm_fence_context *fctx = container_of(t,
			struct msm_fence_context, deadline_timer);

	kthread_queue_work(fctx2gpu(fctx)->worker, &fctx->deadline_work);

	return HRTIMER_NORESTART;
}

static void deadline_work(struct kthread_work *work)
{
	struct msm_fence_context *fctx = container_of(work,
			struct msm_fence_context, deadline_work);

	/* If deadline fence has already passed, nothing to do: */
	if (msm_fence_completed(fctx, fctx->next_deadline_fence))
		return;

	msm_devfreq_boost(fctx2gpu(fctx), 2);
}


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
	strscpy(fctx->name, name, sizeof(fctx->name));
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

	hrtimer_init(&fctx->deadline_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	fctx->deadline_timer.function = deadline_timer;

	kthread_init_work(&fctx->deadline_work, deadline_work);

	fctx->next_deadline = ktime_get();

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
	if (msm_fence_completed(fctx, fctx->next_deadline_fence))
		hrtimer_cancel(&fctx->deadline_timer);
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

static void msm_fence_set_deadline(struct dma_fence *fence, ktime_t deadline)
{
	struct msm_fence *f = to_msm_fence(fence);
	struct msm_fence_context *fctx = f->fctx;
	unsigned long flags;
	ktime_t now;

	spin_lock_irqsave(&fctx->spinlock, flags);
	now = ktime_get();

	if (ktime_after(now, fctx->next_deadline) ||
			ktime_before(deadline, fctx->next_deadline)) {
		fctx->next_deadline = deadline;
		fctx->next_deadline_fence =
			max(fctx->next_deadline_fence, (uint32_t)fence->seqno);

		/*
		 * Set timer to trigger boost 3ms before deadline, or
		 * if we are already less than 3ms before the deadline
		 * schedule boost work immediately.
		 */
		deadline = ktime_sub(deadline, ms_to_ktime(3));

		if (ktime_after(now, deadline)) {
			kthread_queue_work(fctx2gpu(fctx)->worker,
					&fctx->deadline_work);
		} else {
			hrtimer_start(&fctx->deadline_timer, deadline,
					HRTIMER_MODE_ABS);
		}
	}

	spin_unlock_irqrestore(&fctx->spinlock, flags);
}

static const struct dma_fence_ops msm_fence_ops = {
	.get_driver_name = msm_fence_get_driver_name,
	.get_timeline_name = msm_fence_get_timeline_name,
	.signaled = msm_fence_signaled,
	.set_deadline = msm_fence_set_deadline,
};

struct dma_fence *
msm_fence_alloc(void)
{
	struct msm_fence *f;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return ERR_PTR(-ENOMEM);

	return &f->base;
}

void
msm_fence_init(struct dma_fence *fence, struct msm_fence_context *fctx)
{
	struct msm_fence *f = to_msm_fence(fence);

	f->fctx = fctx;

	dma_fence_init(&f->base, &msm_fence_ops, &fctx->spinlock,
		       fctx->context, ++fctx->last_fence);
}
