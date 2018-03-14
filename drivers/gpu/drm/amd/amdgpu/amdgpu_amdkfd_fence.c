/*
 * Copyright 2016-2018 Advanced Micro Devices, Inc.
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
 */

#include <linux/dma-fence.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/stacktrace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include "amdgpu_amdkfd.h"

static const struct dma_fence_ops amdkfd_fence_ops;
static atomic_t fence_seq = ATOMIC_INIT(0);

/* Eviction Fence
 * Fence helper functions to deal with KFD memory eviction.
 * Big Idea - Since KFD submissions are done by user queues, a BO cannot be
 *  evicted unless all the user queues for that process are evicted.
 *
 * All the BOs in a process share an eviction fence. When process X wants
 * to map VRAM memory but TTM can't find enough space, TTM will attempt to
 * evict BOs from its LRU list. TTM checks if the BO is valuable to evict
 * by calling ttm_bo_driver->eviction_valuable().
 *
 * ttm_bo_driver->eviction_valuable() - will return false if the BO belongs
 *  to process X. Otherwise, it will return true to indicate BO can be
 *  evicted by TTM.
 *
 * If ttm_bo_driver->eviction_valuable returns true, then TTM will continue
 * the evcition process for that BO by calling ttm_bo_evict --> amdgpu_bo_move
 * --> amdgpu_copy_buffer(). This sets up job in GPU scheduler.
 *
 * GPU Scheduler (amd_sched_main) - sets up a cb (fence_add_callback) to
 *  nofity when the BO is free to move. fence_add_callback --> enable_signaling
 *  --> amdgpu_amdkfd_fence.enable_signaling
 *
 * amdgpu_amdkfd_fence.enable_signaling - Start a work item that will quiesce
 * user queues and signal fence. The work item will also start another delayed
 * work item to restore BOs
 */

struct amdgpu_amdkfd_fence *amdgpu_amdkfd_fence_create(u64 context,
						       struct mm_struct *mm)
{
	struct amdgpu_amdkfd_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (fence == NULL)
		return NULL;

	/* This reference gets released in amdkfd_fence_release */
	mmgrab(mm);
	fence->mm = mm;
	get_task_comm(fence->timeline_name, current);
	spin_lock_init(&fence->lock);

	dma_fence_init(&fence->base, &amdkfd_fence_ops, &fence->lock,
		   context, atomic_inc_return(&fence_seq));

	return fence;
}

struct amdgpu_amdkfd_fence *to_amdgpu_amdkfd_fence(struct dma_fence *f)
{
	struct amdgpu_amdkfd_fence *fence;

	if (!f)
		return NULL;

	fence = container_of(f, struct amdgpu_amdkfd_fence, base);
	if (fence && f->ops == &amdkfd_fence_ops)
		return fence;

	return NULL;
}

static const char *amdkfd_fence_get_driver_name(struct dma_fence *f)
{
	return "amdgpu_amdkfd_fence";
}

static const char *amdkfd_fence_get_timeline_name(struct dma_fence *f)
{
	struct amdgpu_amdkfd_fence *fence = to_amdgpu_amdkfd_fence(f);

	return fence->timeline_name;
}

/**
 * amdkfd_fence_enable_signaling - This gets called when TTM wants to evict
 *  a KFD BO and schedules a job to move the BO.
 *  If fence is already signaled return true.
 *  If fence is not signaled schedule a evict KFD process work item.
 */
static bool amdkfd_fence_enable_signaling(struct dma_fence *f)
{
	struct amdgpu_amdkfd_fence *fence = to_amdgpu_amdkfd_fence(f);

	if (!fence)
		return false;

	if (dma_fence_is_signaled(f))
		return true;

	if (!kgd2kfd->schedule_evict_and_restore_process(fence->mm, f))
		return true;

	return false;
}

/**
 * amdkfd_fence_release - callback that fence can be freed
 *
 * @fence: fence
 *
 * This function is called when the reference count becomes zero.
 * Drops the mm_struct reference and RCU schedules freeing up the fence.
 */
static void amdkfd_fence_release(struct dma_fence *f)
{
	struct amdgpu_amdkfd_fence *fence = to_amdgpu_amdkfd_fence(f);

	/* Unconditionally signal the fence. The process is getting
	 * terminated.
	 */
	if (WARN_ON(!fence))
		return; /* Not an amdgpu_amdkfd_fence */

	mmdrop(fence->mm);
	kfree_rcu(f, rcu);
}

/**
 * amdkfd_fence_check_mm - Check if @mm is same as that of the fence @f
 *  if same return TRUE else return FALSE.
 *
 * @f: [IN] fence
 * @mm: [IN] mm that needs to be verified
 */
bool amdkfd_fence_check_mm(struct dma_fence *f, struct mm_struct *mm)
{
	struct amdgpu_amdkfd_fence *fence = to_amdgpu_amdkfd_fence(f);

	if (!fence)
		return false;
	else if (fence->mm == mm)
		return true;

	return false;
}

static const struct dma_fence_ops amdkfd_fence_ops = {
	.get_driver_name = amdkfd_fence_get_driver_name,
	.get_timeline_name = amdkfd_fence_get_timeline_name,
	.enable_signaling = amdkfd_fence_enable_signaling,
	.signaled = NULL,
	.wait = dma_fence_default_wait,
	.release = amdkfd_fence_release,
};
