// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <drm/drm_file.h>

#include <linux/bitfield.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>
#include <linux/module.h>
#include <uapi/drm/ivpu_accel.h>

#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_job.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_mmu.h"
#include "ivpu_pm.h"
#include "ivpu_trace.h"
#include "vpu_boot_api.h"

#define CMD_BUF_IDX	     0
#define JOB_MAX_BUFFER_COUNT 65535

static void ivpu_cmdq_ring_db(struct ivpu_device *vdev, struct ivpu_cmdq *cmdq)
{
	ivpu_hw_db_set(vdev, cmdq->db_id);
}

static int ivpu_preemption_buffers_create(struct ivpu_device *vdev,
					  struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	u64 primary_size = ALIGN(vdev->fw->primary_preempt_buf_size, PAGE_SIZE);
	u64 secondary_size = ALIGN(vdev->fw->secondary_preempt_buf_size, PAGE_SIZE);

	if (vdev->fw->sched_mode != VPU_SCHEDULING_MODE_HW ||
	    ivpu_test_mode & IVPU_TEST_MODE_MIP_DISABLE)
		return 0;

	cmdq->primary_preempt_buf = ivpu_bo_create(vdev, &file_priv->ctx, &vdev->hw->ranges.user,
						   primary_size, DRM_IVPU_BO_WC);
	if (!cmdq->primary_preempt_buf) {
		ivpu_err(vdev, "Failed to create primary preemption buffer\n");
		return -ENOMEM;
	}

	cmdq->secondary_preempt_buf = ivpu_bo_create(vdev, &file_priv->ctx, &vdev->hw->ranges.dma,
						     secondary_size, DRM_IVPU_BO_WC);
	if (!cmdq->secondary_preempt_buf) {
		ivpu_err(vdev, "Failed to create secondary preemption buffer\n");
		goto err_free_primary;
	}

	return 0;

err_free_primary:
	ivpu_bo_free(cmdq->primary_preempt_buf);
	cmdq->primary_preempt_buf = NULL;
	return -ENOMEM;
}

static void ivpu_preemption_buffers_free(struct ivpu_device *vdev,
					 struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	if (vdev->fw->sched_mode != VPU_SCHEDULING_MODE_HW)
		return;

	if (cmdq->primary_preempt_buf)
		ivpu_bo_free(cmdq->primary_preempt_buf);
	if (cmdq->secondary_preempt_buf)
		ivpu_bo_free(cmdq->secondary_preempt_buf);
}

static struct ivpu_cmdq *ivpu_cmdq_alloc(struct ivpu_file_priv *file_priv)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_cmdq *cmdq;
	int ret;

	cmdq = kzalloc(sizeof(*cmdq), GFP_KERNEL);
	if (!cmdq)
		return NULL;

	cmdq->mem = ivpu_bo_create_global(vdev, SZ_4K, DRM_IVPU_BO_WC | DRM_IVPU_BO_MAPPABLE);
	if (!cmdq->mem)
		goto err_free_cmdq;

	ret = ivpu_preemption_buffers_create(vdev, file_priv, cmdq);
	if (ret)
		ivpu_warn(vdev, "Failed to allocate preemption buffers, preemption limited\n");

	return cmdq;

err_free_cmdq:
	kfree(cmdq);
	return NULL;
}

static void ivpu_cmdq_free(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	ivpu_preemption_buffers_free(file_priv->vdev, file_priv, cmdq);
	ivpu_bo_free(cmdq->mem);
	kfree(cmdq);
}

static struct ivpu_cmdq *ivpu_cmdq_create(struct ivpu_file_priv *file_priv, u8 priority,
					  bool is_legacy)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_cmdq *cmdq = NULL;
	int ret;

	lockdep_assert_held(&file_priv->lock);

	cmdq = ivpu_cmdq_alloc(file_priv);
	if (!cmdq) {
		ivpu_err(vdev, "Failed to allocate command queue\n");
		return NULL;
	}

	cmdq->priority = priority;
	cmdq->is_legacy = is_legacy;

	ret = xa_alloc_cyclic(&file_priv->cmdq_xa, &cmdq->id, cmdq, file_priv->cmdq_limit,
			      &file_priv->cmdq_id_next, GFP_KERNEL);
	if (ret < 0) {
		ivpu_err(vdev, "Failed to allocate command queue ID: %d\n", ret);
		goto err_free_cmdq;
	}

	ivpu_dbg(vdev, JOB, "Command queue %d created, ctx %d\n", cmdq->id, file_priv->ctx.id);
	return cmdq;

