/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/firmware.h>
#include <drm/drm_exec.h>

#include "amdgpu_mes.h"
#include "amdgpu.h"
#include "soc15_common.h"
#include "amdgpu_mes_ctx.h"

#define AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS 1024
#define AMDGPU_ONE_DOORBELL_SIZE 8

int amdgpu_mes_doorbell_process_slice(struct amdgpu_device *adev)
{
	return roundup(AMDGPU_ONE_DOORBELL_SIZE *
		       AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS,
		       PAGE_SIZE);
}

static int amdgpu_mes_kernel_doorbell_get(struct amdgpu_device *adev,
					 int ip_type, uint64_t *doorbell_index)
{
	unsigned int offset, found;
	struct amdgpu_mes *mes = &adev->mes;

	if (ip_type == AMDGPU_RING_TYPE_SDMA)
		offset = adev->doorbell_index.sdma_engine[0];
	else
		offset = 0;

	found = find_next_zero_bit(mes->doorbell_bitmap, mes->num_mes_dbs, offset);
	if (found >= mes->num_mes_dbs) {
		DRM_WARN("No doorbell available\n");
		return -ENOSPC;
	}

	set_bit(found, mes->doorbell_bitmap);

	/* Get the absolute doorbell index on BAR */
	*doorbell_index = mes->db_start_dw_offset + found * 2;
	return 0;
}

static void amdgpu_mes_kernel_doorbell_free(struct amdgpu_device *adev,
					   uint32_t doorbell_index)
{
	unsigned int old, rel_index;
	struct amdgpu_mes *mes = &adev->mes;

	/* Find the relative index of the doorbell in this object */
	rel_index = (doorbell_index - mes->db_start_dw_offset) / 2;
	old = test_and_clear_bit(rel_index, mes->doorbell_bitmap);
	WARN_ON(!old);
}

static int amdgpu_mes_doorbell_init(struct amdgpu_device *adev)
{
	int i;
	struct amdgpu_mes *mes = &adev->mes;

	/* Bitmap for dynamic allocation of kernel doorbells */
	mes->doorbell_bitmap = bitmap_zalloc(PAGE_SIZE / sizeof(u32), GFP_KERNEL);
	if (!mes->doorbell_bitmap) {
		DRM_ERROR("Failed to allocate MES doorbell bitmap\n");
		return -ENOMEM;
	}

	mes->num_mes_dbs = PAGE_SIZE / AMDGPU_ONE_DOORBELL_SIZE;
	for (i = 0; i < AMDGPU_MES_PRIORITY_NUM_LEVELS; i++) {
		adev->mes.aggregated_doorbells[i] = mes->db_start_dw_offset + i * 2;
		set_bit(i, mes->doorbell_bitmap);
	}

	return 0;
}

static int amdgpu_mes_event_log_init(struct amdgpu_device *adev)
{
	int r;

	if (!amdgpu_mes_log_enable)
		return 0;

	r = amdgpu_bo_create_kernel(adev, adev->mes.event_log_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_VRAM,
				    &adev->mes.event_log_gpu_obj,
				    &adev->mes.event_log_gpu_addr,
				    &adev->mes.event_log_cpu_addr);
	if (r) {
		dev_warn(adev->dev, "failed to create MES event log buffer (%d)", r);
		return r;
	}

	memset(adev->mes.event_log_cpu_addr, 0, adev->mes.event_log_size);

	return  0;

}

static void amdgpu_mes_doorbell_free(struct amdgpu_device *adev)
{
	bitmap_free(adev->mes.doorbell_bitmap);
}

int amdgpu_mes_init(struct amdgpu_device *adev)
{
	int i, r;

	adev->mes.adev = adev;

	idr_init(&adev->mes.pasid_idr);
	idr_init(&adev->mes.gang_id_idr);
	idr_init(&adev->mes.queue_id_idr);
	ida_init(&adev->mes.doorbell_ida);
	spin_lock_init(&adev->mes.queue_id_lock);
	mutex_init(&adev->mes.mutex_hidden);

	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++)
		spin_lock_init(&adev->mes.ring_lock[i]);

	adev->mes.total_max_queue = AMDGPU_FENCE_MES_QUEUE_ID_MASK;
	adev->mes.vmid_mask_mmhub = 0xffffff00;
	adev->mes.vmid_mask_gfxhub = 0xffffff00;

	for (i = 0; i < AMDGPU_MES_MAX_COMPUTE_PIPES; i++) {
		if (i >= (adev->gfx.mec.num_pipe_per_mec * adev->gfx.mec.num_mec))
			break;
		adev->mes.compute_hqd_mask[i] = 0xc;
	}

	for (i = 0; i < AMDGPU_MES_MAX_GFX_PIPES; i++)
		adev->mes.gfx_hqd_mask[i] = i ? 0 : 0xfffffffe;

	for (i = 0; i < AMDGPU_MES_MAX_SDMA_PIPES; i++) {
		if (i >= adev->sdma.num_instances)
			break;
		adev->mes.sdma_hqd_mask[i] = 0xfc;
	}

	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++) {
		r = amdgpu_device_wb_get(adev, &adev->mes.sch_ctx_offs[i]);
		if (r) {
			dev_err(adev->dev,
				"(%d) ring trail_fence_offs wb alloc failed\n",
				r);
			goto error;
		}
		adev->mes.sch_ctx_gpu_addr[i] =
			adev->wb.gpu_addr + (adev->mes.sch_ctx_offs[i] * 4);
		adev->mes.sch_ctx_ptr[i] =
			(uint64_t *)&adev->wb.wb[adev->mes.sch_ctx_offs[i]];

		r = amdgpu_device_wb_get(adev,
				 &adev->mes.query_status_fence_offs[i]);
		if (r) {
			dev_err(adev->dev,
			      "(%d) query_status_fence_offs wb alloc failed\n",
			      r);
			goto error;
		}
		adev->mes.query_status_fence_gpu_addr[i] = adev->wb.gpu_addr +
			(adev->mes.query_status_fence_offs[i] * 4);
		adev->mes.query_status_fence_ptr[i] =
			(uint64_t *)&adev->wb.wb[adev->mes.query_status_fence_offs[i]];
	}

	r = amdgpu_mes_doorbell_init(adev);
	if (r)
		goto error;

	r = amdgpu_mes_event_log_init(adev);
	if (r)
		goto error_doorbell;

	return 0;

error_doorbell:
	amdgpu_mes_doorbell_free(adev);
error:
	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++) {
		if (adev->mes.sch_ctx_ptr[i])
			amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs[i]);
		if (adev->mes.query_status_fence_ptr[i])
			amdgpu_device_wb_free(adev,
				      adev->mes.query_status_fence_offs[i]);
	}

	idr_destroy(&adev->mes.pasid_idr);
	idr_destroy(&adev->mes.gang_id_idr);
	idr_destroy(&adev->mes.queue_id_idr);
	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex_hidden);
	return r;
}

void amdgpu_mes_fini(struct amdgpu_device *adev)
{
	int i;

	amdgpu_bo_free_kernel(&adev->mes.event_log_gpu_obj,
			      &adev->mes.event_log_gpu_addr,
			      &adev->mes.event_log_cpu_addr);

	for (i = 0; i < AMDGPU_MAX_MES_PIPES; i++) {
		if (adev->mes.sch_ctx_ptr[i])
			amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs[i]);
		if (adev->mes.query_status_fence_ptr[i])
			amdgpu_device_wb_free(adev,
				      adev->mes.query_status_fence_offs[i]);
	}

	amdgpu_mes_doorbell_free(adev);

	idr_destroy(&adev->mes.pasid_idr);
	idr_destroy(&adev->mes.gang_id_idr);
	idr_destroy(&adev->mes.queue_id_idr);
	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex_hidden);
}

