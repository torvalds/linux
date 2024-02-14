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

int amdgpu_mes_alloc_process_doorbells(struct amdgpu_device *adev,
				      unsigned int *doorbell_index)
{
	int r = ida_simple_get(&adev->mes.doorbell_ida, 2,
			       adev->mes.max_doorbell_slices,
			       GFP_KERNEL);
	if (r > 0)
		*doorbell_index = r;

	return r;
}

void amdgpu_mes_free_process_doorbells(struct amdgpu_device *adev,
				      unsigned int doorbell_index)
{
	if (doorbell_index)
		ida_simple_remove(&adev->mes.doorbell_ida, doorbell_index);
}

unsigned int amdgpu_mes_get_doorbell_dw_offset_in_bar(
					struct amdgpu_device *adev,
					uint32_t doorbell_index,
					unsigned int doorbell_id)
{
	return ((doorbell_index *
		amdgpu_mes_doorbell_process_slice(adev)) / sizeof(u32) +
		doorbell_id * 2);
}

static int amdgpu_mes_queue_doorbell_get(struct amdgpu_device *adev,
					 struct amdgpu_mes_process *process,
					 int ip_type, uint64_t *doorbell_index)
{
	unsigned int offset, found;

	if (ip_type == AMDGPU_RING_TYPE_SDMA) {
		offset = adev->doorbell_index.sdma_engine[0];
		found = find_next_zero_bit(process->doorbell_bitmap,
					   AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS,
					   offset);
	} else {
		found = find_first_zero_bit(process->doorbell_bitmap,
					    AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS);
	}

	if (found >= AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS) {
		DRM_WARN("No doorbell available\n");
		return -ENOSPC;
	}

	set_bit(found, process->doorbell_bitmap);

	*doorbell_index = amdgpu_mes_get_doorbell_dw_offset_in_bar(adev,
				process->doorbell_index, found);

	return 0;
}

static void amdgpu_mes_queue_doorbell_free(struct amdgpu_device *adev,
					   struct amdgpu_mes_process *process,
					   uint32_t doorbell_index)
{
	unsigned int old, doorbell_id;

	doorbell_id = doorbell_index -
		(process->doorbell_index *
		 amdgpu_mes_doorbell_process_slice(adev)) / sizeof(u32);
	doorbell_id /= 2;

	old = test_and_clear_bit(doorbell_id, process->doorbell_bitmap);
	WARN_ON(!old);
}

