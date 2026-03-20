/* SPDX-License-Identifier: MIT */
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

#ifndef AMDGPU_EV_FENCE_H_
#define AMDGPU_EV_FENCE_H_

#include <linux/dma-fence.h>

struct amdgpu_eviction_fence {
	struct dma_fence base;
	spinlock_t	 lock;
	char		 timeline_name[TASK_COMM_LEN];
	struct amdgpu_eviction_fence_mgr *evf_mgr;
};

struct amdgpu_eviction_fence_mgr {
	u64			ev_fence_ctx;
	atomic_t		ev_fence_seq;

	/*
	 * Only updated while holding the VM resv lock.
	 * Only signaled while holding the userq mutex.
	 */
	struct dma_fence __rcu	*ev_fence;
	struct work_struct	suspend_work;
	bool			shutdown;
};

static inline struct dma_fence *
amdgpu_evf_mgr_get_fence(struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	struct dma_fence *ev_fence;

	rcu_read_lock();
	ev_fence = dma_fence_get_rcu_safe(&evf_mgr->ev_fence);
	rcu_read_unlock();
	return ev_fence;
}

int amdgpu_evf_mgr_attach_fence(struct amdgpu_eviction_fence_mgr *evf_mgr,
				struct amdgpu_bo *bo);
int amdgpu_evf_mgr_rearm(struct amdgpu_eviction_fence_mgr *evf_mgr,
			 struct drm_exec *exec);
void amdgpu_evf_mgr_detach_fence(struct amdgpu_eviction_fence_mgr *evf_mgr,
				 struct amdgpu_bo *bo);
void amdgpu_evf_mgr_init(struct amdgpu_eviction_fence_mgr *evf_mgr);
void amdgpu_evf_mgr_shutdown(struct amdgpu_eviction_fence_mgr *evf_mgr);
void amdgpu_evf_mgr_flush_suspend(struct amdgpu_eviction_fence_mgr *evf_mgr);
void amdgpu_evf_mgr_fini(struct amdgpu_eviction_fence_mgr *evf_mgr);

#endif
