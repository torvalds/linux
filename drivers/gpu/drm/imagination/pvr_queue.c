// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include <drm/drm_managed.h>
#include <drm/gpu_scheduler.h>

#include "pvr_cccb.h"
#include "pvr_context.h"
#include "pvr_device.h"
#include "pvr_drv.h"
#include "pvr_job.h"
#include "pvr_queue.h"
#include "pvr_vm.h"

#include "pvr_rogue_fwif_client.h"

#define MAX_DEADLINE_MS 30000

#define CTX_COMPUTE_CCCB_SIZE_LOG2 15
#define CTX_FRAG_CCCB_SIZE_LOG2 15
#define CTX_GEOM_CCCB_SIZE_LOG2 15
#define CTX_TRANSFER_CCCB_SIZE_LOG2 15

static int get_xfer_ctx_state_size(struct pvr_device *pvr_dev)
{
	u32 num_isp_store_registers;

	if (PVR_HAS_FEATURE(pvr_dev, xe_memory_hierarchy)) {
		num_isp_store_registers = 1;
	} else {
		int err;

		err = PVR_FEATURE_VALUE(pvr_dev, num_isp_ipp_pipes, &num_isp_store_registers);
		if (WARN_ON(err))
			return err;
	}

	return sizeof(struct rogue_fwif_frag_ctx_state) +
	       (num_isp_store_registers *
		sizeof(((struct rogue_fwif_frag_ctx_state *)0)->frag_reg_isp_store[0]));
}

static int get_frag_ctx_state_size(struct pvr_device *pvr_dev)
{
	u32 num_isp_store_registers;
	int err;

	if (PVR_HAS_FEATURE(pvr_dev, xe_memory_hierarchy)) {
		err = PVR_FEATURE_VALUE(pvr_dev, num_raster_pipes, &num_isp_store_registers);
		if (WARN_ON(err))
			return err;

		if (PVR_HAS_FEATURE(pvr_dev, gpu_multicore_support)) {
			u32 xpu_max_slaves;

			err = PVR_FEATURE_VALUE(pvr_dev, xpu_max_slaves, &xpu_max_slaves);
			if (WARN_ON(err))
				return err;

			num_isp_store_registers *= (1 + xpu_max_slaves);
		}
	} else {
		err = PVR_FEATURE_VALUE(pvr_dev, num_isp_ipp_pipes, &num_isp_store_registers);
		if (WARN_ON(err))
			return err;
	}

	return sizeof(struct rogue_fwif_frag_ctx_state) +
	       (num_isp_store_registers *
		sizeof(((struct rogue_fwif_frag_ctx_state *)0)->frag_reg_isp_store[0]));
}

static int get_ctx_state_size(struct pvr_device *pvr_dev, enum drm_pvr_job_type type)
{
	switch (type) {
	case DRM_PVR_JOB_TYPE_GEOMETRY:
		return sizeof(struct rogue_fwif_geom_ctx_state);
	case DRM_PVR_JOB_TYPE_FRAGMENT:
		return get_frag_ctx_state_size(pvr_dev);
	case DRM_PVR_JOB_TYPE_COMPUTE:
		return sizeof(struct rogue_fwif_compute_ctx_state);
	case DRM_PVR_JOB_TYPE_TRANSFER_FRAG:
		return get_xfer_ctx_state_size(pvr_dev);
	}

	WARN(1, "Invalid queue type");
	return -EINVAL;
}

static u32 get_ctx_offset(enum drm_pvr_job_type type)
{
	switch (type) {
	case DRM_PVR_JOB_TYPE_GEOMETRY:
		return offsetof(struct rogue_fwif_fwrendercontext, geom_context);
	case DRM_PVR_JOB_TYPE_FRAGMENT:
		return offsetof(struct rogue_fwif_fwrendercontext, frag_context);
	case DRM_PVR_JOB_TYPE_COMPUTE:
		return offsetof(struct rogue_fwif_fwcomputecontext, cdm_context);
	case DRM_PVR_JOB_TYPE_TRANSFER_FRAG:
		return offsetof(struct rogue_fwif_fwtransfercontext, tq_context);
	}

	return 0;
}

static const char *
pvr_queue_fence_get_driver_name(struct dma_fence *f)
{
	return PVR_DRIVER_NAME;
}

static void pvr_queue_fence_release_work(struct work_struct *w)
{
	struct pvr_queue_fence *fence = container_of(w, struct pvr_queue_fence, release_work);

	pvr_context_put(fence->queue->ctx);
	dma_fence_free(&fence->base);
}

static void pvr_queue_fence_release(struct dma_fence *f)
{
	struct pvr_queue_fence *fence = container_of(f, struct pvr_queue_fence, base);
	struct pvr_device *pvr_dev = fence->queue->ctx->pvr_dev;

	queue_work(pvr_dev->sched_wq, &fence->release_work);
}

static const char *
pvr_queue_job_fence_get_timeline_name(struct dma_fence *f)
{
	struct pvr_queue_fence *fence = container_of(f, struct pvr_queue_fence, base);

	switch (fence->queue->type) {
	case DRM_PVR_JOB_TYPE_GEOMETRY:
		return "geometry";

	case DRM_PVR_JOB_TYPE_FRAGMENT:
		return "fragment";

	case DRM_PVR_JOB_TYPE_COMPUTE:
		return "compute";

	case DRM_PVR_JOB_TYPE_TRANSFER_FRAG:
		return "transfer";
	}

	WARN(1, "Invalid queue type");
	return "invalid";
}

static const char *
pvr_queue_cccb_fence_get_timeline_name(struct dma_fence *f)
{
	struct pvr_queue_fence *fence = container_of(f, struct pvr_queue_fence, base);

	switch (fence->queue->type) {
	case DRM_PVR_JOB_TYPE_GEOMETRY:
		return "geometry-cccb";

	case DRM_PVR_JOB_TYPE_FRAGMENT:
		return "fragment-cccb";

	case DRM_PVR_JOB_TYPE_COMPUTE:
		return "compute-cccb";

	case DRM_PVR_JOB_TYPE_TRANSFER_FRAG:
		return "transfer-cccb";
	}

	WARN(1, "Invalid queue type");
	return "invalid";
}

static const struct dma_fence_ops pvr_queue_job_fence_ops = {
	.get_driver_name = pvr_queue_fence_get_driver_name,
	.get_timeline_name = pvr_queue_job_fence_get_timeline_name,
	.release = pvr_queue_fence_release,
};

/**
 * to_pvr_queue_job_fence() - Return a pvr_queue_fence object if the fence is
 * backed by a UFO.
 * @f: The dma_fence to turn into a pvr_queue_fence.
 *
 * Return:
 *  * A non-NULL pvr_queue_fence object if the dma_fence is backed by a UFO, or
 *  * NULL otherwise.
 */