static int amdgpu_mes_doorbell_init(struct amdgpu_device *adev)
{
	size_t doorbell_start_offset;
	size_t doorbell_aperture_size;
	size_t doorbell_process_limit;
	size_t aggregated_doorbell_start;
	int i;

	aggregated_doorbell_start = (adev->doorbell_index.max_assignment + 1) * sizeof(u32);
	aggregated_doorbell_start =
		roundup(aggregated_doorbell_start, PAGE_SIZE);

	doorbell_start_offset = aggregated_doorbell_start + PAGE_SIZE;
	doorbell_start_offset =
		roundup(doorbell_start_offset,
			amdgpu_mes_doorbell_process_slice(adev));

	doorbell_aperture_size = adev->doorbell.size;
	doorbell_aperture_size =
			rounddown(doorbell_aperture_size,
				  amdgpu_mes_doorbell_process_slice(adev));

	if (doorbell_aperture_size > doorbell_start_offset)
		doorbell_process_limit =
			(doorbell_aperture_size - doorbell_start_offset) /
			amdgpu_mes_doorbell_process_slice(adev);
	else
		return -ENOSPC;

	adev->mes.doorbell_id_offset = doorbell_start_offset / sizeof(u32);
	adev->mes.max_doorbell_slices = doorbell_process_limit;

	/* allocate Qword range for aggregated doorbell */
	for (i = 0; i < AMDGPU_MES_PRIORITY_NUM_LEVELS; i++)
		adev->mes.aggregated_doorbells[i] =
			aggregated_doorbell_start / sizeof(u32) + i * 2;

	DRM_INFO("max_doorbell_slices=%zu\n", doorbell_process_limit);
	return 0;
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
	spin_lock_init(&adev->mes.ring_lock);
	mutex_init(&adev->mes.mutex_hidden);

	adev->mes.total_max_queue = AMDGPU_FENCE_MES_QUEUE_ID_MASK;
	adev->mes.vmid_mask_mmhub = 0xffffff00;
	adev->mes.vmid_mask_gfxhub = 0xffffff00;

	for (i = 0; i < AMDGPU_MES_MAX_COMPUTE_PIPES; i++) {
		/* use only 1st MEC pipes */
		if (i >= 4)
			continue;
		adev->mes.compute_hqd_mask[i] = 0xc;
	}

	for (i = 0; i < AMDGPU_MES_MAX_GFX_PIPES; i++)
		adev->mes.gfx_hqd_mask[i] = i ? 0 : 0xfffffffe;

	for (i = 0; i < AMDGPU_MES_MAX_SDMA_PIPES; i++) {
		if (adev->ip_versions[SDMA0_HWIP][0] < IP_VERSION(6, 0, 0))
			adev->mes.sdma_hqd_mask[i] = i ? 0 : 0x3fc;
		/* zero sdma_hqd_mask for non-existent engine */
		else if (adev->sdma.num_instances == 1)
			adev->mes.sdma_hqd_mask[i] = i ? 0 : 0xfc;
		else
			adev->mes.sdma_hqd_mask[i] = 0xfc;
	}

	r = amdgpu_device_wb_get(adev, &adev->mes.sch_ctx_offs);
	if (r) {
		dev_err(adev->dev,
			"(%d) ring trail_fence_offs wb alloc failed\n", r);
		goto error_ids;
	}
	adev->mes.sch_ctx_gpu_addr =
		adev->wb.gpu_addr + (adev->mes.sch_ctx_offs * 4);
	adev->mes.sch_ctx_ptr =
		(uint64_t *)&adev->wb.wb[adev->mes.sch_ctx_offs];

	r = amdgpu_device_wb_get(adev, &adev->mes.query_status_fence_offs);
	if (r) {
		amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs);
		dev_err(adev->dev,
			"(%d) query_status_fence_offs wb alloc failed\n", r);
		goto error_ids;
	}
	adev->mes.query_status_fence_gpu_addr =
		adev->wb.gpu_addr + (adev->mes.query_status_fence_offs * 4);
	adev->mes.query_status_fence_ptr =
		(uint64_t *)&adev->wb.wb[adev->mes.query_status_fence_offs];

	r = amdgpu_device_wb_get(adev, &adev->mes.read_val_offs);
	if (r) {
		amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs);
		amdgpu_device_wb_free(adev, adev->mes.query_status_fence_offs);
		dev_err(adev->dev,
			"(%d) read_val_offs alloc failed\n", r);
		goto error_ids;
	}
	adev->mes.read_val_gpu_addr =
		adev->wb.gpu_addr + (adev->mes.read_val_offs * 4);
	adev->mes.read_val_ptr =
		(uint32_t *)&adev->wb.wb[adev->mes.read_val_offs];

	r = amdgpu_mes_doorbell_init(adev);
	if (r)
		goto error;

	return 0;

error:
	amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs);
	amdgpu_device_wb_free(adev, adev->mes.query_status_fence_offs);
	amdgpu_device_wb_free(adev, adev->mes.read_val_offs);
error_ids:
	idr_destroy(&adev->mes.pasid_idr);
	idr_destroy(&adev->mes.gang_id_idr);
	idr_destroy(&adev->mes.queue_id_idr);
	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex_hidden);
	return r;
}

void amdgpu_mes_fini(struct amdgpu_device *adev)
{
	amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs);
	amdgpu_device_wb_free(adev, adev->mes.query_status_fence_offs);
	amdgpu_device_wb_free(adev, adev->mes.read_val_offs);

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

	process->doorbell_bitmap =
		kzalloc(DIV_ROUND_UP(AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS,
				     BITS_PER_BYTE), GFP_KERNEL);
	if (!process->doorbell_bitmap) {
		DRM_ERROR("failed to allocate doorbell bitmap\n");
		kfree(process);
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

	/* allocate the starting doorbell index of the process */
	r = amdgpu_mes_alloc_process_doorbells(adev, &process->doorbell_index);
	if (r < 0) {
		DRM_ERROR("failed to allocate doorbell for process\n");
		goto clean_up_pasid;
	}

	DRM_DEBUG("process doorbell index = %d\n", process->doorbell_index);

	INIT_LIST_HEAD(&process->gang_list);
	process->vm = vm;
	process->pasid = pasid;
	process->process_quantum = adev->mes.default_process_quantum;
	process->pd_gpu_addr = amdgpu_bo_gpu_offset(vm->root.bo);

	amdgpu_mes_unlock(&adev->mes);
	return 0;

clean_up_pasid:
	idr_remove(&adev->mes.pasid_idr, pasid);
	amdgpu_mes_unlock(&adev->mes);
clean_up_ctx:
	amdgpu_bo_free_kernel(&process->proc_ctx_bo,
			      &process->proc_ctx_gpu_addr,
			      &process->proc_ctx_cpu_ptr);
clean_up_memory:
	kfree(process->doorbell_bitmap);
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

	amdgpu_mes_free_process_doorbells(adev, process->doorbell_index);
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
	kfree(process->doorbell_bitmap);
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
	struct idr *idp;
	struct amdgpu_mes_process *process;
	struct amdgpu_mes_gang *gang;
	struct mes_suspend_gang_input input;
	int r, pasid;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	idp = &adev->mes.pasid_idr;

	idr_for_each_entry(idp, process, pasid) {
		list_for_each_entry(gang, &process->gang_list, list) {
			r = adev->mes.funcs->suspend_gang(&adev->mes, &input);
			if (r)
				DRM_ERROR("failed to suspend pasid %d gangid %d",
					 pasid, gang->gang_id);
		}
	}

	amdgpu_mes_unlock(&adev->mes);
	return 0;
}

