// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_sched_job.h"

#include <drm/xe_drm.h>
#include <linux/dma-fence-chain.h>
#include <linux/slab.h>

#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_fence.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_pm.h"
#include "xe_sync_types.h"
#include "xe_trace.h"
#include "xe_vm.h"

static struct kmem_cache *xe_sched_job_slab;
static struct kmem_cache *xe_sched_job_parallel_slab;

int __init xe_sched_job_module_init(void)
{
	xe_sched_job_slab =
		kmem_cache_create("xe_sched_job",
				  sizeof(struct xe_sched_job) +
				  sizeof(struct xe_job_ptrs), 0,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!xe_sched_job_slab)
		return -ENOMEM;

	xe_sched_job_parallel_slab =
		kmem_cache_create("xe_sched_job_parallel",
				  sizeof(struct xe_sched_job) +
				  sizeof(struct xe_job_ptrs) *
				  XE_HW_ENGINE_MAX_INSTANCE, 0,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!xe_sched_job_parallel_slab) {
		kmem_cache_destroy(xe_sched_job_slab);
		return -ENOMEM;
	}

	return 0;
}

void xe_sched_job_module_exit(void)
{
	kmem_cache_destroy(xe_sched_job_slab);
	kmem_cache_destroy(xe_sched_job_parallel_slab);
}

static struct xe_sched_job *job_alloc(bool parallel)
{
	return kmem_cache_zalloc(parallel ? xe_sched_job_parallel_slab :
				 xe_sched_job_slab, GFP_KERNEL);
}

bool xe_sched_job_is_migration(struct xe_exec_queue *q)
{
	return q->vm && (q->vm->flags & XE_VM_FLAG_MIGRATION);
}

static void job_free(struct xe_sched_job *job)
{
	struct xe_exec_queue *q = job->q;
	bool is_migration = xe_sched_job_is_migration(q);

	kmem_cache_free(xe_exec_queue_is_parallel(job->q) || is_migration ?
			xe_sched_job_parallel_slab : xe_sched_job_slab, job);
}

static struct xe_device *job_to_xe(struct xe_sched_job *job)
{
	return gt_to_xe(job->q->gt);
}

/* Free unused pre-allocated fences */
static void xe_sched_job_free_fences(struct xe_sched_job *job)
{
	int i;

	for (i = 0; i < job->q->width; ++i) {
		struct xe_job_ptrs *ptrs = &job->ptrs[i];

		if (ptrs->lrc_fence)
			xe_lrc_free_seqno_fence(ptrs->lrc_fence);
		if (ptrs->chain_fence)
			dma_fence_chain_free(ptrs->chain_fence);
	}
}

struct xe_sched_job *xe_sched_job_create(struct xe_exec_queue *q,
					 u64 *batch_addr)
{
	bool is_migration = xe_sched_job_is_migration(q);
	struct xe_sched_job *job;
	int err;
	int i;
	u32 width;

	/* only a kernel context can submit a vm-less job */
	XE_WARN_ON(!q->vm && !(q->flags & EXEC_QUEUE_FLAG_KERNEL));

	job = job_alloc(xe_exec_queue_is_parallel(q) || is_migration);
	if (!job)
		return ERR_PTR(-ENOMEM);

	job->q = q;
	kref_init(&job->refcount);
	xe_exec_queue_get(job->q);

	err = drm_sched_job_init(&job->drm, q->entity, 1, NULL);
	if (err)
		goto err_free;

	for (i = 0; i < q->width; ++i) {
		struct dma_fence *fence = xe_lrc_alloc_seqno_fence();
		struct dma_fence_chain *chain;

		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto err_sched_job;
		}
		job->ptrs[i].lrc_fence = fence;

		if (i + 1 == q->width)
			continue;

		chain = dma_fence_chain_alloc();
		if (!chain) {
			err = -ENOMEM;
			goto err_sched_job;
		}
		job->ptrs[i].chain_fence = chain;
	}

	width = q->width;
	if (is_migration)
		width = 2;

	for (i = 0; i < width; ++i)
		job->ptrs[i].batch_addr = batch_addr[i];

	xe_pm_runtime_get_noresume(job_to_xe(job));
	trace_xe_sched_job_create(job);
	return job;