static void amdgpu_mes_queue_free_mqd(struct amdgpu_mes_queue *q)
{
	amdgpu_bo_free_kernel(&q->mqd_obj,
			      &q->mqd_gpu_addr,
			      &q->mqd_cpu_ptr);
}

int amdgpu_mes_create_process(struct amdgpu_device *adev, int pasid,
			      struct amdgpu_vm *vm)
{
	struct amdgpu_mes_process *process;
	int r;

	/* allocate the mes process buffer */
	process = kzalloc(sizeof(struct amdgpu_mes_process), GFP_KERNEL);
	if (!process) {
		DRM_ERROR("no more memory to create mes process\n");
		return -ENOMEM;
	}

	/* allocate the process context bo and map it */
	r = amdgpu_bo_create_kernel(adev, AMDGPU_MES_PROC_CTX_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &process->proc_ctx_bo,
				    &process->proc_ctx_gpu_addr,
				    &process->proc_ctx_cpu_ptr);
	if (r) {
		DRM_ERROR("failed to allocate process context bo\n");
		goto clean_up_memory;
	}
	memset(process->proc_ctx_cpu_ptr, 0, AMDGPU_MES_PROC_CTX_SIZE);

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	/* add the mes process to idr list */
	r = idr_alloc(&adev->mes.pasid_idr, process, pasid, pasid + 1,
		      GFP_KERNEL);
	if (r < 0) {
		DRM_ERROR("failed to lock pasid=%d\n", pasid);
		goto clean_up_ctx;
	}

	INIT_LIST_HEAD(&process->gang_list);
	process->vm = vm;
	process->pasid = pasid;
	process->process_quantum = adev->mes.default_process_quantum;
	process->pd_gpu_addr = amdgpu_bo_gpu_offset(vm->root.bo);

	amdgpu_mes_unlock(&adev->mes);
	return 0;

clean_up_ctx:
	amdgpu_mes_unlock(&adev->mes);
	amdgpu_bo_free_kernel(&process->proc_ctx_bo,
			      &process->proc_ctx_gpu_addr,
			      &process->proc_ctx_cpu_ptr);
clean_up_memory:
	kfree(process);
	return r;
}

void amdgpu_mes_destroy_process(struct amdgpu_device *adev, int pasid)
{
	struct amdgpu_mes_process *process;
	struct amdgpu_mes_gang *gang, *tmp1;
	struct amdgpu_mes_queue *queue, *tmp2;
	struct mes_remove_queue_input queue_input;
	unsigned long flags;
	int r;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	process = idr_find(&adev->mes.pasid_idr, pasid);
	if (!process) {
		DRM_WARN("pasid %d doesn't exist\n", pasid);
		amdgpu_mes_unlock(&adev->mes);
		return;
	}

	/* Remove all queues from hardware */
	list_for_each_entry_safe(gang, tmp1, &process->gang_list, list) {
		list_for_each_entry_safe(queue, tmp2, &gang->queue_list, list) {
			spin_lock_irqsave(&adev->mes.queue_id_lock, flags);
			idr_remove(&adev->mes.queue_id_idr, queue->queue_id);
			spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);

			queue_input.doorbell_offset = queue->doorbell_off;
			queue_input.gang_context_addr = gang->gang_ctx_gpu_addr;

			r = adev->mes.funcs->remove_hw_queue(&adev->mes,
							     &queue_input);
			if (r)
				DRM_WARN("failed to remove hardware queue\n");
		}

		idr_remove(&adev->mes.gang_id_idr, gang->gang_id);
	}

	idr_remove(&adev->mes.pasid_idr, pasid);
	amdgpu_mes_unlock(&adev->mes);

	/* free all memory allocated by the process */
	list_for_each_entry_safe(gang, tmp1, &process->gang_list, list) {
		/* free all queues in the gang */
		list_for_each_entry_safe(queue, tmp2, &gang->queue_list, list) {
			amdgpu_mes_queue_free_mqd(queue);
			list_del(&queue->list);
			kfree(queue);
		}
		amdgpu_bo_free_kernel(&gang->gang_ctx_bo,
				      &gang->gang_ctx_gpu_addr,
				      &gang->gang_ctx_cpu_ptr);
		list_del(&gang->list);
		kfree(gang);

	}
	amdgpu_bo_free_kernel(&process->proc_ctx_bo,
			      &process->proc_ctx_gpu_addr,
			      &process->proc_ctx_cpu_ptr);
	kfree(process);
}

int amdgpu_mes_add_gang(struct amdgpu_device *adev, int pasid,
			struct amdgpu_mes_gang_properties *gprops,
			int *gang_id)
{
	struct amdgpu_mes_process *process;
	struct amdgpu_mes_gang *gang;
	int r;

	/* allocate the mes gang buffer */
	gang = kzalloc(sizeof(struct amdgpu_mes_gang), GFP_KERNEL);
	if (!gang) {
		return -ENOMEM;
	}

	/* allocate the gang context bo and map it to cpu space */
	r = amdgpu_bo_create_kernel(adev, AMDGPU_MES_GANG_CTX_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &gang->gang_ctx_bo,
				    &gang->gang_ctx_gpu_addr,
				    &gang->gang_ctx_cpu_ptr);
	if (r) {
		DRM_ERROR("failed to allocate process context bo\n");
		goto clean_up_mem;
	}
	memset(gang->gang_ctx_cpu_ptr, 0, AMDGPU_MES_GANG_CTX_SIZE);

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	process = idr_find(&adev->mes.pasid_idr, pasid);
	if (!process) {
		DRM_ERROR("pasid %d doesn't exist\n", pasid);
		r = -EINVAL;
		goto clean_up_ctx;
	}

	/* add the mes gang to idr list */
	r = idr_alloc(&adev->mes.gang_id_idr, gang, 1, 0,
		      GFP_KERNEL);
	if (r < 0) {
		DRM_ERROR("failed to allocate idr for gang\n");
		goto clean_up_ctx;
	}

	gang->gang_id = r;
	*gang_id = r;

	INIT_LIST_HEAD(&gang->queue_list);
	gang->process = process;
	gang->priority = gprops->priority;
	gang->gang_quantum = gprops->gang_quantum ?
		gprops->gang_quantum : adev->mes.default_gang_quantum;
	gang->global_priority_level = gprops->global_priority_level;
	gang->inprocess_gang_priority = gprops->inprocess_gang_priority;
	list_add_tail(&gang->list, &process->gang_list);

	amdgpu_mes_unlock(&adev->mes);
	return 0;

clean_up_ctx:
	amdgpu_mes_unlock(&adev->mes);
	amdgpu_bo_free_kernel(&gang->gang_ctx_bo,
			      &gang->gang_ctx_gpu_addr,
			      &gang->gang_ctx_cpu_ptr);
clean_up_mem:
	kfree(gang);
	return r;
}

