// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <drm/drm_file.h>

#include <linux/bitfield.h>
#include <linux/highmem.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <uapi/drm/ivpu_accel.h>

#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_job.h"
#include "ivpu_jsm_msg.h"
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

	ret = xa_alloc_cyclic(&vdev->db_xa, &cmdq->db_id, NULL, vdev->db_limit, &vdev->db_next,
			      GFP_KERNEL);
	if (ret < 0) {
		ivpu_err(vdev, "Failed to allocate doorbell id: %d\n", ret);
		goto err_free_cmdq;
	}

	ret = xa_alloc_cyclic(&file_priv->cmdq_xa, &cmdq->id, cmdq, file_priv->cmdq_limit,
			      &file_priv->cmdq_id_next, GFP_KERNEL);
	if (ret < 0) {
		ivpu_err(vdev, "Failed to allocate command queue id: %d\n", ret);
		goto err_erase_db_xa;
	}

	cmdq->mem = ivpu_bo_create_global(vdev, SZ_4K, DRM_IVPU_BO_WC | DRM_IVPU_BO_MAPPABLE);
	if (!cmdq->mem)
		goto err_erase_cmdq_xa;

	ret = ivpu_preemption_buffers_create(vdev, file_priv, cmdq);
	if (ret)
		ivpu_warn(vdev, "Failed to allocate preemption buffers, preemption limited\n");

	return cmdq;

err_erase_cmdq_xa:
	xa_erase(&file_priv->cmdq_xa, cmdq->id);
err_erase_db_xa:
	xa_erase(&vdev->db_xa, cmdq->db_id);
err_free_cmdq:
	kfree(cmdq);
	return NULL;
}

static void ivpu_cmdq_free(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	if (!cmdq)
		return;

	ivpu_preemption_buffers_free(file_priv->vdev, file_priv, cmdq);
	ivpu_bo_free(cmdq->mem);
	xa_erase(&file_priv->vdev->db_xa, cmdq->db_id);
	kfree(cmdq);
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

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW)
		ret = ivpu_jsm_hws_register_db(vdev, file_priv->ctx.id, cmdq->id, cmdq->db_id,
					       cmdq->mem->vpu_addr, ivpu_bo_size(cmdq->mem));
	else
		ret = ivpu_jsm_register_db(vdev, file_priv->ctx.id, cmdq->db_id,
					   cmdq->mem->vpu_addr, ivpu_bo_size(cmdq->mem));

	if (!ret)
		ivpu_dbg(vdev, JOB, "DB %d registered to cmdq %d ctx %d\n",
			 cmdq->db_id, cmdq->id, file_priv->ctx.id);

	return ret;
}

static int
ivpu_cmdq_init(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq, u8 priority)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct vpu_job_queue_header *jobq_header;
	int ret;

	lockdep_assert_held(&file_priv->lock);

	if (cmdq->db_registered)
		return 0;

	cmdq->entry_count = (u32)((ivpu_bo_size(cmdq->mem) - sizeof(struct vpu_job_queue_header)) /
				  sizeof(struct vpu_job_queue_entry));

	cmdq->jobq = (struct vpu_job_queue *)ivpu_bo_vaddr(cmdq->mem);
	jobq_header = &cmdq->jobq->header;
	jobq_header->engine_idx = VPU_ENGINE_COMPUTE;
	jobq_header->head = 0;
	jobq_header->tail = 0;
	if (ivpu_test_mode & IVPU_TEST_MODE_TURBO) {
		ivpu_dbg(vdev, JOB, "Turbo mode enabled");
		jobq_header->flags = VPU_JOB_QUEUE_FLAGS_TURBO_MODE;
	}

	wmb(); /* Flush WC buffer for jobq->header */

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW) {
		ret = ivpu_hws_cmdq_init(file_priv, cmdq, VPU_ENGINE_COMPUTE, priority);
		if (ret)
			return ret;
	}

	ret = ivpu_register_db(file_priv, cmdq);
	if (ret)
		return ret;

	cmdq->db_registered = true;

	return 0;
}

static int ivpu_cmdq_fini(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	struct ivpu_device *vdev = file_priv->vdev;
	int ret;

	lockdep_assert_held(&file_priv->lock);

	if (!cmdq->db_registered)
		return 0;

	cmdq->db_registered = false;

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_HW) {
		ret = ivpu_jsm_hws_destroy_cmdq(vdev, file_priv->ctx.id, cmdq->id);
		if (!ret)
			ivpu_dbg(vdev, JOB, "Command queue %d destroyed\n", cmdq->id);
	}

	ret = ivpu_jsm_unregister_db(vdev, cmdq->db_id);
	if (!ret)
		ivpu_dbg(vdev, JOB, "DB %d unregistered\n", cmdq->db_id);

	return 0;
}