err_free_cmdq:
	ivpu_cmdq_free(file_priv, cmdq);
	return NULL;
}

static int ivpu_hws_cmdq_init(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq, u16 engine,
			      u8 priority)
{
	struct ivpu_device *vdev = file_priv->vdev;
	int ret;

	ret = ivpu_jsm_hws_create_cmdq(vdev, file_priv->ctx.id, file_priv->ctx.id, cmdq->id,
				       task_pid_nr(current), engine,
				       cmdq->mem->vpu_addr, ivpu_bo_size(cmdq->mem));
	if (ret)
		return ret;

	ret = ivpu_jsm_hws_set_context_sched_properties(vdev, file_priv->ctx.id, cmdq->id,
							priority);
	if (ret)
		return ret;

	return 0;
}

static int ivpu_register_db(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	struct ivpu_device *vdev = file_priv->vdev;
	int ret;

	ret = xa_alloc_cyclic(&vdev->db_xa, &cmdq->db_id, NULL, vdev->db_limit, &vdev->db_next,
			      GFP_KERNEL);
	if (ret < 0) {
		ivpu_err(vdev, "Failed to allocate doorbell ID: %d\n", ret);
		return ret;
	}

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW)
		ret = ivpu_jsm_hws_register_db(vdev, file_priv->ctx.id, cmdq->id, cmdq->db_id,
					       cmdq->mem->vpu_addr, ivpu_bo_size(cmdq->mem));
	else
		ret = ivpu_jsm_register_db(vdev, file_priv->ctx.id, cmdq->db_id,
					   cmdq->mem->vpu_addr, ivpu_bo_size(cmdq->mem));

	if (!ret)
		ivpu_dbg(vdev, JOB, "DB %d registered to cmdq %d ctx %d priority %d\n",
			 cmdq->db_id, cmdq->id, file_priv->ctx.id, cmdq->priority);
	else
		xa_erase(&vdev->db_xa, cmdq->db_id);

	return ret;
}

static void ivpu_cmdq_jobq_init(struct ivpu_device *vdev, struct vpu_job_queue *jobq)
{
	jobq->header.engine_idx = VPU_ENGINE_COMPUTE;
	jobq->header.head = 0;
	jobq->header.tail = 0;

	if (ivpu_test_mode & IVPU_TEST_MODE_TURBO) {
		ivpu_dbg(vdev, JOB, "Turbo mode enabled");
		jobq->header.flags = VPU_JOB_QUEUE_FLAGS_TURBO_MODE;
	}

	wmb(); /* Flush WC buffer for jobq->header */
}

static inline u32 ivpu_cmdq_get_entry_count(struct ivpu_cmdq *cmdq)
{
	size_t size = ivpu_bo_size(cmdq->mem) - sizeof(struct vpu_job_queue_header);

	return size / sizeof(struct vpu_job_queue_entry);
}

static int ivpu_cmdq_register(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	struct ivpu_device *vdev = file_priv->vdev;
	int ret;

	lockdep_assert_held(&file_priv->lock);

	if (cmdq->db_id)
		return 0;

	cmdq->entry_count = ivpu_cmdq_get_entry_count(cmdq);
	cmdq->jobq = (struct vpu_job_queue *)ivpu_bo_vaddr(cmdq->mem);

	ivpu_cmdq_jobq_init(vdev, cmdq->jobq);

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW) {
		ret = ivpu_hws_cmdq_init(file_priv, cmdq, VPU_ENGINE_COMPUTE, cmdq->priority);
		if (ret)
			return ret;
	}

	ret = ivpu_register_db(file_priv, cmdq);
	if (ret)
		return ret;

	return 0;
}

static int ivpu_cmdq_unregister(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	struct ivpu_device *vdev = file_priv->vdev;
	int ret;

	lockdep_assert_held(&file_priv->lock);

	if (!cmdq->db_id)
		return 0;

	ret = ivpu_jsm_unregister_db(vdev, cmdq->db_id);
	if (!ret)
		ivpu_dbg(vdev, JOB, "DB %d unregistered\n", cmdq->db_id);

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW) {
		ret = ivpu_jsm_hws_destroy_cmdq(vdev, file_priv->ctx.id, cmdq->id);
		if (!ret)
			ivpu_dbg(vdev, JOB, "Command queue %d destroyed, ctx %d\n",
				 cmdq->id, file_priv->ctx.id);
	}

	xa_erase(&file_priv->vdev->db_xa, cmdq->db_id);
	cmdq->db_id = 0;

	return 0;
}