int amdgpu_mes_remove_gang(struct amdgpu_device *adev, int gang_id)
{
	struct amdgpu_mes_gang *gang;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	gang = idr_find(&adev->mes.gang_id_idr, gang_id);
	if (!gang) {
		DRM_ERROR("gang id %d doesn't exist\n", gang_id);
		amdgpu_mes_unlock(&adev->mes);
		return -EINVAL;
	}

	if (!list_empty(&gang->queue_list)) {
		DRM_ERROR("queue list is not empty\n");
		amdgpu_mes_unlock(&adev->mes);
		return -EBUSY;
	}

	idr_remove(&adev->mes.gang_id_idr, gang->gang_id);
	list_del(&gang->list);
	amdgpu_mes_unlock(&adev->mes);

	amdgpu_bo_free_kernel(&gang->gang_ctx_bo,
			      &gang->gang_ctx_gpu_addr,
			      &gang->gang_ctx_cpu_ptr);

	kfree(gang);

	return 0;
}

int amdgpu_mes_suspend(struct amdgpu_device *adev)
{
	struct mes_suspend_gang_input input;
	int r;

	if (!amdgpu_mes_suspend_resume_all_supported(adev))
		return 0;

	memset(&input, 0x0, sizeof(struct mes_suspend_gang_input));
	input.suspend_all_gangs = 1;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->suspend_gang(&adev->mes, &input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		DRM_ERROR("failed to suspend all gangs");

	return r;
}

int amdgpu_mes_resume(struct amdgpu_device *adev)
{
	struct mes_resume_gang_input input;
	int r;

	if (!amdgpu_mes_suspend_resume_all_supported(adev))
		return 0;

	memset(&input, 0x0, sizeof(struct mes_resume_gang_input));
	input.resume_all_gangs = 1;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->resume_gang(&adev->mes, &input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		DRM_ERROR("failed to resume all gangs");

	return r;
}

static int amdgpu_mes_queue_alloc_mqd(struct amdgpu_device *adev,
				     struct amdgpu_mes_queue *q,
				     struct amdgpu_mes_queue_properties *p)
{
	struct amdgpu_mqd *mqd_mgr = &adev->mqds[p->queue_type];
	u32 mqd_size = mqd_mgr->mqd_size;
	int r;

	r = amdgpu_bo_create_kernel(adev, mqd_size, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &q->mqd_obj,
				    &q->mqd_gpu_addr, &q->mqd_cpu_ptr);
	if (r) {
		dev_warn(adev->dev, "failed to create queue mqd bo (%d)", r);
		return r;
	}
	memset(q->mqd_cpu_ptr, 0, mqd_size);

	r = amdgpu_bo_reserve(q->mqd_obj, false);
	if (unlikely(r != 0))
		goto clean_up;

	return 0;

clean_up:
	amdgpu_bo_free_kernel(&q->mqd_obj,
			      &q->mqd_gpu_addr,
			      &q->mqd_cpu_ptr);
	return r;
}

static void amdgpu_mes_queue_init_mqd(struct amdgpu_device *adev,
				     struct amdgpu_mes_queue *q,
				     struct amdgpu_mes_queue_properties *p)
{
	struct amdgpu_mqd *mqd_mgr = &adev->mqds[p->queue_type];
	struct amdgpu_mqd_prop mqd_prop = {0};

	mqd_prop.mqd_gpu_addr = q->mqd_gpu_addr;
	mqd_prop.hqd_base_gpu_addr = p->hqd_base_gpu_addr;
	mqd_prop.rptr_gpu_addr = p->rptr_gpu_addr;
	mqd_prop.wptr_gpu_addr = p->wptr_gpu_addr;
	mqd_prop.queue_size = p->queue_size;
	mqd_prop.use_doorbell = true;
	mqd_prop.doorbell_index = p->doorbell_off;
	mqd_prop.eop_gpu_addr = p->eop_gpu_addr;
	mqd_prop.hqd_pipe_priority = p->hqd_pipe_priority;
	mqd_prop.hqd_queue_priority = p->hqd_queue_priority;
	mqd_prop.hqd_active = false;

	if (p->queue_type == AMDGPU_RING_TYPE_GFX ||
	    p->queue_type == AMDGPU_RING_TYPE_COMPUTE) {
		mutex_lock(&adev->srbm_mutex);
		amdgpu_gfx_select_me_pipe_q(adev, p->ring->me, p->ring->pipe, 0, 0, 0);
	}

	mqd_mgr->init_mqd(adev, q->mqd_cpu_ptr, &mqd_prop);

	if (p->queue_type == AMDGPU_RING_TYPE_GFX ||
	    p->queue_type == AMDGPU_RING_TYPE_COMPUTE) {
		amdgpu_gfx_select_me_pipe_q(adev, 0, 0, 0, 0, 0);
		mutex_unlock(&adev->srbm_mutex);
	}

	amdgpu_bo_unreserve(q->mqd_obj);
}

int amdgpu_mes_add_hw_queue(struct amdgpu_device *adev, int gang_id,
			    struct amdgpu_mes_queue_properties *qprops,
			    int *queue_id)
{
	struct amdgpu_mes_queue *queue;
	struct amdgpu_mes_gang *gang;
	struct mes_add_queue_input queue_input;
	unsigned long flags;
	int r;

	memset(&queue_input, 0, sizeof(struct mes_add_queue_input));

	/* allocate the mes queue buffer */
	queue = kzalloc(sizeof(struct amdgpu_mes_queue), GFP_KERNEL);
	if (!queue) {
		DRM_ERROR("Failed to allocate memory for queue\n");
		return -ENOMEM;
	}

	/* Allocate the queue mqd */
	r = amdgpu_mes_queue_alloc_mqd(adev, queue, qprops);
	if (r)
		goto clean_up_memory;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	gang = idr_find(&adev->mes.gang_id_idr, gang_id);
	if (!gang) {
		DRM_ERROR("gang id %d doesn't exist\n", gang_id);
		r = -EINVAL;
		goto clean_up_mqd;
	}

	/* add the mes gang to idr list */
	spin_lock_irqsave(&adev->mes.queue_id_lock, flags);
	r = idr_alloc(&adev->mes.queue_id_idr, queue, 1, 0,
		      GFP_ATOMIC);
	if (r < 0) {
		spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
		goto clean_up_mqd;
	}
	spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
	*queue_id = queue->queue_id = r;

	/* allocate a doorbell index for the queue */
	r = amdgpu_mes_kernel_doorbell_get(adev,
					  qprops->queue_type,
					  &qprops->doorbell_off);
	if (r)
		goto clean_up_queue_id;

	/* initialize the queue mqd */
	amdgpu_mes_queue_init_mqd(adev, queue, qprops);

	/* add hw queue to mes */
	queue_input.process_id = gang->process->pasid;

	queue_input.page_table_base_addr =
		adev->vm_manager.vram_base_offset + gang->process->pd_gpu_addr -
		adev->gmc.vram_start;

	queue_input.process_va_start = 0;
	queue_input.process_va_end =
		(adev->vm_manager.max_pfn - 1) << AMDGPU_GPU_PAGE_SHIFT;
	queue_input.process_quantum = gang->process->process_quantum;
	queue_input.process_context_addr = gang->process->proc_ctx_gpu_addr;
	queue_input.gang_quantum = gang->gang_quantum;
	queue_input.gang_context_addr = gang->gang_ctx_gpu_addr;
	queue_input.inprocess_gang_priority = gang->inprocess_gang_priority;
	queue_input.gang_global_priority_level = gang->global_priority_level;
	queue_input.doorbell_offset = qprops->doorbell_off;
	queue_input.mqd_addr = queue->mqd_gpu_addr;
	queue_input.wptr_addr = qprops->wptr_gpu_addr;
	queue_input.wptr_mc_addr = qprops->wptr_mc_addr;
	queue_input.queue_type = qprops->queue_type;
	queue_input.paging = qprops->paging;
	queue_input.is_kfd_process = 0;

	r = adev->mes.funcs->add_hw_queue(&adev->mes, &queue_input);
	if (r) {
		DRM_ERROR("failed to add hardware queue to MES, doorbell=0x%llx\n",
			  qprops->doorbell_off);
		goto clean_up_doorbell;
	}

	DRM_DEBUG("MES hw queue was added, pasid=%d, gang id=%d, "
		  "queue type=%d, doorbell=0x%llx\n",
		  gang->process->pasid, gang_id, qprops->queue_type,
		  qprops->doorbell_off);

	queue->ring = qprops->ring;
	queue->doorbell_off = qprops->doorbell_off;
	queue->wptr_gpu_addr = qprops->wptr_gpu_addr;
	queue->queue_type = qprops->queue_type;
	queue->paging = qprops->paging;
	queue->gang = gang;
	queue->ring->mqd_ptr = queue->mqd_cpu_ptr;
	list_add_tail(&queue->list, &gang->queue_list);

	amdgpu_mes_unlock(&adev->mes);
	return 0;

clean_up_doorbell:
	amdgpu_mes_kernel_doorbell_free(adev, qprops->doorbell_off);
clean_up_queue_id:
	spin_lock_irqsave(&adev->mes.queue_id_lock, flags);
	idr_remove(&adev->mes.queue_id_idr, queue->queue_id);
	spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
clean_up_mqd:
	amdgpu_mes_unlock(&adev->mes);
	amdgpu_mes_queue_free_mqd(queue);
clean_up_memory:
	kfree(queue);
	return r;
}

int amdgpu_mes_remove_hw_queue(struct amdgpu_device *adev, int queue_id)
{
	unsigned long flags;
	struct amdgpu_mes_queue *queue;
	struct amdgpu_mes_gang *gang;
	struct mes_remove_queue_input queue_input;
	int r;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	/* remove the mes gang from idr list */
	spin_lock_irqsave(&adev->mes.queue_id_lock, flags);

	queue = idr_find(&adev->mes.queue_id_idr, queue_id);
	if (!queue) {
		spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
		amdgpu_mes_unlock(&adev->mes);
		DRM_ERROR("queue id %d doesn't exist\n", queue_id);
		return -EINVAL;
	}

	idr_remove(&adev->mes.queue_id_idr, queue_id);
	spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);

	DRM_DEBUG("try to remove queue, doorbell off = 0x%llx\n",
		  queue->doorbell_off);

	gang = queue->gang;
	queue_input.doorbell_offset = queue->doorbell_off;
	queue_input.gang_context_addr = gang->gang_ctx_gpu_addr;

	r = adev->mes.funcs->remove_hw_queue(&adev->mes, &queue_input);
	if (r)
		DRM_ERROR("failed to remove hardware queue, queue id = %d\n",
			  queue_id);

	list_del(&queue->list);
	amdgpu_mes_kernel_doorbell_free(adev, queue->doorbell_off);
	amdgpu_mes_unlock(&adev->mes);

	amdgpu_mes_queue_free_mqd(queue);
	kfree(queue);
	return 0;
}

int amdgpu_mes_reset_hw_queue(struct amdgpu_device *adev, int queue_id)
{
	unsigned long flags;
	struct amdgpu_mes_queue *queue;
	struct amdgpu_mes_gang *gang;
	struct mes_reset_queue_input queue_input;
	int r;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	/* remove the mes gang from idr list */
	spin_lock_irqsave(&adev->mes.queue_id_lock, flags);

	queue = idr_find(&adev->mes.queue_id_idr, queue_id);
	if (!queue) {
		spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
		amdgpu_mes_unlock(&adev->mes);
		DRM_ERROR("queue id %d doesn't exist\n", queue_id);
		return -EINVAL;
	}
	spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);

	DRM_DEBUG("try to reset queue, doorbell off = 0x%llx\n",
		  queue->doorbell_off);

	gang = queue->gang;
	queue_input.doorbell_offset = queue->doorbell_off;
	queue_input.gang_context_addr = gang->gang_ctx_gpu_addr;

	r = adev->mes.funcs->reset_hw_queue(&adev->mes, &queue_input);
	if (r)
		DRM_ERROR("failed to reset hardware queue, queue id = %d\n",
			  queue_id);

	amdgpu_mes_unlock(&adev->mes);

	return 0;
}