int amdgpu_mes_resume(struct amdgpu_device *adev)
{
	struct idr *idp;
	struct amdgpu_mes_process *process;
	struct amdgpu_mes_gang *gang;
	struct mes_resume_gang_input input;
	int r, pasid;

	/*
	 * Avoid taking any other locks under MES lock to avoid circular
	 * lock dependencies.
	 */
	amdgpu_mes_lock(&adev->mes);

	idp = &adev->mes.pasid_idr;

	idr_for_each_entry(idp, process, pasid) {
		list_for_each_entry(gang, &process->gang_list, list) {
			r = adev->mes.funcs->resume_gang(&adev->mes, &input);
			if (r)
				DRM_ERROR("failed to resume pasid %d gangid %d",
					 pasid, gang->gang_id);
		}
	}

	amdgpu_mes_unlock(&adev->mes);
	return 0;
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

	mqd_mgr->init_mqd(adev, q->mqd_cpu_ptr, &mqd_prop);

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
	r = amdgpu_mes_queue_doorbell_get(adev, gang->process,
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
	amdgpu_mes_queue_doorbell_free(adev, gang->process,
				       qprops->doorbell_off);
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
	amdgpu_mes_queue_doorbell_free(adev, gang->process,
				       queue->doorbell_off);
	amdgpu_mes_unlock(&adev->mes);

	amdgpu_mes_queue_free_mqd(queue);
	kfree(queue);
	return 0;
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

uint32_t amdgpu_mes_rreg(struct amdgpu_device *adev, uint32_t reg)
{
	struct mes_misc_op_input op_input;
	int r, val = 0;

	op_input.op = MES_MISC_OP_READ_REG;
	op_input.read_reg.reg_offset = reg;
	op_input.read_reg.buffer_addr = adev->mes.read_val_gpu_addr;

	if (!adev->mes.funcs->misc_op) {
		DRM_ERROR("mes rreg is not supported!\n");
		goto error;
	}

	r = adev->mes.funcs->misc_op(&adev->mes, &op_input);
	if (r)
		DRM_ERROR("failed to read reg (0x%x)\n", reg);
	else
		val = *(adev->mes.read_val_ptr);

error:
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
		break;
	case AMDGPU_RING_TYPE_COMPUTE:
		ring->funcs = adev->gfx.compute_ring[0].funcs;
		break;
	case AMDGPU_RING_TYPE_SDMA:
		ring->funcs = adev->sdma.instance[0].ring.funcs;
		break;
	default:
		BUG();
	}

	r = amdgpu_ring_init(adev, ring, 1024, NULL, 0,
			     AMDGPU_RING_PRIO_DEFAULT, NULL);
	if (r)
		goto clean_up_memory;

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
	amdgpu_mes_unlock(&adev->mes);
	return r;
}

void amdgpu_mes_remove_ring(struct amdgpu_device *adev,
			    struct amdgpu_ring *ring)
{
	if (!ring)
		return;

	amdgpu_mes_remove_hw_queue(adev, ring->hw_queue_id);
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
	struct ww_acquire_ctx ticket;
	struct list_head list;
	struct amdgpu_bo_list_entry pd;
	struct ttm_validate_buffer csa_tv;
	struct amdgpu_sync sync;
	int r;

	amdgpu_sync_create(&sync);
	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&csa_tv.head);

	csa_tv.bo = &ctx_data->meta_data_obj->tbo;
	csa_tv.num_shared = 1;

	list_add(&csa_tv.head, &list);
	amdgpu_vm_get_pd_bo(vm, &list, &pd);

	r = ttm_eu_reserve_buffers(&ticket, &list, true, NULL);
	if (r) {
		DRM_ERROR("failed to reserve meta data BO: err=%d\n", r);
		return r;
	}

	bo_va = amdgpu_vm_bo_add(adev, vm, ctx_data->meta_data_obj);
	if (!bo_va) {
		ttm_eu_backoff_reservation(&ticket, &list);
		DRM_ERROR("failed to create bo_va for meta data BO\n");
		return -ENOMEM;
	}

	r = amdgpu_vm_bo_map(adev, bo_va, ctx_data->meta_data_gpu_addr, 0,
			     sizeof(struct amdgpu_mes_ctx_meta_data),
			     AMDGPU_PTE_READABLE | AMDGPU_PTE_WRITEABLE |
			     AMDGPU_PTE_EXECUTABLE);

	if (r) {
		DRM_ERROR("failed to do bo_map on meta data, err=%d\n", r);
		goto error;
	}

	r = amdgpu_vm_bo_update(adev, bo_va, false);
	if (r) {
		DRM_ERROR("failed to do vm_bo_update on meta data\n");
		goto error;
	}
	amdgpu_sync_fence(&sync, bo_va->last_pt_update);

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r) {
		DRM_ERROR("failed to update pdes on meta data\n");
		goto error;
	}
	amdgpu_sync_fence(&sync, vm->last_update);

	amdgpu_sync_wait(&sync, false);
	ttm_eu_backoff_reservation(&ticket, &list);

	amdgpu_sync_free(&sync);
	ctx_data->meta_data_va = bo_va;
	return 0;