static inline u8 ivpu_job_to_jsm_priority(u8 priority)
{
	if (priority == DRM_IVPU_JOB_PRIORITY_DEFAULT)
		return VPU_JOB_SCHEDULING_PRIORITY_BAND_NORMAL;

	return priority - 1;
}

static void ivpu_cmdq_destroy(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	ivpu_cmdq_unregister(file_priv, cmdq);
	xa_erase(&file_priv->cmdq_xa, cmdq->id);
	ivpu_cmdq_free(file_priv, cmdq);
}

static struct ivpu_cmdq *ivpu_cmdq_acquire_legacy(struct ivpu_file_priv *file_priv, u8 priority)
{
	struct ivpu_cmdq *cmdq;
	unsigned long id;

	lockdep_assert_held(&file_priv->lock);

	xa_for_each(&file_priv->cmdq_xa, id, cmdq)
		if (cmdq->is_legacy && cmdq->priority == priority)
			break;

	if (!cmdq) {
		cmdq = ivpu_cmdq_create(file_priv, priority, true);
		if (!cmdq)
			return NULL;
	}

	return cmdq;
}

static struct ivpu_cmdq *ivpu_cmdq_acquire(struct ivpu_file_priv *file_priv, u32 cmdq_id)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_cmdq *cmdq;

	lockdep_assert_held(&file_priv->lock);

	cmdq = xa_load(&file_priv->cmdq_xa, cmdq_id);
	if (!cmdq) {
		ivpu_warn_ratelimited(vdev, "Failed to find command queue with ID: %u\n", cmdq_id);
		return NULL;
	}

	return cmdq;
}

void ivpu_cmdq_release_all_locked(struct ivpu_file_priv *file_priv)
{
	struct ivpu_cmdq *cmdq;
	unsigned long cmdq_id;

	lockdep_assert_held(&file_priv->lock);

	xa_for_each(&file_priv->cmdq_xa, cmdq_id, cmdq)
		ivpu_cmdq_destroy(file_priv, cmdq);
}

/*
 * Mark the doorbell as unregistered
 * This function needs to be called when the VPU hardware is restarted
 * and FW loses job queue state. The next time job queue is used it
 * will be registered again.
 */
static void ivpu_cmdq_reset(struct ivpu_file_priv *file_priv)
{
	struct ivpu_cmdq *cmdq;
	unsigned long cmdq_id;

	mutex_lock(&file_priv->lock);

	xa_for_each(&file_priv->cmdq_xa, cmdq_id, cmdq) {
		xa_erase(&file_priv->vdev->db_xa, cmdq->db_id);
		cmdq->db_id = 0;
	}

	mutex_unlock(&file_priv->lock);
}

void ivpu_cmdq_reset_all_contexts(struct ivpu_device *vdev)
{
	struct ivpu_file_priv *file_priv;
	unsigned long ctx_id;

	mutex_lock(&vdev->context_list_lock);

	xa_for_each(&vdev->context_xa, ctx_id, file_priv)
		ivpu_cmdq_reset(file_priv);

	mutex_unlock(&vdev->context_list_lock);
}

void ivpu_context_abort_locked(struct ivpu_file_priv *file_priv)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_cmdq *cmdq;
	unsigned long cmdq_id;

	lockdep_assert_held(&file_priv->lock);
	ivpu_dbg(vdev, JOB, "Context ID: %u abort\n", file_priv->ctx.id);

	xa_for_each(&file_priv->cmdq_xa, cmdq_id, cmdq)
		ivpu_cmdq_unregister(file_priv, cmdq);

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_OS)
		ivpu_jsm_context_release(vdev, file_priv->ctx.id);

	ivpu_mmu_disable_ssid_events(vdev, file_priv->ctx.id);

	file_priv->aborted = true;
}

