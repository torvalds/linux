// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_dep_job_types.h"
#include "xe_dep_scheduler.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_gt_tlb_inval_job.h"
#include "xe_migrate.h"
#include "xe_pm.h"

/** struct xe_gt_tlb_inval_job - GT TLB invalidation job */
struct xe_gt_tlb_inval_job {
	/** @dep: base generic dependency Xe job */
	struct xe_dep_job dep;
	/** @gt: GT to invalidate */
	struct xe_gt *gt;
	/** @q: exec queue issuing the invalidate */
	struct xe_exec_queue *q;
	/** @refcount: ref count of this job */
	struct kref refcount;
	/**
	 * @fence: dma fence to indicate completion. 1 way relationship - job
	 * can safely reference fence, fence cannot safely reference job.
	 */
	struct dma_fence *fence;
	/** @start: Start address to invalidate */
	u64 start;
	/** @end: End address to invalidate */
	u64 end;
	/** @asid: Address space ID to invalidate */
	u32 asid;
	/** @fence_armed: Fence has been armed */
	bool fence_armed;
};

static struct dma_fence *xe_gt_tlb_inval_job_run(struct xe_dep_job *dep_job)
{
	struct xe_gt_tlb_inval_job *job =
		container_of(dep_job, typeof(*job), dep);
	struct xe_gt_tlb_invalidation_fence *ifence =
		container_of(job->fence, typeof(*ifence), base);

	xe_gt_tlb_invalidation_range(job->gt, ifence, job->start,
				     job->end, job->asid);

	return job->fence;
}

static void xe_gt_tlb_inval_job_free(struct xe_dep_job *dep_job)
{
	struct xe_gt_tlb_inval_job *job =
		container_of(dep_job, typeof(*job), dep);

	/* Pairs with get in xe_gt_tlb_inval_job_push */
	xe_gt_tlb_inval_job_put(job);
}

static const struct xe_dep_job_ops dep_job_ops = {
	.run_job = xe_gt_tlb_inval_job_run,
	.free_job = xe_gt_tlb_inval_job_free,
};

static int xe_gt_tlb_inval_context(struct xe_gt *gt)
{
	return xe_gt_is_media_type(gt) ? XE_EXEC_QUEUE_TLB_INVAL_MEDIA_GT :
		XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT;
}

/**
 * xe_gt_tlb_inval_job_create() - GT TLB invalidation job create
 * @gt: GT to invalidate
 * @q: exec queue issuing the invalidate
 * @start: Start address to invalidate
 * @end: End address to invalidate
 * @asid: Address space ID to invalidate
 *
 * Create a GT TLB invalidation job and initialize internal fields. The caller is
 * responsible for releasing the creation reference.
 *
 * Return: GT TLB invalidation job object on success, ERR_PTR failure
 */
struct xe_gt_tlb_inval_job *xe_gt_tlb_inval_job_create(struct xe_exec_queue *q,
						       struct xe_gt *gt,
						       u64 start, u64 end,
						       u32 asid)
{
	struct xe_gt_tlb_inval_job *job;
	struct xe_dep_scheduler *dep_scheduler =
		q->tlb_inval[xe_gt_tlb_inval_context(gt)].dep_scheduler;
	struct drm_sched_entity *entity =
		xe_dep_scheduler_entity(dep_scheduler);
	struct xe_gt_tlb_invalidation_fence *ifence;
	int err;

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return ERR_PTR(-ENOMEM);

	job->q = q;
	job->gt = gt;
	job->start = start;
	job->end = end;
	job->asid = asid;
	job->fence_armed = false;
	job->dep.ops = &dep_job_ops;
	kref_init(&job->refcount);
	xe_exec_queue_get(q);	/* Pairs with put in xe_gt_tlb_inval_job_destroy */

	ifence = kmalloc(sizeof(*ifence), GFP_KERNEL);
	if (!ifence) {
		err = -ENOMEM;
		goto err_job;
	}
	job->fence = &ifence->base;

	err = drm_sched_job_init(&job->dep.drm, entity, 1, NULL,
				 q->xef ? q->xef->drm->client_id : 0);
	if (err)
		goto err_fence;

	/* Pairs with put in xe_gt_tlb_inval_job_destroy */
	xe_pm_runtime_get_noresume(gt_to_xe(job->gt));

	return job;

err_fence:
	kfree(ifence);
err_job:
	xe_exec_queue_put(q);
	kfree(job);

	return ERR_PTR(err);
}

static void xe_gt_tlb_inval_job_destroy(struct kref *ref)
{
	struct xe_gt_tlb_inval_job *job = container_of(ref, typeof(*job),
						       refcount);
	struct xe_gt_tlb_invalidation_fence *ifence =
		container_of(job->fence, typeof(*ifence), base);
	struct xe_device *xe = gt_to_xe(job->gt);
	struct xe_exec_queue *q = job->q;

	if (!job->fence_armed)
		kfree(ifence);
	else
		/* Ref from xe_gt_tlb_invalidation_fence_init */
		dma_fence_put(job->fence);

	drm_sched_job_cleanup(&job->dep.drm);
	kfree(job);
	xe_exec_queue_put(q);	/* Pairs with get from xe_gt_tlb_inval_job_create */
	xe_pm_runtime_put(xe);	/* Pairs with get from xe_gt_tlb_inval_job_create */
}

