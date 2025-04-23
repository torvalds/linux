// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_context.h"
#include "pvr_device.h"
#include "pvr_drv.h"
#include "pvr_gem.h"
#include "pvr_hwrt.h"
#include "pvr_job.h"
#include "pvr_mmu.h"
#include "pvr_power.h"
#include "pvr_rogue_fwif.h"
#include "pvr_rogue_fwif_client.h"
#include "pvr_stream.h"
#include "pvr_stream_defs.h"
#include "pvr_sync.h"

#include <drm/drm_exec.h>
#include <drm/drm_gem.h>
#include <linux/types.h>
#include <uapi/drm/pvr_drm.h>

static void pvr_job_release(struct kref *kref)
{
	struct pvr_job *job = container_of(kref, struct pvr_job, ref_count);

	xa_erase(&job->pvr_dev->job_ids, job->id);

	pvr_hwrt_data_put(job->hwrt);
	pvr_context_put(job->ctx);

	WARN_ON(job->paired_job);

	pvr_queue_job_cleanup(job);
	pvr_job_release_pm_ref(job);

	kfree(job->cmd);
	kfree(job);
}

/**
 * pvr_job_put() - Release reference on job
 * @job: Target job.
 */
void
pvr_job_put(struct pvr_job *job)
{
	if (job)
		kref_put(&job->ref_count, pvr_job_release);
}

/**
 * pvr_job_process_stream() - Build job FW structure from stream
 * @pvr_dev: Device pointer.
 * @cmd_defs: Stream definition.
 * @stream: Pointer to command stream.
 * @stream_size: Size of command stream, in bytes.
 * @job: Pointer to job.
 *
 * Caller is responsible for freeing the output structure.
 *
 * Returns:
 *  * 0 on success,
 *  * -%ENOMEM on out of memory, or
 *  * -%EINVAL on malformed stream.
 */
static int
pvr_job_process_stream(struct pvr_device *pvr_dev, const struct pvr_stream_cmd_defs *cmd_defs,
		       void *stream, u32 stream_size, struct pvr_job *job)
{
	int err;

	job->cmd = kzalloc(cmd_defs->dest_size, GFP_KERNEL);
	if (!job->cmd)
		return -ENOMEM;

	job->cmd_len = cmd_defs->dest_size;

	err = pvr_stream_process(pvr_dev, cmd_defs, stream, stream_size, job->cmd);
	if (err)
		kfree(job->cmd);

	return err;
}

static int pvr_fw_cmd_init(struct pvr_device *pvr_dev, struct pvr_job *job,
			   const struct pvr_stream_cmd_defs *stream_def,
			   u64 stream_userptr, u32 stream_len)
{
	void *stream;
	int err;

	stream = memdup_user(u64_to_user_ptr(stream_userptr), stream_len);
	if (IS_ERR(stream))
		return PTR_ERR(stream);

	err = pvr_job_process_stream(pvr_dev, stream_def, stream, stream_len, job);

	kfree(stream);
	return err;
}

static u32
convert_geom_flags(u32 in_flags)
{
	u32 out_flags = 0;

	if (in_flags & DRM_PVR_SUBMIT_JOB_GEOM_CMD_FIRST)
		out_flags |= ROGUE_GEOM_FLAGS_FIRSTKICK;
	if (in_flags & DRM_PVR_SUBMIT_JOB_GEOM_CMD_LAST)
		out_flags |= ROGUE_GEOM_FLAGS_LASTKICK;
	if (in_flags & DRM_PVR_SUBMIT_JOB_GEOM_CMD_SINGLE_CORE)
		out_flags |= ROGUE_GEOM_FLAGS_SINGLE_CORE;

	return out_flags;
}

