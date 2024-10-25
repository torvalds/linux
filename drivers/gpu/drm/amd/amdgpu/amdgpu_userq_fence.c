// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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

#include <linux/kref.h>
#include <linux/slab.h>

#include <drm/drm_syncobj.h>

#include "amdgpu.h"
#include "amdgpu_userq_fence.h"

static const struct dma_fence_ops amdgpu_userq_fence_ops;
static struct kmem_cache *amdgpu_userq_fence_slab;

int amdgpu_userq_fence_slab_init(void)
{
	amdgpu_userq_fence_slab = kmem_cache_create("amdgpu_userq_fence",
						    sizeof(struct amdgpu_userq_fence),
						    0,
						    SLAB_HWCACHE_ALIGN,
						    NULL);
	if (!amdgpu_userq_fence_slab)
		return -ENOMEM;

	return 0;
}

void amdgpu_userq_fence_slab_fini(void)
{
	rcu_barrier();
	kmem_cache_destroy(amdgpu_userq_fence_slab);
}

static inline struct amdgpu_userq_fence *to_amdgpu_userq_fence(struct dma_fence *f)
{
	if (!f || f->ops != &amdgpu_userq_fence_ops)
		return NULL;

	return container_of(f, struct amdgpu_userq_fence, base);
}

static u64 amdgpu_userq_fence_read(struct amdgpu_userq_fence_driver *fence_drv)
{
	return le64_to_cpu(*fence_drv->cpu_addr);
}

int amdgpu_userq_fence_driver_alloc(struct amdgpu_device *adev,
				    struct amdgpu_usermode_queue *userq)
{
	struct amdgpu_userq_fence_driver *fence_drv;
	int r;

	fence_drv = kzalloc(sizeof(*fence_drv), GFP_KERNEL);
	if (!fence_drv) {
		DRM_ERROR("Failed to allocate memory for fence driver\n");
		return -ENOMEM;
	}

	/* Acquire seq64 memory */
	r = amdgpu_seq64_alloc(adev, &fence_drv->gpu_addr,
			       &fence_drv->cpu_addr);
	if (r) {
		kfree(fence_drv);
		return -ENOMEM;
	}

	memset(fence_drv->cpu_addr, 0, sizeof(u64));

	kref_init(&fence_drv->refcount);
	INIT_LIST_HEAD(&fence_drv->fences);
	spin_lock_init(&fence_drv->fence_list_lock);

	fence_drv->adev = adev;
	fence_drv->context = dma_fence_context_alloc(1);
	get_task_comm(fence_drv->timeline_name, current);

	userq->fence_drv = fence_drv;

	return 0;
}

void amdgpu_userq_fence_driver_process(struct amdgpu_userq_fence_driver *fence_drv)
{
	struct amdgpu_userq_fence *userq_fence, *tmp;
	struct dma_fence *fence;
	u64 rptr;

	if (!fence_drv)
		return;

	rptr = amdgpu_userq_fence_read(fence_drv);

	spin_lock(&fence_drv->fence_list_lock);
	list_for_each_entry_safe(userq_fence, tmp, &fence_drv->fences, link) {
		fence = &userq_fence->base;

		if (rptr >= fence->seqno) {
			dma_fence_signal(fence);
			list_del(&userq_fence->link);

			dma_fence_put(fence);
		} else {
			break;
		}
	}
	spin_unlock(&fence_drv->fence_list_lock);
}

void amdgpu_userq_fence_driver_destroy(struct kref *ref)
{
	struct amdgpu_userq_fence_driver *fence_drv = container_of(ref,
					 struct amdgpu_userq_fence_driver,
					 refcount);
	struct amdgpu_device *adev = fence_drv->adev;
	struct amdgpu_userq_fence *fence, *tmp;
	struct dma_fence *f;

	spin_lock(&fence_drv->fence_list_lock);
	list_for_each_entry_safe(fence, tmp, &fence_drv->fences, link) {
		f = &fence->base;

		if (!dma_fence_is_signaled(f)) {
			dma_fence_set_error(f, -ECANCELED);
			dma_fence_signal(f);
		}

		list_del(&fence->link);
		dma_fence_put(f);
	}
	spin_unlock(&fence_drv->fence_list_lock);

	/* Free seq64 memory */
	amdgpu_seq64_free(adev, fence_drv->gpu_addr);
	kfree(fence_drv);
}

