// SPDX-License-Identifier: MIT

#include <linux/slab.h>
#include <drm/gpu_scheduler.h>
#include <drm/drm_syncobj.h>

#include "nouveau_drv.h"
#include "nouveau_gem.h"
#include "nouveau_mem.h"
#include "nouveau_dma.h"
#include "nouveau_exec.h"
#include "nouveau_abi16.h"
#include "nouveau_sched.h"

#define NOUVEAU_SCHED_JOB_TIMEOUT_MS		10000

/* Starts at 0, since the DRM scheduler interprets those parameters as (initial)
 * index to the run-queue array.
 */
enum nouveau_sched_priority {
	NOUVEAU_SCHED_PRIORITY_SINGLE = DRM_SCHED_PRIORITY_KERNEL,
	NOUVEAU_SCHED_PRIORITY_COUNT,
};

int
nouveau_job_init(struct nouveau_job *job,
		 struct nouveau_job_args *args)
{
	struct nouveau_sched *sched = args->sched;
	int ret;

	INIT_LIST_HEAD(&job->entry);

	job->file_priv = args->file_priv;
	job->cli = nouveau_cli(args->file_priv);
	job->sched = sched;

	job->sync = args->sync;
	job->resv_usage = args->resv_usage;

	job->ops = args->ops;

	job->in_sync.count = args->in_sync.count;
	if (job->in_sync.count) {
		if (job->sync)
			return -EINVAL;

		job->in_sync.data = kmemdup(args->in_sync.s,
					 sizeof(*args->in_sync.s) *
					 args->in_sync.count,
					 GFP_KERNEL);
		if (!job->in_sync.data)
			return -ENOMEM;
	}

	job->out_sync.count = args->out_sync.count;
	if (job->out_sync.count) {
		if (job->sync) {
			ret = -EINVAL;
			goto err_free_in_sync;
		}

		job->out_sync.data = kmemdup(args->out_sync.s,
					  sizeof(*args->out_sync.s) *
					  args->out_sync.count,
					  GFP_KERNEL);
		if (!job->out_sync.data) {
			ret = -ENOMEM;
			goto err_free_in_sync;
		}

		job->out_sync.objs = kcalloc(job->out_sync.count,
					     sizeof(*job->out_sync.objs),
					     GFP_KERNEL);
		if (!job->out_sync.objs) {
			ret = -ENOMEM;
			goto err_free_out_sync;
		}

		job->out_sync.chains = kcalloc(job->out_sync.count,
					       sizeof(*job->out_sync.chains),
					       GFP_KERNEL);
		if (!job->out_sync.chains) {
			ret = -ENOMEM;
			goto err_free_objs;
		}
	}

	ret = drm_sched_job_init(&job->base, &sched->entity,
				 args->credits, NULL);
	if (ret)
		goto err_free_chains;

	job->state = NOUVEAU_JOB_INITIALIZED;

	return 0;

err_free_chains:
	kfree(job->out_sync.chains);
err_free_objs:
	kfree(job->out_sync.objs);
err_free_out_sync:
	kfree(job->out_sync.data);
err_free_in_sync:
	kfree(job->in_sync.data);
return ret;
}

void
nouveau_job_fini(struct nouveau_job *job)
{
	dma_fence_put(job->done_fence);
	drm_sched_job_cleanup(&job->base);

	job->ops->free(job);
}

void
nouveau_job_done(struct nouveau_job *job)
{
	struct nouveau_sched *sched = job->sched;

	spin_lock(&sched->job.list.lock);
	list_del(&job->entry);
	spin_unlock(&sched->job.list.lock);

	wake_up(&sched->job.wq);
}

void
nouveau_job_free(struct nouveau_job *job)
{
	kfree(job->in_sync.data);
	kfree(job->out_sync.data);
	kfree(job->out_sync.objs);
	kfree(job->out_sync.chains);
}

static int
sync_find_fence(struct nouveau_job *job,
		struct drm_nouveau_sync *sync,
		struct dma_fence **fence)
{
	u32 stype = sync->flags & DRM_NOUVEAU_SYNC_TYPE_MASK;
	u64 point = 0;
	int ret;

	if (stype != DRM_NOUVEAU_SYNC_SYNCOBJ &&
	    stype != DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ)
		return -EOPNOTSUPP;

	if (stype == DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ)
		point = sync->timeline_value;

	ret = drm_syncobj_find_fence(job->file_priv,
				     sync->handle, point,
				     0 /* flags */, fence);
	if (ret)
		return ret;

	return 0;
}

static int
nouveau_job_add_deps(struct nouveau_job *job)
{
	struct dma_fence *in_fence = NULL;
	int ret, i;

	for (i = 0; i < job->in_sync.count; i++) {
		struct drm_nouveau_sync *sync = &job->in_sync.data[i];

		ret = sync_find_fence(job, sync, &in_fence);
		if (ret) {
			NV_PRINTK(warn, job->cli,
				  "Failed to find syncobj (-> in): handle=%d\n",
				  sync->handle);
			return ret;
		}

		ret = drm_sched_job_add_dependency(&job->base, in_fence);
		if (ret)
			return ret;
	}

	return 0;
}