err_sched_job:
	xe_sched_job_free_fences(job);
	drm_sched_job_cleanup(&job->drm);
err_free:
	xe_exec_queue_put(q);
	job_free(job);
	return ERR_PTR(err);
}

/**
 * xe_sched_job_destroy - Destroy XE schedule job
 * @ref: reference to XE schedule job
 *
 * Called when ref == 0, drop a reference to job's xe_engine + fence, cleanup
 * base DRM schedule job, and free memory for XE schedule job.
 */
void xe_sched_job_destroy(struct kref *ref)
{
	struct xe_sched_job *job =
		container_of(ref, struct xe_sched_job, refcount);
	struct xe_device *xe = job_to_xe(job);

	xe_sched_job_free_fences(job);
	xe_exec_queue_put(job->q);
	dma_fence_put(job->fence);
	drm_sched_job_cleanup(&job->drm);
	job_free(job);
	xe_pm_runtime_put(xe);
}

/* Set the error status under the fence to avoid racing with signaling */
static bool xe_fence_set_error(struct dma_fence *fence, int error)
{
	unsigned long irq_flags;
	bool signaled;

	spin_lock_irqsave(fence->lock, irq_flags);
	signaled = test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags);
	if (!signaled)
		dma_fence_set_error(fence, error);
	spin_unlock_irqrestore(fence->lock, irq_flags);

	return signaled;
}

void xe_sched_job_set_error(struct xe_sched_job *job, int error)
{
	if (xe_fence_set_error(job->fence, error))
		return;

	if (dma_fence_is_chain(job->fence)) {
		struct dma_fence *iter;

		dma_fence_chain_for_each(iter, job->fence)
			xe_fence_set_error(dma_fence_chain_contained(iter),
					   error);
	}

	trace_xe_sched_job_set_error(job);

	dma_fence_enable_sw_signaling(job->fence);
	xe_hw_fence_irq_run(job->q->fence_irq);
}

bool xe_sched_job_started(struct xe_sched_job *job)
{
	struct xe_lrc *lrc = job->q->lrc[0];

	return !__dma_fence_is_later(xe_sched_job_lrc_seqno(job),
				     xe_lrc_start_seqno(lrc),
				     dma_fence_chain_contained(job->fence)->ops);
}

bool xe_sched_job_completed(struct xe_sched_job *job)
{
	struct xe_lrc *lrc = job->q->lrc[0];

	/*
	 * Can safely check just LRC[0] seqno as that is last seqno written when
	 * parallel handshake is done.
	 */

	return !__dma_fence_is_later(xe_sched_job_lrc_seqno(job),
				     xe_lrc_seqno(lrc),
				     dma_fence_chain_contained(job->fence)->ops);
}