static u32
convert_frag_flags(u32 in_flags)
{
	u32 out_flags = 0;

	if (in_flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_SINGLE_CORE)
		out_flags |= ROGUE_FRAG_FLAGS_SINGLE_CORE;
	if (in_flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_DEPTHBUFFER)
		out_flags |= ROGUE_FRAG_FLAGS_DEPTHBUFFER;
	if (in_flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_STENCILBUFFER)
		out_flags |= ROGUE_FRAG_FLAGS_STENCILBUFFER;
	if (in_flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_PREVENT_CDM_OVERLAP)
		out_flags |= ROGUE_FRAG_FLAGS_PREVENT_CDM_OVERLAP;
	if (in_flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_SCRATCHBUFFER)
		out_flags |= ROGUE_FRAG_FLAGS_SCRATCHBUFFER;
	if (in_flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_GET_VIS_RESULTS)
		out_flags |= ROGUE_FRAG_FLAGS_GET_VIS_RESULTS;
	if (in_flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_DISABLE_PIXELMERGE)
		out_flags |= ROGUE_FRAG_FLAGS_DISABLE_PIXELMERGE;

	return out_flags;
}

static int
pvr_geom_job_fw_cmd_init(struct pvr_job *job,
			 struct drm_pvr_job *args)
{
	struct rogue_fwif_cmd_geom *cmd;
	int err;

	if (args->flags & ~DRM_PVR_SUBMIT_JOB_GEOM_CMD_FLAGS_MASK)
		return -EINVAL;

	if (job->ctx->type != DRM_PVR_CTX_TYPE_RENDER)
		return -EINVAL;

	if (!job->hwrt)
		return -EINVAL;

	job->fw_ccb_cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_GEOM;
	err = pvr_fw_cmd_init(job->pvr_dev, job, &pvr_cmd_geom_stream,
			      args->cmd_stream, args->cmd_stream_len);
	if (err)
		return err;

	cmd = job->cmd;
	cmd->cmd_shared.cmn.frame_num = 0;
	cmd->flags = convert_geom_flags(args->flags);
	pvr_fw_object_get_fw_addr(job->hwrt->fw_obj, &cmd->cmd_shared.hwrt_data_fw_addr);
	return 0;
}

static int
pvr_frag_job_fw_cmd_init(struct pvr_job *job,
			 struct drm_pvr_job *args)
{
	struct rogue_fwif_cmd_frag *cmd;
	int err;

	if (args->flags & ~DRM_PVR_SUBMIT_JOB_FRAG_CMD_FLAGS_MASK)
		return -EINVAL;

	if (job->ctx->type != DRM_PVR_CTX_TYPE_RENDER)
		return -EINVAL;

	if (!job->hwrt)
		return -EINVAL;

	job->fw_ccb_cmd_type = (args->flags & DRM_PVR_SUBMIT_JOB_FRAG_CMD_PARTIAL_RENDER) ?
			       ROGUE_FWIF_CCB_CMD_TYPE_FRAG_PR :
			       ROGUE_FWIF_CCB_CMD_TYPE_FRAG;
	err = pvr_fw_cmd_init(job->pvr_dev, job, &pvr_cmd_frag_stream,
			      args->cmd_stream, args->cmd_stream_len);
	if (err)
		return err;

	cmd = job->cmd;
	cmd->cmd_shared.cmn.frame_num = 0;
	cmd->flags = convert_frag_flags(args->flags);
	pvr_fw_object_get_fw_addr(job->hwrt->fw_obj, &cmd->cmd_shared.hwrt_data_fw_addr);
	return 0;
}

static u32
convert_compute_flags(u32 in_flags)
{
	u32 out_flags = 0;

	if (in_flags & DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_PREVENT_ALL_OVERLAP)
		out_flags |= ROGUE_COMPUTE_FLAG_PREVENT_ALL_OVERLAP;
	if (in_flags & DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_SINGLE_CORE)
		out_flags |= ROGUE_COMPUTE_FLAG_SINGLE_CORE;

	return out_flags;
}

static int
pvr_compute_job_fw_cmd_init(struct pvr_job *job,
			    struct drm_pvr_job *args)
{
	struct rogue_fwif_cmd_compute *cmd;
	int err;

	if (args->flags & ~DRM_PVR_SUBMIT_JOB_COMPUTE_CMD_FLAGS_MASK)
		return -EINVAL;

	if (job->ctx->type != DRM_PVR_CTX_TYPE_COMPUTE)
		return -EINVAL;

	job->fw_ccb_cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_CDM;
	err = pvr_fw_cmd_init(job->pvr_dev, job, &pvr_cmd_compute_stream,
			      args->cmd_stream, args->cmd_stream_len);
	if (err)
		return err;

	cmd = job->cmd;
	cmd->common.frame_num = 0;
	cmd->flags = convert_compute_flags(args->flags);
	return 0;
}