static int ivpu_cmdq_push_job(struct ivpu_cmdq *cmdq, struct ivpu_job *job)
{
	struct ivpu_device *vdev = job->vdev;
	struct vpu_job_queue_header *header = &cmdq->jobq->header;
	struct vpu_job_queue_entry *entry;
	u32 tail = READ_ONCE(header->tail);
	u32 next_entry = (tail + 1) % cmdq->entry_count;

	/* Check if there is space left in job queue */
	if (next_entry == header->head) {
		ivpu_dbg(vdev, JOB, "Job queue full: ctx %d cmdq %d db %d head %d tail %d\n",
			 job->file_priv->ctx.id, cmdq->id, cmdq->db_id, header->head, tail);
		return -EBUSY;
	}

	entry = &cmdq->jobq->slot[tail].job;
	entry->batch_buf_addr = job->cmd_buf_vpu_addr;
	entry->job_id = job->job_id;
	entry->flags = 0;
	if (unlikely(ivpu_test_mode & IVPU_TEST_MODE_NULL_SUBMISSION))
		entry->flags = VPU_JOB_FLAGS_NULL_SUBMISSION_MASK;

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW) {
		if (cmdq->primary_preempt_buf) {
			entry->primary_preempt_buf_addr = cmdq->primary_preempt_buf->vpu_addr;
			entry->primary_preempt_buf_size = ivpu_bo_size(cmdq->primary_preempt_buf);
		}

		if (cmdq->secondary_preempt_buf) {
			entry->secondary_preempt_buf_addr = cmdq->secondary_preempt_buf->vpu_addr;
			entry->secondary_preempt_buf_size =
				ivpu_bo_size(cmdq->secondary_preempt_buf);
		}
	}

	wmb(); /* Ensure that tail is updated after filling entry */
	header->tail = next_entry;
	wmb(); /* Flush WC buffer for jobq header */

	return 0;
}

struct ivpu_fence {
	struct dma_fence base;
	spinlock_t lock; /* protects base */
	struct ivpu_device *vdev;
};

static inline struct ivpu_fence *to_vpu_fence(struct dma_fence *fence)
{
	return container_of(fence, struct ivpu_fence, base);
}

static const char *ivpu_fence_get_driver_name(struct dma_fence *fence)
{
	return DRIVER_NAME;
}

static const char *ivpu_fence_get_timeline_name(struct dma_fence *fence)
{
	struct ivpu_fence *ivpu_fence = to_vpu_fence(fence);

	return dev_name(ivpu_fence->vdev->drm.dev);
}

static const struct dma_fence_ops ivpu_fence_ops = {
	.get_driver_name = ivpu_fence_get_driver_name,
	.get_timeline_name = ivpu_fence_get_timeline_name,
};

static struct dma_fence *ivpu_fence_create(struct ivpu_device *vdev)
{
	struct ivpu_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return NULL;

	fence->vdev = vdev;
	spin_lock_init(&fence->lock);
	dma_fence_init(&fence->base, &ivpu_fence_ops, &fence->lock, dma_fence_context_alloc(1), 1);

	return &fence->base;
}

static void ivpu_job_destroy(struct ivpu_job *job)
{
	struct ivpu_device *vdev = job->vdev;
	u32 i;

	ivpu_dbg(vdev, JOB, "Job destroyed: id %3u ctx %2d cmdq_id %u engine %d",
		 job->job_id, job->file_priv->ctx.id, job->cmdq_id, job->engine_idx);

	for (i = 0; i < job->bo_count; i++)
		if (job->bos[i])
			drm_gem_object_put(&job->bos[i]->base.base);

	dma_fence_put(job->done_fence);
	ivpu_file_priv_put(&job->file_priv);
	kfree(job);
}

static struct ivpu_job *
ivpu_job_create(struct ivpu_file_priv *file_priv, u32 engine_idx, u32 bo_count)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_job *job;

	job = kzalloc(struct_size(job, bos, bo_count), GFP_KERNEL);
	if (!job)
		return NULL;

	job->vdev = vdev;
	job->engine_idx = engine_idx;
	job->bo_count = bo_count;
	job->done_fence = ivpu_fence_create(vdev);
	if (!job->done_fence) {
		ivpu_warn_ratelimited(vdev, "Failed to create a fence\n");
		goto err_free_job;
	}

	job->file_priv = ivpu_file_priv_get(file_priv);

	trace_job("create", job);
	ivpu_dbg(vdev, JOB, "Job created: ctx %2d engine %d", file_priv->ctx.id, job->engine_idx);
	return job;

err_free_job:
	kfree(job);
	return NULL;
}

static struct ivpu_job *ivpu_job_remove_from_submitted_jobs(struct ivpu_device *vdev, u32 job_id)
{
	struct ivpu_job *job;

	lockdep_assert_held(&vdev->submitted_jobs_lock);

