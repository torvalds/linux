// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include "xe_assert.h"
#include "xe_dep_job_types.h"
#include "xe_dep_scheduler.h"
#include "xe_exec_queue.h"
#include "xe_gt_printk.h"
#include "xe_gt_types.h"
#include "xe_page_reclaim.h"
#include "xe_tlb_inval.h"
#include "xe_tlb_inval_job.h"
#include "xe_migrate.h"
#include "xe_pm.h"
#include "xe_vm.h"

/** struct xe_tlb_inval_job - TLB invalidation job */
struct xe_tlb_inval_job {
	/** @dep: base generic dependency Xe job */
	struct xe_dep_job dep;
	/** @tlb_inval: TLB invalidation client */
	struct xe_tlb_inval *tlb_inval;
	/** @q: exec queue issuing the invalidate */
	struct xe_exec_queue *q;
	/** @vm: VM which TLB invalidation is being issued for */
	struct xe_vm *vm;
	/** @prl: Embedded copy of page reclaim list */
	struct xe_page_reclaim_list prl;
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
	/** @type: GT type */
	int type;
	/** @fence_armed: Fence has been armed */
	bool fence_armed;
};

static struct dma_fence *xe_tlb_inval_job_run(struct xe_dep_job *dep_job)
{
	struct xe_tlb_inval_job *job =
		container_of(dep_job, typeof(*job), dep);
	struct xe_tlb_inval_fence *ifence =
		container_of(job->fence, typeof(*ifence), base);
	struct drm_suballoc *prl_sa = NULL;

	if (xe_page_reclaim_list_valid(&job->prl)) {
		prl_sa = xe_page_reclaim_create_prl_bo(job->tlb_inval, &job->prl, ifence);
		if (IS_ERR(prl_sa))
			prl_sa = NULL; /* Indicate fall back PPC flush with NULL */
	}

	xe_tlb_inval_range(job->tlb_inval, ifence, job->start,
			   job->end, job->vm->usm.asid, prl_sa);

	return job->fence;
}

static void xe_tlb_inval_job_free(struct xe_dep_job *dep_job)
{
	struct xe_tlb_inval_job *job =
		container_of(dep_job, typeof(*job), dep);

	/* Pairs with get in xe_tlb_inval_job_push */
	xe_tlb_inval_job_put(job);
}

static const struct xe_dep_job_ops dep_job_ops = {
	.run_job = xe_tlb_inval_job_run,
	.free_job = xe_tlb_inval_job_free,
};

/**
 * xe_tlb_inval_job_create() - TLB invalidation job create
 * @q: exec queue issuing the invalidate
 * @tlb_inval: TLB invalidation client
 * @dep_scheduler: Dependency scheduler for job
 * @vm: VM which TLB invalidation is being issued for
 * @start: Start address to invalidate
 * @end: End address to invalidate
 * @type: GT type
 *
 * Create a TLB invalidation job and initialize internal fields. The caller is
 * responsible for releasing the creation reference.
 *
 * Return: TLB invalidation job object on success, ERR_PTR failure
 */
struct xe_tlb_inval_job *
xe_tlb_inval_job_create(struct xe_exec_queue *q, struct xe_tlb_inval *tlb_inval,
			struct xe_dep_scheduler *dep_scheduler,
			struct xe_vm *vm, u64 start, u64 end, int type)
{
	struct xe_tlb_inval_job *job;
	struct drm_sched_entity *entity =
		xe_dep_scheduler_entity(dep_scheduler);
	struct xe_tlb_inval_fence *ifence;
	int err;

	xe_assert(vm->xe, type == XE_EXEC_QUEUE_TLB_INVAL_MEDIA_GT ||
		  type == XE_EXEC_QUEUE_TLB_INVAL_PRIMARY_GT);

	job = kmalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return ERR_PTR(-ENOMEM);

	job->q = q;
	job->vm = vm;
	job->tlb_inval = tlb_inval;
	job->start = start;
	job->end = end;
	job->fence_armed = false;
	xe_page_reclaim_list_init(&job->prl);
	job->dep.ops = &dep_job_ops;
	job->type = type;
	kref_init(&job->refcount);
	xe_exec_queue_get(q);	/* Pairs with put in xe_tlb_inval_job_destroy */
	xe_vm_get(vm);		/* Pairs with put in xe_tlb_inval_job_destroy */

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

	/* Pairs with put in xe_tlb_inval_job_destroy */
	xe_pm_runtime_get_noresume(gt_to_xe(q->gt));

	return job;

err_fence:
	kfree(ifence);
err_job:
	xe_vm_put(vm);
	xe_exec_queue_put(q);
	kfree(job);

	return ERR_PTR(err);
}

/**
 * xe_tlb_inval_job_add_page_reclaim() - Embed PRL into a TLB job
 * @job: TLB invalidation job that may trigger reclamation
 * @prl: Page reclaim list populated during unbind
 *
 * Copies @prl into the job and takes an extra reference to the entry page so
 * ownership can transfer to the TLB fence when the job is pushed.
 */
void xe_tlb_inval_job_add_page_reclaim(struct xe_tlb_inval_job *job,
				       struct xe_page_reclaim_list *prl)
{
	struct xe_device *xe = gt_to_xe(job->q->gt);

	xe_gt_WARN_ON(job->q->gt, !xe->info.has_page_reclaim_hw_assist);
	job->prl = *prl;
	/* Pair with put in job_destroy */
	xe_page_reclaim_entries_get(job->prl.entries);
}

