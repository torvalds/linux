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

/* FIXME
 *
 * We want to make sure that jobs currently executing can't be deferred by
 * other jobs competing for the hardware. Otherwise we might end up with job
 * timeouts just because of too many clients submitting too many jobs. We don't
 * want jobs to time out because of system load, but because of the job being
 * too bulky.
 *
 * For now allow for up to 16 concurrent jobs in flight until we know how many
 * rings the hardware can process in parallel.
 */
#define NOUVEAU_SCHED_HW_SUBMISSIONS		16
#define NOUVEAU_SCHED_JOB_TIMEOUT_MS		10000

int
nouveau_job_init(struct nouveau_job *job,
		 struct nouveau_job_args *args)
{
	struct nouveau_sched_entity *entity = args->sched_entity;
	int ret;

	job->file_priv = args->file_priv;
	job->cli = nouveau_cli(args->file_priv);
	job->entity = entity;

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

	ret = drm_sched_job_init(&job->base, &entity->base, NULL);
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
nouveau_job_free(struct nouveau_job *job)
{
	kfree(job->in_sync.data);
	kfree(job->out_sync.data);
	kfree(job->out_sync.objs);
	kfree(job->out_sync.chains);
}

void nouveau_job_fini(struct nouveau_job *job)
{
	dma_fence_put(job->done_fence);
	drm_sched_job_cleanup(&job->base);
	job->ops->free(job);
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
	struct nouveau_sched_entity *entity = to_nouveau_sched_entity(job->base.entity);
	struct dma_fence *done_fence = NULL;
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
	mutex_lock(&entity->mutex);

	/* Guarantee we won't fail after the submit() callback returned
	 * successfully.
	 */
	if (job->ops->submit) {
		ret = job->ops->submit(job);
		if (ret)
			goto err_cleanup;
	}

	drm_sched_job_arm(&job->base);
	job->done_fence = dma_fence_get(&job->base.s_fence->finished);
	if (job->sync)
		done_fence = dma_fence_get(job->done_fence);

	/* If a sched job depends on a dma-fence from a job from the same GPU
	 * scheduler instance, but a different scheduler entity, the GPU
	 * scheduler does only wait for the particular job to be scheduled,
	 * rather than for the job to fully complete. This is due to the GPU
	 * scheduler assuming that there is a scheduler instance per ring.
	 * However, the current implementation, in order to avoid arbitrary
	 * amounts of kthreads, has a single scheduler instance while scheduler
	 * entities represent rings.
	 *
	 * As a workaround, set the DRM_SCHED_FENCE_DONT_PIPELINE for all
	 * out-fences in order to force the scheduler to wait for full job
	 * completion for dependent jobs from different entities and same
	 * scheduler instance.
	 *
	 * There is some work in progress [1] to address the issues of firmware
	 * schedulers; once it is in-tree the scheduler topology in Nouveau
	 * should be re-worked accordingly.
	 *
	 * [1] https://lore.kernel.org/dri-devel/20230801205103.627779-1-matthew.brost@intel.com/
	 */
	set_bit(DRM_SCHED_FENCE_DONT_PIPELINE, &job->done_fence->flags);

	if (job->ops->armed_submit)
		job->ops->armed_submit(job);

	nouveau_job_fence_attach(job);

	/* Set job state before pushing the job to the scheduler,
	 * such that we do not overwrite the job state set in run().
	 */
	job->state = NOUVEAU_JOB_SUBMIT_SUCCESS;

	drm_sched_entity_push_job(&job->base);

	mutex_unlock(&entity->mutex);

	if (done_fence) {
		dma_fence_wait(done_fence, true);
		dma_fence_put(done_fence);
	}

	return 0;

err_cleanup:
	mutex_unlock(&entity->mutex);
	nouveau_job_fence_attach_cleanup(job);
err:
	job->state = NOUVEAU_JOB_SUBMIT_FAILED;
	return ret;
}

bool
nouveau_sched_entity_qwork(struct nouveau_sched_entity *entity,
			   struct work_struct *work)
{
	return queue_work(entity->sched_wq, work);
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

	drm_sched_start(sched, true);

	return stat;
}

static void
nouveau_sched_free_job(struct drm_sched_job *sched_job)
{
	struct nouveau_job *job = to_nouveau_job(sched_job);

	nouveau_job_fini(job);
}

int nouveau_sched_entity_init(struct nouveau_sched_entity *entity,
			      struct drm_gpu_scheduler *sched,
			      struct workqueue_struct *sched_wq)
{
	mutex_init(&entity->mutex);
	spin_lock_init(&entity->job.list.lock);
	INIT_LIST_HEAD(&entity->job.list.head);
	init_waitqueue_head(&entity->job.wq);

	entity->sched_wq = sched_wq;
	return drm_sched_entity_init(&entity->base,
				     DRM_SCHED_PRIORITY_NORMAL,
				     &sched, 1, NULL);
}

void
nouveau_sched_entity_fini(struct nouveau_sched_entity *entity)
{
	drm_sched_entity_destroy(&entity->base);
}

static const struct drm_sched_backend_ops nouveau_sched_ops = {
	.run_job = nouveau_sched_run_job,
	.timedout_job = nouveau_sched_timedout_job,
	.free_job = nouveau_sched_free_job,
};

int nouveau_sched_init(struct nouveau_drm *drm)
{
	struct drm_gpu_scheduler *sched = &drm->sched;
	long job_hang_limit = msecs_to_jiffies(NOUVEAU_SCHED_JOB_TIMEOUT_MS);

	drm->sched_wq = create_singlethread_workqueue("nouveau_sched_wq");
	if (!drm->sched_wq)
		return -ENOMEM;

	return drm_sched_init(sched, &nouveau_sched_ops,
			      DRM_SCHED_PRIORITY_COUNT,
			      NOUVEAU_SCHED_HW_SUBMISSIONS, 0, job_hang_limit,
			      NULL, NULL, "nouveau_sched", drm->dev->dev);
}

void nouveau_sched_fini(struct nouveau_drm *drm)
{
	destroy_workqueue(drm->sched_wq);
	drm_sched_fini(&drm->sched);
}