int amdgpu_mes_reset_hw_queue_mmio(struct amdgpu_device *adev, int queue_type,
				   int me_id, int pipe_id, int queue_id, int vmid)
{
	struct mes_reset_queue_input queue_input;
	int r;

	queue_input.queue_type = queue_type;
	queue_input.use_mmio = true;
	queue_input.me_id = me_id;
	queue_input.pipe_id = pipe_id;
	queue_input.queue_id = queue_id;
	queue_input.vmid = vmid;
	r = adev->mes.funcs->reset_hw_queue(&adev->mes, &queue_input);
	if (r)
		DRM_ERROR("failed to reset hardware queue by mmio, queue id = %d\n",
			  queue_id);
	return r;
}

int amdgpu_mes_map_legacy_queue(struct amdgpu_device *adev,
				struct amdgpu_ring *ring)
{
	struct mes_map_legacy_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

	queue_input.queue_type = ring->funcs->type;
	queue_input.doorbell_offset = ring->doorbell_index;
	queue_input.pipe_id = ring->pipe;
	queue_input.queue_id = ring->queue;
	queue_input.mqd_addr = amdgpu_bo_gpu_offset(ring->mqd_obj);
	queue_input.wptr_addr = ring->wptr_gpu_addr;

	r = adev->mes.funcs->map_legacy_queue(&adev->mes, &queue_input);
	if (r)
		DRM_ERROR("failed to map legacy queue\n");

	return r;
}

int amdgpu_mes_unmap_legacy_queue(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring,
				  enum amdgpu_unmap_queues_action action,
				  u64 gpu_addr, u64 seq)
{
	struct mes_unmap_legacy_queue_input queue_input;
	int r;

	queue_input.action = action;
	queue_input.queue_type = ring->funcs->type;
	queue_input.doorbell_offset = ring->doorbell_index;
	queue_input.pipe_id = ring->pipe;
	queue_input.queue_id = ring->queue;
	queue_input.trail_fence_addr = gpu_addr;
	queue_input.trail_fence_data = seq;

	r = adev->mes.funcs->unmap_legacy_queue(&adev->mes, &queue_input);
	if (r)
		DRM_ERROR("failed to unmap legacy queue\n");

	return r;
}