static void xe_tlb_inval_job_destroy(struct kref *ref)
{
	struct xe_tlb_inval_job *job = container_of(ref, typeof(*job),
						    refcount);
	struct xe_tlb_inval_fence *ifence =
		container_of(job->fence, typeof(*ifence), base);
	struct xe_exec_queue *q = job->q;
	struct xe_device *xe = gt_to_xe(q->gt);
	struct xe_vm *vm = job->vm;

	/* BO creation retains a copy (if used), so no longer needed */
	xe_page_reclaim_entries_put(job->prl.entries);

	if (!job->fence_armed)
		kfree(ifence);
	else
		/* Ref from xe_tlb_inval_fence_init */
		dma_fence_put(job->fence);

	drm_sched_job_cleanup(&job->dep.drm);
	kfree(job);
	xe_vm_put(vm);		/* Pairs with get from xe_tlb_inval_job_create */
	xe_exec_queue_put(q);	/* Pairs with get from xe_tlb_inval_job_create */
	xe_pm_runtime_put(xe);	/* Pairs with get from xe_tlb_inval_job_create */
}

/**
 * xe_tlb_inval_job_alloc_dep() - TLB invalidation job alloc dependency
 * @job: TLB invalidation job to alloc dependency for
 *
 * Allocate storage for a dependency in the TLB invalidation fence. This
 * function should be called at most once per job and must be paired with
 * xe_tlb_inval_job_push being called with a real fence.
 *
 * Return: 0 on success, -errno on failure
 */
int xe_tlb_inval_job_alloc_dep(struct xe_tlb_inval_job *job)
{
	xe_assert(gt_to_xe(job->q->gt), !xa_load(&job->dep.drm.dependencies, 0));
	might_alloc(GFP_KERNEL);

	return drm_sched_job_add_dependency(&job->dep.drm,
					    dma_fence_get_stub());
}

/**
 * xe_tlb_inval_job_push() - TLB invalidation job push
 * @job: TLB invalidation job to push
 * @m: The migration object being used
 * @fence: Dependency for TLB invalidation job
 *
 * Pushes a TLB invalidation job for execution, using @fence as a dependency.
 * Storage for @fence must be preallocated with xe_tlb_inval_job_alloc_dep
 * prior to this call if @fence is not signaled. Takes a reference to the job’s
 * finished fence, which the caller is responsible for releasing, and return it
 * to the caller. This function is safe to be called in the path of reclaim.
 *
 * Return: Job's finished fence on success, cannot fail
 */
struct dma_fence *xe_tlb_inval_job_push(struct xe_tlb_inval_job *job,
					struct xe_migrate *m,
					struct dma_fence *fence)
{
	struct xe_tlb_inval_fence *ifence =
		container_of(job->fence, typeof(*ifence), base);

	if (!dma_fence_is_signaled(fence)) {
		void *ptr;

		/*
		 * Can be in path of reclaim, hence the preallocation of fence
		 * storage in xe_tlb_inval_job_alloc_dep. Verify caller did
		 * this correctly.
		 */
		xe_assert(gt_to_xe(job->q->gt),
			  xa_load(&job->dep.drm.dependencies, 0) ==
			  dma_fence_get_stub());

		dma_fence_get(fence);	/* ref released once dependency processed by scheduler */
		ptr = xa_store(&job->dep.drm.dependencies, 0, fence,
			       GFP_ATOMIC);
		xe_assert(gt_to_xe(job->q->gt), !xa_is_err(ptr));
	}

	xe_tlb_inval_job_get(job);	/* Pairs with put in free_job */
	job->fence_armed = true;

	/*
	 * We need the migration lock to protect the job's seqno and the spsc
	 * queue, only taken on migration queue, user queues protected dma-resv
	 * VM lock.
	 */
	xe_migrate_job_lock(m, job->q);

	/* Creation ref pairs with put in xe_tlb_inval_job_destroy */
	xe_tlb_inval_fence_init(job->tlb_inval, ifence, false);
	dma_fence_get(job->fence);	/* Pairs with put in DRM scheduler */

	drm_sched_job_arm(&job->dep.drm);
	/*
	 * caller ref, get must be done before job push as it could immediately
	 * signal and free.
	 */
	dma_fence_get(&job->dep.drm.s_fence->finished);
	drm_sched_entity_push_job(&job->dep.drm);

	/* Let the upper layers fish this out */
	xe_exec_queue_tlb_inval_last_fence_set(job->q, job->vm,
					       &job->dep.drm.s_fence->finished,
					       job->type);

	xe_migrate_job_unlock(m, job->q);

	/*
	 * Not using job->fence, as it has its own dma-fence context, which does
	 * not allow TLB invalidation fences on the same queue, GT tuple to
	 * be squashed in dma-resv/DRM scheduler. Instead, we use the DRM scheduler
	 * context and job's finished fence, which enables squashing.
	 */
	return &job->dep.drm.s_fence->finished;
}

/**
 * xe_tlb_inval_job_get() - Get a reference to TLB invalidation job
 * @job: TLB invalidation job object
 *
 * Increment the TLB invalidation job's reference count
 */
void xe_tlb_inval_job_get(struct xe_tlb_inval_job *job)
{
	kref_get(&job->refcount);
}

/**
 * xe_tlb_inval_job_put() - Put a reference to TLB invalidation job
 * @job: TLB invalidation job object
 *
 * Decrement the TLB invalidation job's reference count, call
 * xe_tlb_inval_job_destroy when reference count == 0. Skips decrement if
 * input @job is NULL or IS_ERR.
 */
void xe_tlb_inval_job_put(struct xe_tlb_inval_job *job)
{
	if (!IS_ERR_OR_NULL(job))
		kref_put(&job->refcount, xe_tlb_inval_job_destroy);
}