	job = xa_erase(&vdev->submitted_jobs_xa, job_id);
	if (xa_empty(&vdev->submitted_jobs_xa) && job) {
		vdev->busy_time = ktime_add(ktime_sub(ktime_get(), vdev->busy_start_ts),
					    vdev->busy_time);
	}

	return job;
}

static int ivpu_job_signal_and_destroy(struct ivpu_device *vdev, u32 job_id, u32 job_status)
{
	struct ivpu_job *job;

	lockdep_assert_held(&vdev->submitted_jobs_lock);

	job = xa_load(&vdev->submitted_jobs_xa, job_id);
	if (!job)
		return -ENOENT;

	if (job_status == VPU_JSM_STATUS_MVNCI_CONTEXT_VIOLATION_HW) {
		guard(mutex)(&job->file_priv->lock);

		if (job->file_priv->has_mmu_faults)
			return 0;

		/*
		 * Mark context as faulty and defer destruction of the job to jobs abort thread
		 * handler to synchronize between both faults and jobs returning context violation
		 * status and ensure both are handled in the same way
		 */
		job->file_priv->has_mmu_faults = true;
		queue_work(system_wq, &vdev->context_abort_work);
		return 0;
	}

	job = ivpu_job_remove_from_submitted_jobs(vdev, job_id);
	if (!job)
		return -ENOENT;

	if (job->file_priv->has_mmu_faults)
		job_status = DRM_IVPU_JOB_STATUS_ABORTED;

	job->bos[CMD_BUF_IDX]->job_status = job_status;
	dma_fence_signal(job->done_fence);

	trace_job("done", job);
	ivpu_dbg(vdev, JOB, "Job complete:  id %3u ctx %2d cmdq_id %u engine %d status 0x%x\n",
		 job->job_id, job->file_priv->ctx.id, job->cmdq_id, job->engine_idx, job_status);

	ivpu_job_destroy(job);
	ivpu_stop_job_timeout_detection(vdev);

	ivpu_rpm_put(vdev);

	if (!xa_empty(&vdev->submitted_jobs_xa))
		ivpu_start_job_timeout_detection(vdev);

	return 0;
}

void ivpu_jobs_abort_all(struct ivpu_device *vdev)
{
	struct ivpu_job *job;
	unsigned long id;

	mutex_lock(&vdev->submitted_jobs_lock);

	xa_for_each(&vdev->submitted_jobs_xa, id, job)
		ivpu_job_signal_and_destroy(vdev, id, DRM_IVPU_JOB_STATUS_ABORTED);

	mutex_unlock(&vdev->submitted_jobs_lock);
}

void ivpu_cmdq_abort_all_jobs(struct ivpu_device *vdev, u32 ctx_id, u32 cmdq_id)
{
	struct ivpu_job *job;
	unsigned long id;

	mutex_lock(&vdev->submitted_jobs_lock);

	xa_for_each(&vdev->submitted_jobs_xa, id, job)
		if (job->file_priv->ctx.id == ctx_id && job->cmdq_id == cmdq_id)
			ivpu_job_signal_and_destroy(vdev, id, DRM_IVPU_JOB_STATUS_ABORTED);

	mutex_unlock(&vdev->submitted_jobs_lock);
}

