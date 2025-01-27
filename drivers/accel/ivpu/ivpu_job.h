/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#ifndef __IVPU_JOB_H__
#define __IVPU_JOB_H__

#include <linux/kref.h>
#include <linux/idr.h>

#include "ivpu_gem.h"

struct ivpu_device;
struct ivpu_file_priv;

/**
 * struct ivpu_cmdq - Object representing device queue used to send jobs.
 * @jobq:	   Pointer to job queue memory shared with the device
 * @mem:           Memory allocated for the job queue, shared with device
 * @entry_count    Number of job entries in the queue
 * @db_id:	   Doorbell assigned to this job queue
 * @db_registered: True if doorbell is registered in device
 */
struct ivpu_cmdq {
	struct vpu_job_queue *jobq;
	struct ivpu_bo *primary_preempt_buf;
	struct ivpu_bo *secondary_preempt_buf;
	struct ivpu_bo *mem;
	u32 entry_count;
	u32 id;
	u32 db_id;
	bool db_registered;
	u8 priority;
};

/**
 * struct ivpu_job - KMD object that represents batchbuffer / DMA buffer.
 * Each batch / DMA buffer is a job to be submitted and executed by the VPU FW.
 * This is a unit of execution, and be tracked by the job_id for
 * any status reporting from VPU FW through IPC JOB RET/DONE message.
 * @file_priv:		  The client that submitted this job
 * @job_id:		  Job ID for KMD tracking and job status reporting from VPU FW
 * @status:		  Status of the Job from IPC JOB RET/DONE message
 * @batch_buffer:	  CPU vaddr points to the batch buffer memory allocated for the job
 * @submit_status_offset: Offset within batch buffer where job completion handler
			  will update the job status
 */
struct ivpu_job {
	struct ivpu_device *vdev;
	struct ivpu_file_priv *file_priv;
	struct dma_fence *done_fence;
	u64 cmd_buf_vpu_addr;
	u32 job_id;
	u32 engine_idx;
	size_t bo_count;
	struct ivpu_bo *bos[] __counted_by(bo_count);
};

int ivpu_submit_ioctl(struct drm_device *dev, void *data, struct drm_file *file);

void ivpu_context_abort_locked(struct ivpu_file_priv *file_priv);

void ivpu_cmdq_release_all_locked(struct ivpu_file_priv *file_priv);
void ivpu_cmdq_reset_all_contexts(struct ivpu_device *vdev);

void ivpu_job_done_consumer_init(struct ivpu_device *vdev);
void ivpu_job_done_consumer_fini(struct ivpu_device *vdev);

void ivpu_jobs_abort_all(struct ivpu_device *vdev);

#endif /* __IVPU_JOB_H__ */