static struct pvr_queue_fence *
to_pvr_queue_job_fence(struct dma_fence *f)
{
	struct drm_sched_fence *sched_fence = to_drm_sched_fence(f);

	if (sched_fence)
		f = sched_fence->parent;

	if (f && f->ops == &pvr_queue_job_fence_ops)
		return container_of(f, struct pvr_queue_fence, base);

	return NULL;
}

static const struct dma_fence_ops pvr_queue_cccb_fence_ops = {
	.get_driver_name = pvr_queue_fence_get_driver_name,
	.get_timeline_name = pvr_queue_cccb_fence_get_timeline_name,
	.release = pvr_queue_fence_release,
};

/**
 * pvr_queue_fence_put() - Put wrapper for pvr_queue_fence objects.
 * @f: The dma_fence object to put.
 *
 * If the pvr_queue_fence has been initialized, we call dma_fence_put(),
 * otherwise we free the object with dma_fence_free(). This allows us
 * to do the right thing before and after pvr_queue_fence_init() had been
 * called.
 */
static void pvr_queue_fence_put(struct dma_fence *f)
{
	if (!f)
		return;

	if (WARN_ON(f->ops &&
		    f->ops != &pvr_queue_cccb_fence_ops &&
		    f->ops != &pvr_queue_job_fence_ops))
		return;

	/* If the fence hasn't been initialized yet, free the object directly. */
	if (f->ops)
		dma_fence_put(f);
	else
		dma_fence_free(f);
}

/**
 * pvr_queue_fence_alloc() - Allocate a pvr_queue_fence fence object
 *
 * Call this function to allocate job CCCB and done fences. This only
 * allocates the objects. Initialization happens when the underlying
 * dma_fence object is to be returned to drm_sched (in prepare_job() or
 * run_job()).
 *
 * Return:
 *  * A valid pointer if the allocation succeeds, or
 *  * NULL if the allocation fails.
 */
static struct dma_fence *
pvr_queue_fence_alloc(void)
{
	struct pvr_queue_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	return &fence->base;
}

/**
 * pvr_queue_fence_init() - Initializes a pvr_queue_fence object.
 * @f: The fence to initialize
 * @queue: The queue this fence belongs to.
 * @fence_ops: The fence operations.
 * @fence_ctx: The fence context.
 *
 * Wrapper around dma_fence_init() that takes care of initializing the
 * pvr_queue_fence::queue field too.
 */
static void
pvr_queue_fence_init(struct dma_fence *f,
		     struct pvr_queue *queue,
		     const struct dma_fence_ops *fence_ops,
		     struct pvr_queue_fence_ctx *fence_ctx)
{
	struct pvr_queue_fence *fence = container_of(f, struct pvr_queue_fence, base);

	pvr_context_get(queue->ctx);
	fence->queue = queue;
	INIT_WORK(&fence->release_work, pvr_queue_fence_release_work);
	dma_fence_init(&fence->base, fence_ops,
		       &fence_ctx->lock, fence_ctx->id,
		       atomic_inc_return(&fence_ctx->seqno));
}

/**
 * pvr_queue_cccb_fence_init() - Initializes a CCCB fence object.
 * @fence: The fence to initialize.
 * @queue: The queue this fence belongs to.
 *
 * Initializes a fence that can be used to wait for CCCB space.
 *
 * Should be called in the ::prepare_job() path, so the fence returned to
 * drm_sched is valid.
 */
static void
pvr_queue_cccb_fence_init(struct dma_fence *fence, struct pvr_queue *queue)
{
	pvr_queue_fence_init(fence, queue, &pvr_queue_cccb_fence_ops,
			     &queue->cccb_fence_ctx.base);
}

/**
 * pvr_queue_job_fence_init() - Initializes a job done fence object.
 * @fence: The fence to initialize.
 * @queue: The queue this fence belongs to.
 *
 * Initializes a fence that will be signaled when the GPU is done executing
 * a job.
 *
 * Should be called *before* the ::run_job() path, so the fence is initialised
 * before being placed in the pending_list.
 */
static void
pvr_queue_job_fence_init(struct dma_fence *fence, struct pvr_queue *queue)
{
	if (!fence->ops)
		pvr_queue_fence_init(fence, queue, &pvr_queue_job_fence_ops,
				     &queue->job_fence_ctx);
}

/**
 * pvr_queue_fence_ctx_init() - Queue fence context initialization.
 * @fence_ctx: The context to initialize
 */
static void
pvr_queue_fence_ctx_init(struct pvr_queue_fence_ctx *fence_ctx)
{
	spin_lock_init(&fence_ctx->lock);
	fence_ctx->id = dma_fence_context_alloc(1);
	atomic_set(&fence_ctx->seqno, 0);
}

static u32 ufo_cmds_size(u32 elem_count)
{
	/* We can pass at most ROGUE_FWIF_CCB_CMD_MAX_UFOS per UFO-related command. */
	u32 full_cmd_count = elem_count / ROGUE_FWIF_CCB_CMD_MAX_UFOS;
	u32 remaining_elems = elem_count % ROGUE_FWIF_CCB_CMD_MAX_UFOS;
	u32 size = full_cmd_count *
		   pvr_cccb_get_size_of_cmd_with_hdr(ROGUE_FWIF_CCB_CMD_MAX_UFOS *
						     sizeof(struct rogue_fwif_ufo));

	if (remaining_elems) {
		size += pvr_cccb_get_size_of_cmd_with_hdr(remaining_elems *
							  sizeof(struct rogue_fwif_ufo));
	}

	return size;
}

static u32 job_cmds_size(struct pvr_job *job, u32 ufo_wait_count)
{
	/* One UFO cmd for the fence signaling, one UFO cmd per native fence native,
	 * and a command for the job itself.
	 */
	return ufo_cmds_size(1) + ufo_cmds_size(ufo_wait_count) +
	       pvr_cccb_get_size_of_cmd_with_hdr(job->cmd_len);
}

/**
 * job_count_remaining_native_deps() - Count the number of non-signaled native dependencies.
 * @job: Job to operate on.
 *
 * Returns: Number of non-signaled native deps remaining.
 */
static unsigned long job_count_remaining_native_deps(struct pvr_job *job)
{
	unsigned long remaining_count = 0;
	struct dma_fence *fence = NULL;
	unsigned long index;

	xa_for_each(&job->base.dependencies, index, fence) {
		struct pvr_queue_fence *jfence;

		jfence = to_pvr_queue_job_fence(fence);
		if (!jfence)
			continue;

		if (!dma_fence_is_signaled(&jfence->base))
			remaining_count++;
	}

	return remaining_count;
}

