// SPDX-License-Identifier: MIT
/*
 * Copyright © 2025 Intel Corporation
 */

#include <linux/slab.h>

#include <drm/gpu_scheduler.h>

#include "xe_dep_job_types.h"
#include "xe_dep_scheduler.h"
#include "xe_device_types.h"

/**
 * DOC: Xe Dependency Scheduler
 *
 * The Xe dependency scheduler is a simple wrapper built around the DRM
 * scheduler to execute jobs once their dependencies are resolved (i.e., all
 * input fences specified as dependencies are signaled). The jobs that are
 * executed contain virtual functions to run (execute) and free the job,
 * allowing a single dependency scheduler to handle jobs performing different
 * operations.
 *
 * Example use cases include deferred resource freeing, TLB invalidations after
 * bind jobs, etc.
 */

/** struct xe_dep_scheduler - Generic Xe dependency scheduler */
struct xe_dep_scheduler {
	/** @sched: DRM GPU scheduler */
	struct drm_gpu_scheduler sched;
	/** @entity: DRM scheduler entity  */
	struct drm_sched_entity entity;
	/** @rcu: For safe freeing of exported dma fences */
	struct rcu_head rcu;
};

static struct dma_fence *xe_dep_scheduler_run_job(struct drm_sched_job *drm_job)
{
	struct xe_dep_job *dep_job =
		container_of(drm_job, typeof(*dep_job), drm);

	return dep_job->ops->run_job(dep_job);
}

static void xe_dep_scheduler_free_job(struct drm_sched_job *drm_job)
{
	struct xe_dep_job *dep_job =
		container_of(drm_job, typeof(*dep_job), drm);

	dep_job->ops->free_job(dep_job);
}

static const struct drm_sched_backend_ops sched_ops = {
	.run_job = xe_dep_scheduler_run_job,
	.free_job = xe_dep_scheduler_free_job,
};

/**
 * xe_dep_scheduler_create() - Generic Xe dependency scheduler create
 * @xe: Xe device
 * @submit_wq: Submit workqueue struct (can be NULL)
 * @name: Name of dependency scheduler
 * @job_limit: Max dependency jobs that can be scheduled
 *
 * Create a generic Xe dependency scheduler and initialize internal DRM
 * scheduler objects.
 *
 * Return: Generic Xe dependency scheduler object on success, ERR_PTR failure
 */
struct xe_dep_scheduler *
xe_dep_scheduler_create(struct xe_device *xe,
			struct workqueue_struct *submit_wq,
			const char *name, u32 job_limit)
{
	struct xe_dep_scheduler *dep_scheduler;
	struct drm_gpu_scheduler *sched;
	const struct drm_sched_init_args args = {
		.ops = &sched_ops,
		.submit_wq = submit_wq,
		.num_rqs = 1,
		.credit_limit = job_limit,
		.timeout = MAX_SCHEDULE_TIMEOUT,
		.name = name,
		.dev = xe->drm.dev,
	};
	int err;

	dep_scheduler = kzalloc(sizeof(*dep_scheduler), GFP_KERNEL);
	if (!dep_scheduler)
		return ERR_PTR(-ENOMEM);

	err = drm_sched_init(&dep_scheduler->sched, &args);
	if (err)
		goto err_free;

	sched = &dep_scheduler->sched;
	err = drm_sched_entity_init(&dep_scheduler->entity, 0, &sched, 1, NULL);
	if (err)
		goto err_sched;

	init_rcu_head(&dep_scheduler->rcu);

	return dep_scheduler;

err_sched:
	drm_sched_fini(&dep_scheduler->sched);
err_free:
	kfree(dep_scheduler);

	return ERR_PTR(err);
}

/**
 * xe_dep_scheduler_fini() - Generic Xe dependency scheduler finalize
 * @dep_scheduler: Generic Xe dependency scheduler object
 *
 * Finalize internal DRM scheduler objects and free generic Xe dependency
 * scheduler object
 */
void xe_dep_scheduler_fini(struct xe_dep_scheduler *dep_scheduler)
{
	drm_sched_entity_fini(&dep_scheduler->entity);
	drm_sched_fini(&dep_scheduler->sched);
	/*
	 * RCU free due sched being exported via DRM scheduler fences
	 * (timeline name).
	 */
	kfree_rcu(dep_scheduler, rcu);
}

/**
 * xe_dep_scheduler_entity() - Retrieve a generic Xe dependency scheduler
 *                             DRM scheduler entity
 * @dep_scheduler: Generic Xe dependency scheduler object
 *
 * Return: The generic Xe dependency scheduler's DRM scheduler entity
 */
struct drm_sched_entity *
xe_dep_scheduler_entity(struct xe_dep_scheduler *dep_scheduler)
{
	return &dep_scheduler->entity;
}