static u32
convert_transfer_flags(u32 in_flags)
{
	u32 out_flags = 0;

	if (in_flags & DRM_PVR_SUBMIT_JOB_TRANSFER_CMD_SINGLE_CORE)
		out_flags |= ROGUE_TRANSFER_FLAGS_SINGLE_CORE;

	return out_flags;
}

static int
pvr_transfer_job_fw_cmd_init(struct pvr_job *job,
			     struct drm_pvr_job *args)
{
	struct rogue_fwif_cmd_transfer *cmd;
	int err;

	if (args->flags & ~DRM_PVR_SUBMIT_JOB_TRANSFER_CMD_FLAGS_MASK)
		return -EINVAL;

	if (job->ctx->type != DRM_PVR_CTX_TYPE_TRANSFER_FRAG)
		return -EINVAL;

	job->fw_ccb_cmd_type = ROGUE_FWIF_CCB_CMD_TYPE_TQ_3D;
	err = pvr_fw_cmd_init(job->pvr_dev, job, &pvr_cmd_transfer_stream,
			      args->cmd_stream, args->cmd_stream_len);
	if (err)
		return err;

	cmd = job->cmd;
	cmd->common.frame_num = 0;
	cmd->flags = convert_transfer_flags(args->flags);
	return 0;
}

static int
pvr_job_fw_cmd_init(struct pvr_job *job,
		    struct drm_pvr_job *args)
{
	switch (args->type) {
	case DRM_PVR_JOB_TYPE_GEOMETRY:
		return pvr_geom_job_fw_cmd_init(job, args);

	case DRM_PVR_JOB_TYPE_FRAGMENT:
		return pvr_frag_job_fw_cmd_init(job, args);

	case DRM_PVR_JOB_TYPE_COMPUTE:
		return pvr_compute_job_fw_cmd_init(job, args);

	case DRM_PVR_JOB_TYPE_TRANSFER_FRAG:
		return pvr_transfer_job_fw_cmd_init(job, args);

	default:
		return -EINVAL;
	}
}

/**
 * struct pvr_job_data - Helper container for pairing jobs with the
 * sync_ops supplied for them by the user.
 */
struct pvr_job_data {
	/** @job: Pointer to the job. */
	struct pvr_job *job;

	/** @sync_ops: Pointer to the sync_ops associated with @job. */
	struct drm_pvr_sync_op *sync_ops;

	/** @sync_op_count: Number of members of @sync_ops. */
	u32 sync_op_count;
};

/**
 * prepare_job_syncs() - Prepare all sync objects for a single job.
 * @pvr_file: PowerVR file.
 * @job_data: Precreated job and sync_ops array.
 * @signal_array: xarray to receive signal sync objects.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error code returned by pvr_sync_signal_array_collect_ops(),
 *    pvr_sync_add_deps_to_job(), drm_sched_job_add_resv_dependencies() or
 *    pvr_sync_signal_array_update_fences().
 */
static int
prepare_job_syncs(struct pvr_file *pvr_file,
		  struct pvr_job_data *job_data,
		  struct xarray *signal_array)
{
	struct dma_fence *done_fence;
	int err = pvr_sync_signal_array_collect_ops(signal_array,
						    from_pvr_file(pvr_file),
						    job_data->sync_op_count,
						    job_data->sync_ops);

	if (err)
		return err;

	err = pvr_sync_add_deps_to_job(pvr_file, &job_data->job->base,
				       job_data->sync_op_count,
				       job_data->sync_ops, signal_array);
	if (err)
		return err;

	if (job_data->job->hwrt) {
		/* The geometry job writes the HWRT region headers, which are
		 * then read by the fragment job.
		 */
		struct drm_gem_object *obj =
			gem_from_pvr_gem(job_data->job->hwrt->fw_obj->gem);
		enum dma_resv_usage usage =
			dma_resv_usage_rw(job_data->job->type ==
					  DRM_PVR_JOB_TYPE_GEOMETRY);

		dma_resv_lock(obj->resv, NULL);
		err = drm_sched_job_add_resv_dependencies(&job_data->job->base,
							  obj->resv, usage);
		dma_resv_unlock(obj->resv);
		if (err)
			return err;
	}