int amdgpu_mes_reset_legacy_queue(struct amdgpu_device *adev,
				  struct amdgpu_ring *ring,
				  unsigned int vmid,
				  bool use_mmio)
{
	struct mes_reset_legacy_queue_input queue_input;
	int r;

	memset(&queue_input, 0, sizeof(queue_input));

	queue_input.queue_type = ring->funcs->type;
	queue_input.doorbell_offset = ring->doorbell_index;
	queue_input.me_id = ring->me;
	queue_input.pipe_id = ring->pipe;
	queue_input.queue_id = ring->queue;
	queue_input.mqd_addr = ring->mqd_obj ? amdgpu_bo_gpu_offset(ring->mqd_obj) : 0;
	queue_input.wptr_addr = ring->wptr_gpu_addr;
	queue_input.vmid = vmid;
	queue_input.use_mmio = use_mmio;

	r = adev->mes.funcs->reset_legacy_queue(&adev->mes, &queue_input);
	if (r)
		DRM_ERROR("failed to reset legacy queue\n");

	return r;
}

uint32_t amdgpu_mes_rreg(struct amdgpu_device *adev, uint32_t reg)
{
	struct mes_misc_op_input op_input;
	int r, val = 0;
	uint32_t addr_offset = 0;
	uint64_t read_val_gpu_addr;
	uint32_t *read_val_ptr;

	if (amdgpu_device_wb_get(adev, &addr_offset)) {
		DRM_ERROR("critical bug! too many mes readers\n");
		goto error;
	}
	read_val_gpu_addr = adev->wb.gpu_addr + (addr_offset * 4);
	read_val_ptr = (uint32_t *)&adev->wb.wb[addr_offset];
	op_input.op = MES_MISC_OP_READ_REG;
	op_input.read_reg.reg_offset = reg;
	op_input.read_reg.buffer_addr = read_val_gpu_addr;

	if (!adev->mes.funcs->misc_op) {
		DRM_ERROR("mes rreg is not supported!\n");
		goto error;
	}

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		DRM_ERROR("failed to read reg (0x%x)\n", reg);
	else
		val = *(read_val_ptr);

error:
	if (addr_offset)
		amdgpu_device_wb_free(adev, addr_offset);
	return val;
}

