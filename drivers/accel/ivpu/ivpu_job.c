// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#include <drm/drm_file.h>

#include <linux/bitfield.h>
#include <linux/highmem.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <uapi/drm/ivpu_accel.h>

#include "ivpu_drv.h"
#include "ivpu_hw.h"
#include "ivpu_ipc.h"
#include "ivpu_job.h"
#include "ivpu_jsm_msg.h"
#include "ivpu_pm.h"

#define CMD_BUF_IDX	     0
#define JOB_ID_JOB_MASK	     GENMASK(7, 0)
#define JOB_ID_CONTEXT_MASK  GENMASK(31, 8)
#define JOB_MAX_BUFFER_COUNT 65535

static unsigned int ivpu_tdr_timeout_ms;
module_param_named(tdr_timeout_ms, ivpu_tdr_timeout_ms, uint, 0644);
MODULE_PARM_DESC(tdr_timeout_ms, "Timeout for device hang detection, in milliseconds, 0 - default");

static void ivpu_cmdq_ring_db(struct ivpu_device *vdev, struct ivpu_cmdq *cmdq)
{
	ivpu_hw_reg_db_set(vdev, cmdq->db_id);
}

static struct ivpu_cmdq *ivpu_cmdq_alloc(struct ivpu_file_priv *file_priv, u16 engine)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct vpu_job_queue_header *jobq_header;
	struct ivpu_cmdq *cmdq;

	cmdq = kzalloc(sizeof(*cmdq), GFP_KERNEL);
	if (!cmdq)
		return NULL;

	cmdq->mem = ivpu_bo_alloc_internal(vdev, 0, SZ_4K, DRM_IVPU_BO_WC);
	if (!cmdq->mem)
		goto cmdq_free;

	cmdq->db_id = file_priv->ctx.id + engine * ivpu_get_context_count(vdev);
	cmdq->entry_count = (u32)((ivpu_bo_size(cmdq->mem) - sizeof(struct vpu_job_queue_header)) /
				  sizeof(struct vpu_job_queue_entry));

	cmdq->jobq = (struct vpu_job_queue *)ivpu_bo_vaddr(cmdq->mem);
	jobq_header = &cmdq->jobq->header;
	jobq_header->engine_idx = engine;
	jobq_header->head = 0;
	jobq_header->tail = 0;
	wmb(); /* Flush WC buffer for jobq->header */

	return cmdq;

cmdq_free:
	kfree(cmdq);
	return NULL;
}

static void ivpu_cmdq_free(struct ivpu_file_priv *file_priv, struct ivpu_cmdq *cmdq)
{
	if (!cmdq)
		return;

	ivpu_bo_free_internal(cmdq->mem);
	kfree(cmdq);
}

static struct ivpu_cmdq *ivpu_cmdq_acquire(struct ivpu_file_priv *file_priv, u16 engine)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_cmdq *cmdq = file_priv->cmdq[engine];
	int ret;

	lockdep_assert_held(&file_priv->lock);

	if (!cmdq) {
		cmdq = ivpu_cmdq_alloc(file_priv, engine);
		if (!cmdq)
			return NULL;
		file_priv->cmdq[engine] = cmdq;
	}

	if (cmdq->db_registered)
		return cmdq;

	ret = ivpu_jsm_register_db(vdev, file_priv->ctx.id, cmdq->db_id,
				   cmdq->mem->vpu_addr, ivpu_bo_size(cmdq->mem));
	if (ret)
		return NULL;

	cmdq->db_registered = true;

	return cmdq;
}

static void ivpu_cmdq_release_locked(struct ivpu_file_priv *file_priv, u16 engine)
{
	struct ivpu_cmdq *cmdq = file_priv->cmdq[engine];

	lockdep_assert_held(&file_priv->lock);

	if (cmdq) {
		file_priv->cmdq[engine] = NULL;
		if (cmdq->db_registered)
			ivpu_jsm_unregister_db(file_priv->vdev, cmdq->db_id);

		ivpu_cmdq_free(file_priv, cmdq);
	}
}

void ivpu_cmdq_release_all(struct ivpu_file_priv *file_priv)
{
	int i;

	mutex_lock(&file_priv->lock);

	for (i = 0; i < IVPU_NUM_ENGINES; i++)
		ivpu_cmdq_release_locked(file_priv, i);

	mutex_unlock(&file_priv->lock);
}

