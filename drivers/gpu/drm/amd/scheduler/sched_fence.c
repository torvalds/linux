/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 *
 */
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <drm/drmP.h>
#include "gpu_scheduler.h"

struct amd_sched_fence *amd_sched_fence_create(struct amd_sched_entity *entity,
					       void *owner)
{
	struct amd_sched_fence *fence = NULL;
	unsigned seq;

	fence = kmem_cache_zalloc(sched_fence_slab, GFP_KERNEL);
	if (fence == NULL)
		return NULL;

	fence->owner = owner;
	fence->sched = entity->sched;
	spin_lock_init(&fence->lock);

	seq = atomic_inc_return(&entity->fence_seq);
	dma_fence_init(&fence->scheduled, &amd_sched_fence_ops_scheduled,
		       &fence->lock, entity->fence_context, seq);
	dma_fence_init(&fence->finished, &amd_sched_fence_ops_finished,
		       &fence->lock, entity->fence_context + 1, seq);

	return fence;
}

void amd_sched_fence_scheduled(struct amd_sched_fence *fence)
{
	int ret = dma_fence_signal(&fence->scheduled);

	if (!ret)
		DMA_FENCE_TRACE(&fence->scheduled,
				"signaled from irq context\n");
	else
		DMA_FENCE_TRACE(&fence->scheduled,
				"was already signaled\n");
}

void amd_sched_fence_finished(struct amd_sched_fence *fence)
{
	int ret = dma_fence_signal(&fence->finished);

	if (!ret)
		DMA_FENCE_TRACE(&fence->finished,
				"signaled from irq context\n");
	else
		DMA_FENCE_TRACE(&fence->finished,
				"was already signaled\n");
}

static const char *amd_sched_fence_get_driver_name(struct dma_fence *fence)
{
	return "amd_sched";
}

static const char *amd_sched_fence_get_timeline_name(struct dma_fence *f)
{
	struct amd_sched_fence *fence = to_amd_sched_fence(f);
	return (const char *)fence->sched->name;
}

static bool amd_sched_fence_enable_signaling(struct dma_fence *f)
{
	return true;
}

/**
 * amd_sched_fence_free - free up the fence memory
 *
 * @rcu: RCU callback head
 *
 * Free up the fence memory after the RCU grace period.
 */
static void amd_sched_fence_free(struct rcu_head *rcu)
{
	struct dma_fence *f = container_of(rcu, struct dma_fence, rcu);
	struct amd_sched_fence *fence = to_amd_sched_fence(f);

	dma_fence_put(fence->parent);
	kmem_cache_free(sched_fence_slab, fence);
}

/**
 * amd_sched_fence_release - callback that fence can be freed
 *
 * @fence: fence
 *
 * This function is called when the reference count becomes zero.
 * It just RCU schedules freeing up the fence.
 */
static void amd_sched_fence_release_scheduled(struct dma_fence *f)
{
	struct amd_sched_fence *fence = to_amd_sched_fence(f);

	call_rcu(&fence->finished.rcu, amd_sched_fence_free);
}

/**
 * amd_sched_fence_release_scheduled - drop extra reference
 *
 * @f: fence
 *
 * Drop the extra reference from the scheduled fence to the base fence.
 */
static void amd_sched_fence_release_finished(struct dma_fence *f)
{
	struct amd_sched_fence *fence = to_amd_sched_fence(f);

	dma_fence_put(&fence->scheduled);
}

const struct dma_fence_ops amd_sched_fence_ops_scheduled = {
	.get_driver_name = amd_sched_fence_get_driver_name,
	.get_timeline_name = amd_sched_fence_get_timeline_name,
	.enable_signaling = amd_sched_fence_enable_signaling,
	.signaled = NULL,
	.wait = dma_fence_default_wait,
	.release = amd_sched_fence_release_scheduled,
};

const struct dma_fence_ops amd_sched_fence_ops_finished = {
	.get_driver_name = amd_sched_fence_get_driver_name,
	.get_timeline_name = amd_sched_fence_get_timeline_name,
	.enable_signaling = amd_sched_fence_enable_signaling,
	.signaled = NULL,
	.wait = dma_fence_default_wait,
	.release = amd_sched_fence_release_finished,
};
