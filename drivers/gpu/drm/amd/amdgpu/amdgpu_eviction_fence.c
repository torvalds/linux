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
#include "amdgpu.h"

static const char *
amdgpu_eviction_fence_get_driver_name(struct dma_fence *fence)
{
	return "amdgpu";
}

static const char *
amdgpu_eviction_fence_get_timeline_name(struct dma_fence *f)
{
	struct amdgpu_eviction_fence *ef;

	ef = container_of(f, struct amdgpu_eviction_fence, base);
	return ef->timeline_name;
}

static const struct dma_fence_ops amdgpu_eviction_fence_ops = {
	.use_64bit_seqno = true,
	.get_driver_name = amdgpu_eviction_fence_get_driver_name,
	.get_timeline_name = amdgpu_eviction_fence_get_timeline_name,
};

void amdgpu_eviction_fence_signal(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	spin_lock(&evf_mgr->ev_fence_lock);
	dma_fence_signal(&evf_mgr->ev_fence->base);
	spin_unlock(&evf_mgr->ev_fence_lock);
}

struct amdgpu_eviction_fence *
amdgpu_eviction_fence_create(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	struct amdgpu_eviction_fence *ev_fence;

	ev_fence = kzalloc(sizeof(*ev_fence), GFP_KERNEL);
	if (!ev_fence)
		return NULL;

	ev_fence->evf_mgr = evf_mgr;
	get_task_comm(ev_fence->timeline_name, current);
	spin_lock_init(&ev_fence->lock);
	dma_fence_init(&ev_fence->base, &amdgpu_eviction_fence_ops,
		       &ev_fence->lock, evf_mgr->ev_fence_ctx,
		       atomic_inc_return(&evf_mgr->ev_fence_seq));
	return ev_fence;
}

void amdgpu_eviction_fence_destroy(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	struct amdgpu_eviction_fence *ev_fence;

	spin_lock(&evf_mgr->ev_fence_lock);
	ev_fence = evf_mgr->ev_fence;
	spin_unlock(&evf_mgr->ev_fence_lock);

	if (!ev_fence)
		return;

	/* Last unref of ev_fence */
	dma_fence_put(&evf_mgr->ev_fence->base);
}

int amdgpu_eviction_fence_attach(struct amdgpu_eviction_fence_mgr *evf_mgr,
				 struct amdgpu_bo *bo)
{
	struct dma_fence *ef;
	struct amdgpu_eviction_fence *ev_fence;
	struct dma_resv *resv = bo->tbo.base.resv;
	int ret;

	if (!resv)
		return 0;

	ret = dma_resv_reserve_fences(resv, 1);
	if (ret) {
		DRM_DEBUG_DRIVER("Failed to resv fence space\n");
		return ret;
	}

	spin_lock(&evf_mgr->ev_fence_lock);
	ev_fence = evf_mgr->ev_fence;
	if (ev_fence) {
		ef = dma_fence_get(&ev_fence->base);
		dma_resv_add_fence(resv, ef, DMA_RESV_USAGE_BOOKKEEP);
	}
	spin_unlock(&evf_mgr->ev_fence_lock);
	return 0;
}

void amdgpu_eviction_fence_detach(struct amdgpu_eviction_fence_mgr *evf_mgr,
				  struct amdgpu_bo *bo)
{
	struct dma_fence *stub = dma_fence_get_stub();

	dma_resv_replace_fences(bo->tbo.base.resv, evf_mgr->ev_fence_ctx,
				stub, DMA_RESV_USAGE_BOOKKEEP);
	dma_fence_put(stub);
}

int amdgpu_eviction_fence_init(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	struct amdgpu_eviction_fence *ev_fence;

	/* This needs to be done one time per open */
	atomic_set(&evf_mgr->ev_fence_seq, 0);
	evf_mgr->ev_fence_ctx = dma_fence_context_alloc(1);
	spin_lock_init(&evf_mgr->ev_fence_lock);

	ev_fence = amdgpu_eviction_fence_create(evf_mgr);
	if (!ev_fence) {
		DRM_ERROR("Failed to craete eviction fence\n");
		return -ENOMEM;
	}

	spin_lock(&evf_mgr->ev_fence_lock);
	evf_mgr->ev_fence = ev_fence;
	spin_unlock(&evf_mgr->ev_fence_lock);
	return 0;
}