/**
 * pvr_queue_get_job_cccb_fence() - Get the CCCB fence attached to a job.
 * @queue: The queue this job will be submitted to.
 * @job: The job to get the CCCB fence on.
 *
 * The CCCB fence is a synchronization primitive allowing us to delay job
 * submission until there's enough space in the CCCB to submit the job.
 *
 * Return:
 *  * NULL if there's enough space in the CCCB to submit this job, or
 *  * A valid dma_fence object otherwise.
 */
static struct dma_fence *
pvr_queue_get_job_cccb_fence(struct pvr_queue *queue, struct pvr_job *job)
{
	struct pvr_queue_fence *cccb_fence;
	unsigned int native_deps_remaining;

	/* If the fence is NULL, that means we already checked that we had
	 * enough space in the cccb for our job.
	 */
	if (!job->cccb_fence)
		return NULL;

	mutex_lock(&queue->cccb_fence_ctx.job_lock);

	/* Count remaining native dependencies and check if the job fits in the CCCB. */
	native_deps_remaining = job_count_remaining_native_deps(job);
	if (pvr_cccb_cmdseq_fits(&queue->cccb, job_cmds_size(job, native_deps_remaining))) {
		pvr_queue_fence_put(job->cccb_fence);
		job->cccb_fence = NULL;
		goto out_unlock;
	}

	/* There should be no job attached to the CCCB fence context:
	 * drm_sched_entity guarantees that jobs are submitted one at a time.
	 */
	if (WARN_ON(queue->cccb_fence_ctx.job))
		pvr_job_put(queue->cccb_fence_ctx.job);

	queue->cccb_fence_ctx.job = pvr_job_get(job);

	/* Initialize the fence before returning it. */
	cccb_fence = container_of(job->cccb_fence, struct pvr_queue_fence, base);
	if (!WARN_ON(cccb_fence->queue))
		pvr_queue_cccb_fence_init(job->cccb_fence, queue);

out_unlock:
	mutex_unlock(&queue->cccb_fence_ctx.job_lock);

	return dma_fence_get(job->cccb_fence);
}

/**
 * pvr_queue_get_job_kccb_fence() - Get the KCCB fence attached to a job.
 * @queue: The queue this job will be submitted to.
 * @job: The job to get the KCCB fence on.
 *
 * The KCCB fence is a synchronization primitive allowing us to delay job
 * submission until there's enough space in the KCCB to submit the job.
 *
 * Return:
 *  * NULL if there's enough space in the KCCB to submit this job, or
 *  * A valid dma_fence object otherwise.
 */
static struct dma_fence *
pvr_queue_get_job_kccb_fence(struct pvr_queue *queue, struct pvr_job *job)
{
	struct pvr_device *pvr_dev = queue->ctx->pvr_dev;
	struct dma_fence *kccb_fence = NULL;

	/* If the fence is NULL, that means we already checked that we had
	 * enough space in the KCCB for our job.
	 */
	if (!job->kccb_fence)
		return NULL;

	if (!WARN_ON(job->kccb_fence->ops)) {
		kccb_fence = pvr_kccb_reserve_slot(pvr_dev, job->kccb_fence);
		job->kccb_fence = NULL;
	}

	return kccb_fence;
}

static struct dma_fence *
pvr_queue_get_paired_frag_job_dep(struct pvr_queue *queue, struct pvr_job *job)
{
	struct pvr_job *frag_job = job->type == DRM_PVR_JOB_TYPE_GEOMETRY ?
				   job->paired_job : NULL;
	struct dma_fence *f;
	unsigned long index;

	if (!frag_job)
		return NULL;

	xa_for_each(&frag_job->base.dependencies, index, f) {
		/* Skip already signaled fences. */
		if (dma_fence_is_signaled(f))
			continue;

		/* Skip our own fence. */
		if (f == &job->base.s_fence->scheduled)
			continue;

		return dma_fence_get(f);
	}

	return frag_job->base.sched->ops->prepare_job(&frag_job->base, &queue->entity);
}

/**
 * pvr_queue_prepare_job() - Return the next internal dependencies expressed as a dma_fence.
 * @sched_job: The job to query the next internal dependency on
 * @s_entity: The entity this job is queue on.
 *
 * After iterating over drm_sched_job::dependencies, drm_sched let the driver return
 * its own internal dependencies. We use this function to return our internal dependencies.
 */
static struct dma_fence *
pvr_queue_prepare_job(struct drm_sched_job *sched_job,
		      struct drm_sched_entity *s_entity)
{
	struct pvr_job *job = container_of(sched_job, struct pvr_job, base);
	struct pvr_queue *queue = container_of(s_entity, struct pvr_queue, entity);
	struct dma_fence *internal_dep = NULL;

	/*
	 * Initialize the done_fence, so we can signal it. This must be done
	 * here because otherwise by the time of run_job() the job will end up
	 * in the pending list without a valid fence.
	 */
	if (job->type == DRM_PVR_JOB_TYPE_FRAGMENT && job->paired_job) {
		/*
		 * This will be called on a paired fragment job after being
		 * submitted to firmware. We can tell if this is the case and
		 * bail early from whether run_job() has been called on the
		 * geometry job, which would issue a pm ref.
		 */
		if (job->paired_job->has_pm_ref)
			return NULL;

		/*
		 * In this case we need to use the job's own ctx to initialise
		 * the done_fence.  The other steps are done in the ctx of the
		 * paired geometry job.
		 */
		pvr_queue_job_fence_init(job->done_fence,
					 job->ctx->queues.fragment);
	} else {
		pvr_queue_job_fence_init(job->done_fence, queue);
	}

	/* CCCB fence is used to make sure we have enough space in the CCCB to
	 * submit our commands.
	 */
	internal_dep = pvr_queue_get_job_cccb_fence(queue, job);

	/* KCCB fence is used to make sure we have a KCCB slot to queue our
	 * CMD_KICK.
	 */
	if (!internal_dep)
		internal_dep = pvr_queue_get_job_kccb_fence(queue, job);

	/* Any extra internal dependency should be added here, using the following
	 * pattern:
	 *
	 *	if (!internal_dep)
	 *		internal_dep = pvr_queue_get_job_xxxx_fence(queue, job);
	 */

	/* The paired job fence should come last, when everything else is ready. */
	if (!internal_dep)
		internal_dep = pvr_queue_get_paired_frag_job_dep(queue, job);

	return internal_dep;
}

/**
 * pvr_queue_update_active_state_locked() - Update the queue active state.
 * @queue: Queue to update the state on.
 *
 * Locked version of pvr_queue_update_active_state(). Must be called with
 * pvr_device::queue::lock held.
 */