/*
 * Mark the doorbell as unregistered and reset job queue pointers.
 * This function needs to be called when the VPU hardware is restarted
 * and FW looses job queue state. The next time job queue is used it
 * will be registered again.
 */
static void ivpu_cmdq_reset_locked(struct ivpu_file_priv *file_priv, u16 engine)
{
	struct ivpu_cmdq *cmdq = file_priv->cmdq[engine];

	lockdep_assert_held(&file_priv->lock);

	if (cmdq) {
		cmdq->db_registered = false;
		cmdq->jobq->header.head = 0;
		cmdq->jobq->header.tail = 0;
		wmb(); /* Flush WC buffer for jobq header */
	}
}

static void ivpu_cmdq_reset_all(struct ivpu_file_priv *file_priv)
{
	int i;

	mutex_lock(&file_priv->lock);

	for (i = 0; i < IVPU_NUM_ENGINES; i++)
		ivpu_cmdq_reset_locked(file_priv, i);

	mutex_unlock(&file_priv->lock);
}

void ivpu_cmdq_reset_all_contexts(struct ivpu_device *vdev)
{
	struct ivpu_file_priv *file_priv;
	unsigned long ctx_id;

	xa_for_each(&vdev->context_xa, ctx_id, file_priv) {
		file_priv = ivpu_file_priv_get_by_ctx_id(vdev, ctx_id);
		if (!file_priv)
			continue;

		ivpu_cmdq_reset_all(file_priv);

		ivpu_file_priv_put(&file_priv);
	}
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
		ivpu_dbg(vdev, JOB, "Job queue full: ctx %d engine %d db %d head %d tail %d\n",
			 job->file_priv->ctx.id, job->engine_idx, cmdq->db_id, header->head, tail);
		return -EBUSY;
	}

	entry = &cmdq->jobq->job[tail];
	entry->batch_buf_addr = job->cmd_buf_vpu_addr;
	entry->job_id = job->job_id;
	entry->flags = 0;
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

static void job_get(struct ivpu_job *job, struct ivpu_job **link)
{
	struct ivpu_device *vdev = job->vdev;

	kref_get(&job->ref);
	*link = job;

	ivpu_dbg(vdev, KREF, "Job get: id %u refcount %u\n", job->job_id, kref_read(&job->ref));
}

static void job_release(struct kref *ref)
{
	struct ivpu_job *job = container_of(ref, struct ivpu_job, ref);
	struct ivpu_device *vdev = job->vdev;
	u32 i;

	for (i = 0; i < job->bo_count; i++)
		if (job->bos[i])
			drm_gem_object_put(&job->bos[i]->base);

	dma_fence_put(job->done_fence);
	ivpu_file_priv_put(&job->file_priv);

	ivpu_dbg(vdev, KREF, "Job released: id %u\n", job->job_id);
	kfree(job);

	/* Allow the VPU to get suspended, must be called after ivpu_file_priv_put() */
	ivpu_rpm_put(vdev);
}

static void job_put(struct ivpu_job *job)
{
	struct ivpu_device *vdev = job->vdev;

	ivpu_dbg(vdev, KREF, "Job put: id %u refcount %u\n", job->job_id, kref_read(&job->ref));
	kref_put(&job->ref, job_release);
}

static struct ivpu_job *
ivpu_create_job(struct ivpu_file_priv *file_priv, u32 engine_idx, u32 bo_count)
{
	struct ivpu_device *vdev = file_priv->vdev;
	struct ivpu_job *job;
	int ret;

	ret = ivpu_rpm_get(vdev);
	if (ret < 0)
		return NULL;

	job = kzalloc(struct_size(job, bos, bo_count), GFP_KERNEL);
	if (!job)
		goto err_rpm_put;

	kref_init(&job->ref);

	job->vdev = vdev;
	job->engine_idx = engine_idx;
	job->bo_count = bo_count;
	job->done_fence = ivpu_fence_create(vdev);
	if (!job->done_fence) {
		ivpu_warn_ratelimited(vdev, "Failed to create a fence\n");
		goto err_free_job;
	}

	job->file_priv = ivpu_file_priv_get(file_priv);

	ivpu_dbg(vdev, JOB, "Job created: ctx %2d engine %d", file_priv->ctx.id, job->engine_idx);

	return job;

err_free_job:
	kfree(job);
err_rpm_put:
	ivpu_rpm_put(vdev);
	return NULL;
}

