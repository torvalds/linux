// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_hw_fence.h"

#include <linux/device.h>
#include <linux/slab.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_hw_engine.h"
#include "xe_macros.h"
#include "xe_map.h"
#include "xe_trace.h"

static struct kmem_cache *xe_hw_fence_slab;

int __init xe_hw_fence_module_init(void)
{
	xe_hw_fence_slab = kmem_cache_create("xe_hw_fence",
					     sizeof(struct xe_hw_fence), 0,
					     SLAB_HWCACHE_ALIGN, NULL);
	if (!xe_hw_fence_slab)
		return -ENOMEM;

	return 0;
}

void xe_hw_fence_module_exit(void)
{
	rcu_barrier();
	kmem_cache_destroy(xe_hw_fence_slab);
}

static struct xe_hw_fence *fence_alloc(void)
{
	return kmem_cache_zalloc(xe_hw_fence_slab, GFP_KERNEL);
}

static void fence_free(struct rcu_head *rcu)
{
	struct xe_hw_fence *fence =
		container_of(rcu, struct xe_hw_fence, dma.rcu);

	if (!WARN_ON_ONCE(!fence))
		kmem_cache_free(xe_hw_fence_slab, fence);
}

static void hw_fence_irq_run_cb(struct irq_work *work)
{
	struct xe_hw_fence_irq *irq = container_of(work, typeof(*irq), work);
	struct xe_hw_fence *fence, *next;
	bool tmp;

	tmp = dma_fence_begin_signalling();
	spin_lock(&irq->lock);
	if (irq->enabled) {
		list_for_each_entry_safe(fence, next, &irq->pending, irq_link) {
			struct dma_fence *dma_fence = &fence->dma;

			trace_xe_hw_fence_try_signal(fence);
			if (dma_fence_is_signaled_locked(dma_fence)) {
				trace_xe_hw_fence_signal(fence);
				list_del_init(&fence->irq_link);
				dma_fence_put(dma_fence);
			}
		}
	}
	spin_unlock(&irq->lock);
	dma_fence_end_signalling(tmp);
}

void xe_hw_fence_irq_init(struct xe_hw_fence_irq *irq)
{
	spin_lock_init(&irq->lock);
	init_irq_work(&irq->work, hw_fence_irq_run_cb);
	INIT_LIST_HEAD(&irq->pending);
	irq->enabled = true;
}

void xe_hw_fence_irq_finish(struct xe_hw_fence_irq *irq)
{
	struct xe_hw_fence *fence, *next;
	unsigned long flags;
	int err;
	bool tmp;

	if (XE_WARN_ON(!list_empty(&irq->pending))) {
		tmp = dma_fence_begin_signalling();
		spin_lock_irqsave(&irq->lock, flags);
		list_for_each_entry_safe(fence, next, &irq->pending, irq_link) {
			list_del_init(&fence->irq_link);
			err = dma_fence_signal_locked(&fence->dma);
			dma_fence_put(&fence->dma);
			XE_WARN_ON(err);
		}
		spin_unlock_irqrestore(&irq->lock, flags);
		dma_fence_end_signalling(tmp);
	}
}

void xe_hw_fence_irq_run(struct xe_hw_fence_irq *irq)
{
	irq_work_queue(&irq->work);
}

void xe_hw_fence_irq_stop(struct xe_hw_fence_irq *irq)
{
	spin_lock_irq(&irq->lock);
	irq->enabled = false;
	spin_unlock_irq(&irq->lock);
}

void xe_hw_fence_irq_start(struct xe_hw_fence_irq *irq)
{
	spin_lock_irq(&irq->lock);
	irq->enabled = true;
	spin_unlock_irq(&irq->lock);

	irq_work_queue(&irq->work);
}

void xe_hw_fence_ctx_init(struct xe_hw_fence_ctx *ctx, struct xe_gt *gt,
			  struct xe_hw_fence_irq *irq, const char *name)
{
	ctx->gt = gt;
	ctx->irq = irq;
	ctx->dma_fence_ctx = dma_fence_context_alloc(1);
	ctx->next_seqno = XE_FENCE_INITIAL_SEQNO;
	snprintf(ctx->name, sizeof(ctx->name), "%s", name);
}

void xe_hw_fence_ctx_finish(struct xe_hw_fence_ctx *ctx)
{
}

static struct xe_hw_fence *to_xe_hw_fence(struct dma_fence *fence);

static struct xe_hw_fence_irq *xe_hw_fence_irq(struct xe_hw_fence *fence)
{
	return container_of(fence->dma.lock, struct xe_hw_fence_irq, lock);
}

static const char *xe_hw_fence_get_driver_name(struct dma_fence *dma_fence)
{
	struct xe_hw_fence *fence = to_xe_hw_fence(dma_fence);

	return dev_name(gt_to_xe(fence->ctx->gt)->drm.dev);
}

static const char *xe_hw_fence_get_timeline_name(struct dma_fence *dma_fence)
{
	struct xe_hw_fence *fence = to_xe_hw_fence(dma_fence);

	return fence->ctx->name;
}

static bool xe_hw_fence_signaled(struct dma_fence *dma_fence)
{
	struct xe_hw_fence *fence = to_xe_hw_fence(dma_fence);
	struct xe_device *xe = gt_to_xe(fence->ctx->gt);
	u32 seqno = xe_map_rd(xe, &fence->seqno_map, 0, u32);

	return dma_fence->error ||
		!__dma_fence_is_later(dma_fence->seqno, seqno, dma_fence->ops);
}

static bool xe_hw_fence_enable_signaling(struct dma_fence *dma_fence)
{
	struct xe_hw_fence *fence = to_xe_hw_fence(dma_fence);
	struct xe_hw_fence_irq *irq = xe_hw_fence_irq(fence);

	dma_fence_get(dma_fence);
	list_add_tail(&fence->irq_link, &irq->pending);

	/* SW completed (no HW IRQ) so kick handler to signal fence */
	if (xe_hw_fence_signaled(dma_fence))
		xe_hw_fence_irq_run(irq);

	return true;
}

static void xe_hw_fence_release(struct dma_fence *dma_fence)
{
	struct xe_hw_fence *fence = to_xe_hw_fence(dma_fence);

	trace_xe_hw_fence_free(fence);
	XE_WARN_ON(!list_empty(&fence->irq_link));
	call_rcu(&dma_fence->rcu, fence_free);
}

static const struct dma_fence_ops xe_hw_fence_ops = {
	.get_driver_name = xe_hw_fence_get_driver_name,
	.get_timeline_name = xe_hw_fence_get_timeline_name,
	.enable_signaling = xe_hw_fence_enable_signaling,
	.signaled = xe_hw_fence_signaled,
	.release = xe_hw_fence_release,
};

static struct xe_hw_fence *to_xe_hw_fence(struct dma_fence *fence)
{
	if (XE_WARN_ON(fence->ops != &xe_hw_fence_ops))
		return NULL;

	return container_of(fence, struct xe_hw_fence, dma);
}

struct xe_hw_fence *xe_hw_fence_create(struct xe_hw_fence_ctx *ctx,
				       struct iosys_map seqno_map)
{
	struct xe_hw_fence *fence;

	fence = fence_alloc();
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->ctx = ctx;
	fence->seqno_map = seqno_map;
	INIT_LIST_HEAD(&fence->irq_link);

	dma_fence_init(&fence->dma, &xe_hw_fence_ops, &ctx->irq->lock,
		       ctx->dma_fence_ctx, ctx->next_seqno++);

	trace_xe_hw_fence_create(fence);

	return fence;
}