int amdgpu_mes_wreg(struct amdgpu_device *adev,
		    uint32_t reg, uint32_t val)
{
	struct mes_misc_op_input op_input;
	int r;

	op_input.op = MES_MISC_OP_WRITE_REG;
	op_input.write_reg.reg_offset = reg;
	op_input.write_reg.reg_value = val;

	if (!adev->mes.funcs->misc_op) {
		DRM_ERROR("mes wreg is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		DRM_ERROR("failed to write reg (0x%x)\n", reg);

error:
	return r;
}

int amdgpu_mes_reg_write_reg_wait(struct amdgpu_device *adev,
				  uint32_t reg0, uint32_t reg1,
				  uint32_t ref, uint32_t mask)
{
	struct mes_misc_op_input op_input;
	int r;

	op_input.op = MES_MISC_OP_WRM_REG_WR_WAIT;
	op_input.wrm_reg.reg0 = reg0;
	op_input.wrm_reg.reg1 = reg1;
	op_input.wrm_reg.ref = ref;
	op_input.wrm_reg.mask = mask;

	if (!adev->mes.funcs->misc_op) {
		DRM_ERROR("mes reg_write_reg_wait is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		DRM_ERROR("failed to reg_write_reg_wait\n");

error:
	return r;
}

int amdgpu_mes_reg_wait(struct amdgpu_device *adev, uint32_t reg,
			uint32_t val, uint32_t mask)
{
	struct mes_misc_op_input op_input;
	int r;

	op_input.op = MES_MISC_OP_WRM_REG_WAIT;
	op_input.wrm_reg.reg0 = reg;
	op_input.wrm_reg.ref = val;
	op_input.wrm_reg.mask = mask;

	if (!adev->mes.funcs->misc_op) {
		DRM_ERROR("mes reg wait is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		DRM_ERROR("failed to reg_write_reg_wait\n");

error:
	return r;
}

int amdgpu_mes_set_shader_debugger(struct amdgpu_device *adev,
				uint64_t process_context_addr,
				uint32_t spi_gdbg_per_vmid_cntl,
				const uint32_t *tcp_watch_cntl,
				uint32_t flags,
				bool trap_en)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	if (!adev->mes.funcs->misc_op) {
		DRM_ERROR("mes set shader debugger is not supported!\n");
		return -EINVAL;
	}

	op_input.op = MES_MISC_OP_SET_SHADER_DEBUGGER;
	op_input.set_shader_debugger.process_context_addr = process_context_addr;
	op_input.set_shader_debugger.flags.u32all = flags;

	/* use amdgpu mes_flush_shader_debugger instead */
	if (op_input.set_shader_debugger.flags.process_ctx_flush)
		return -EINVAL;

	op_input.set_shader_debugger.spi_gdbg_per_vmid_cntl = spi_gdbg_per_vmid_cntl;
	memcpy(op_input.set_shader_debugger.tcp_watch_cntl, tcp_watch_cntl,
			sizeof(op_input.set_shader_debugger.tcp_watch_cntl));

	if (((adev->mes.sched_version & AMDGPU_MES_API_VERSION_MASK) >>
			AMDGPU_MES_API_VERSION_SHIFT) >= 14)
		op_input.set_shader_debugger.trap_en = trap_en;

	amdgpu_mes_lock(&adev->mes);

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		DRM_ERROR("failed to set_shader_debugger\n");

	amdgpu_mes_unlock(&adev->mes);

	return r;
}

int amdgpu_mes_flush_shader_debugger(struct amdgpu_device *adev,
				     uint64_t process_context_addr)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	if (!adev->mes.funcs->misc_op) {
		DRM_ERROR("mes flush shader debugger is not supported!\n");
		return -EINVAL;
	}

	op_input.op = MES_MISC_OP_SET_SHADER_DEBUGGER;
	op_input.set_shader_debugger.process_context_addr = process_context_addr;
	op_input.set_shader_debugger.flags.process_ctx_flush = true;

	amdgpu_mes_lock(&adev->mes);

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		DRM_ERROR("failed to set_shader_debugger\n");

	amdgpu_mes_unlock(&adev->mes);

	return r;
}

static void
amdgpu_mes_ring_to_queue_props(struct amdgpu_device *adev,
			       struct amdgpu_ring *ring,
			       struct amdgpu_mes_queue_properties *props)
{
	props->queue_type = ring->funcs->type;
	props->hqd_base_gpu_addr = ring->gpu_addr;
	props->rptr_gpu_addr = ring->rptr_gpu_addr;
	props->wptr_gpu_addr = ring->wptr_gpu_addr;
	props->wptr_mc_addr =
		ring->mes_ctx->meta_data_mc_addr + ring->wptr_offs;
	props->queue_size = ring->ring_size;
	props->eop_gpu_addr = ring->eop_gpu_addr;
	props->hqd_pipe_priority = AMDGPU_GFX_PIPE_PRIO_NORMAL;
	props->hqd_queue_priority = AMDGPU_GFX_QUEUE_PRIORITY_MINIMUM;
	props->paging = false;
	props->ring = ring;
}

#define DEFINE_AMDGPU_MES_CTX_GET_OFFS_ENG(_eng)			\
do {									\
       if (id_offs < AMDGPU_MES_CTX_MAX_OFFS)				\
		return offsetof(struct amdgpu_mes_ctx_meta_data,	\
				_eng[ring->idx].slots[id_offs]);        \
       else if (id_offs == AMDGPU_MES_CTX_RING_OFFS)			\
		return offsetof(struct amdgpu_mes_ctx_meta_data,        \
				_eng[ring->idx].ring);                  \
       else if (id_offs == AMDGPU_MES_CTX_IB_OFFS)			\
		return offsetof(struct amdgpu_mes_ctx_meta_data,        \
				_eng[ring->idx].ib);                    \
       else if (id_offs == AMDGPU_MES_CTX_PADDING_OFFS)			\
		return offsetof(struct amdgpu_mes_ctx_meta_data,        \
				_eng[ring->idx].padding);               \
} while(0)

int amdgpu_mes_ctx_get_offs(struct amdgpu_ring *ring, unsigned int id_offs)
{
	switch (ring->funcs->type) {
	case AMDGPU_RING_TYPE_GFX:
		DEFINE_AMDGPU_MES_CTX_GET_OFFS_ENG(gfx);
		break;
	case AMDGPU_RING_TYPE_COMPUTE:
		DEFINE_AMDGPU_MES_CTX_GET_OFFS_ENG(compute);
		break;
	case AMDGPU_RING_TYPE_SDMA:
		DEFINE_AMDGPU_MES_CTX_GET_OFFS_ENG(sdma);
		break;
	default:
		break;
	}

	WARN_ON(1);
	return -EINVAL;
}

int amdgpu_mes_add_ring(struct amdgpu_device *adev, int gang_id,
			int queue_type, int idx,
			struct amdgpu_mes_ctx_data *ctx_data,
			struct amdgpu_ring **out)
{
	struct amdgpu_ring *ring;
	struct amdgpu_mes_gang *gang;
	struct amdgpu_mes_queue_properties qprops = {0};
	int r, queue_id, pasid;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);
	gang = idr_find(&adev->mes.gang_id_idr, gang_id);
	if (!gang) {
		DRM_ERROR("gang id %d doesn't exist\n", gang_id);
		amdgpu_mes_unlock(&adev->mes);
		return -EINVAL;
	}
	pasid = gang->process->pasid;

	ring = kzalloc(sizeof(struct amdgpu_ring), GFP_KERNEL);
	if (!ring) {
		amdgpu_mes_unlock(&adev->mes);
		return -ENOMEM;
	}

	ring->ring_obj = NULL;
	ring->use_doorbell = true;
	ring->is_mes_queue = true;
	ring->mes_ctx = ctx_data;
	ring->idx = idx;
	ring->no_scheduler = true;

	if (queue_type == AMDGPU_RING_TYPE_COMPUTE) {
		int offset = offsetof(struct amdgpu_mes_ctx_meta_data,
				      compute[ring->idx].mec_hpd);
		ring->eop_gpu_addr =
			amdgpu_mes_ctx_get_offs_gpu_addr(ring, offset);
	}

	switch (queue_type) {
	case AMDGPU_RING_TYPE_GFX:
		ring->funcs = adev->gfx.gfx_ring[0].funcs;
		ring->me = adev->gfx.gfx_ring[0].me;
		ring->pipe = adev->gfx.gfx_ring[0].pipe;
		break;
	case AMDGPU_RING_TYPE_COMPUTE:
		ring->funcs = adev->gfx.compute_ring[0].funcs;
		ring->me = adev->gfx.compute_ring[0].me;
		ring->pipe = adev->gfx.compute_ring[0].pipe;
		break;
	case AMDGPU_RING_TYPE_SDMA:
		ring->funcs = adev->sdma.instance[0].ring.funcs;
		break;
	default:
		BUG();
	}

	r = amdgpu_ring_init(adev, ring, 1024, NULL, 0,
			     AMDGPU_RING_PRIO_DEFAULT, NULL);
	if (r) {
		amdgpu_mes_unlock(&adev->mes);
		goto clean_up_memory;
	}

	amdgpu_mes_ring_to_queue_props(adev, ring, &qprops);

	dma_fence_wait(gang->process->vm->last_update, false);
	dma_fence_wait(ctx_data->meta_data_va->last_pt_update, false);
	amdgpu_mes_unlock(&adev->mes);

	r = amdgpu_mes_add_hw_queue(adev, gang_id, &qprops, &queue_id);
	if (r)
		goto clean_up_ring;

	ring->hw_queue_id = queue_id;
	ring->doorbell_index = qprops.doorbell_off;

	if (queue_type == AMDGPU_RING_TYPE_GFX)
		sprintf(ring->name, "gfx_%d.%d.%d", pasid, gang_id, queue_id);
	else if (queue_type == AMDGPU_RING_TYPE_COMPUTE)
		sprintf(ring->name, "compute_%d.%d.%d", pasid, gang_id,
			queue_id);
	else if (queue_type == AMDGPU_RING_TYPE_SDMA)
		sprintf(ring->name, "sdma_%d.%d.%d", pasid, gang_id,
			queue_id);
	else
		BUG();

	*out = ring;
	return 0;

clean_up_ring:
	amdgpu_ring_fini(ring);
clean_up_memory:
	kfree(ring);
	return r;
}

void amdgpu_mes_remove_ring(struct amdgpu_device *adev,
			    struct amdgpu_ring *ring)
{
	if (!ring)
		return;

	amdgpu_mes_remove_hw_queue(adev, ring->hw_queue_id);
	timer_delete_sync(&ring->fence_drv.fallback_timer);
	amdgpu_ring_fini(ring);
	kfree(ring);
}

uint32_t amdgpu_mes_get_aggregated_doorbell_index(struct amdgpu_device *adev,
						   enum amdgpu_mes_priority_level prio)
{
	return adev->mes.aggregated_doorbells[prio];
}

int amdgpu_mes_ctx_alloc_meta_data(struct amdgpu_device *adev,
				   struct amdgpu_mes_ctx_data *ctx_data)
{
	int r;

	r = amdgpu_bo_create_kernel(adev,
			    sizeof(struct amdgpu_mes_ctx_meta_data),
			    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
			    &ctx_data->meta_data_obj,
			    &ctx_data->meta_data_mc_addr,
			    &ctx_data->meta_data_ptr);
	if (r) {
		dev_warn(adev->dev, "(%d) create CTX bo failed\n", r);
		return r;
	}

	if (!ctx_data->meta_data_obj)
		return -ENOMEM;

	memset(ctx_data->meta_data_ptr, 0,
	       sizeof(struct amdgpu_mes_ctx_meta_data));

	return 0;
}

void amdgpu_mes_ctx_free_meta_data(struct amdgpu_mes_ctx_data *ctx_data)
{
	if (ctx_data->meta_data_obj)
		amdgpu_bo_free_kernel(&ctx_data->meta_data_obj,
				      &ctx_data->meta_data_mc_addr,
				      &ctx_data->meta_data_ptr);
}

int amdgpu_mes_ctx_map_meta_data(struct amdgpu_device *adev,
				 struct amdgpu_vm *vm,
				 struct amdgpu_mes_ctx_data *ctx_data)
{
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_sync sync;
	struct drm_exec exec;
	int r;

	amdgpu_sync_create(&sync);

	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		r = drm_exec_lock_obj(&exec,
				      &ctx_data->meta_data_obj->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto error_fini_exec;

		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto error_fini_exec;
	}

	bo_va = amdgpu_vm_bo_add(adev, vm, ctx_data->meta_data_obj);
	if (!bo_va) {
		DRM_ERROR("failed to create bo_va for meta data BO\n");
		r = -ENOMEM;
		goto error_fini_exec;
	}

	r = amdgpu_vm_bo_map(adev, bo_va, ctx_data->meta_data_gpu_addr, 0,
			     sizeof(struct amdgpu_mes_ctx_meta_data),
			     AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
			     AMDGPU_PTE_EXECUTABLE);

	if (r) {
		DRM_ERROR("failed to do bo_map on meta data, err=%d\n", r);
		goto error_del_bo_va;
	}

	r = amdgpu_vm_bo_update(adev, bo_va, false);
	if (r) {
		DRM_ERROR("failed to do vm_bo_update on meta data\n");
		goto error_del_bo_va;
	}
	amdgpu_sync_fence(&sync, bo_va->last_pt_update, GFP_KERNEL);

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r) {
		DRM_ERROR("failed to update pdes on meta data\n");
		goto error_del_bo_va;
	}
	amdgpu_sync_fence(&sync, vm->last_update, GFP_KERNEL);

	amdgpu_sync_wait(&sync, false);
	drm_exec_fini(&exec);

	amdgpu_sync_free(&sync);
	ctx_data->meta_data_va = bo_va;
	return 0;

error_del_bo_va:
	amdgpu_vm_bo_del(adev, bo_va);

error_fini_exec:
	drm_exec_fini(&exec);
	amdgpu_sync_free(&sync);
	return r;
}

int amdgpu_mes_ctx_unmap_meta_data(struct amdgpu_device *adev,
				   struct amdgpu_mes_ctx_data *ctx_data)
{
	struct amdgpu_bo_va *bo_va = ctx_data->meta_data_va;
	struct amdgpu_bo *bo = ctx_data->meta_data_obj;
	struct amdgpu_vm *vm = bo_va->base.vm;
	struct dma_fence *fence;
	struct drm_exec exec;
	long r;

	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		r = drm_exec_lock_obj(&exec,
				      &ctx_data->meta_data_obj->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto out_unlock;

		r = amdgpu_vm_lock_pd(vm, &exec, 0);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(r))
			goto out_unlock;
	}

	amdgpu_vm_bo_del(adev, bo_va);
	if (!amdgpu_vm_ready(vm))
		goto out_unlock;

	r = dma_resv_get_singleton(bo->tbo.base.resv, DMA_RESV_USAGE_BOOKKEEP,
				   &fence);
	if (r)
		goto out_unlock;
	if (fence) {
		amdgpu_bo_fence(bo, fence, true);
		fence = NULL;
	}

	r = amdgpu_vm_clear_freed(adev, vm, &fence);
	if (r || !fence)
		goto out_unlock;

	dma_fence_wait(fence, false);
	amdgpu_bo_fence(bo, fence, true);
	dma_fence_put(fence);

out_unlock:
	if (unlikely(r < 0))
		dev_err(adev->dev, "failed to clear page tables (%ld)\n", r);
	drm_exec_fini(&exec);

	return r;
}

static int amdgpu_mes_test_create_gang_and_queues(struct amdgpu_device *adev,
					  int pasid, int *gang_id,
					  int queue_type, int num_queue,
					  struct amdgpu_ring **added_rings,
					  struct amdgpu_mes_ctx_data *ctx_data)
{
	struct amdgpu_ring *ring;
	struct amdgpu_mes_gang_properties gprops = {0};
	int r, j;

	/* create a gang for the process */
	gprops.priority = AMDGPU_MES_PRIORITY_LEVEL_NORMAL;
	gprops.gang_quantum = adev->mes.default_gang_quantum;
	gprops.inprocess_gang_priority = AMDGPU_MES_PRIORITY_LEVEL_NORMAL;
	gprops.priority_level = AMDGPU_MES_PRIORITY_LEVEL_NORMAL;
	gprops.global_priority_level = AMDGPU_MES_PRIORITY_LEVEL_NORMAL;

	r = amdgpu_mes_add_gang(adev, pasid, &gprops, gang_id);
	if (r) {
		DRM_ERROR("failed to add gang\n");
		return r;
	}

	/* create queues for the gang */
	for (j = 0; j < num_queue; j++) {
		r = amdgpu_mes_add_ring(adev, *gang_id, queue_type, j,
					ctx_data, &ring);
		if (r) {
			DRM_ERROR("failed to add ring\n");
			break;
		}

		DRM_INFO("ring %s was added\n", ring->name);
		added_rings[j] = ring;
	}

	return 0;
}

static int amdgpu_mes_test_queues(struct amdgpu_ring **added_rings)
{
	struct amdgpu_ring *ring;
	int i, r;

	for (i = 0; i < AMDGPU_MES_CTX_MAX_RINGS; i++) {
		ring = added_rings[i];
		if (!ring)
			continue;

		r = amdgpu_ring_test_helper(ring);
		if (r)
			return r;

		r = amdgpu_ring_test_ib(ring, 1000 * 10);
		if (r) {
			DRM_DEV_ERROR(ring->adev->dev,
				      "ring %s ib test failed (%d)\n",
				      ring->name, r);
			return r;
		} else
			DRM_INFO("ring %s ib test pass\n", ring->name);
	}

	return 0;
}

int amdgpu_mes_self_test(struct amdgpu_device *adev)
{
	struct amdgpu_vm *vm = NULL;
	struct amdgpu_mes_ctx_data ctx_data = {0};
	struct amdgpu_ring *added_rings[AMDGPU_MES_CTX_MAX_RINGS] = { NULL };
	int gang_ids[3] = {0};
	int queue_types[][2] = { { AMDGPU_RING_TYPE_GFX, 1 },
				 { AMDGPU_RING_TYPE_COMPUTE, 1 },
				 { AMDGPU_RING_TYPE_SDMA, 1} };
	int i, r, pasid, k = 0;

	pasid = amdgpu_pasid_alloc(16);
	if (pasid < 0) {
		dev_warn(adev->dev, "No more PASIDs available!");
		pasid = 0;
	}

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm) {
		r = -ENOMEM;
		goto error_pasid;
	}

	r = amdgpu_vm_init(adev, vm, -1);
	if (r) {
		DRM_ERROR("failed to initialize vm\n");
		goto error_pasid;
	}

	r = amdgpu_mes_ctx_alloc_meta_data(adev, &ctx_data);
	if (r) {
		DRM_ERROR("failed to alloc ctx meta data\n");
		goto error_fini;
	}

	ctx_data.meta_data_gpu_addr = AMDGPU_VA_RESERVED_BOTTOM;
	r = amdgpu_mes_ctx_map_meta_data(adev, vm, &ctx_data);
	if (r) {
		DRM_ERROR("failed to map ctx meta data\n");
		goto error_vm;
	}

	r = amdgpu_mes_create_process(adev, pasid, vm);
	if (r) {
		DRM_ERROR("failed to create MES process\n");
		goto error_vm;
	}

	for (i = 0; i < ARRAY_SIZE(queue_types); i++) {
		/* On GFX v10.3, fw hasn't supported to map sdma queue. */
		if (amdgpu_ip_version(adev, GC_HWIP, 0) >=
			    IP_VERSION(10, 3, 0) &&
		    amdgpu_ip_version(adev, GC_HWIP, 0) <
			    IP_VERSION(11, 0, 0) &&
		    queue_types[i][0] == AMDGPU_RING_TYPE_SDMA)
			continue;

		r = amdgpu_mes_test_create_gang_and_queues(adev, pasid,
							   &gang_ids[i],
							   queue_types[i][0],
							   queue_types[i][1],
							   &added_rings[k],
							   &ctx_data);
		if (r)
			goto error_queues;

		k += queue_types[i][1];
	}

	/* start ring test and ib test for MES queues */
	amdgpu_mes_test_queues(added_rings);

error_queues:
	/* remove all queues */
	for (i = 0; i < ARRAY_SIZE(added_rings); i++) {
		if (!added_rings[i])
			continue;
		amdgpu_mes_remove_ring(adev, added_rings[i]);
	}

	for (i = 0; i < ARRAY_SIZE(gang_ids); i++) {
		if (!gang_ids[i])
			continue;
		amdgpu_mes_remove_gang(adev, gang_ids[i]);
	}

	amdgpu_mes_destroy_process(adev, pasid);

error_vm:
	amdgpu_mes_ctx_unmap_meta_data(adev, &ctx_data);

error_fini:
	amdgpu_vm_fini(adev, vm);

error_pasid:
	if (pasid)
		amdgpu_pasid_free(pasid);

	amdgpu_mes_ctx_free_meta_data(&ctx_data);
	kfree(vm);
	return 0;
}

