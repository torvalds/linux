/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2025 Intel Corporation
 */

#ifndef __IVPU_JOB_H__
#define __IVPU_JOB_H__

#include <linux/kref.h>
#include <linux/idr.h>

#include "ivpu_gem.h"

struct ivpu_device;
struct ivpu_file_priv;

/**
 * struct ivpu_cmdq - Represents a command queue for submitting jobs to the VPU.
 * Tracks queue memory, preemption buffers, and metadata for job management.
 * @jobq:                Pointer to job queue memory shared with the device
 * @primary_preempt_buf: Primary preemption buffer for this queue (optional)
 * @secondary_preempt_buf: Secondary preemption buffer for this queue (optional)
 * @mem:                 Memory allocated for the job queue, shared with device
 * @entry_count:         Number of job entries in the queue
 * @id:                  Unique command queue ID
 * @db_id:               Doorbell ID assigned to this job queue
 * @priority:            Priority level of the command queue
 * @is_legacy:           True if this is a legacy command queue
 */
struct ivpu_cmdq {
	struct vpu_job_queue *jobq;
	struct ivpu_bo *primary_preempt_buf;
	struct ivpu_bo *secondary_preempt_buf;
	struct ivpu_bo *mem;
	u32 entry_count;
	u32 id;
	u32 db_id;
	u8 priority;
	bool is_legacy;
};

/**
 * struct ivpu_job - Representing a batch or DMA buffer submitted to the VPU.
 * Each job is a unit of execution, tracked by job_id for status reporting from VPU FW.
 * The structure holds all resources and metadata needed for job submission, execution,
 * and completion handling.
 * @vdev:                Pointer to the VPU device
 * @file_priv:           The client context that submitted this job
 * @done_fence:          Fence signaled when job completes
 * @cmd_buf_vpu_addr:    VPU address of the command buffer for this job
 * @cmdq_id:             Command queue ID used for submission
 * @job_id:              Unique job ID for tracking and status reporting
 * @engine_idx:          Engine index for job execution
 * @job_status:          Status reported by firmware for this job
 * @primary_preempt_buf: Primary preemption buffer for job
 * @secondary_preempt_buf: Secondary preemption buffer for job (optional)
 * @bo_count:            Number of buffer objects associated with this job
 * @bos:                 Array of buffer objects used by the job (batch buffer is at index 0)
 */
struct ivpu_job {
	struct ivpu_device *vdev;
	struct ivpu_file_priv *file_priv;
	struct dma_fence *done_fence;
	u64 cmd_buf_vpu_addr;
	u32 cmdq_id;
	u32 job_id;
	u32 engine_idx;
	u32 job_status;
	struct ivpu_bo *primary_preempt_buf;
	struct ivpu_bo *secondary_preempt_buf;
	size_t bo_count;
	struct ivpu_bo *bos[] __counted_by(bo_count);
};

int ivpu_submit_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_cmdq_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_cmdq_destroy_ioctl(struct drm_device *dev, void *data, struct drm_file *file);
int ivpu_cmdq_submit_ioctl(struct drm_device *dev, void *data, struct drm_file *file);

void ivpu_context_abort_locked(struct ivpu_file_priv *file_priv);

void ivpu_cmdq_release_all_locked(struct ivpu_file_priv *file_priv);
void ivpu_cmdq_reset_all_contexts(struct ivpu_device *vdev);
void ivpu_cmdq_abort_all_jobs(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id);

void ivpu_job_done_consumer_init(struct ivpu_device *vdev);
void ivpu_job_done_consumer_fini(struct ivpu_device *vdev);
bool ivpu_job_handle_engine_error(struct ivpu_device *vdev, u32 job_id, u32 job_status);
void ivpu_context_abort_work_fn(struct work_struct *work);

void ivpu_jobs_abort_all(struct ivpu_device *vdev);

#endif /* __IVPU_JOB_H__ */