static void
nouveau_job_fence_attach_cleanup(struct nouveau_job *job)
{
	int i;

	for (i = 0; i < job->out_sync.count; i++) {
		struct drm_syncobj *obj = job->out_sync.objs[i];
		struct dma_fence_chain *chain = job->out_sync.chains[i];

		if (obj)
			drm_syncobj_put(obj);

		if (chain)
			dma_fence_chain_free(chain);
	}
}

static int
nouveau_job_fence_attach_prepare(struct nouveau_job *job)
{
	int i, ret;

	for (i = 0; i < job->out_sync.count; i++) {
		struct drm_nouveau_sync *sync = &job->out_sync.data[i];
		struct drm_syncobj **pobj = &job->out_sync.objs[i];
		struct dma_fence_chain **pchain = &job->out_sync.chains[i];
		u32 stype = sync->flags & DRM_NOUVEAU_SYNC_TYPE_MASK;

		if (stype != DRM_NOUVEAU_SYNC_SYNCOBJ &&
		    stype != DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ) {
			ret = -EINVAL;
			goto err_sync_cleanup;
		}

		*pobj = drm_syncobj_find(job->file_priv, sync->handle);
		if (!*pobj) {
			NV_PRINTK(warn, job->cli,
				  "Failed to find syncobj (-> out): handle=%d\n",
				  sync->handle);
			ret = -ENOENT;
			goto err_sync_cleanup;
		}

		if (stype == DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ) {
			*pchain = dma_fence_chain_alloc();
			if (!*pchain) {
				ret = -ENOMEM;
				goto err_sync_cleanup;
			}
		}
	}

	return 0;

err_sync_cleanup:
	nouveau_job_fence_attach_cleanup(job);
	return ret;
}

static void
nouveau_job_fence_attach(struct nouveau_job *job)
{
	struct dma_fence *fence = job->done_fence;
	int i;

	for (i = 0; i < job->out_sync.count; i++) {
		struct drm_nouveau_sync *sync = &job->out_sync.data[i];
		struct drm_syncobj **pobj = &job->out_sync.objs[i];
		struct dma_fence_chain **pchain = &job->out_sync.chains[i];
		u32 stype = sync->flags & DRM_NOUVEAU_SYNC_TYPE_MASK;

		if (stype == DRM_NOUVEAU_SYNC_TIMELINE_SYNCOBJ) {
			drm_syncobj_add_point(*pobj, *pchain, fence,
					      sync->timeline_value);
		} else {
			drm_syncobj_replace_fence(*pobj, fence);
		}

		drm_syncobj_put(*pobj);
		*pobj = NULL;
		*pchain = NULL;
	}
}

int
nouveau_job_submit(struct nouveau_job *job)
{
	struct nouveau_sched *sched = job->sched;
	struct dma_fence *done_fence = NULL;
	struct drm_gpuvm_exec vm_exec = {
		.vm = &nouveau_cli_uvmm(job->cli)->base,
		.flags = DRM_EXEC_IGNORE_DUPLICATES,
		.num_fences = 1,
	};
	int ret;

	ret = nouveau_job_add_deps(job);
	if (ret)
		goto err;

	ret = nouveau_job_fence_attach_prepare(job);
	if (ret)
		goto err;

	/* Make sure the job appears on the sched_entity's queue in the same
	 * order as it was submitted.
	 */
	mutex_lock(&sched->mutex);

	/* Guarantee we won't fail after the submit() callback returned
	 * successfully.
	 */
	if (job->ops->submit) {
		ret = job->ops->submit(job, &vm_exec);
		if (ret)
			goto err_cleanup;
	}

	/* Submit was successful; add the job to the schedulers job list. */
	spin_lock(&sched->job.list.lock);
	list_add(&job->entry, &sched->job.list.head);
	spin_unlock(&sched->job.list.lock);

	drm_sched_job_arm(&job->base);
	job->done_fence = dma_fence_get(&job->base.s_fence->finished);
	if (job->sync)
		done_fence = dma_fence_get(job->done_fence);

	if (job->ops->armed_submit)
		job->ops->armed_submit(job, &vm_exec);

	nouveau_job_fence_attach(job);

	/* Set job state before pushing the job to the scheduler,
	 * such that we do not overwrite the job state set in run().
	 */
	job->state = NOUVEAU_JOB_SUBMIT_SUCCESS;

	drm_sched_entity_push_job(&job->base);

	mutex_unlock(&sched->mutex);

	if (done_fence) {
		dma_fence_wait(done_fence, true);
		dma_fence_put(done_fence);
	}

	return 0;

err_cleanup:
	mutex_unlock(&sched->mutex);
	nouveau_job_fence_attach_cleanup(job);
err:
	job->state = NOUVEAU_JOB_SUBMIT_FAILED;
	return ret;
}