	/* We need to arm the job to get the job done fence. */
	done_fence = pvr_queue_job_arm(job_data->job);

	err = pvr_sync_signal_array_update_fences(signal_array,
						  job_data->sync_op_count,
						  job_data->sync_ops,
						  done_fence);
	return err;
}

/**
 * prepare_job_syncs_for_each() - Prepare all sync objects for an array of jobs.
 * @pvr_file: PowerVR file.
 * @job_data: Array of precreated jobs and their sync_ops.
 * @job_count: Number of jobs.
 * @signal_array: xarray to receive signal sync objects.
 *
 * Returns:
 *  * 0 on success, or
 *  * Any error code returned by pvr_vm_bind_job_prepare_syncs().
 */
static int
prepare_job_syncs_for_each(struct pvr_file *pvr_file,
			   struct pvr_job_data *job_data,
			   u32 *job_count,
			   struct xarray *signal_array)
{
	for (u32 i = 0; i < *job_count; i++) {
		int err = prepare_job_syncs(pvr_file, &job_data[i],
					    signal_array);

		if (err) {
			*job_count = i;
			return err;
		}
	}

	return 0;
}

static struct pvr_job *
create_job(struct pvr_device *pvr_dev,
	   struct pvr_file *pvr_file,
	   struct drm_pvr_job *args)
{
	struct pvr_job *job = NULL;
	int err;

	if (!args->cmd_stream || !args->cmd_stream_len)
		return ERR_PTR(-EINVAL);

	if (args->type != DRM_PVR_JOB_TYPE_GEOMETRY &&
	    args->type != DRM_PVR_JOB_TYPE_FRAGMENT &&
	    (args->hwrt.set_handle || args->hwrt.data_index))
		return ERR_PTR(-EINVAL);

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return ERR_PTR(-ENOMEM);

	kref_init(&job->ref_count);
	job->type = args->type;
	job->pvr_dev = pvr_dev;

	err = xa_alloc(&pvr_dev->job_ids, &job->id, job, xa_limit_32b, GFP_KERNEL);
	if (err)
		goto err_put_job;

	job->ctx = pvr_context_lookup(pvr_file, args->context_handle);
	if (!job->ctx) {
		err = -EINVAL;
		goto err_put_job;
	}

	if (args->hwrt.set_handle) {
		job->hwrt = pvr_hwrt_data_lookup(pvr_file, args->hwrt.set_handle,
						 args->hwrt.data_index);
		if (!job->hwrt) {
			err = -EINVAL;
			goto err_put_job;
		}
	}

	err = pvr_job_fw_cmd_init(job, args);
	if (err)
		goto err_put_job;

	err = pvr_queue_job_init(job);
	if (err)
		goto err_put_job;

	return job;

err_put_job:
	pvr_job_put(job);
	return ERR_PTR(err);
}

/**
 * pvr_job_data_fini() - Cleanup all allocs used to set up job submission.
 * @job_data: Job data array.
 * @job_count: Number of members of @job_data.
 */
static void
pvr_job_data_fini(struct pvr_job_data *job_data, u32 job_count)
{
	for (u32 i = 0; i < job_count; i++) {
		pvr_job_put(job_data[i].job);
		kvfree(job_data[i].sync_ops);
	}
}

/**
 * pvr_job_data_init() - Init an array of created jobs, associating them with
 * the appropriate sync_ops args, which will be copied in.
 * @pvr_dev: Target PowerVR device.
 * @pvr_file: Pointer to PowerVR file structure.
 * @job_args: Job args array copied from user.
 * @job_count: Number of members of @job_args.
 * @job_data_out: Job data array.
 */
static int pvr_job_data_init(struct pvr_device *pvr_dev,
			     struct pvr_file *pvr_file,
			     struct drm_pvr_job *job_args,
			     u32 *job_count,
			     struct pvr_job_data *job_data_out)
{
	int err = 0, i = 0;

	for (; i < *job_count; i++) {
		job_data_out[i].job =
			create_job(pvr_dev, pvr_file, &job_args[i]);
		err = PTR_ERR_OR_ZERO(job_data_out[i].job);

		if (err) {
			*job_count = i;
			job_data_out[i].job = NULL;
			goto err_cleanup;
		}

		err = PVR_UOBJ_GET_ARRAY(job_data_out[i].sync_ops,
					 &job_args[i].sync_ops);
		if (err) {
			*job_count = i;

			/* Ensure the job created above is also cleaned up. */
			i++;
			goto err_cleanup;
		}

		job_data_out[i].sync_op_count = job_args[i].sync_ops.count;
	}

