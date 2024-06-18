/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_JOB_H
#define PVR_JOB_H

#include <uapi/drm/pvr_drm.h>

#include <linux/kref.h>
#include <linux/types.h>

#include <drm/drm_gem.h>
#include <drm/gpu_scheduler.h>

#include "pvr_power.h"

/* Forward declaration from "pvr_context.h". */
struct pvr_context;

/* Forward declarations from "pvr_device.h". */
struct pvr_device;
struct pvr_file;

/* Forward declarations from "pvr_hwrt.h". */
struct pvr_hwrt_data;

/* Forward declaration from "pvr_queue.h". */
struct pvr_queue;

struct pvr_job {
	/** @base: drm_sched_job object. */
	struct drm_sched_job base;

	/** @ref_count: Refcount for job. */
	struct kref ref_count;

	/** @type: Type of job. */
	enum drm_pvr_job_type type;

	/** @id: Job ID number. */
	u32 id;

	/**
	 * @paired_job: Job paired to this job.
	 *
	 * This field is only meaningful for geometry and fragment jobs.
	 *
	 * Paired jobs are executed on the same context, and need to be submitted
	 * atomically to the FW, to make sure the partial render logic has a
	 * fragment job to execute when the Parameter Manager runs out of memory.
	 *
	 * The geometry job should point to the fragment job it's paired with,
	 * and the fragment job should point to the geometry job it's paired with.
	 */
	struct pvr_job *paired_job;

	/** @cccb_fence: Fence used to wait for CCCB space. */
	struct dma_fence *cccb_fence;

	/** @kccb_fence: Fence used to wait for KCCB space. */
	struct dma_fence *kccb_fence;

	/** @done_fence: Fence to signal when the job is done. */
	struct dma_fence *done_fence;

	/** @pvr_dev: Device pointer. */
	struct pvr_device *pvr_dev;

	/** @ctx: Pointer to owning context. */
	struct pvr_context *ctx;

	/** @cmd: Command data. Format depends on @type. */
	void *cmd;

	/** @cmd_len: Length of command data, in bytes. */
	u32 cmd_len;

	/**
	 * @fw_ccb_cmd_type: Firmware CCB command type. Must be one of %ROGUE_FWIF_CCB_CMD_TYPE_*.
	 */
	u32 fw_ccb_cmd_type;

	/** @hwrt: HWRT object. Will be NULL for compute and transfer jobs. */
	struct pvr_hwrt_data *hwrt;

	/**
	 * @has_pm_ref: True if the job has a power ref, thus forcing the GPU to stay on until
	 * the job is done.
	 */
	bool has_pm_ref;
};

/**
 * pvr_job_get() - Take additional reference on job.
 * @job: Job pointer.
 *
 * Call pvr_job_put() to release.
 *
 * Returns:
 *  * The requested job on success, or
 *  * %NULL if no job pointer passed.
 */
static __always_inline struct pvr_job *
pvr_job_get(struct pvr_job *job)
{
	if (job)
		kref_get(&job->ref_count);

	return job;
}

void pvr_job_put(struct pvr_job *job);

/**
 * pvr_job_release_pm_ref() - Release the PM ref if the job acquired it.
 * @job: The job to release the PM ref on.
 */
static __always_inline void
pvr_job_release_pm_ref(struct pvr_job *job)
{
	if (job->has_pm_ref) {
		pvr_power_put(job->pvr_dev);
		job->has_pm_ref = false;
	}
}

/**
 * pvr_job_get_pm_ref() - Get a PM ref and attach it to the job.
 * @job: The job to attach the PM ref to.
 *
 * Return:
 *  * 0 on success, or
 *  * Any error returned by pvr_power_get() otherwise.
 */
static __always_inline int
pvr_job_get_pm_ref(struct pvr_job *job)
{
	int err;

	if (job->has_pm_ref)
		return 0;

	err = pvr_power_get(job->pvr_dev);
	if (!err)
		job->has_pm_ref = true;

	return err;
}

int pvr_job_wait_first_non_signaled_native_dep(struct pvr_job *job);

bool pvr_job_non_native_deps_done(struct pvr_job *job);

int pvr_job_fits_in_cccb(struct pvr_job *job, unsigned long native_dep_count);

void pvr_job_submit(struct pvr_job *job);

int pvr_submit_jobs(struct pvr_device *pvr_dev, struct pvr_file *pvr_file,
		    struct drm_pvr_ioctl_submit_jobs_args *args);

#endif /* PVR_JOB_H */
