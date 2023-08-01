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

bool xe_sched_job_is_migration(struct xe_engine *e)
{
	return e->vm && (e->vm->flags & XE_VM_FLAG_MIGRATION) &&
		!(e->flags & ENGINE_FLAG_WA);
}

static void job_free(struct xe_sched_job *job)
{
	struct xe_engine *e = job->engine;
	bool is_migration = xe_sched_job_is_migration(e);

	kmem_cache_free(xe_engine_is_parallel(job->engine) || is_migration ?
			xe_sched_job_parallel_slab : xe_sched_job_slab, job);
}

static struct xe_device *job_to_xe(struct xe_sched_job *job)
{
	return gt_to_xe(job->engine->gt);
}

struct xe_sched_job *xe_sched_job_create(struct xe_engine *e,
					 u64 *batch_addr)
{
	struct xe_sched_job *job;
	struct dma_fence **fences;
	bool is_migration = xe_sched_job_is_migration(e);
	int err;
	int i, j;
	u32 width;

	/* Migration and kernel engines have their own locking */
	if (!(e->flags & (ENGINE_FLAG_KERNEL | ENGINE_FLAG_VM |
			  ENGINE_FLAG_WA))) {
		lockdep_assert_held(&e->vm->lock);
		if (!xe_vm_no_dma_fences(e->vm))
			xe_vm_assert_held(e->vm);
	}

	job = job_alloc(xe_engine_is_parallel(e) || is_migration);
	if (!job)
		return ERR_PTR(-ENOMEM);

	job->engine = e;
	kref_init(&job->refcount);
	xe_engine_get(job->engine);

	err = drm_sched_job_init(&job->drm, e->entity, 1, NULL);
	if (err)
		goto err_free;

	if (!xe_engine_is_parallel(e)) {
		job->fence = xe_lrc_create_seqno_fence(e->lrc);
		if (IS_ERR(job->fence)) {
			err = PTR_ERR(job->fence);
			goto err_sched_job;
		}
	} else {
		struct dma_fence_array *cf;

		fences = kmalloc_array(e->width, sizeof(*fences), GFP_KERNEL);
		if (!fences) {
			err = -ENOMEM;
			goto err_sched_job;
		}

		for (j = 0; j < e->width; ++j) {
			fences[j] = xe_lrc_create_seqno_fence(e->lrc + j);
			if (IS_ERR(fences[j])) {
				err = PTR_ERR(fences[j]);
				goto err_fences;
			}
		}

		cf = dma_fence_array_create(e->width, fences,
					    e->parallel.composite_fence_ctx,
					    e->parallel.composite_fence_seqno++,
					    false);
		if (!cf) {
			--e->parallel.composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}

		/* Sanity check */
		for (j = 0; j < e->width; ++j)
			XE_WARN_ON(cf->base.seqno != fences[j]->seqno);

		job->fence = &cf->base;
	}

	width = e->width;
	if (is_migration)
		width = 2;

	for (i = 0; i < width; ++i)
		job->batch_addr[i] = batch_addr[i];

	/* All other jobs require a VM to be open which has a ref */
	if (unlikely(e->flags & ENGINE_FLAG_KERNEL))
		xe_device_mem_access_get(job_to_xe(job));
	xe_device_assert_mem_access(job_to_xe(job));

	trace_xe_sched_job_create(job);
	return job;

err_fences:
	for (j = j - 1; j >= 0; --j) {
		--e->lrc[j].fence_ctx.next_seqno;
		dma_fence_put(fences[j]);
	}
	kfree(fences);
err_sched_job:
	drm_sched_job_cleanup(&job->drm);
err_free:
	xe_engine_put(e);
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

	if (unlikely(job->engine->flags & ENGINE_FLAG_KERNEL))
		xe_device_mem_access_put(job_to_xe(job));
	xe_engine_put(job->engine);
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
	xe_hw_fence_irq_run(job->engine->fence_irq);
}

bool xe_sched_job_started(struct xe_sched_job *job)
{
	struct xe_lrc *lrc = job->engine->lrc;

	return !__dma_fence_is_later(xe_sched_job_seqno(job),
				     xe_lrc_start_seqno(lrc),
				     job->fence->ops);
}

bool xe_sched_job_completed(struct xe_sched_job *job)
{
	struct xe_lrc *lrc = job->engine->lrc;

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