static struct dma_fence *
nouveau_job_run(struct nouveau_job *job)
{
	struct dma_fence *fence;

	fence = job->ops->run(job);
	if (IS_ERR(fence))
		job->state = NOUVEAU_JOB_RUN_FAILED;
	else
		job->state = NOUVEAU_JOB_RUN_SUCCESS;

	return fence;
}

static struct dma_fence *
nouveau_sched_run_job(struct drm_sched_job *sched_job)
{
	struct nouveau_job *job = to_nouveau_job(sched_job);

	return nouveau_job_run(job);
}

static enum drm_gpu_sched_stat
nouveau_sched_timedout_job(struct drm_sched_job *sched_job)
{
	struct drm_gpu_scheduler *sched = sched_job->sched;
	struct nouveau_job *job = to_nouveau_job(sched_job);
	enum drm_gpu_sched_stat stat = DRM_GPU_SCHED_STAT_NOMINAL;

	drm_sched_stop(sched, sched_job);

	if (job->ops->timeout)
		stat = job->ops->timeout(job);
	else
		NV_PRINTK(warn, job->cli, "Generic job timeout.\n");

	drm_sched_start(sched, 0);

	return stat;
}

static void
nouveau_sched_free_job(struct drm_sched_job *sched_job)
{
	struct nouveau_job *job = to_nouveau_job(sched_job);

	nouveau_job_fini(job);
}

static const struct drm_sched_backend_ops nouveau_sched_ops = {
	.run_job = nouveau_sched_run_job,
	.timedout_job = nouveau_sched_timedout_job,
	.free_job = nouveau_sched_free_job,
};

static int
nouveau_sched_init(struct nouveau_sched *sched, struct nouveau_drm *drm,
		   struct workqueue_struct *wq, u32 credit_limit)
{
	struct drm_gpu_scheduler *drm_sched = &sched->base;
	struct drm_sched_entity *entity = &sched->entity;
	struct drm_sched_init_args args = {
		.ops = &nouveau_sched_ops,
		.num_rqs = DRM_SCHED_PRIORITY_COUNT,
		.credit_limit = credit_limit,
		.timeout = msecs_to_jiffies(NOUVEAU_SCHED_JOB_TIMEOUT_MS),
		.name = "nouveau_sched",
		.dev = drm->dev->dev
	};
	int ret;

	if (!wq) {
		wq = alloc_workqueue("nouveau_sched_wq_%d", 0, WQ_MAX_ACTIVE,
				     current->pid);
		if (!wq)
			return -ENOMEM;

		sched->wq = wq;
	}

	args.submit_wq = wq,

	ret = drm_sched_init(drm_sched, &args);
	if (ret)
		goto fail_wq;

	/* Using DRM_SCHED_PRIORITY_KERNEL, since that's what we're required to use
	 * when we want to have a single run-queue only.
	 *
	 * It's not documented, but one will find out when trying to use any
	 * other priority running into faults, because the scheduler uses the
	 * priority as array index.
	 *
	 * Can't use NOUVEAU_SCHED_PRIORITY_SINGLE either, because it's not
	 * matching the enum type used in drm_sched_entity_init().
	 */
	ret = drm_sched_entity_init(entity, DRM_SCHED_PRIORITY_KERNEL,
				    &drm_sched, 1, NULL);
	if (ret)
		goto fail_sched;

	mutex_init(&sched->mutex);
	spin_lock_init(&sched->job.list.lock);
	INIT_LIST_HEAD(&sched->job.list.head);
	init_waitqueue_head(&sched->job.wq);

	return 0;

fail_sched:
	drm_sched_fini(drm_sched);
fail_wq:
	if (sched->wq)
		destroy_workqueue(sched->wq);
	return ret;
}

int
nouveau_sched_create(struct nouveau_sched **psched, struct nouveau_drm *drm,
		     struct workqueue_struct *wq, u32 credit_limit)
{
	struct nouveau_sched *sched;
	int ret;

	sched = kzalloc(sizeof(*sched), GFP_KERNEL);
	if (!sched)
		return -ENOMEM;

	ret = nouveau_sched_init(sched, drm, wq, credit_limit);
	if (ret) {
		kfree(sched);
		return ret;
	}

	*psched = sched;

	return 0;
}


static void
nouveau_sched_fini(struct nouveau_sched *sched)
{
	struct drm_gpu_scheduler *drm_sched = &sched->base;
	struct drm_sched_entity *entity = &sched->entity;

	rmb(); /* for list_empty to work without lock */
	wait_event(sched->job.wq, list_empty(&sched->job.list.head));

	drm_sched_entity_fini(entity);
	drm_sched_fini(drm_sched);

	/* Destroy workqueue after scheduler tear down, otherwise it might still
	 * be in use.
	 */
	if (sched->wq)
		destroy_workqueue(sched->wq);
}

void
nouveau_sched_destroy(struct nouveau_sched **psched)
{
	struct nouveau_sched *sched = *psched;

	nouveau_sched_fini(sched);
	kfree(sched);

	*psched = NULL;
}
