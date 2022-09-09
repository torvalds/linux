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
 */

#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/wait.h>

#include <drm/gpu_scheduler.h>

static struct kmem_cache *sched_fence_slab;

static int __init drm_sched_fence_slab_init(void)
{
	sched_fence_slab = kmem_cache_create(
		"drm_sched_fence", sizeof(struct drm_sched_fence), 0,
		SLAB_HWCACHE_ALIGN, NULL);
	if (!sched_fence_slab)
		return -ENOMEM;

	return 0;
}

static void __exit drm_sched_fence_slab_fini(void)
{
	rcu_barrier();
	kmem_cache_destroy(sched_fence_slab);
}

void drm_sched_fence_scheduled(struct drm_sched_fence *fence)
{
	dma_fence_signal(&fence->scheduled);
}

void drm_sched_fence_finished(struct drm_sched_fence *fence)
{
	dma_fence_signal(&fence->finished);
}

static const char *drm_sched_fence_get_driver_name(struct dma_fence *fence)
{
	return "drm_sched";
}

static const char *drm_sched_fence_get_timeline_name(struct dma_fence *f)
{
	struct drm_sched_fence *fence = to_drm_sched_fence(f);
	return (const char *)fence->sched->name;
}

static void drm_sched_fence_free_rcu(struct rcu_head *rcu)
{
	struct dma_fence *f = container_of(rcu, struct dma_fence, rcu);
	struct drm_sched_fence *fence = to_drm_sched_fence(f);

	if (!WARN_ON_ONCE(!fence))
		kmem_cache_free(sched_fence_slab, fence);
}

/**
 * drm_sched_fence_free - free up an uninitialized fence
 *
 * @fence: fence to free
 *
 * Free up the fence memory. Should only be used if drm_sched_fence_init()
 * has not been called yet.
 */
void drm_sched_fence_free(struct drm_sched_fence *fence)
{
	/* This function should not be called if the fence has been initialized. */
	if (!WARN_ON_ONCE(fence->sched))
		kmem_cache_free(sched_fence_slab, fence);
}

/**
 * drm_sched_fence_release_scheduled - callback that fence can be freed
 *
 * @f: fence
 *
 * This function is called when the reference count becomes zero.
 * It just RCU schedules freeing up the fence.
 */
static void drm_sched_fence_release_scheduled(struct dma_fence *f)
{
	struct drm_sched_fence *fence = to_drm_sched_fence(f);

	dma_fence_put(fence->parent);
	call_rcu(&fence->finished.rcu, drm_sched_fence_free_rcu);
}

/**
 * drm_sched_fence_release_finished - drop extra reference
 *
 * @f: fence
 *
 * Drop the extra reference from the scheduled fence to the base fence.
 */
static void drm_sched_fence_release_finished(struct dma_fence *f)
{
	struct drm_sched_fence *fence = to_drm_sched_fence(f);

	dma_fence_put(&fence->scheduled);
}

static const struct dma_fence_ops drm_sched_fence_ops_scheduled = {
	.get_driver_name = drm_sched_fence_get_driver_name,
	.get_timeline_name = drm_sched_fence_get_timeline_name,
	.release = drm_sched_fence_release_scheduled,
};

static const struct dma_fence_ops drm_sched_fence_ops_finished = {
	.get_driver_name = drm_sched_fence_get_driver_name,
	.get_timeline_name = drm_sched_fence_get_timeline_name,
	.release = drm_sched_fence_release_finished,
};

struct drm_sched_fence *to_drm_sched_fence(struct dma_fence *f)
{
	if (f->ops == &drm_sched_fence_ops_scheduled)
		return container_of(f, struct drm_sched_fence, scheduled);

	if (f->ops == &drm_sched_fence_ops_finished)
		return container_of(f, struct drm_sched_fence, finished);

	return NULL;
}
EXPORT_SYMBOL(to_drm_sched_fence);

struct drm_sched_fence *drm_sched_fence_alloc(struct drm_sched_entity *entity,
					      void *owner)
{
	struct drm_sched_fence *fence = NULL;

	fence = kmem_cache_zalloc(sched_fence_slab, GFP_KERNEL);
	if (fence == NULL)
		return NULL;

	fence->owner = owner;
	spin_lock_init(&fence->lock);

	return fence;
}

void drm_sched_fence_init(struct drm_sched_fence *fence,
			  struct drm_sched_entity *entity)
{
	unsigned seq;

	fence->sched = entity->rq->sched;
	seq = atomic_inc_return(&entity->fence_seq);
	dma_fence_init(&fence->scheduled, &drm_sched_fence_ops_scheduled,
		       &fence->lock, entity->fence_context, seq);
	dma_fence_init(&fence->finished, &drm_sched_fence_ops_finished,
		       &fence->lock, entity->fence_context + 1, seq);
}

module_init(drm_sched_fence_slab_init);
module_exit(drm_sched_fence_slab_fini);

MODULE_DESCRIPTION("DRM GPU scheduler");
MODULE_LICENSE("GPL and additional rights");