static void pvr_queue_update_active_state_locked(struct pvr_queue *queue)
{
	struct pvr_device *pvr_dev = queue->ctx->pvr_dev;

	lockdep_assert_held(&pvr_dev->queues.lock);

	/* The queue is temporary out of any list when it's being reset,
	 * we don't want a call to pvr_queue_update_active_state_locked()
	 * to re-insert it behind our back.
	 */
	if (list_empty(&queue->node))
		return;

	if (!atomic_read(&queue->in_flight_job_count))
		list_move_tail(&queue->node, &pvr_dev->queues.idle);
	else
		list_move_tail(&queue->node, &pvr_dev->queues.active);
}

/**
 * pvr_queue_update_active_state() - Update the queue active state.
 * @queue: Queue to update the state on.
 *
 * Active state is based on the in_flight_job_count value.
 *
 * Updating the active state implies moving the queue in or out of the
 * active queue list, which also defines whether the queue is checked
 * or not when a FW event is received.
 *
 * This function should be called any time a job is submitted or it done
 * fence is signaled.
 */
static void pvr_queue_update_active_state(struct pvr_queue *queue)
{
	struct pvr_device *pvr_dev = queue->ctx->pvr_dev;

	mutex_lock(&pvr_dev->queues.lock);
	pvr_queue_update_active_state_locked(queue);
	mutex_unlock(&pvr_dev->queues.lock);
}

static void pvr_queue_submit_job_to_cccb(struct pvr_job *job)
{
	struct pvr_queue *queue = container_of(job->base.sched, struct pvr_queue, scheduler);
	struct rogue_fwif_ufo ufos[ROGUE_FWIF_CCB_CMD_MAX_UFOS];
	struct pvr_cccb *cccb = &queue->cccb;
	struct pvr_queue_fence *jfence;
	struct dma_fence *fence;
	unsigned long index;
	u32 ufo_count = 0;

	/* We need to add the queue to the active list before updating the CCCB,
	 * otherwise we might miss the FW event informing us that something
	 * happened on this queue.
	 */
	atomic_inc(&queue->in_flight_job_count);
	pvr_queue_update_active_state(queue);

	xa_for_each(&job->base.dependencies, index, fence) {
		jfence = to_pvr_queue_job_fence(fence);
		if (!jfence)
			continue;

		/* Skip the partial render fence, we will place it at the end. */
		if (job->type == DRM_PVR_JOB_TYPE_FRAGMENT && job->paired_job &&
		    &job->paired_job->base.s_fence->scheduled == fence)
			continue;

		if (dma_fence_is_signaled(&jfence->base))
			continue;

		pvr_fw_object_get_fw_addr(jfence->queue->timeline_ufo.fw_obj,
					  &ufos[ufo_count].addr);
		ufos[ufo_count++].value = jfence->base.seqno;

		if (ufo_count == ARRAY_SIZE(ufos)) {
			pvr_cccb_write_command_with_header(cccb, ROGUE_FWIF_CCB_CMD_TYPE_FENCE_PR,
							   sizeof(ufos), ufos, 0, 0);
			ufo_count = 0;
		}
	}

	/* Partial render fence goes last. */
	if (job->type == DRM_PVR_JOB_TYPE_FRAGMENT && job->paired_job) {
		jfence = to_pvr_queue_job_fence(job->paired_job->done_fence);
		if (!WARN_ON(!jfence)) {
			pvr_fw_object_get_fw_addr(jfence->queue->timeline_ufo.fw_obj,
						  &ufos[ufo_count].addr);
			ufos[ufo_count++].value = job->paired_job->done_fence->seqno;
		}
	}

	if (ufo_count) {
		pvr_cccb_write_command_with_header(cccb, ROGUE_FWIF_CCB_CMD_TYPE_FENCE_PR,
						   sizeof(ufos[0]) * ufo_count, ufos, 0, 0);
	}

	if (job->type == DRM_PVR_JOB_TYPE_GEOMETRY && job->paired_job) {
		struct rogue_fwif_cmd_geom *cmd = job->cmd;

		/* Reference value for the partial render test is the current queue fence
		 * seqno minus one.
		 */
		pvr_fw_object_get_fw_addr(queue->timeline_ufo.fw_obj,
					  &cmd->partial_render_geom_frag_fence.addr);
		cmd->partial_render_geom_frag_fence.value = job->done_fence->seqno - 1;
	}

	/* Submit job to FW */
	pvr_cccb_write_command_with_header(cccb, job->fw_ccb_cmd_type, job->cmd_len, job->cmd,
					   job->id, job->id);

	/* Signal the job fence. */
	pvr_fw_object_get_fw_addr(queue->timeline_ufo.fw_obj, &ufos[0].addr);
	ufos[0].value = job->done_fence->seqno;
	pvr_cccb_write_command_with_header(cccb, ROGUE_FWIF_CCB_CMD_TYPE_UPDATE,
					   sizeof(ufos[0]), ufos, 0, 0);
}

/**
 * pvr_queue_run_job() - Submit a job to the FW.
 * @sched_job: The job to submit.
 *
 * This function is called when all non-native dependencies have been met and
 * when the commands resulting from this job are guaranteed to fit in the CCCB.
 */
static struct dma_fence *pvr_queue_run_job(struct drm_sched_job *sched_job)
{
	struct pvr_job *job = container_of(sched_job, struct pvr_job, base);
	struct pvr_device *pvr_dev = job->pvr_dev;
	int err;

	/* The fragment job is issued along the geometry job when we use combined
	 * geom+frag kicks. When we get there, we should simply return the
	 * done_fence that's been initialized earlier.
	 */
	if (job->paired_job && job->type == DRM_PVR_JOB_TYPE_FRAGMENT &&
	    job->done_fence->ops) {
		return dma_fence_get(job->done_fence);
	}

	/* The only kind of jobs that can be paired are geometry and fragment, and
	 * we bail out early if we see a fragment job that's paired with a geomtry
	 * job.
	 * Paired jobs must also target the same context and point to the same
	 * HWRT.
	 */
	if (WARN_ON(job->paired_job &&
		    (job->type != DRM_PVR_JOB_TYPE_GEOMETRY ||
		     job->paired_job->type != DRM_PVR_JOB_TYPE_FRAGMENT ||
		     job->hwrt != job->paired_job->hwrt ||
		     job->ctx != job->paired_job->ctx)))
		return ERR_PTR(-EINVAL);

	err = pvr_job_get_pm_ref(job);
	if (WARN_ON(err))
		return ERR_PTR(err);

	if (job->paired_job) {
		err = pvr_job_get_pm_ref(job->paired_job);
		if (WARN_ON(err))
			return ERR_PTR(err);
	}

