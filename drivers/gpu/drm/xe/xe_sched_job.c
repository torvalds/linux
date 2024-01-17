// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_sched_job.h"

#include <linux/dma-fence-array.h>
#include <linux/slab.h>

#include "xe_device.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_hw_engine_types.h"
#include "xe_hw_fence.h"
#include "xe_lrc.h"
#include "xe_macros.h"
#include "xe_trace.h"
#include "xe_vm.h"

static struct kmem_cache *xe_sched_job_slab;
static struct kmem_cache *xe_sched_job_parallel_slab;

int __init xe_sched_job_module_init(void)
{
	xe_sched_job_slab =
		kmem_cache_create("xe_sched_job",
				  sizeof(struct xe_sched_job) +
				  sizeof(u64), 0,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!xe_sched_job_slab)
		return -ENOMEM;

	xe_sched_job_parallel_slab =
		kmem_cache_create("xe_sched_job_parallel",
				  sizeof(struct xe_sched_job) +
				  sizeof(u64) *
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

struct xe_sched_job *xe_sched_job_create(struct xe_exec_queue *q,
					 u64 *batch_addr)
{
	struct xe_sched_job *job;
	struct dma_fence **fences;
	bool is_migration = xe_sched_job_is_migration(q);
	int err;
	int i, j;
	u32 width;

	/* only a kernel context can submit a vm-less job */
	XE_WARN_ON(!q->vm && !(q->flags & EXEC_QUEUE_FLAG_KERNEL));

	/* Migration and kernel engines have their own locking */
	if (!(q->flags & (EXEC_QUEUE_FLAG_KERNEL | EXEC_QUEUE_FLAG_VM))) {
		lockdep_assert_held(&q->vm->lock);
		if (!xe_vm_in_lr_mode(q->vm))
			xe_vm_assert_held(q->vm);
	}

	job = job_alloc(xe_exec_queue_is_parallel(q) || is_migration);
	if (!job)
		return ERR_PTR(-ENOMEM);

	job->q = q;
	kref_init(&job->refcount);
	xe_exec_queue_get(job->q);

	err = drm_sched_job_init(&job->drm, q->entity, 1, NULL);
	if (err)
		goto err_free;

	if (!xe_exec_queue_is_parallel(q)) {
		job->fence = xe_lrc_create_seqno_fence(q->lrc);
		if (IS_ERR(job->fence)) {
			err = PTR_ERR(job->fence);
			goto err_sched_job;
		}
	} else {
		struct dma_fence_array *cf;

		fences = kmalloc_array(q->width, sizeof(*fences), GFP_KERNEL);
		if (!fences) {
			err = -ENOMEM;
			goto err_sched_job;
		}

		for (j = 0; j < q->width; ++j) {
			fences[j] = xe_lrc_create_seqno_fence(q->lrc + j);
			if (IS_ERR(fences[j])) {
				err = PTR_ERR(fences[j]);
				goto err_fences;
			}
		}

		cf = dma_fence_array_create(q->width, fences,
					    q->parallel.composite_fence_ctx,
					    q->parallel.composite_fence_seqno++,
					    false);
		if (!cf) {
			--q->parallel.composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}

		/* Sanity check */
		for (j = 0; j < q->width; ++j)
			xe_assert(job_to_xe(job), cf->base.seqno == fences[j]->seqno);

		job->fence = &cf->base;
	}

	width = q->width;
	if (is_migration)
		width = 2;

	for (i = 0; i < width; ++i)
		job->batch_addr[i] = batch_addr[i];

	/* All other jobs require a VM to be open which has a ref */
	if (unlikely(q->flags & EXEC_QUEUE_FLAG_KERNEL))
		xe_device_mem_access_get(job_to_xe(job));
	xe_device_assert_mem_access(job_to_xe(job));

	trace_xe_sched_job_create(job);
	return job;

err_fences:
	for (j = j - 1; j >= 0; --j) {
		--q->lrc[j].fence_ctx.next_seqno;
		dma_fence_put(fences[j]);
	}
	kfree(fences);
err_sched_job:
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

	if (unlikely(job->q->flags & EXEC_QUEUE_FLAG_KERNEL))
		xe_device_mem_access_put(job_to_xe(job));
	xe_exec_queue_put(job->q);
	dma_fence_put(job->fence);
	drm_sched_job_cleanup(&job->drm);
	job_free(job);
}

void xe_sched_job_set_error(struct xe_sched_job *job, int error)
{
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &job->fence->flags))
		return;

	dma_fence_set_error(job->fence, error);

	if (dma_fence_is_array(job->fence)) {
		struct dma_fence_array *array =
			to_dma_fence_array(job->fence);
		struct dma_fence **child = array->fences;
		unsigned int nchild = array->num_fences;

		do {
			struct dma_fence *current_fence = *child++;

			if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				     &current_fence->flags))
				continue;
			dma_fence_set_error(current_fence, error);
		} while (--nchild);
	}

	trace_xe_sched_job_set_error(job);

	dma_fence_enable_sw_signaling(job->fence);
	xe_hw_fence_irq_run(job->q->fence_irq);
}

bool xe_sched_job_started(struct xe_sched_job *job)
{
	struct xe_lrc *lrc = job->q->lrc;

	return !__dma_fence_is_later(xe_sched_job_seqno(job),
				     xe_lrc_start_seqno(lrc),
				     job->fence->ops);
}

bool xe_sched_job_completed(struct xe_sched_job *job)
{
	struct xe_lrc *lrc = job->q->lrc;

	/*
	 * Can safely check just LRC[0] seqno as that is last seqno written when
	 * parallel handshake is done.
	 */

	return !__dma_fence_is_later(xe_sched_job_seqno(job), xe_lrc_seqno(lrc),
				     job->fence->ops);
}

void xe_sched_job_arm(struct xe_sched_job *job)
{
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
	dma_fence_get(fence);

	return drm_sched_job_add_dependency(&job->drm, fence);
}