static struct ivpu_cmdq *ivpu_cmdq_acquire(struct ivpu_file_priv *file_priv, u8 priority)
{
	struct ivpu_cmdq *cmdq;
	unsigned long cmdq_id;
	int ret;

	lockdep_assert_held(&file_priv->lock);

	xa_for_each(&file_priv->cmdq_xa, cmdq_id, cmdq)
		if (cmdq->priority == priority)
			break;

	if (!cmdq) {
		cmdq = ivpu_cmdq_alloc(file_priv);
		if (!cmdq)
			return NULL;
		cmdq->priority = priority;
	}

	ret = ivpu_cmdq_init(file_priv, cmdq, priority);
	if (ret)
		return NULL;

	return cmdq;
}

void ivpu_cmdq_release_all_locked(struct ivpu_file_priv *file_priv)
{
	struct ivpu_cmdq *cmdq;
	unsigned long cmdq_id;

	lockdep_assert_held(&file_priv->lock);

	xa_for_each(&file_priv->cmdq_xa, cmdq_id, cmdq) {
		xa_erase(&file_priv->cmdq_xa, cmdq_id);
		ivpu_cmdq_fini(file_priv, cmdq);
		ivpu_cmdq_free(file_priv, cmdq);
	}
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

	xa_for_each(&file_priv->cmdq_xa, cmdq_id, cmdq)
		cmdq->db_registered = false;

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

static void ivpu_cmdq_fini_all(struct ivpu_file_priv *file_priv)
{
	struct ivpu_cmdq *cmdq;
	unsigned long cmdq_id;

	xa_for_each(&file_priv->cmdq_xa, cmdq_id, cmdq)
		ivpu_cmdq_fini(file_priv, cmdq);
}

void ivpu_context_abort_locked(struct ivpu_file_priv *file_priv)
{
	struct ivpu_device *vdev = file_priv->vdev;

	lockdep_assert_held(&file_priv->lock);

	ivpu_cmdq_fini_all(file_priv);

	if (vdev->fw->sched_mode == VPU_SCHEDULING_MODE_OS)
		ivpu_jsm_context_release(vdev, file_priv->ctx.id);
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

	ivpu_dbg(vdev, JOB, "Job destroyed: id %3u ctx %2d engine %d",
		 job->job_id, job->file_priv->ctx.id, job->engine_idx);

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

	xa_lock(&vdev->submitted_jobs_xa);
	job = __xa_erase(&vdev->submitted_jobs_xa, job_id);

	if (xa_empty(&vdev->submitted_jobs_xa) && job) {
		vdev->busy_time = ktime_add(ktime_sub(ktime_get(), vdev->busy_start_ts),
					    vdev->busy_time);
	}

	xa_unlock(&vdev->submitted_jobs_xa);

	return job;
}

static int ivpu_job_signal_and_destroy(struct ivpu_device *vdev, u32 job_id, u32 job_status)
{
	struct ivpu_job *job;

	job = ivpu_job_remove_from_submitted_jobs(vdev, job_id);
	if (!job)
		return -ENOENT;

	if (job->file_priv->has_mmu_faults)
		job_status = DRM_IVPU_JOB_STATUS_ABORTED;

	job->bos[CMD_BUF_IDX]->job_status = job_status;
	dma_fence_signal(job->done_fence);

	trace_job("done", job);
	ivpu_dbg(vdev, JOB, "Job complete:  id %3u ctx %2d engine %d status 0x%x\n",
		 job->job_id, job->file_priv->ctx.id, job->engine_idx, job_status);

	ivpu_job_destroy(job);
	ivpu_stop_job_timeout_detection(vdev);

	ivpu_rpm_put(vdev);
	return 0;
}

void ivpu_jobs_abort_all(struct ivpu_device *vdev)
{
	struct ivpu_job *job;
	unsigned long id;

	xa_for_each(&vdev->submitted_jobs_xa, id, job)
		ivpu_job_signal_and_destroy(vdev, id, DRM_IVPU_JOB_STATUS_ABORTED);
}

static int ivpu_job_submit(struct ivpu_job *job, u8 priority)
{
	struct ivpu_file_priv *file_priv = job->file_priv;
	struct ivpu_device *vdev = job->vdev;
	struct ivpu_cmdq *cmdq;
	bool is_first_job;
	int ret;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return ret;

	mutex_lock(&file_priv->lock);

	cmdq = ivpu_cmdq_acquire(file_priv, priority);
	if (!cmdq) {
		ivpu_warn_ratelimited(vdev, "Failed to get job queue, ctx %d engine %d prio %d\n",
				      file_priv->ctx.id, job->engine_idx, priority);
		ret = -EINVAL;
		goto err_unlock_file_priv;
	}

	xa_lock(&vdev->submitted_jobs_xa);
	is_first_job = xa_empty(&vdev->submitted_jobs_xa);
	ret = __xa_alloc_cyclic(&vdev->submitted_jobs_xa, &job->job_id, job, file_priv->job_limit,
				&file_priv->job_id_next, GFP_KERNEL);
	if (ret < 0) {
		ivpu_dbg(vdev, JOB, "Too many active jobs in ctx %d\n",
			 file_priv->ctx.id);
		ret = -EBUSY;
		goto err_unlock_submitted_jobs_xa;
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
	ivpu_dbg(vdev, JOB, "Job submitted: id %3u ctx %2d engine %d prio %d addr 0x%llx next %d\n",
		 job->job_id, file_priv->ctx.id, job->engine_idx, priority,
		 job->cmd_buf_vpu_addr, cmdq->jobq->header.tail);

	xa_unlock(&vdev->submitted_jobs_xa);

	mutex_unlock(&file_priv->lock);

	if (unlikely(ivpu_test_mode & IVPU_TEST_MODE_NULL_HW))
		ivpu_job_signal_and_destroy(vdev, job->job_id, VPU_JSM_STATUS_SUCCESS);

	return 0;

err_erase_xa:
	__xa_erase(&vdev->submitted_jobs_xa, job->job_id);
err_unlock_submitted_jobs_xa:
	xa_unlock(&vdev->submitted_jobs_xa);
err_unlock_file_priv:
	mutex_unlock(&file_priv->lock);
	ivpu_rpm_put(vdev);
	return ret;
}

static int
ivpu_job_prepare_bos_for_submit(struct drm_file *file, struct ivpu_job *job, u32 *buf_handles,
				u32 buf_count, u32 commands_offset)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
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

static inline u8 ivpu_job_to_hws_priority(struct ivpu_file_priv *file_priv, u8 priority)
{
	if (priority == DRM_IVPU_JOB_PRIORITY_DEFAULT)
		return DRM_IVPU_JOB_PRIORITY_NORMAL;

	return priority - 1;
}

int ivpu_submit_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct drm_ivpu_submit *params = data;
	struct ivpu_job *job;
	u32 *buf_handles;
	int idx, ret;
	u8 priority;

	if (params->engine != DRM_IVPU_ENGINE_COMPUTE)
		return -EINVAL;

	if (params->priority > DRM_IVPU_JOB_PRIORITY_REALTIME)
		return -EINVAL;

	if (params->buffer_count == 0 || params->buffer_count > JOB_MAX_BUFFER_COUNT)
		return -EINVAL;

	if (!IS_ALIGNED(params->commands_offset, 8))
		return -EINVAL;

	if (!file_priv->ctx.id)
		return -EINVAL;

	if (file_priv->has_mmu_faults)
		return -EBADFD;

	buf_handles = kcalloc(params->buffer_count, sizeof(u32), GFP_KERNEL);
	if (!buf_handles)
		return -ENOMEM;

	ret = copy_from_user(buf_handles,
			     (void __user *)params->buffers_ptr,
			     params->buffer_count * sizeof(u32));
	if (ret) {
		ret = -EFAULT;
		goto err_free_handles;
	}

	if (!drm_dev_enter(&vdev->drm, &idx)) {
		ret = -ENODEV;
		goto err_free_handles;
	}

	ivpu_dbg(vdev, JOB, "Submit ioctl: ctx %u buf_count %u\n",
		 file_priv->ctx.id, params->buffer_count);

	job = ivpu_job_create(file_priv, params->engine, params->buffer_count);
	if (!job) {
		ivpu_err(vdev, "Failed to create job\n");
		ret = -ENOMEM;
		goto err_exit_dev;
	}

	ret = ivpu_job_prepare_bos_for_submit(file, job, buf_handles, params->buffer_count,
					      params->commands_offset);
	if (ret) {
		ivpu_err(vdev, "Failed to prepare job: %d\n", ret);
		goto err_destroy_job;
	}

	priority = ivpu_job_to_hws_priority(file_priv, params->priority);

	down_read(&vdev->pm->reset_lock);
	ret = ivpu_job_submit(job, priority);
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

static void
ivpu_job_done_callback(struct ivpu_device *vdev, struct ivpu_ipc_hdr *ipc_hdr,
		       struct vpu_jsm_msg *jsm_msg)
{
	struct vpu_ipc_msg_payload_job_done *payload;
	int ret;

	if (!jsm_msg) {
		ivpu_err(vdev, "IPC message has no JSM payload\n");
		return;
	}

	if (jsm_msg->result != VPU_JSM_STATUS_SUCCESS) {
		ivpu_err(vdev, "Invalid JSM message result: %d\n", jsm_msg->result);
		return;
	}

	payload = (struct vpu_ipc_msg_payload_job_done *)&jsm_msg->payload;
	ret = ivpu_job_signal_and_destroy(vdev, payload->job_id, payload->job_status);
	if (!ret && !xa_empty(&vdev->submitted_jobs_xa))
		ivpu_start_job_timeout_detection(vdev);
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