	/* Submit our job to the CCCB */
	pvr_queue_submit_job_to_cccb(job);

	if (job->paired_job) {
		struct pvr_job *geom_job = job;
		struct pvr_job *frag_job = job->paired_job;
		struct pvr_queue *geom_queue = job->ctx->queues.geometry;
		struct pvr_queue *frag_queue = job->ctx->queues.fragment;

		/* Submit the fragment job along the geometry job and send a combined kick. */
		pvr_queue_submit_job_to_cccb(frag_job);
		pvr_cccb_send_kccb_combined_kick(pvr_dev,
						 &geom_queue->cccb, &frag_queue->cccb,
						 pvr_context_get_fw_addr(geom_job->ctx) +
						 geom_queue->ctx_offset,
						 pvr_context_get_fw_addr(frag_job->ctx) +
						 frag_queue->ctx_offset,
						 job->hwrt,
						 frag_job->fw_ccb_cmd_type ==
						 ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR);
	} else {
		struct pvr_queue *queue = container_of(job->base.sched,
						       struct pvr_queue, scheduler);

		pvr_cccb_send_kccb_kick(pvr_dev, &queue->cccb,
					pvr_context_get_fw_addr(job->ctx) + queue->ctx_offset,
					job->hwrt);
	}

	return dma_fence_get(job->done_fence);
}

static void pvr_queue_stop(struct pvr_queue *queue, struct pvr_job *bad_job)
{
	drm_sched_stop(&queue->scheduler, bad_job ? &bad_job->base : NULL);
}

static void pvr_queue_start(struct pvr_queue *queue)
{
	struct pvr_job *job;

	/* Make sure we CPU-signal the UFO object, so other queues don't get
	 * blocked waiting on it.
	 */
	*queue->timeline_ufo.value = atomic_read(&queue->job_fence_ctx.seqno);

	list_for_each_entry(job, &queue->scheduler.pending_list, base.list) {
		if (dma_fence_is_signaled(job->done_fence)) {
			/* Jobs might have completed after drm_sched_stop() was called.
			 * In that case, re-assign the parent field to the done_fence.
			 */
			WARN_ON(job->base.s_fence->parent);
			job->base.s_fence->parent = dma_fence_get(job->done_fence);
		} else {
			/* If we had unfinished jobs, flag the entity as guilty so no
			 * new job can be submitted.
			 */
			atomic_set(&queue->ctx->faulty, 1);
		}
	}

	drm_sched_start(&queue->scheduler, 0);
}

/**
 * pvr_queue_timedout_job() - Handle a job timeout event.
 * @s_job: The job this timeout occurred on.
 *
 * FIXME: We don't do anything here to unblock the situation, we just stop+start
 * the scheduler, and re-assign parent fences in the middle.
 *
 * Return:
 *  * DRM_GPU_SCHED_STAT_NOMINAL.
 */
static enum drm_gpu_sched_stat
pvr_queue_timedout_job(struct drm_sched_job *s_job)
{
	struct drm_gpu_scheduler *sched = s_job->sched;
	struct pvr_queue *queue = container_of(sched, struct pvr_queue, scheduler);
	struct pvr_device *pvr_dev = queue->ctx->pvr_dev;
	struct pvr_job *job;
	u32 job_count = 0;

	dev_err(sched->dev, "Job timeout\n");

	/* Before we stop the scheduler, make sure the queue is out of any list, so
	 * any call to pvr_queue_update_active_state_locked() that might happen
	 * until the scheduler is really stopped doesn't end up re-inserting the
	 * queue in the active list. This would cause
	 * pvr_queue_signal_done_fences() and drm_sched_stop() to race with each
	 * other when accessing the pending_list, since drm_sched_stop() doesn't
	 * grab the job_list_lock when modifying the list (it's assuming the
	 * only other accessor is the scheduler, and it's safe to not grab the
	 * lock since it's stopped).
	 */
	mutex_lock(&pvr_dev->queues.lock);
	list_del_init(&queue->node);
	mutex_unlock(&pvr_dev->queues.lock);

	drm_sched_stop(sched, s_job);

	/* Re-assign job parent fences. */
	list_for_each_entry(job, &sched->pending_list, base.list) {
		job->base.s_fence->parent = dma_fence_get(job->done_fence);
		job_count++;
	}
	WARN_ON(atomic_read(&queue->in_flight_job_count) != job_count);

	/* Re-insert the queue in the proper list, and kick a queue processing
	 * operation if there were jobs pending.
	 */
	mutex_lock(&pvr_dev->queues.lock);
	if (!job_count) {
		list_move_tail(&queue->node, &pvr_dev->queues.idle);
	} else {
		atomic_set(&queue->in_flight_job_count, job_count);
		list_move_tail(&queue->node, &pvr_dev->queues.active);
		pvr_queue_process(queue);
	}
	mutex_unlock(&pvr_dev->queues.lock);

	drm_sched_start(sched, 0);

	return DRM_GPU_SCHED_STAT_NOMINAL;
}

/**
 * pvr_queue_free_job() - Release the reference the scheduler had on a job object.
 * @sched_job: Job object to free.
 */
static void pvr_queue_free_job(struct drm_sched_job *sched_job)
{
	struct pvr_job *job = container_of(sched_job, struct pvr_job, base);

	drm_sched_job_cleanup(sched_job);
	job->paired_job = NULL;
	pvr_job_put(job);
}

static const struct drm_sched_backend_ops pvr_queue_sched_ops = {
	.prepare_job = pvr_queue_prepare_job,
	.run_job = pvr_queue_run_job,
	.timedout_job = pvr_queue_timedout_job,
	.free_job = pvr_queue_free_job,
};

/**
 * pvr_queue_fence_is_ufo_backed() - Check if a dma_fence is backed by a UFO object
 * @f: Fence to test.
 *
 * A UFO-backed fence is a fence that can be signaled or waited upon FW-side.
 * pvr_job::done_fence objects are backed by the timeline UFO attached to the queue
 * they are pushed to, but those fences are not directly exposed to the outside
 * world, so we also need to check if the fence we're being passed is a
 * drm_sched_fence that was coming from our driver.
 */
bool pvr_queue_fence_is_ufo_backed(struct dma_fence *f)
{
	struct drm_sched_fence *sched_fence = f ? to_drm_sched_fence(f) : NULL;

	if (sched_fence &&
	    sched_fence->sched->ops == &pvr_queue_sched_ops)
		return true;

	if (f && f->ops == &pvr_queue_job_fence_ops)
		return true;

	return false;
}

/**
 * pvr_queue_signal_done_fences() - Signal done fences.
 * @queue: Queue to check.
 *
 * Signal done fences of jobs whose seqno is less than the current value of
 * the UFO object attached to the queue.
 */