static int ivpu_job_done(struct ivpu_device *vdev, u32 job_id, u32 job_status)
{
	struct ivpu_job *job;

	job = xa_erase(&vdev->submitted_jobs_xa, job_id);
	if (!job)
		return -ENOENT;

	if (job->file_priv->has_mmu_faults)
		job_status = VPU_JSM_STATUS_ABORTED;

	job->bos[CMD_BUF_IDX]->job_status = job_status;
	dma_fence_signal(job->done_fence);

	ivpu_dbg(vdev, JOB, "Job complete:  id %3u ctx %2d engine %d status 0x%x\n",
		 job->job_id, job->file_priv->ctx.id, job->engine_idx, job_status);

	job_put(job);
	return 0;
}

static void ivpu_job_done_message(struct ivpu_device *vdev, void *msg)
{
	struct vpu_ipc_msg_payload_job_done *payload;
	struct vpu_jsm_msg *job_ret_msg = msg;
	int ret;

	payload = (struct vpu_ipc_msg_payload_job_done *)&job_ret_msg->payload;

	ret = ivpu_job_done(vdev, payload->job_id, payload->job_status);
	if (ret)
		ivpu_err(vdev, "Failed to finish job %d: %d\n", payload->job_id, ret);
}

void ivpu_jobs_abort_all(struct ivpu_device *vdev)
{
	struct ivpu_job *job;
	unsigned long id;

	xa_for_each(&vdev->submitted_jobs_xa, id, job)
		ivpu_job_done(vdev, id, VPU_JSM_STATUS_ABORTED);
}

static int ivpu_direct_job_submission(struct ivpu_job *job)
{
	struct ivpu_file_priv *file_priv = job->file_priv;
	struct ivpu_device *vdev = job->vdev;
	struct xa_limit job_id_range;
	struct ivpu_cmdq *cmdq;
	int ret;

	mutex_lock(&file_priv->lock);

	cmdq = ivpu_cmdq_acquire(job->file_priv, job->engine_idx);
	if (!cmdq) {
		ivpu_warn(vdev, "Failed get job queue, ctx %d engine %d\n",
			  file_priv->ctx.id, job->engine_idx);
		ret = -EINVAL;
		goto err_unlock;
	}

	job_id_range.min = FIELD_PREP(JOB_ID_CONTEXT_MASK, (file_priv->ctx.id - 1));
	job_id_range.max = job_id_range.min | JOB_ID_JOB_MASK;

	job_get(job, &job);
	ret = xa_alloc(&vdev->submitted_jobs_xa, &job->job_id, job, job_id_range, GFP_KERNEL);
	if (ret) {
		ivpu_warn_ratelimited(vdev, "Failed to allocate job id: %d\n", ret);
		goto err_job_put;
	}

	ret = ivpu_cmdq_push_job(cmdq, job);
	if (ret)
		goto err_xa_erase;

	ivpu_dbg(vdev, JOB, "Job submitted: id %3u addr 0x%llx ctx %2d engine %d next %d\n",
		 job->job_id, job->cmd_buf_vpu_addr, file_priv->ctx.id,
		 job->engine_idx, cmdq->jobq->header.tail);

	if (ivpu_test_mode == IVPU_TEST_MODE_NULL_HW) {
		ivpu_job_done(vdev, job->job_id, VPU_JSM_STATUS_SUCCESS);
		cmdq->jobq->header.head = cmdq->jobq->header.tail;
		wmb(); /* Flush WC buffer for jobq header */
	} else {
		ivpu_cmdq_ring_db(vdev, cmdq);
	}

	mutex_unlock(&file_priv->lock);
	return 0;

err_xa_erase:
	xa_erase(&vdev->submitted_jobs_xa, job->job_id);
err_job_put:
	job_put(job);
err_unlock:
	mutex_unlock(&file_priv->lock);
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
	if (!dma_resv_test_signaled(bo->base.resv, DMA_RESV_USAGE_READ)) {
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
		ret = dma_resv_reserve_fences(job->bos[i]->base.resv, 1);
		if (ret) {
			ivpu_warn(vdev, "Failed to reserve fences: %d\n", ret);
			goto unlock_reservations;
		}
	}

	for (i = 0; i < buf_count; i++) {
		usage = (i == CMD_BUF_IDX) ? DMA_RESV_USAGE_WRITE : DMA_RESV_USAGE_BOOKKEEP;
		dma_resv_add_fence(job->bos[i]->base.resv, job->done_fence, usage);
	}

unlock_reservations:
	drm_gem_unlock_reservations((struct drm_gem_object **)job->bos, buf_count, &acquire_ctx);

	wmb(); /* Flush write combining buffers */

	return ret;
}