int amdgpu_mes_init_microcode(struct amdgpu_device *adev, int pipe)
{
	const struct mes_firmware_header_v1_0 *mes_hdr;
	struct amdgpu_firmware_info *info;
	char ucode_prefix[30];
	char fw_name[50];
	bool need_retry = false;
	u32 *ucode_ptr;
	int r;

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix,
				       sizeof(ucode_prefix));
	if (adev->enable_uni_mes) {
		snprintf(fw_name, sizeof(fw_name),
			 "amdgpu/%s_uni_mes.bin", ucode_prefix);
	} else if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0) &&
	    amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(12, 0, 0)) {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes%s.bin",
			 ucode_prefix,
			 pipe == AMDGPU_MES_SCHED_PIPE ? "_2" : "1");
		need_retry = true;
	} else {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes%s.bin",
			 ucode_prefix,
			 pipe == AMDGPU_MES_SCHED_PIPE ? "" : "1");
	}

	r = amdgpu_ucode_request(adev, &adev->mes.fw[pipe], AMDGPU_UCODE_REQUIRED,
				 "%s", fw_name);
	if (r && need_retry && pipe == AMDGPU_MES_SCHED_PIPE) {
		dev_info(adev->dev, "try to fall back to %s_mes.bin\n", ucode_prefix);
		r = amdgpu_ucode_request(adev, &adev->mes.fw[pipe],
					 AMDGPU_UCODE_REQUIRED,
					 "amdgpu/%s_mes.bin", ucode_prefix);
	}

	if (r)
		goto out;

	mes_hdr = (const struct mes_firmware_header_v1_0 *)
		adev->mes.fw[pipe]->data;
	adev->mes.uc_start_addr[pipe] =
		le32_to_cpu(mes_hdr->mes_uc_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_uc_start_addr_hi)) << 32);
	adev->mes.data_start_addr[pipe] =
		le32_to_cpu(mes_hdr->mes_data_start_addr_lo) |
		((uint64_t)(le32_to_cpu(mes_hdr->mes_data_start_addr_hi)) << 32);
	ucode_ptr = (u32 *)(adev->mes.fw[pipe]->data +
			  sizeof(union amdgpu_firmware_header));
	adev->mes.fw_version[pipe] =
		le32_to_cpu(ucode_ptr[24]) & AMDGPU_MES_VERSION_MASK;

	if (adev->firmware.load_type == AMDGPU_FW_LOAD_PSP) {
		int ucode, ucode_data;

		if (pipe == AMDGPU_MES_SCHED_PIPE) {
			ucode = AMDGPU_UCODE_ID_CP_MES;
			ucode_data = AMDGPU_UCODE_ID_CP_MES_DATA;
		} else {
			ucode = AMDGPU_UCODE_ID_CP_MES1;
			ucode_data = AMDGPU_UCODE_ID_CP_MES1_DATA;
		}

		info = &adev->firmware.ucode[ucode];
		info->ucode_id = ucode;
		info->fw = adev->mes.fw[pipe];
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(mes_hdr->mes_ucode_size_bytes),
			      PAGE_SIZE);

		info = &adev->firmware.ucode[ucode_data];
		info->ucode_id = ucode_data;
		info->fw = adev->mes.fw[pipe];
		adev->firmware.fw_size +=
			ALIGN(le32_to_cpu(mes_hdr->mes_ucode_data_size_bytes),
			      PAGE_SIZE);
	}

	return 0;