static void
pvr_queue_signal_done_fences(struct pvr_queue *queue)
{
	struct pvr_job *job, *tmp_job;
	u32 cur_seqno;

	spin_lock(&queue->scheduler.job_list_lock);
	cur_seqno = *queue->timeline_ufo.value;
	list_for_each_entry_safe(job, tmp_job, &queue->scheduler.pending_list, base.list) {
		if ((int)(cur_seqno - lower_32_bits(job->done_fence->seqno)) < 0)
			break;

		if (!dma_fence_is_signaled(job->done_fence)) {
			dma_fence_signal(job->done_fence);
			pvr_job_release_pm_ref(job);
			atomic_dec(&queue->in_flight_job_count);
		}
	}
	spin_unlock(&queue->scheduler.job_list_lock);
}

/**
 * pvr_queue_check_job_waiting_for_cccb_space() - Check if the job waiting for CCCB space
 * can be unblocked
 * pushed to the CCCB
 * @queue: Queue to check
 *
 * If we have a job waiting for CCCB, and this job now fits in the CCCB, we signal
 * its CCCB fence, which should kick drm_sched.
 */
static void
pvr_queue_check_job_waiting_for_cccb_space(struct pvr_queue *queue)
{
	struct pvr_queue_fence *cccb_fence;
	u32 native_deps_remaining;
	struct pvr_job *job;

	mutex_lock(&queue->cccb_fence_ctx.job_lock);
	job = queue->cccb_fence_ctx.job;
	if (!job)
		goto out_unlock;

	/* If we have a job attached to the CCCB fence context, its CCCB fence
	 * shouldn't be NULL.
	 */
	if (WARN_ON(!job->cccb_fence)) {
		job = NULL;
		goto out_unlock;
	}

	/* If we get there, CCCB fence has to be initialized. */
	cccb_fence = container_of(job->cccb_fence, struct pvr_queue_fence, base);
	if (WARN_ON(!cccb_fence->queue)) {
		job = NULL;
		goto out_unlock;
	}

	/* Evict signaled dependencies before checking for CCCB space.
	 * If the job fits, signal the CCCB fence, this should unblock
	 * the drm_sched_entity.
	 */
	native_deps_remaining = job_count_remaining_native_deps(job);
	if (!pvr_cccb_cmdseq_fits(&queue->cccb, job_cmds_size(job, native_deps_remaining))) {
		job = NULL;
		goto out_unlock;
	}

	dma_fence_signal(job->cccb_fence);
	pvr_queue_fence_put(job->cccb_fence);
	job->cccb_fence = NULL;
	queue->cccb_fence_ctx.job = NULL;

out_unlock:
	mutex_unlock(&queue->cccb_fence_ctx.job_lock);

	pvr_job_put(job);
}

/**
 * pvr_queue_process() - Process events that happened on a queue.
 * @queue: Queue to check
 *
 * Signal job fences and check if jobs waiting for CCCB space can be unblocked.
 */
void pvr_queue_process(struct pvr_queue *queue)
{
	lockdep_assert_held(&queue->ctx->pvr_dev->queues.lock);

	pvr_queue_check_job_waiting_for_cccb_space(queue);
	pvr_queue_signal_done_fences(queue);
	pvr_queue_update_active_state_locked(queue);
}

static u32 get_dm_type(struct pvr_queue *queue)
{
	switch (queue->type) {
	case DRM_PVR_JOB_TYPE_GEOMETRY:
		return PVR_FWIF_DM_GEOM;
	case DRM_PVR_JOB_TYPE_TRANSFER_FRAG:
	case DRM_PVR_JOB_TYPE_FRAGMENT:
		return PVR_FWIF_DM_FRAG;
	case DRM_PVR_JOB_TYPE_COMPUTE:
		return PVR_FWIF_DM_CDM;
	}

	return ~0;
}

/**
 * init_fw_context() - Initializes the queue part of a FW context.
 * @queue: Queue object to initialize the FW context for.
 * @fw_ctx_map: The FW context CPU mapping.
 *
 * FW contexts are containing various states, one of them being a per-queue state
 * that needs to be initialized for each queue being exposed by a context. This
 * function takes care of that.
 */
static void init_fw_context(struct pvr_queue *queue, void *fw_ctx_map)
{
	struct pvr_context *ctx = queue->ctx;
	struct pvr_fw_object *fw_mem_ctx_obj = pvr_vm_get_fw_mem_context(ctx->vm_ctx);
	struct rogue_fwif_fwcommoncontext *cctx_fw;
	struct pvr_cccb *cccb = &queue->cccb;

	cctx_fw = fw_ctx_map + queue->ctx_offset;
	cctx_fw->ccbctl_fw_addr = cccb->ctrl_fw_addr;
	cctx_fw->ccb_fw_addr = cccb->cccb_fw_addr;

	cctx_fw->dm = get_dm_type(queue);
	cctx_fw->priority = ctx->priority;
	cctx_fw->priority_seq_num = 0;
	cctx_fw->max_deadline_ms = MAX_DEADLINE_MS;
	cctx_fw->pid = task_tgid_nr(current);
	cctx_fw->server_common_context_id = ctx->ctx_id;

	pvr_fw_object_get_fw_addr(fw_mem_ctx_obj, &cctx_fw->fw_mem_context_fw_addr);

	pvr_fw_object_get_fw_addr(queue->reg_state_obj, &cctx_fw->context_state_addr);
}

/**
 * pvr_queue_cleanup_fw_context() - Wait for the FW context to be idle and clean it up.
 * @queue: Queue on FW context to clean up.
 *
 * Return:
 *  * 0 on success,
 *  * Any error returned by pvr_fw_structure_cleanup() otherwise.
 */
static int pvr_queue_cleanup_fw_context(struct pvr_queue *queue)
{
	if (!queue->ctx->fw_obj)
		return 0;

	return pvr_fw_structure_cleanup(queue->ctx->pvr_dev,
					ROGUE_FWIF_CLEANUP_FWCOMMONCONTEXT,
					queue->ctx->fw_obj, queue->ctx_offset);
}

/**
 * pvr_queue_job_init() - Initialize queue related fields in a pvr_job object.
 * @job: The job to initialize.
 *
 * Bind the job to a queue and allocate memory to guarantee pvr_queue_job_arm()
 * and pvr_queue_job_push() can't fail. We also make sure the context type is
 * valid and the job can fit in the CCCB.
 *
 * Return:
 *  * 0 on success, or
 *  * An error code if something failed.
 */