/**
 * xe_gt_tlb_inval_alloc_dep() - GT TLB invalidation job alloc dependency
 * @job: GT TLB invalidation job to alloc dependency for
 *
 * Allocate storage for a dependency in the GT TLB invalidation fence. This
 * function should be called at most once per job and must be paired with
 * xe_gt_tlb_inval_job_push being called with a real fence.
 *
 * Return: 0 on success, -errno on failure
 */
int xe_gt_tlb_inval_job_alloc_dep(struct xe_gt_tlb_inval_job *job)
{
	xe_assert(gt_to_xe(job->gt), !xa_load(&job->dep.drm.dependencies, 0));
	might_alloc(GFP_KERNEL);

	return drm_sched_job_add_dependency(&job->dep.drm,
					    dma_fence_get_stub());
}

/**
 * xe_gt_tlb_inval_job_push() - GT TLB invalidation job push
 * @job: GT TLB invalidation job to push
 * @m: The migration object being used
 * @fence: Dependency for GT TLB invalidation job
 *
 * Pushes a GT TLB invalidation job for execution, using @fence as a dependency.
 * Storage for @fence must be preallocated with xe_gt_tlb_inval_job_alloc_dep
 * prior to this call if @fence is not signaled. Takes a reference to the job’s
 * finished fence, which the caller is responsible for releasing, and return it
 * to the caller. This function is safe to be called in the path of reclaim.
 *
 * Return: Job's finished fence on success, cannot fail
 */
struct dma_fence *xe_gt_tlb_inval_job_push(struct xe_gt_tlb_inval_job *job,
					   struct xe_migrate *m,
					   struct dma_fence *fence)
{
	struct xe_gt_tlb_invalidation_fence *ifence =
		container_of(job->fence, typeof(*ifence), base);

	if (!dma_fence_is_signaled(fence)) {
		void *ptr;

		/*
		 * Can be in path of reclaim, hence the preallocation of fence
		 * storage in xe_gt_tlb_inval_job_alloc_dep. Verify caller did
		 * this correctly.
		 */
		xe_assert(gt_to_xe(job->gt),
			  xa_load(&job->dep.drm.dependencies, 0) ==
			  dma_fence_get_stub());

		dma_fence_get(fence);	/* ref released once dependency processed by scheduler */
		ptr = xa_store(&job->dep.drm.dependencies, 0, fence,
			       GFP_ATOMIC);
		xe_assert(gt_to_xe(job->gt), !xa_is_err(ptr));
	}

	xe_gt_tlb_inval_job_get(job);	/* Pairs with put in free_job */
	job->fence_armed = true;

	/*
	 * We need the migration lock to protect the job's seqno and the spsc
	 * queue, only taken on migration queue, user queues protected dma-resv
	 * VM lock.
	 */
	xe_migrate_job_lock(m, job->q);

	/* Creation ref pairs with put in xe_gt_tlb_inval_job_destroy */
	xe_gt_tlb_invalidation_fence_init(job->gt, ifence, false);
	dma_fence_get(job->fence);	/* Pairs with put in DRM scheduler */

	drm_sched_job_arm(&job->dep.drm);
	/*
	 * caller ref, get must be done before job push as it could immediately
	 * signal and free.
	 */
	dma_fence_get(&job->dep.drm.s_fence->finished);
	drm_sched_entity_push_job(&job->dep.drm);

	xe_migrate_job_unlock(m, job->q);

	/*
	 * Not using job->fence, as it has its own dma-fence context, which does
	 * not allow GT TLB invalidation fences on the same queue, GT tuple to
	 * be squashed in dma-resv/DRM scheduler. Instead, we use the DRM scheduler
	 * context and job's finished fence, which enables squashing.
	 */
	return &job->dep.drm.s_fence->finished;
}

/**
 * xe_gt_tlb_inval_job_get() - Get a reference to GT TLB invalidation job
 * @job: GT TLB invalidation job object
 *
 * Increment the GT TLB invalidation job's reference count
 */
void xe_gt_tlb_inval_job_get(struct xe_gt_tlb_inval_job *job)
{
	kref_get(&job->refcount);
}

/**
 * xe_gt_tlb_inval_job_put() - Put a reference to GT TLB invalidation job
 * @job: GT TLB invalidation job object
 *
 * Decrement the GT TLB invalidation job's reference count, call
 * xe_gt_tlb_inval_job_destroy when reference count == 0. Skips decrement if
 * input @job is NULL or IS_ERR.
 */
void xe_gt_tlb_inval_job_put(struct xe_gt_tlb_inval_job *job)
{
	if (!IS_ERR_OR_NULL(job))
		kref_put(&job->refcount, xe_gt_tlb_inval_job_destroy);
}