static int ivpu_job_submit(struct ivpu_job *job, u8 priority, u32 cmdq_id)
{
	struct ivpu_file_priv *file_priv = job->file_priv;
	struct ivpu_device *vdev = job->vdev;
	struct ivpu_cmdq *cmdq;
	bool is_first_job;
	int ret;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	mutex_lock(&vdev->submitted_jobs_lock);
	mutex_lock(&file_priv->lock);

	if (cmdq_id == 0)
		cmdq = ivpu_cmdq_acquire_legacy(file_priv, priority);
	else
		cmdq = ivpu_cmdq_acquire(file_priv, cmdq_id);
	if (!cmdq) {
		ivpu_warn_ratelimited(vdev, "Failed to get job queue, ctx %d\n", file_priv->ctx.id);
		ret = -EINVAL;
		goto err_unlock;
	}

	ret = ivpu_cmdq_register(file_priv, cmdq);
	if (ret) {
		ivpu_err(vdev, "Failed to register command queue: %d\n", ret);
		goto err_unlock;
	}

	job->cmdq_id = cmdq->id;

	is_first_job = xa_empty(&vdev->submitted_jobs_xa);
	ret = xa_alloc_cyclic(&vdev->submitted_jobs_xa, &job->job_id, job, file_priv->job_limit,
			      &file_priv->job_id_next, GFP_KERNEL);
	if (ret < 0) {
		ivpu_dbg(vdev, JOB, "Too many active jobs in ctx %d\n",
			 file_priv->ctx.id);
		ret = -EBUSY;
		goto err_unlock;
	}

	ret = ivpu_cmdq_push_job(cmdq, job);
	if (ret)
		goto err_erase_xa;

	ivpu_start_job_timeout_detection(vdev);

	if (unlikely(ivpu_test_mode & IVPU_TEST_MODE_NULL_HW)) {
		cmdq->jobq->header.head = cmdq->jobq->header.tail;
		wmb(); /* Flush WC buffer for jobq header */
	} else {
		ivpu_cmdq_ring_db(vdev, cmdq);
		if (is_first_job)
			vdev->busy_start_ts = ktime_get();
	}

	trace_job("submit", job);
	ivpu_dbg(vdev, JOB, "Job submitted: id %3u ctx %2d cmdq_id %u engine %d prio %d addr 0x%llx next %d\n",
		 job->job_id, file_priv->ctx.id, cmdq->id, job->engine_idx, cmdq->priority,
		 job->cmd_buf_vpu_addr, cmdq->jobq->header.tail);

	mutex_unlock(&file_priv->lock);

	if (unlikely(ivpu_test_mode & IVPU_TEST_MODE_NULL_HW)) {
		ivpu_job_signal_and_destroy(vdev, job->job_id, VPU_JSM_STATUS_SUCCESS);
	}

	mutex_unlock(&vdev->submitted_jobs_lock);

	return 0;

err_erase_xa:
	xa_erase(&vdev->submitted_jobs_xa, job->job_id);
err_unlock:
	mutex_unlock(&file_priv->lock);
	mutex_unlock(&vdev->submitted_jobs_lock);
	ivpu_rpm_put(vdev);
	return ret;
}

static int
ivpu_job_prepare_bos_for_submit(struct drm_file *file, struct ivpu_job *job, u32 *buf_handles,
				u32 buf_count, u32 commands_offset)
{
	struct ivpu_file_priv *file_priv = job->file_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct ww_acquire_ctx acquire_ctx;
	enum dma_resv_usage usage;
	struct ivpu_bo *bo;
	int ret;
	u32 i;

	for (i = 0; i < buf_count; i++) {
		struct drm_gem_object *obj = drm_gem_object_lookup(file, buf_handles[i]);

		if (!obj)
			return -ENOENT;

		job->bos[i] = to_ivpu_bo(obj);

		ret = ivpu_bo_pin(job->bos[i]);
		if (ret)
			return ret;
	}

	bo = job->bos[CMD_BUF_IDX];
	if (!dma_resv_test_signaled(bo->base.base.resv, DMA_RESV_USAGE_READ)) {
		ivpu_warn(vdev, "Buffer is already in use\n");
		return -EBUSY;
	}

	if (commands_offset >= ivpu_bo_size(bo)) {
		ivpu_warn(vdev, "Invalid command buffer offset %u\n", commands_offset);
		return -EINVAL;
	}

	job->cmd_buf_vpu_addr = bo->vpu_addr + commands_offset;

	ret = drm_gem_lock_reservations((struct drm_gem_object **)job->bos, buf_count,
					&acquire_ctx);
	if (ret) {
		ivpu_warn(vdev, "Failed to lock reservations: %d\n", ret);
		return ret;
	}

	for (i = 0; i < buf_count; i++) {
		ret = dma_resv_reserve_fences(job->bos[i]->base.base.resv, 1);
		if (ret) {
			ivpu_warn(vdev, "Failed to reserve fences: %d\n", ret);
			goto unlock_reservations;
		}
	}

	for (i = 0; i < buf_count; i++) {
		usage = (i == CMD_BUF_IDX) ? DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_BOOKKEEP;
		dma_resv_add_fence(job->bos[i]->base.base.resv, job->done_fence, usage);
	}

unlock_reservations:
	drm_gem_unlock_reservations((struct drm_gem_object **)job->bos, buf_count, &acquire_ctx);

	wmb(); /* Flush write combining buffers */

	return ret;
}