int pvr_queue_job_init(struct pvr_job *job)
{
	/* Fragment jobs need at least one native fence wait on the geometry job fence. */
	u32 min_native_dep_count = job->type == DRM_PVR_JOB_TYPE_FRAGMENT ? 1 : 0;
	struct pvr_queue *queue;
	int err;

	if (atomic_read(&job->ctx->faulty))
		return -EIO;

	queue = pvr_context_get_queue_for_job(job->ctx, job->type);
	if (!queue)
		return -EINVAL;

	if (!pvr_cccb_cmdseq_can_fit(&queue->cccb, job_cmds_size(job, min_native_dep_count)))
		return -E2BIG;

	err = drm_sched_job_init(&job->base, &queue->entity, 1, THIS_MODULE);
	if (err)
		return err;

	job->cccb_fence = pvr_queue_fence_alloc();
	job->kccb_fence = pvr_kccb_fence_alloc();
	job->done_fence = pvr_queue_fence_alloc();
	if (!job->cccb_fence || !job->kccb_fence || !job->done_fence)
		return -ENOMEM;

	return 0;
}

/**
 * pvr_queue_job_arm() - Arm a job object.
 * @job: The job to arm.
 *
 * Initializes fences and return the drm_sched finished fence so it can
 * be exposed to the outside world. Once this function is called, you should
 * make sure the job is pushed using pvr_queue_job_push(), or guarantee that
 * no one grabbed a reference to the returned fence. The latter can happen if
 * we do multi-job submission, and something failed when creating/initializing
 * a job. In that case, we know the fence didn't leave the driver, and we
 * can thus guarantee nobody will wait on an dead fence object.
 *
 * Return:
 *  * A dma_fence object.
 */
struct dma_fence *pvr_queue_job_arm(struct pvr_job *job)
{
	drm_sched_job_arm(&job->base);

	return &job->base.s_fence->finished;
}

/**
 * pvr_queue_job_cleanup() - Cleanup fence/scheduler related fields in the job object.
 * @job: The job to cleanup.
 *
 * Should be called in the job release path.
 */
void pvr_queue_job_cleanup(struct pvr_job *job)
{
	pvr_queue_fence_put(job->done_fence);
	pvr_queue_fence_put(job->cccb_fence);
	pvr_kccb_fence_put(job->kccb_fence);

	if (job->base.s_fence)
		drm_sched_job_cleanup(&job->base);
}

/**
 * pvr_queue_job_push() - Push a job to its queue.
 * @job: The job to push.
 *
 * Must be called after pvr_queue_job_init() and after all dependencies
 * have been added to the job. This will effectively queue the job to
 * the drm_sched_entity attached to the queue. We grab a reference on
 * the job object, so the caller is free to drop its reference when it's
 * done accessing the job object.
 */
void pvr_queue_job_push(struct pvr_job *job)
{
	struct pvr_queue *queue = container_of(job->base.sched, struct pvr_queue, scheduler);

	/* Keep track of the last queued job scheduled fence for combined submit. */
	dma_fence_put(queue->last_queued_job_scheduled_fence);
	queue->last_queued_job_scheduled_fence = dma_fence_get(&job->base.s_fence->scheduled);

	pvr_job_get(job);
	drm_sched_entity_push_job(&job->base);
}

static void reg_state_init(void *cpu_ptr, void *priv)
{
	struct pvr_queue *queue = priv;

	if (queue->type == DRM_PVR_JOB_TYPE_GEOMETRY) {
		struct rogue_fwif_geom_ctx_state *geom_ctx_state_fw = cpu_ptr;

		geom_ctx_state_fw->geom_core[0].geom_reg_vdm_call_stack_pointer_init =
			queue->callstack_addr;
	}
}

/**
 * pvr_queue_create() - Create a queue object.
 * @ctx: The context this queue will be attached to.
 * @type: The type of jobs being pushed to this queue.
 * @args: The arguments passed to the context creation function.
 * @fw_ctx_map: CPU mapping of the FW context object.
 *
 * Create a queue object that will be used to queue and track jobs.
 *
 * Return:
 *  * A valid pointer to a pvr_queue object, or
 *  * An error pointer if the creation/initialization failed.
 */