void xe_sched_job_arm(struct xe_sched_job *job)
{
	struct xe_exec_queue *q = job->q;
	struct dma_fence *fence, *prev;
	struct xe_vm *vm = q->vm;
	u64 seqno = 0;
	int i;

	/* Migration and kernel engines have their own locking */
	if (IS_ENABLED(CONFIG_LOCKDEP) &&
	    !(q->flags & (EXEC_QUEUE_FLAG_KERNEL | EXEC_QUEUE_FLAG_VM))) {
		lockdep_assert_held(&q->vm->lock);
		if (!xe_vm_in_lr_mode(q->vm))
			xe_vm_assert_held(q->vm);
	}

	if (vm && !xe_sched_job_is_migration(q) && !xe_vm_in_lr_mode(vm) &&
	    (vm->batch_invalidate_tlb || vm->tlb_flush_seqno != q->tlb_flush_seqno)) {
		xe_vm_assert_held(vm);
		q->tlb_flush_seqno = vm->tlb_flush_seqno;
		job->ring_ops_flush_tlb = true;
	}

	/* Arm the pre-allocated fences */
	for (i = 0; i < q->width; prev = fence, ++i) {
		struct dma_fence_chain *chain;

		fence = job->ptrs[i].lrc_fence;
		xe_lrc_init_seqno_fence(q->lrc[i], fence);
		job->ptrs[i].lrc_fence = NULL;
		if (!i) {
			job->lrc_seqno = fence->seqno;
			continue;
		} else {
			xe_assert(gt_to_xe(q->gt), job->lrc_seqno == fence->seqno);
		}

		chain = job->ptrs[i - 1].chain_fence;
		dma_fence_chain_init(chain, prev, fence, seqno++);
		job->ptrs[i - 1].chain_fence = NULL;
		fence = &chain->base;
	}

	job->fence = fence;
	drm_sched_job_arm(&job->drm);
}

void xe_sched_job_push(struct xe_sched_job *job)
{
	xe_sched_job_get(job);
	trace_xe_sched_job_exec(job);
	drm_sched_entity_push_job(&job->drm);
	xe_sched_job_put(job);
}

/**
 * xe_sched_job_last_fence_add_dep - Add last fence dependency to job
 * @job:job to add the last fence dependency to
 * @vm: virtual memory job belongs to
 *
 * Returns:
 * 0 on success, or an error on failing to expand the array.
 */
int xe_sched_job_last_fence_add_dep(struct xe_sched_job *job, struct xe_vm *vm)
{
	struct dma_fence *fence;

	fence = xe_exec_queue_last_fence_get(job->q, vm);

	return drm_sched_job_add_dependency(&job->drm, fence);
}

/**
 * xe_sched_job_init_user_fence - Initialize user_fence for the job
 * @job: job whose user_fence needs an init
 * @sync: sync to be use to init user_fence
 */
void xe_sched_job_init_user_fence(struct xe_sched_job *job,
				  struct xe_sync_entry *sync)
{
	if (sync->type != DRM_XE_SYNC_TYPE_USER_FENCE)
		return;

	job->user_fence.used = true;
	job->user_fence.addr = sync->addr;
	job->user_fence.value = sync->timeline_value;
}

struct xe_sched_job_snapshot *
xe_sched_job_snapshot_capture(struct xe_sched_job *job)
{
	struct xe_exec_queue *q = job->q;
	struct xe_device *xe = q->gt->tile->xe;
	struct xe_sched_job_snapshot *snapshot;
	size_t len = sizeof(*snapshot) + (sizeof(u64) * q->width);
	u16 i;

	snapshot = kzalloc(len, GFP_ATOMIC);
	if (!snapshot)
		return NULL;

	snapshot->batch_addr_len = q->width;
	for (i = 0; i < q->width; i++)
		snapshot->batch_addr[i] =
			xe_device_uncanonicalize_addr(xe, job->ptrs[i].batch_addr);

	return snapshot;
}

void xe_sched_job_snapshot_free(struct xe_sched_job_snapshot *snapshot)
{
	kfree(snapshot);
}

void
xe_sched_job_snapshot_print(struct xe_sched_job_snapshot *snapshot,
			    struct drm_printer *p)
{
	u16 i;

	if (!snapshot)
		return;

	for (i = 0; i < snapshot->batch_addr_len; i++)
		drm_printf(p, "batch_addr[%u]: 0x%016llx\n", i, snapshot->batch_addr[i]);
}

int xe_sched_job_add_deps(struct xe_sched_job *job, struct dma_resv *resv,
			  enum dma_resv_usage usage)
{
	return drm_sched_job_add_resv_dependencies(&job->drm, resv, usage);
}