static int ivpu_submit(struct drm_file *file, struct ivpu_file_priv *file_priv, u32 cmdq_id,
		       u32 buffer_count, u32 engine, void __user *buffers_ptr, u32 cmds_offset,
		       u8 priority)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_job *job;
	u32 *buf_handles;
	int idx, ret;

	buf_handles = kcalloc(buffer_count, sizeof(u32), GFP_KERNEL);
	if (!buf_handles)
		return -ENOMEM;

	ret = copy_from_user(buf_handles, buffers_ptr, buffer_count * sizeof(u32));
	if (ret) {
		ret = -EFAULT;
		goto err_free_handles;
	}

	if (!drm_dev_enter(&vdev->drm, &idx)) {
		ret = -ENODEV;
		goto err_free_handles;
	}

	ivpu_dbg(vdev, JOB, "Submit ioctl: ctx %u cmdq_id %u buf_count %u\n",
		 file_priv->ctx.id, cmdq_id, buffer_count);

	job = ivpu_job_create(file_priv, engine, buffer_count);
	if (!job) {
		ivpu_err(vdev, "Failed to create job\n");
		ret = -ENOMEM;
		goto err_exit_dev;
	}

	ret = ivpu_job_prepare_bos_for_submit(file, job, buf_handles, buffer_count, cmds_offset);
	if (ret) {
		ivpu_err(vdev, "Failed to prepare job: %d\n", ret);
		goto err_destroy_job;
	}

	down_read(&vdev->pm->reset_lock);
	ret = ivpu_job_submit(job, priority, cmdq_id);
	up_read(&vdev->pm->reset_lock);
	if (ret)
		goto err_signal_fence;

	drm_dev_exit(idx);
	kfree(buf_handles);
	return ret;

err_signal_fence:
	dma_fence_signal(job->done_fence);
err_destroy_job:
	ivpu_job_destroy(job);
err_exit_dev:
	drm_dev_exit(idx);
err_free_handles:
	kfree(buf_handles);
	return ret;
}

int ivpu_submit_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct drm_ivpu_submit *args = data;
	u8 priority;

	if (args->engine != DRM_IVPU_ENGINE_COMPUTE)
		return -EINVAL;

	if (args->priority > DRM_IVPU_JOB_PRIORITY_REALTIME)
		return -EINVAL;

	if (args->buffer_count == 0 || args->buffer_count > JOB_MAX_BUFFER_COUNT)
		return -EINVAL;

	if (!IS_ALIGNED(args->commands_offset, 8))
		return -EINVAL;

	if (!file_priv->ctx.id)
		return -EINVAL;

	if (file_priv->has_mmu_faults)
		return -EBADFD;

	priority = ivpu_job_to_jsm_priority(args->priority);

	return ivpu_submit(file, file_priv, 0, args->buffer_count, args->engine,
			   (void __user *)args->buffers_ptr, args->commands_offset, priority);
}

int ivpu_cmdq_submit_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct drm_ivpu_cmdq_submit *args = data;

	if (!ivpu_is_capable(file_priv->vdev, DRM_IVPU_CAP_MANAGE_CMDQ))
		return -ENODEV;

	if (args->cmdq_id < IVPU_CMDQ_MIN_ID || args->cmdq_id > IVPU_CMDQ_MAX_ID)
		return -EINVAL;

	if (args->buffer_count == 0 || args->buffer_count > JOB_MAX_BUFFER_COUNT)
		return -EINVAL;

	if (!IS_ALIGNED(args->commands_offset, 8))
		return -EINVAL;

	if (!file_priv->ctx.id)
		return -EINVAL;

	if (file_priv->has_mmu_faults)
		return -EBADFD;

	return ivpu_submit(file, file_priv, args->cmdq_id, args->buffer_count, VPU_ENGINE_COMPUTE,
			   (void __user *)args->buffers_ptr, args->commands_offset, 0);
}

int ivpu_cmdq_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct drm_ivpu_cmdq_create *args = data;
	struct ivpu_cmdq *cmdq;
	int ret;

	if (!ivpu_is_capable(vdev, DRM_IVPU_CAP_MANAGE_CMDQ))
		return -ENODEV;

	if (args->priority > DRM_IVPU_JOB_PRIORITY_REALTIME)
		return -EINVAL;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	mutex_lock(&file_priv->lock);

	cmdq = ivpu_cmdq_create(file_priv, ivpu_job_to_jsm_priority(args->priority), false);
	if (cmdq)
		args->cmdq_id = cmdq->id;

	mutex_unlock(&file_priv->lock);

	ivpu_rpm_put(vdev);

	return cmdq ? 0 : -ENOMEM;
}