struct pvr_queue *pvr_queue_create(struct pvr_context *ctx,
				   enum drm_pvr_job_type type,
				   struct drm_pvr_ioctl_create_context_args *args,
				   void *fw_ctx_map)
{
	static const struct {
		u32 cccb_size;
		const char *name;
	} props[] = {
		[DRM_PVR_JOB_TYPE_GEOMETRY] = {
			.cccb_size = CTX_GEOM_CCCB_SIZE_LOG2,
			.name = "geometry",
		},
		[DRM_PVR_JOB_TYPE_FRAGMENT] = {
			.cccb_size = CTX_FRAG_CCCB_SIZE_LOG2,
			.name = "fragment"
		},
		[DRM_PVR_JOB_TYPE_COMPUTE] = {
			.cccb_size = CTX_COMPUTE_CCCB_SIZE_LOG2,
			.name = "compute"
		},
		[DRM_PVR_JOB_TYPE_TRANSFER_FRAG] = {
			.cccb_size = CTX_TRANSFER_CCCB_SIZE_LOG2,
			.name = "transfer_frag"
		},
	};
	struct pvr_device *pvr_dev = ctx->pvr_dev;
	const struct drm_sched_init_args sched_args = {
		.ops = &pvr_queue_sched_ops,
		.submit_wq = pvr_dev->sched_wq,
		.num_rqs = 1,
		.credit_limit = 64 * 1024,
		.hang_limit = 1,
		.timeout = msecs_to_jiffies(500),
		.timeout_wq = pvr_dev->sched_wq,
		.name = "pvr-queue",
		.dev = pvr_dev->base.dev,
	};
	struct drm_gpu_scheduler *sched;
	struct pvr_queue *queue;
	int ctx_state_size, err;
	void *cpu_map;

	if (WARN_ON(type >= sizeof(props)))
		return ERR_PTR(-EINVAL);

	switch (ctx->type) {
	case DRM_PVR_CTX_TYPE_RENDER:
		if (type != DRM_PVR_JOB_TYPE_GEOMETRY &&
		    type != DRM_PVR_JOB_TYPE_FRAGMENT)
			return ERR_PTR(-EINVAL);
		break;
	case DRM_PVR_CTX_TYPE_COMPUTE:
		if (type != DRM_PVR_JOB_TYPE_COMPUTE)
			return ERR_PTR(-EINVAL);
		break;
	case DRM_PVR_CTX_TYPE_TRANSFER_FRAG:
		if (type != DRM_PVR_JOB_TYPE_TRANSFER_FRAG)
			return ERR_PTR(-EINVAL);
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ctx_state_size = get_ctx_state_size(pvr_dev, type);
	if (ctx_state_size < 0)
		return ERR_PTR(ctx_state_size);

	queue = kzalloc(sizeof(*queue), GFP_KERNEL);
	if (!queue)
		return ERR_PTR(-ENOMEM);

	queue->type = type;
	queue->ctx_offset = get_ctx_offset(type);
	queue->ctx = ctx;
	queue->callstack_addr = args->callstack_addr;
	sched = &queue->scheduler;
	INIT_LIST_HEAD(&queue->node);
	mutex_init(&queue->cccb_fence_ctx.job_lock);
	pvr_queue_fence_ctx_init(&queue->cccb_fence_ctx.base);
	pvr_queue_fence_ctx_init(&queue->job_fence_ctx);

	err = pvr_cccb_init(pvr_dev, &queue->cccb, props[type].cccb_size, props[type].name);
	if (err)
		goto err_free_queue;

	err = pvr_fw_object_create(pvr_dev, ctx_state_size,
				   PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
				   reg_state_init, queue, &queue->reg_state_obj);
	if (err)
		goto err_cccb_fini;

	init_fw_context(queue, fw_ctx_map);

	if (type != DRM_PVR_JOB_TYPE_GEOMETRY && type != DRM_PVR_JOB_TYPE_FRAGMENT &&
	    args->callstack_addr) {
		err = -EINVAL;
		goto err_release_reg_state;
	}

	cpu_map = pvr_fw_object_create_and_map(pvr_dev, sizeof(*queue->timeline_ufo.value),
					       PVR_BO_FW_FLAGS_DEVICE_UNCACHED,
					       NULL, NULL, &queue->timeline_ufo.fw_obj);
	if (IS_ERR(cpu_map)) {
		err = PTR_ERR(cpu_map);
		goto err_release_reg_state;
	}

	queue->timeline_ufo.value = cpu_map;

	err = drm_sched_init(&queue->scheduler, &sched_args);
	if (err)
		goto err_release_ufo;

	err = drm_sched_entity_init(&queue->entity,
				    DRM_SCHED_PRIORITY_KERNEL,
				    &sched, 1, &ctx->faulty);
	if (err)
		goto err_sched_fini;

	mutex_lock(&pvr_dev->queues.lock);
	list_add_tail(&queue->node, &pvr_dev->queues.idle);
	mutex_unlock(&pvr_dev->queues.lock);

	return queue;

err_sched_fini:
	drm_sched_fini(&queue->scheduler);

err_release_ufo:
	pvr_fw_object_unmap_and_destroy(queue->timeline_ufo.fw_obj);

err_release_reg_state:
	pvr_fw_object_destroy(queue->reg_state_obj);

err_cccb_fini:
	pvr_cccb_fini(&queue->cccb);

err_free_queue:
	mutex_destroy(&queue->cccb_fence_ctx.job_lock);
	kfree(queue);

	return ERR_PTR(err);
}

void pvr_queue_device_pre_reset(struct pvr_device *pvr_dev)
{
	struct pvr_queue *queue;

	mutex_lock(&pvr_dev->queues.lock);
	list_for_each_entry(queue, &pvr_dev->queues.idle, node)
		pvr_queue_stop(queue, NULL);
	list_for_each_entry(queue, &pvr_dev->queues.active, node)
		pvr_queue_stop(queue, NULL);
	mutex_unlock(&pvr_dev->queues.lock);
}

void pvr_queue_device_post_reset(struct pvr_device *pvr_dev)
{
	struct pvr_queue *queue;

	mutex_lock(&pvr_dev->queues.lock);
	list_for_each_entry(queue, &pvr_dev->queues.active, node)
		pvr_queue_start(queue);
	list_for_each_entry(queue, &pvr_dev->queues.idle, node)
		pvr_queue_start(queue);
	mutex_unlock(&pvr_dev->queues.lock);
}

/**
 * pvr_queue_kill() - Kill a queue.
 * @queue: The queue to kill.
 *
 * Kill the queue so no new jobs can be pushed. Should be called when the
 * context handle is destroyed. The queue object might last longer if jobs
 * are still in flight and holding a reference to the context this queue
 * belongs to.
 */
void pvr_queue_kill(struct pvr_queue *queue)
{
	drm_sched_entity_destroy(&queue->entity);
	dma_fence_put(queue->last_queued_job_scheduled_fence);
	queue->last_queued_job_scheduled_fence = NULL;
}

/**
 * pvr_queue_destroy() - Destroy a queue.
 * @queue: The queue to destroy.
 *
 * Cleanup the queue and free the resources attached to it. Should be
 * called from the context release function.
 */
void pvr_queue_destroy(struct pvr_queue *queue)
{
	if (!queue)
		return;

	mutex_lock(&queue->ctx->pvr_dev->queues.lock);
	list_del_init(&queue->node);
	mutex_unlock(&queue->ctx->pvr_dev->queues.lock);

	drm_sched_fini(&queue->scheduler);
	drm_sched_entity_fini(&queue->entity);

	if (WARN_ON(queue->last_queued_job_scheduled_fence))
		dma_fence_put(queue->last_queued_job_scheduled_fence);

	pvr_queue_cleanup_fw_context(queue);

	pvr_fw_object_unmap_and_destroy(queue->timeline_ufo.fw_obj);
	pvr_fw_object_destroy(queue->reg_state_obj);
	pvr_cccb_fini(&queue->cccb);
	mutex_destroy(&queue->cccb_fence_ctx.job_lock);
	kfree(queue);
}

/**
 * pvr_queue_device_init() - Device-level initialization of queue related fields.
 * @pvr_dev: The device to initialize.
 *
 * Initializes all fields related to queue management in pvr_device.
 *
 * Return:
 *  * 0 on success, or
 *  * An error code on failure.
 */
int pvr_queue_device_init(struct pvr_device *pvr_dev)
{
	int err;

	INIT_LIST_HEAD(&pvr_dev->queues.active);
	INIT_LIST_HEAD(&pvr_dev->queues.idle);
	err = drmm_mutex_init(from_pvr_device(pvr_dev), &pvr_dev->queues.lock);
	if (err)
		return err;

	pvr_dev->sched_wq = alloc_workqueue("powervr-sched", WQ_UNBOUND, 0);
	if (!pvr_dev->sched_wq)
		return -ENOMEM;

	return 0;
}

/**
 * pvr_queue_device_fini() - Device-level cleanup of queue related fields.
 * @pvr_dev: The device to cleanup.
 *
 * Cleanup/free all queue-related resources attached to a pvr_device object.
 */
void pvr_queue_device_fini(struct pvr_device *pvr_dev)
{
	destroy_workqueue(pvr_dev->sched_wq);
}