void amdgpu_userq_fence_driver_get(struct amdgpu_userq_fence_driver *fence_drv)
{
	kref_get(&fence_drv->refcount);
}

void amdgpu_userq_fence_driver_put(struct amdgpu_userq_fence_driver *fence_drv)
{
	kref_put(&fence_drv->refcount, amdgpu_userq_fence_driver_destroy);
}

int amdgpu_userq_fence_create(struct amdgpu_usermode_queue *userq,
			      u64 seq, struct dma_fence **f)
{
	struct amdgpu_userq_fence_driver *fence_drv;
	struct amdgpu_userq_fence *userq_fence;
	struct dma_fence *fence;
	unsigned long flags;

	fence_drv = userq->fence_drv;
	if (!fence_drv)
		return -EINVAL;

	userq_fence = kmem_cache_alloc(amdgpu_userq_fence_slab, GFP_ATOMIC);
	if (!userq_fence)
		return -ENOMEM;

	spin_lock_init(&userq_fence->lock);
	INIT_LIST_HEAD(&userq_fence->link);
	fence = &userq_fence->base;
	userq_fence->fence_drv = fence_drv;

	dma_fence_init(fence, &amdgpu_userq_fence_ops, &userq_fence->lock,
		       fence_drv->context, seq);

	amdgpu_userq_fence_driver_get(fence_drv);
	dma_fence_get(fence);

	/* Check if hardware has already processed the job */
	spin_lock_irqsave(&fence_drv->fence_list_lock, flags);
	if (!dma_fence_is_signaled_locked(fence))
		list_add_tail(&userq_fence->link, &fence_drv->fences);
	else
		dma_fence_put(fence);

	spin_unlock_irqrestore(&fence_drv->fence_list_lock, flags);

	*f = fence;

	return 0;
}

static const char *amdgpu_userq_fence_get_driver_name(struct dma_fence *f)
{
	return "amdgpu_userqueue_fence";
}

static const char *amdgpu_userq_fence_get_timeline_name(struct dma_fence *f)
{
	struct amdgpu_userq_fence *fence = to_amdgpu_userq_fence(f);

	return fence->fence_drv->timeline_name;
}

static bool amdgpu_userq_fence_signaled(struct dma_fence *f)
{
	struct amdgpu_userq_fence *fence = to_amdgpu_userq_fence(f);
	struct amdgpu_userq_fence_driver *fence_drv = fence->fence_drv;
	u64 rptr, wptr;

	rptr = amdgpu_userq_fence_read(fence_drv);
	wptr = fence->base.seqno;

	if (rptr >= wptr)
		return true;

	return false;
}

static void amdgpu_userq_fence_free(struct rcu_head *rcu)
{
	struct dma_fence *fence = container_of(rcu, struct dma_fence, rcu);
	struct amdgpu_userq_fence *userq_fence = to_amdgpu_userq_fence(fence);
	struct amdgpu_userq_fence_driver *fence_drv = userq_fence->fence_drv;

	/* Release the fence driver reference */
	amdgpu_userq_fence_driver_put(fence_drv);
	kmem_cache_free(amdgpu_userq_fence_slab, userq_fence);
}

static void amdgpu_userq_fence_release(struct dma_fence *f)
{
	call_rcu(&f->rcu, amdgpu_userq_fence_free);
}

static const struct dma_fence_ops amdgpu_userq_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = amdgpu_userq_fence_get_driver_name,
	.get_timeline_name = amdgpu_userq_fence_get_timeline_name,
	.signaled = amdgpu_userq_fence_signaled,
	.release = amdgpu_userq_fence_release,
};