int ivpu_cmdq_destroy_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct drm_ivpu_cmdq_destroy *args = data;
	struct ivpu_cmdq *cmdq;
	u32 cmdq_id = 0;
	int ret;

	if (!ivpu_is_capable(vdev, DRM_IVPU_CAP_MANAGE_CMDQ))
		return -ENODEV;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	mutex_lock(&file_priv->lock);

	cmdq = xa_load(&file_priv->cmdq_xa, args->cmdq_id);
	if (!cmdq || cmdq->is_legacy) {
		ret = -ENOENT;
	} else {
		cmdq_id = cmdq->id;
		ivpu_cmdq_destroy(file_priv, cmdq);
		ret = 0;
	}

	mutex_unlock(&file_priv->lock);

	/* Abort any pending jobs only if cmdq was destroyed */
	if (!ret)
		ivpu_cmdq_abort_all_jobs(vdev, file_priv->ctx.id, cmdq_id);

	ivpu_rpm_put(vdev);

	return ret;
}

static void
ivpu_job_done_callback(struct ivpu_device *vdev, struct ivpu_ipc_hdr *ipc_hdr,
		       struct vpu_jsm_msg *jsm_msg)
{
	struct vpu_ipc_msg_payload_job_done *payload;

	if (!jsm_msg) {
		ivpu_err(vdev, "IPC message has no JSM payload\n");
		return;
	}

	if (jsm_msg->result != VPU_JSM_STATUS_SUCCESS) {
		ivpu_err(vdev, "Invalid JSM message result: %d\n", jsm_msg->result);
		return;
	}

	payload = (struct vpu_ipc_msg_payload_job_done *)&jsm_msg->payload;

	mutex_lock(&vdev->submitted_jobs_lock);
	ivpu_job_signal_and_destroy(vdev, payload->job_id, payload->job_status);
	mutex_unlock(&vdev->submitted_jobs_lock);
}

void ivpu_job_done_consumer_init(struct ivpu_device *vdev)
{
	ivpu_ipc_consumer_add(vdev, &vdev->job_done_consumer,
			      VPU_IPC_CHAN_JOB_RET, ivpu_job_done_callback);
}

void ivpu_job_done_consumer_fini(struct ivpu_device *vdev)
{
	ivpu_ipc_consumer_del(vdev, &vdev->job_done_consumer);
}

void ivpu_context_abort_work_fn(struct work_struct *work)
{
	struct ivpu_device *vdev = container_of(work, struct ivpu_device, context_abort_work);
	struct ivpu_file_priv *file_priv;
	struct ivpu_job *job;
	unsigned long ctx_id;
	unsigned long id;

	if (drm_WARN_ON(&vdev->drm, pm_runtime_get_if_active(vdev->drm.dev) <= 0))
		return;

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW)
		if (ivpu_jsm_reset_engine(vdev, 0))
			return;

	mutex_lock(&vdev->context_list_lock);
	xa_for_each(&vdev->context_xa, ctx_id, file_priv) {
		if (!file_priv->has_mmu_faults || file_priv->aborted)
			continue;

		mutex_lock(&file_priv->lock);
		ivpu_context_abort_locked(file_priv);
		mutex_unlock(&file_priv->lock);
	}
	mutex_unlock(&vdev->context_list_lock);

	/*
	 * We will not receive new MMU event interrupts until existing events are discarded
	 * however, we want to discard these events only after aborting the faulty context
	 * to avoid generating new faults from that context
	 */
	ivpu_mmu_discard_events(vdev);

	if (vdev->fw->sched_mode != VPU_SCHEDULING_MODE_HW)
		goto runtime_put;

	if (ivpu_jsm_hws_resume_engine(vdev, 0))
		return;
	/*
	 * In hardware scheduling mode NPU already has stopped processing jobs
	 * and won't send us any further notifications, thus we have to free job related resources
	 * and notify userspace
	 */
	mutex_lock(&vdev->submitted_jobs_lock);
	xa_for_each(&vdev->submitted_jobs_xa, id, job)
		if (job->file_priv->aborted)
			ivpu_job_signal_and_destroy(vdev, job->job_id, DRM_IVPU_JOB_STATUS_ABORTED);
	mutex_unlock(&vdev->submitted_jobs_lock);

runtime_put:
	pm_runtime_mark_last_busy(vdev->drm.dev);
	pm_runtime_put_autosuspend(vdev->drm.dev);
}
