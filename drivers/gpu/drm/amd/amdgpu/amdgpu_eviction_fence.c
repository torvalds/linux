// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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
#include <linux/sched.h>
#include <drm/drm_exec.h>
#include "amdgpu.h"

static const char *
amdgpu_eviction_fence_get_driver_name(struct dma_fence *fence)
{
	return "amdgpu_eviction_fence";
}

static const char *
amdgpu_eviction_fence_get_timeline_name(struct dma_fence *f)
{
	struct amdgpu_eviction_fence *ef;

	ef = container_of(f, struct amdgpu_eviction_fence, base);
	return ef->timeline_name;
}

static bool amdgpu_eviction_fence_enable_signaling(struct dma_fence *f)
{
	struct amdgpu_eviction_fence *ev_fence = to_ev_fence(f);

	schedule_work(&ev_fence->evf_mgr->suspend_work);
	return true;
}

static const struct dma_fence_ops amdgpu_eviction_fence_ops = {
	.get_driver_name = amdgpu_eviction_fence_get_driver_name,
	.get_timeline_name = amdgpu_eviction_fence_get_timeline_name,
	.enable_signaling = amdgpu_eviction_fence_enable_signaling,
};

static void
amdgpu_eviction_fence_suspend_worker(struct work_struct *work)
{
	struct amdgpu_eviction_fence_mgr *evf_mgr =
		container_of(work, struct amdgpu_eviction_fence_mgr,
			     suspend_work);
	struct amdgpu_fpriv *fpriv =
		container_of(evf_mgr, struct amdgpu_fpriv, evf_mgr);
	struct amdgpu_userq_mgr *uq_mgr = &fpriv->userq_mgr;
	struct dma_fence *ev_fence;
	bool cookie;

	mutex_lock(&uq_mgr->userq_mutex);

	/*
	 * This is intentionally after taking the userq_mutex since we do
	 * allocate memory while holding this lock, but only after ensuring that
	 * the eviction fence is signaled.
	 */
	cookie = dma_fence_begin_signalling();

	ev_fence = amdgpu_evf_mgr_get_fence(evf_mgr);
	amdgpu_userq_evict(uq_mgr);

	/*
	 * Signaling the eviction fence must be done while holding the
	 * userq_mutex. Otherwise we won't resume the queues before issuing the
	 * next fence.
	 */
	dma_fence_signal(ev_fence);
	dma_fence_end_signalling(cookie);
	dma_fence_put(ev_fence);

	if (!evf_mgr->shutdown)
		schedule_delayed_work(&uq_mgr->resume_work, 0);

	mutex_unlock(&uq_mgr->userq_mutex);
}

int amdgpu_evf_mgr_attach_fence(struct amdgpu_eviction_fence_mgr *evf_mgr,
				struct amdgpu_bo *bo)
{
	struct dma_fence *ev_fence = amdgpu_evf_mgr_get_fence(evf_mgr);
	struct ttm_operation_ctx ctx = { false, false };
	struct dma_resv *resv = bo->tbo.base.resv;
	int ret;

	if (!dma_fence_is_signaled(ev_fence)) {

		amdgpu_bo_placement_from_domain(bo, bo->allowed_domains);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
		if (!ret)
			dma_resv_add_fence(resv, ev_fence,
					   DMA_RESV_USAGE_BOOKKEEP);
	} else {
		ret = 0;
	}

	dma_fence_put(ev_fence);
	return ret;
}

int amdgpu_evf_mgr_rearm(struct amdgpu_eviction_fence_mgr *evf_mgr,
			 struct drm_exec *exec)
{
	struct amdgpu_eviction_fence *ev_fence;
	struct drm_gem_object *obj;
	unsigned long index;

	/* Create and initialize a new eviction fence */
	ev_fence = kzalloc_obj(*ev_fence);
	if (!ev_fence)
		return -ENOMEM;

	ev_fence->evf_mgr = evf_mgr;
	get_task_comm(ev_fence->timeline_name, current);
	spin_lock_init(&ev_fence->lock);
	dma_fence_init64(&ev_fence->base, &amdgpu_eviction_fence_ops,
			 &ev_fence->lock, evf_mgr->ev_fence_ctx,
			 atomic_inc_return(&evf_mgr->ev_fence_seq));

	/* Remember it for newly added BOs */
	dma_fence_put(evf_mgr->ev_fence);
	evf_mgr->ev_fence = &ev_fence->base;

	/* And add it to all existing BOs */
	drm_exec_for_each_locked_object(exec, index, obj) {
		struct amdgpu_bo *bo = gem_to_amdgpu_bo(obj);

		amdgpu_evf_mgr_attach_fence(evf_mgr, bo);
	}
	return 0;
}

void amdgpu_evf_mgr_detach_fence(struct amdgpu_eviction_fence_mgr *evf_mgr,
				 struct amdgpu_bo *bo)
{
	struct dma_fence *stub = dma_fence_get_stub();

	dma_resv_replace_fences(bo->tbo.base.resv, evf_mgr->ev_fence_ctx,
				stub, DMA_RESV_USAGE_BOOKKEEP);
	dma_fence_put(stub);
}

void amdgpu_evf_mgr_init(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	atomic_set(&evf_mgr->ev_fence_seq, 0);
	evf_mgr->ev_fence_ctx = dma_fence_context_alloc(1);
	evf_mgr->ev_fence = dma_fence_get_stub();

	INIT_WORK(&evf_mgr->suspend_work, amdgpu_eviction_fence_suspend_worker);
}

void amdgpu_evf_mgr_shutdown(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	evf_mgr->shutdown = true;
	/* Make sure that the shutdown is visible to the suspend work */
	flush_work(&evf_mgr->suspend_work);
}

void amdgpu_evf_mgr_flush_suspend(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	dma_fence_wait(rcu_dereference_protected(evf_mgr->ev_fence, true),
		       false);
	/* Make sure that we are done with the last suspend work */
	flush_work(&evf_mgr->suspend_work);
}

void amdgpu_evf_mgr_fini(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	dma_fence_put(evf_mgr->ev_fence);
}