error:
	amdgpu_vm_bo_del(adev, bo_va);
	ttm_eu_backoff_reservation(&ticket, &list);
	amdgpu_sync_free(&sync);
	return r;
}

int amdgpu_mes_ctx_unmap_meta_data(struct amdgpu_device *adev,
				   struct amdgpu_mes_ctx_data *ctx_data)
{
	struct amdgpu_bo_va *bo_va = ctx_data->meta_data_va;
	struct amdgpu_bo *bo = ctx_data->meta_data_obj;
	struct amdgpu_vm *vm = bo_va->base.vm;
	struct amdgpu_bo_list_entry vm_pd;
	struct list_head list, duplicates;
	struct dma_fence *fence = NULL;
	struct ttm_validate_buffer tv;
	struct ww_acquire_ctx ticket;
	long r = 0;

	INIT_LIST_HEAD(&list);
	INIT_LIST_HEAD(&duplicates);

	tv.bo = &bo->tbo;
	tv.num_shared = 2;
	list_add(&tv.head, &list);

	amdgpu_vm_get_pd_bo(vm, &list, &vm_pd);

	r = ttm_eu_reserve_buffers(&ticket, &list, false, &duplicates);
	if (r) {
		dev_err(adev->dev, "leaking bo va because "
			"we fail to reserve bo (%ld)\n", r);
		return r;
	}

	amdgpu_vm_bo_del(adev, bo_va);
	if (!amdgpu_vm_ready(vm))
		goto out_unlock;

	r = dma_resv_get_singleton(bo->tbo.base.resv, DMA_RESV_USAGE_BOOKKEEP, &fence);
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
	ttm_eu_backoff_reservation(&ticket, &list);

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

		r = amdgpu_ring_test_ring(ring);
		if (r) {
			DRM_DEV_ERROR(ring->adev->dev,
				      "ring %s test failed (%d)\n",
				      ring->name, r);
			return r;
		} else
			DRM_INFO("ring %s test pass\n", ring->name);

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

	r = amdgpu_vm_init(adev, vm);
	if (r) {
		DRM_ERROR("failed to initialize vm\n");
		goto error_pasid;
	}

	r = amdgpu_mes_ctx_alloc_meta_data(adev, &ctx_data);
	if (r) {
		DRM_ERROR("failed to alloc ctx meta data\n");
		goto error_fini;
	}

	ctx_data.meta_data_gpu_addr = AMDGPU_VA_RESERVED_SIZE;
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
		if (adev->ip_versions[GC_HWIP][0] >= IP_VERSION(10, 3, 0) &&
		    adev->ip_versions[GC_HWIP][0] < IP_VERSION(11, 0, 0) &&
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
	char fw_name[40];
	bool need_retry = false;
	int r;

	amdgpu_ucode_ip_version_decode(adev, GC_HWIP, ucode_prefix,
				       sizeof(ucode_prefix));
	if (adev->ip_versions[GC_HWIP][0] >= IP_VERSION(11, 0, 0)) {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes%s.bin",
			 ucode_prefix,
			 pipe == AMDGPU_MES_SCHED_PIPE ? "_2" : "1");
		need_retry = true;
	} else {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes%s.bin",
			 ucode_prefix,
			 pipe == AMDGPU_MES_SCHED_PIPE ? "" : "1");
	}

	r = amdgpu_ucode_request(adev, &adev->mes.fw[pipe], fw_name);
	if (r && need_retry && pipe == AMDGPU_MES_SCHED_PIPE) {
		snprintf(fw_name, sizeof(fw_name), "amdgpu/%s_mes.bin",
			 ucode_prefix);
		DRM_INFO("try to fall back to %s\n", fw_name);
		r = amdgpu_ucode_request(adev, &adev->mes.fw[pipe],
					 fw_name);
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