	return 0;

err_cleanup:
	pvr_job_data_fini(job_data_out, i);

	return err;
}

static void
push_jobs(struct pvr_job_data *job_data, u32 job_count)
{
	for (u32 i = 0; i < job_count; i++)
		pvr_queue_job_push(job_data[i].job);
}

static int
prepare_fw_obj_resv(struct drm_exec *exec, struct pvr_fw_object *fw_obj)
{
	return drm_exec_prepare_obj(exec, gem_from_pvr_gem(fw_obj->gem), 1);
}

static int
jobs_lock_all_objs(struct drm_exec *exec, struct pvr_job_data *job_data,
		   u32 job_count)
{
	for (u32 i = 0; i < job_count; i++) {
		struct pvr_job *job = job_data[i].job;

		/* Grab a lock on a the context, to guard against
		 * concurrent submission to the same queue.
		 */
		int err = drm_exec_lock_obj(exec,
					    gem_from_pvr_gem(job->ctx->fw_obj->gem));

		if (err)
			return err;

		if (job->hwrt) {
			err = prepare_fw_obj_resv(exec,
						  job->hwrt->fw_obj);
			if (err)
				return err;
		}
	}

	return 0;
}

static int
prepare_job_resvs_for_each(struct drm_exec *exec, struct pvr_job_data *job_data,
			   u32 job_count)
{
	drm_exec_until_all_locked(exec) {
		int err = jobs_lock_all_objs(exec, job_data, job_count);

		drm_exec_retry_on_contention(exec);
		if (err)
			return err;
	}

	return 0;
}

static void
update_job_resvs(struct pvr_job *job)
{
	if (job->hwrt) {
		enum dma_resv_usage usage = job->type == DRM_PVR_JOB_TYPE_GEOMETRY ?
					    DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_READ;
		struct drm_gem_object *obj = gem_from_pvr_gem(job->hwrt->fw_obj->gem);

		dma_resv_add_fence(obj->resv, &job->base.s_fence->finished, usage);
	}
}

static void
update_job_resvs_for_each(struct pvr_job_data *job_data, u32 job_count)
{
	for (u32 i = 0; i < job_count; i++)
		update_job_resvs(job_data[i].job);
}

static bool can_combine_jobs(struct pvr_job *a, struct pvr_job *b)
{
	struct pvr_job *geom_job = a, *frag_job = b;

	/* Geometry and fragment jobs can be combined if they are queued to the
	 * same context and targeting the same HWRT.
	 */
	if (a->type != DRM_PVR_JOB_TYPE_GEOMETRY ||
	    b->type != DRM_PVR_JOB_TYPE_FRAGMENT ||
	    a->ctx != b->ctx ||
	    a->hwrt != b->hwrt)
		return false;

	/* We combine when we see an explicit geom -> frag dep. */
	return drm_sched_job_has_dependency(&frag_job->base,
					    &geom_job->base.s_fence->scheduled);
}

static struct dma_fence *
get_last_queued_job_scheduled_fence(struct pvr_queue *queue,
				    struct pvr_job_data *job_data,
				    u32 cur_job_pos)
{
	/* We iterate over the current job array in reverse order to grab the
	 * last to-be-queued job targeting the same queue.
	 */
	for (u32 i = cur_job_pos; i > 0; i--) {
		struct pvr_job *job = job_data[i - 1].job;

		if (job->ctx == queue->ctx && job->type == queue->type)
			return dma_fence_get(&job->base.s_fence->scheduled);
	}

	/* If we didn't find any, we just return the last queued job scheduled
	 * fence attached to the queue.
	 */
	return dma_fence_get(queue->last_queued_job_scheduled_fence);
}