int ivpu_submit_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct ivpu_file_priv *file_priv = file->driver_priv;
	struct ivpu_device *vdev = file_priv->vdev;
	struct drm_ivpu_submit *params = data;
	struct ivpu_job *job;
	u32 *buf_handles;
	int idx, ret;

	if (params->engine > DRM_IVPU_ENGINE_COPY)
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
		goto free_handles;
	}

	if (!drm_dev_enter(&vdev->drm, &idx)) {
		ret = -ENODEV;
		goto free_handles;
	}

	ivpu_dbg(vdev, JOB, "Submit ioctl: ctx %u buf_count %u\n",
		 file_priv->ctx.id, params->buffer_count);

	job = ivpu_create_job(file_priv, params->engine, params->buffer_count);
	if (!job) {
		ivpu_err(vdev, "Failed to create job\n");
		ret = -ENOMEM;
		goto dev_exit;
	}

	ret = ivpu_job_prepare_bos_for_submit(file, job, buf_handles, params->buffer_count,
					      params->commands_offset);
	if (ret) {
		ivpu_err(vdev, "Failed to prepare job, ret %d\n", ret);
		goto job_put;
	}

	ret = ivpu_direct_job_submission(job);
	if (ret) {
		dma_fence_signal(job->done_fence);
		ivpu_err(vdev, "Failed to submit job to the HW, ret %d\n", ret);
	}

job_put:
	job_put(job);
dev_exit:
	drm_dev_exit(idx);
free_handles:
	kfree(buf_handles);

	return ret;
}

static int ivpu_job_done_thread(void *arg)
{
	struct ivpu_device *vdev = (struct ivpu_device *)arg;
	struct ivpu_ipc_consumer cons;
	struct vpu_jsm_msg jsm_msg;
	bool jobs_submitted;
	unsigned int timeout;
	int ret;

	ivpu_dbg(vdev, JOB, "Started %s\n", __func__);

	ivpu_ipc_consumer_add(vdev, &cons, VPU_IPC_CHAN_JOB_RET);

	while (!kthread_should_stop()) {
		timeout = ivpu_tdr_timeout_ms ? ivpu_tdr_timeout_ms : vdev->timeout.tdr;
		jobs_submitted = !xa_empty(&vdev->submitted_jobs_xa);
		ret = ivpu_ipc_receive(vdev, &cons, NULL, &jsm_msg, timeout);
		if (!ret) {
			ivpu_job_done_message(vdev, &jsm_msg);
		} else if (ret == -ETIMEDOUT) {
			if (jobs_submitted && !xa_empty(&vdev->submitted_jobs_xa)) {
				ivpu_err(vdev, "TDR detected, timeout %d ms", timeout);
				ivpu_hw_diagnose_failure(vdev);
				ivpu_pm_schedule_recovery(vdev);
			}
		}
	}

	ivpu_ipc_consumer_del(vdev, &cons);

	ivpu_jobs_abort_all(vdev);

	ivpu_dbg(vdev, JOB, "Stopped %s\n", __func__);
	return 0;
}

int ivpu_job_done_thread_init(struct ivpu_device *vdev)
{
	struct task_struct *thread;

	thread = kthread_run(&ivpu_job_done_thread, (void *)vdev, "ivpu_job_done_thread");
	if (IS_ERR(thread)) {
		ivpu_err(vdev, "Failed to start job completion thread\n");
		return -EIO;
	}

	get_task_struct(thread);
	wake_up_process(thread);

	vdev->job_done_thread = thread;

	return 0;
}

void ivpu_job_done_thread_fini(struct ivpu_device *vdev)
{
	kthread_stop_put(vdev->job_done_thread);
}