out:
	amdgpu_ucode_release(&adev->mes.fw[pipe]);
	return r;
}

bool amdgpu_mes_suspend_resume_all_supported(struct amdgpu_device *adev)
{
	uint32_t mes_rev = adev->mes.sched_version & AMDGPU_MES_VERSION_MASK;
	bool is_supported = false;

	if (amdgpu_ip_version(adev, GC_HWIP, 0) >= IP_VERSION(11, 0, 0) &&
	    amdgpu_ip_version(adev, GC_HWIP, 0) < IP_VERSION(12, 0, 0) &&
	    mes_rev >= 0x63)
		is_supported = true;

	return is_supported;
}

/* Fix me -- node_id is used to identify the correct MES instances in the future */
static int amdgpu_mes_set_enforce_isolation(struct amdgpu_device *adev,
					    uint32_t node_id, bool enable)
{
	struct mes_misc_op_input op_input = {0};
	int r;

	op_input.op = MES_MISC_OP_CHANGE_CONFIG;
	op_input.change_config.option.limit_single_process = enable ? 1 : 0;

	if (!adev->mes.funcs->misc_op) {
		dev_err(adev->dev, "mes change config is not supported!\n");
		r = -EINVAL;
		goto error;
	}

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		dev_err(adev->dev, "failed to change_config.\n");

error:
	return r;
}

int amdgpu_mes_update_enforce_isolation(struct amdgpu_device *adev)
{
	int i, r = 0;

	if (adev->enable_mes && adev->gfx.enable_cleaner_shader) {
		mutex_lock(&adev->enforce_isolation_mutex);
		for (i = 0; i < (adev->xcp_mgr ? adev->xcp_mgr->num_xcps : 1); i++) {
			if (adev->enforce_isolation[i])
				r |= amdgpu_mes_set_enforce_isolation(adev, i, true);
			else
				r |= amdgpu_mes_set_enforce_isolation(adev, i, false);
		}
		mutex_unlock(&adev->enforce_isolation_mutex);
	}
	return r;
}

#if defined(CONFIG_DEBUG_FS)

static int amdgpu_debugfs_mes_event_log_show(struct seq_file *m, void *unused)
{
	struct amdgpu_device *adev = m->private;
	uint32_t *mem = (uint32_t *)(adev->mes.event_log_cpu_addr);

	seq_hex_dump(m, "", DUMP_PREFIX_OFFSET, 32, 4,
		     mem, adev->mes.event_log_size, false);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(amdgpu_debugfs_mes_event_log);

#endif

void amdgpu_debugfs_mes_event_log_init(struct amdgpu_device *adev)
{

#if defined(CONFIG_DEBUG_FS)
	struct drm_minor *minor = adev_to_drm(adev)->primary;
	struct dentry *root = minor->debugfs_root;
	if (adev->enable_mes && amdgpu_mes_log_enable)
		debugfs_create_file("amdgpu_mes_event_log", 0444, root,
				    adev, &amdgpu_debugfs_mes_event_log_fops);

#endif
}