static int
pvr_jobs_link_geom_frag(struct pvr_job_data *job_data, u32 *job_count)
{
	for (u32 i = 0; i < *job_count - 1; i++) {
		struct pvr_job *geom_job = job_data[i].job;
		struct pvr_job *frag_job = job_data[i + 1].job;
		struct pvr_queue *frag_queue;
		struct dma_fence *f;

		if (!can_combine_jobs(job_data[i].job, job_data[i + 1].job))
			continue;

		/* The fragment job will be submitted by the geometry queue. We
		 * need to make sure it comes after all the other fragment jobs
		 * queued before it.
		 */
		frag_queue = pvr_context_get_queue_for_job(frag_job->ctx,
							   frag_job->type);
		f = get_last_queued_job_scheduled_fence(frag_queue, job_data,
							i);
		if (f) {
			int err = drm_sched_job_add_dependency(&geom_job->base,
							       f);
			if (err) {
				*job_count = i;
				return err;
			}
		}

		/* The KCCB slot will be reserved by the geometry job, so we can
		 * drop the KCCB fence on the fragment job.
		 */
		pvr_kccb_fence_put(frag_job->kccb_fence);
		frag_job->kccb_fence = NULL;

		geom_job->paired_job = frag_job;
		frag_job->paired_job = geom_job;

		/* The geometry job pvr_job structure is used when the fragment
		 * job is being prepared by the GPU scheduler. Have the fragment
		 * job hold a reference on the geometry job to prevent it being
		 * freed until the fragment job has finished with it.
		 */
		pvr_job_get(geom_job);

		/* Skip the fragment job we just paired to the geometry job. */
		i++;
	}

	return 0;
}

/**
 * pvr_submit_jobs() - Submit jobs to the GPU
 * @pvr_dev: Target PowerVR device.
 * @pvr_file: Pointer to PowerVR file structure.
 * @args: Ioctl args.
 *
 * This initial implementation is entirely synchronous; on return the GPU will
 * be idle. This will not be the case for future implementations.
 *
 * Returns:
 *  * 0 on success,
 *  * -%EFAULT if arguments can not be copied from user space, or
 *  * -%EINVAL on invalid arguments, or
 *  * Any other error.
 */
int
pvr_submit_jobs(struct pvr_device *pvr_dev, struct pvr_file *pvr_file,
		struct drm_pvr_ioctl_submit_jobs_args *args)
{
	struct pvr_job_data *job_data = NULL;
	struct drm_pvr_job *job_args;
	struct xarray signal_array;
	u32 jobs_alloced = 0;
	struct drm_exec exec;
	int err;

	if (!args->jobs.count)
		return -EINVAL;

	err = PVR_UOBJ_GET_ARRAY(job_args, &args->jobs);
	if (err)
		return err;

	job_data = kvmalloc_array(args->jobs.count, sizeof(*job_data),
				  GFP_KERNEL | __GFP_ZERO);
	if (!job_data) {
		err = -ENOMEM;
		goto out_free;
	}

	err = pvr_job_data_init(pvr_dev, pvr_file, job_args, &args->jobs.count,
				job_data);
	if (err)
		goto out_free;

	jobs_alloced = args->jobs.count;

	/*
	 * Flush MMU if needed - this has been deferred until now to avoid
	 * overuse of this expensive operation.
	 */
	err = pvr_mmu_flush_exec(pvr_dev, false);
	if (err)
		goto out_job_data_cleanup;

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT | DRM_EXEC_IGNORE_DUPLICATES, 0);

	xa_init_flags(&signal_array, XA_FLAGS_ALLOC);

	err = prepare_job_syncs_for_each(pvr_file, job_data, &args->jobs.count,
					 &signal_array);
	if (err)
		goto out_exec_fini;

	err = prepare_job_resvs_for_each(&exec, job_data, args->jobs.count);
	if (err)
		goto out_exec_fini;

	err = pvr_jobs_link_geom_frag(job_data, &args->jobs.count);
	if (err)
		goto out_exec_fini;

	/* Anything after that point must succeed because we start exposing job
	 * finished fences to the outside world.
	 */
	update_job_resvs_for_each(job_data, args->jobs.count);
	push_jobs(job_data, args->jobs.count);
	pvr_sync_signal_array_push_fences(&signal_array);
	err = 0;

out_exec_fini:
	drm_exec_fini(&exec);
	pvr_sync_signal_array_cleanup(&signal_array);

out_job_data_cleanup:
	pvr_job_data_fini(job_data, jobs_alloced);

out_free:
	kvfree(job_data);
	kvfree(job_args);

	return err;
}
