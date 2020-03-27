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

#include "amdgpu_mes.h"
#include "amdgpu.h"
#include "soc15_common.h"
#include "amdgpu_mes_ctx.h"

#define AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS 1024
#define AMDGPU_ONE_DOORBELL_SIZE 8

static int amdgpu_mes_doorbell_process_slice(struct amdgpu_device *adev)
{
	return roundup(AMDGPU_ONE_DOORBELL_SIZE *
		       AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS,
		       PAGE_SIZE);
}

static int amdgpu_mes_alloc_process_doorbells(struct amdgpu_device *adev,
				      struct amdgpu_mes_process *process)
{
	int r = ida_simple_get(&adev->mes.doorbell_ida, 2,
			       adev->mes.max_doorbell_slices,
			       GFP_KERNEL);
	if (r > 0)
		process->doorbell_index = r;

	return r;
}

static void amdgpu_mes_free_process_doorbells(struct amdgpu_device *adev,
				      struct amdgpu_mes_process *process)
{
	if (process->doorbell_index)
		ida_simple_remove(&adev->mes.doorbell_ida,
				  process->doorbell_index);
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

	*doorbell_index =
		(process->doorbell_index *
		 amdgpu_mes_doorbell_process_slice(adev)) / sizeof(u32) +
		found * 2;

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

	doorbell_start_offset = (adev->doorbell_index.max_assignment+1) * sizeof(u32);
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

	DRM_INFO("max_doorbell_slices=%ld\n", doorbell_process_limit);
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
	mutex_init(&adev->mes.mutex);

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

	for (i = 0; i < AMDGPU_MES_MAX_SDMA_PIPES; i++)
		adev->mes.sdma_hqd_mask[i] = i ? 0 : 0x3fc;

	for (i = 0; i < AMDGPU_MES_PRIORITY_NUM_LEVELS; i++)
		adev->mes.agreegated_doorbells[i] = 0xffffffff;

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
		dev_err(adev->dev,
			"(%d) query_status_fence_offs wb alloc failed\n", r);
		return r;
	}
	adev->mes.query_status_fence_gpu_addr =
		adev->wb.gpu_addr + (adev->mes.query_status_fence_offs * 4);
	adev->mes.query_status_fence_ptr =
		(uint64_t *)&adev->wb.wb[adev->mes.query_status_fence_offs];

	r = amdgpu_mes_doorbell_init(adev);
	if (r)
		goto error;

	return 0;

error:
	amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs);
error_ids:
	idr_destroy(&adev->mes.pasid_idr);
	idr_destroy(&adev->mes.gang_id_idr);
	idr_destroy(&adev->mes.queue_id_idr);
	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex);
	return r;
}

void amdgpu_mes_fini(struct amdgpu_device *adev)
{
	amdgpu_device_wb_free(adev, adev->mes.sch_ctx_offs);

	idr_destroy(&adev->mes.pasid_idr);
	idr_destroy(&adev->mes.gang_id_idr);
	idr_destroy(&adev->mes.queue_id_idr);
	ida_destroy(&adev->mes.doorbell_ida);
	mutex_destroy(&adev->mes.mutex);
}

int amdgpu_mes_create_process(struct amdgpu_device *adev, int pasid,
			      struct amdgpu_vm *vm)
{
	struct amdgpu_mes_process *process;
	int r;

	mutex_lock(&adev->mes.mutex);

	/* allocate the mes process buffer */
	process = kzalloc(sizeof(struct amdgpu_mes_process), GFP_KERNEL);
	if (!process) {
		DRM_ERROR("no more memory to create mes process\n");
		mutex_unlock(&adev->mes.mutex);
		return -ENOMEM;
	}

	process->doorbell_bitmap =
		kzalloc(DIV_ROUND_UP(AMDGPU_MES_MAX_NUM_OF_QUEUES_PER_PROCESS,
				     BITS_PER_BYTE), GFP_KERNEL);
	if (!process->doorbell_bitmap) {
		DRM_ERROR("failed to allocate doorbell bitmap\n");
		kfree(process);
		mutex_unlock(&adev->mes.mutex);
		return -ENOMEM;
	}

	/* add the mes process to idr list */
	r = idr_alloc(&adev->mes.pasid_idr, process, pasid, pasid + 1,
		      GFP_KERNEL);
	if (r < 0) {
		DRM_ERROR("failed to lock pasid=%d\n", pasid);
		goto clean_up_memory;
	}

	/* allocate the process context bo and map it */
	r = amdgpu_bo_create_kernel(adev, AMDGPU_MES_PROC_CTX_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &process->proc_ctx_bo,
				    &process->proc_ctx_gpu_addr,
				    &process->proc_ctx_cpu_ptr);
	if (r) {
		DRM_ERROR("failed to allocate process context bo\n");
		goto clean_up_pasid;
	}
	memset(process->proc_ctx_cpu_ptr, 0, AMDGPU_MES_PROC_CTX_SIZE);

	/* allocate the starting doorbell index of the process */
	r = amdgpu_mes_alloc_process_doorbells(adev, process);
	if (r < 0) {
		DRM_ERROR("failed to allocate doorbell for process\n");
		goto clean_up_ctx;
	}

	DRM_DEBUG("process doorbell index = %d\n", process->doorbell_index);

	INIT_LIST_HEAD(&process->gang_list);
	process->vm = vm;
	process->pasid = pasid;
	process->process_quantum = adev->mes.default_process_quantum;
	process->pd_gpu_addr = amdgpu_bo_gpu_offset(vm->root.bo);

	mutex_unlock(&adev->mes.mutex);
	return 0;

clean_up_ctx:
	amdgpu_bo_free_kernel(&process->proc_ctx_bo,
			      &process->proc_ctx_gpu_addr,
			      &process->proc_ctx_cpu_ptr);
clean_up_pasid:
	idr_remove(&adev->mes.pasid_idr, pasid);
clean_up_memory:
	kfree(process->doorbell_bitmap);
	kfree(process);
	mutex_unlock(&adev->mes.mutex);
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

	mutex_lock(&adev->mes.mutex);

	process = idr_find(&adev->mes.pasid_idr, pasid);
	if (!process) {
		DRM_WARN("pasid %d doesn't exist\n", pasid);
		mutex_unlock(&adev->mes.mutex);
		return;
	}

	/* free all gangs in the process */
	list_for_each_entry_safe(gang, tmp1, &process->gang_list, list) {
		/* free all queues in the gang */
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

			list_del(&queue->list);
			kfree(queue);
		}

		idr_remove(&adev->mes.gang_id_idr, gang->gang_id);
		amdgpu_bo_free_kernel(&gang->gang_ctx_bo,
				      &gang->gang_ctx_gpu_addr,
				      &gang->gang_ctx_cpu_ptr);
		list_del(&gang->list);
		kfree(gang);
	}

	amdgpu_mes_free_process_doorbells(adev, process);

	idr_remove(&adev->mes.pasid_idr, pasid);
	amdgpu_bo_free_kernel(&process->proc_ctx_bo,
			      &process->proc_ctx_gpu_addr,
			      &process->proc_ctx_cpu_ptr);
	kfree(process->doorbell_bitmap);
	kfree(process);

	mutex_unlock(&adev->mes.mutex);
}

int amdgpu_mes_add_gang(struct amdgpu_device *adev, int pasid,
			struct amdgpu_mes_gang_properties *gprops,
			int *gang_id)
{
	struct amdgpu_mes_process *process;
	struct amdgpu_mes_gang *gang;
	int r;

	mutex_lock(&adev->mes.mutex);

	process = idr_find(&adev->mes.pasid_idr, pasid);
	if (!process) {
		DRM_ERROR("pasid %d doesn't exist\n", pasid);
		mutex_unlock(&adev->mes.mutex);
		return -EINVAL;
	}

	/* allocate the mes gang buffer */
	gang = kzalloc(sizeof(struct amdgpu_mes_gang), GFP_KERNEL);
	if (!gang) {
		mutex_unlock(&adev->mes.mutex);
		return -ENOMEM;
	}

	/* add the mes gang to idr list */
	r = idr_alloc(&adev->mes.gang_id_idr, gang, 1, 0,
		      GFP_KERNEL);
	if (r < 0) {
		kfree(gang);
		mutex_unlock(&adev->mes.mutex);
		return r;
	}

	gang->gang_id = r;
	*gang_id = r;

	/* allocate the gang context bo and map it to cpu space */
	r = amdgpu_bo_create_kernel(adev, AMDGPU_MES_GANG_CTX_SIZE, PAGE_SIZE,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &gang->gang_ctx_bo,
				    &gang->gang_ctx_gpu_addr,
				    &gang->gang_ctx_cpu_ptr);
	if (r) {
		DRM_ERROR("failed to allocate process context bo\n");
		goto clean_up;
	}
	memset(gang->gang_ctx_cpu_ptr, 0, AMDGPU_MES_GANG_CTX_SIZE);

	INIT_LIST_HEAD(&gang->queue_list);
	gang->process = process;
	gang->priority = gprops->priority;
	gang->gang_quantum = gprops->gang_quantum ?
		gprops->gang_quantum : adev->mes.default_gang_quantum;
	gang->global_priority_level = gprops->global_priority_level;
	gang->inprocess_gang_priority = gprops->inprocess_gang_priority;
	list_add_tail(&gang->list, &process->gang_list);

	mutex_unlock(&adev->mes.mutex);
	return 0;

clean_up:
	idr_remove(&adev->mes.gang_id_idr, gang->gang_id);
	kfree(gang);
	mutex_unlock(&adev->mes.mutex);
	return r;
}

int amdgpu_mes_remove_gang(struct amdgpu_device *adev, int gang_id)
{
	struct amdgpu_mes_gang *gang;

	mutex_lock(&adev->mes.mutex);

	gang = idr_find(&adev->mes.gang_id_idr, gang_id);
	if (!gang) {
		DRM_ERROR("gang id %d doesn't exist\n", gang_id);
		mutex_unlock(&adev->mes.mutex);
		return -EINVAL;
	}

	if (!list_empty(&gang->queue_list)) {
		DRM_ERROR("queue list is not empty\n");
		mutex_unlock(&adev->mes.mutex);
		return -EBUSY;
	}

	idr_remove(&adev->mes.gang_id_idr, gang->gang_id);
	amdgpu_bo_free_kernel(&gang->gang_ctx_bo,
			      &gang->gang_ctx_gpu_addr,
			      &gang->gang_ctx_cpu_ptr);
	list_del(&gang->list);
	kfree(gang);

	mutex_unlock(&adev->mes.mutex);
	return 0;
}

int amdgpu_mes_suspend(struct amdgpu_device *adev)
{
	struct idr *idp;
	struct amdgpu_mes_process *process;
	struct amdgpu_mes_gang *gang;
	struct mes_suspend_gang_input input;
	int r, pasid;

	mutex_lock(&adev->mes.mutex);

	idp = &adev->mes.pasid_idr;

	idr_for_each_entry(idp, process, pasid) {
		list_for_each_entry(gang, &process->gang_list, list) {
			r = adev->mes.funcs->suspend_gang(&adev->mes, &input);
			if (r)
				DRM_ERROR("failed to suspend pasid %d gangid %d",
					 pasid, gang->gang_id);
		}
	}

	mutex_unlock(&adev->mes.mutex);
	return 0;
}

int amdgpu_mes_resume(struct amdgpu_device *adev)
{
	struct idr *idp;
	struct amdgpu_mes_process *process;
	struct amdgpu_mes_gang *gang;
	struct mes_resume_gang_input input;
	int r, pasid;

	mutex_lock(&adev->mes.mutex);

	idp = &adev->mes.pasid_idr;

	idr_for_each_entry(idp, process, pasid) {
		list_for_each_entry(gang, &process->gang_list, list) {
			r = adev->mes.funcs->resume_gang(&adev->mes, &input);
			if (r)
				DRM_ERROR("failed to resume pasid %d gangid %d",
					 pasid, gang->gang_id);
		}
	}

	mutex_unlock(&adev->mes.mutex);
	return 0;
}

static int amdgpu_mes_queue_init_mqd(struct amdgpu_device *adev,
				     struct amdgpu_mes_queue *q,
				     struct amdgpu_mes_queue_properties *p)
{
	struct amdgpu_mqd *mqd_mgr = &adev->mqds[p->queue_type];
	u32 mqd_size = mqd_mgr->mqd_size;
	struct amdgpu_mqd_prop mqd_prop = {0};
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

	r = amdgpu_bo_reserve(q->mqd_obj, false);
	if (unlikely(r != 0))
		goto clean_up;

	mqd_mgr->init_mqd(adev, q->mqd_cpu_ptr, &mqd_prop);

	amdgpu_bo_unreserve(q->mqd_obj);
	return 0;

clean_up:
	amdgpu_bo_free_kernel(&q->mqd_obj,
			      &q->mqd_gpu_addr,
			      &q->mqd_cpu_ptr);
	return r;
}

static void amdgpu_mes_queue_free_mqd(struct amdgpu_mes_queue *q)
{
	amdgpu_bo_free_kernel(&q->mqd_obj,
			      &q->mqd_gpu_addr,
			      &q->mqd_cpu_ptr);
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

	mutex_lock(&adev->mes.mutex);

	gang = idr_find(&adev->mes.gang_id_idr, gang_id);
	if (!gang) {
		DRM_ERROR("gang id %d doesn't exist\n", gang_id);
		mutex_unlock(&adev->mes.mutex);
		return -EINVAL;
	}

	/* allocate the mes queue buffer */
	queue = kzalloc(sizeof(struct amdgpu_mes_queue), GFP_KERNEL);
	if (!queue) {
		mutex_unlock(&adev->mes.mutex);
		return -ENOMEM;
	}

	/* add the mes gang to idr list */
	spin_lock_irqsave(&adev->mes.queue_id_lock, flags);
	r = idr_alloc(&adev->mes.queue_id_idr, queue, 1, 0,
		      GFP_ATOMIC);
	if (r < 0) {
		spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
		goto clean_up_memory;
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
	r = amdgpu_mes_queue_init_mqd(adev, queue, qprops);
	if (r)
		goto clean_up_doorbell;

	/* add hw queue to mes */
	queue_input.process_id = gang->process->pasid;
	queue_input.page_table_base_addr = gang->process->pd_gpu_addr;
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
	queue_input.queue_type = qprops->queue_type;
	queue_input.paging = qprops->paging;

	r = adev->mes.funcs->add_hw_queue(&adev->mes, &queue_input);
	if (r) {
		DRM_ERROR("failed to add hardware queue to MES, doorbell=0x%llx\n",
			  qprops->doorbell_off);
		goto clean_up_mqd;
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
	list_add_tail(&queue->list, &gang->queue_list);

	mutex_unlock(&adev->mes.mutex);
	return 0;

clean_up_mqd:
	amdgpu_mes_queue_free_mqd(queue);
clean_up_doorbell:
	amdgpu_mes_queue_doorbell_free(adev, gang->process,
				       qprops->doorbell_off);
clean_up_queue_id:
	spin_lock_irqsave(&adev->mes.queue_id_lock, flags);
	idr_remove(&adev->mes.queue_id_idr, queue->queue_id);
	spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
clean_up_memory:
	kfree(queue);
	mutex_unlock(&adev->mes.mutex);
	return r;
}

int amdgpu_mes_remove_hw_queue(struct amdgpu_device *adev, int queue_id)
{
	unsigned long flags;
	struct amdgpu_mes_queue *queue;
	struct amdgpu_mes_gang *gang;
	struct mes_remove_queue_input queue_input;
	int r;

	mutex_lock(&adev->mes.mutex);

	/* remove the mes gang from idr list */
	spin_lock_irqsave(&adev->mes.queue_id_lock, flags);

	queue = idr_find(&adev->mes.queue_id_idr, queue_id);
	if (!queue) {
		spin_unlock_irqrestore(&adev->mes.queue_id_lock, flags);
		mutex_unlock(&adev->mes.mutex);
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

	amdgpu_mes_queue_free_mqd(queue);
	list_del(&queue->list);
	amdgpu_mes_queue_doorbell_free(adev, gang->process,
				       queue->doorbell_off);
	kfree(queue);
	mutex_unlock(&adev->mes.mutex);
	return 0;
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

	mutex_lock(&adev->mes.mutex);
	gang = idr_find(&adev->mes.gang_id_idr, gang_id);
	if (!gang) {
		DRM_ERROR("gang id %d doesn't exist\n", gang_id);
		mutex_unlock(&adev->mes.mutex);
		return -EINVAL;
	}
	pasid = gang->process->pasid;

	ring = kzalloc(sizeof(struct amdgpu_ring), GFP_KERNEL);
	if (!ring) {
		mutex_unlock(&adev->mes.mutex);
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
	mutex_unlock(&adev->mes.mutex);

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
	mutex_unlock(&adev->mes.mutex);
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

int amdgpu_mes_ctx_alloc_meta_data(struct amdgpu_device *adev,
				   struct amdgpu_mes_ctx_data *ctx_data)
{
	int r;

	r = amdgpu_bo_create_kernel(adev,
			    sizeof(struct amdgpu_mes_ctx_meta_data),
			    PAGE_SIZE, AMDGPU_GEM_DOMAIN_GTT,
			    &ctx_data->meta_data_obj, NULL,
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
		amdgpu_bo_free_kernel(&ctx_data->meta_data_obj, NULL, NULL);
}

static int amdgpu_mes_test_map_ctx_meta_data(struct amdgpu_device *adev,
				     struct amdgpu_vm *vm,
				     struct amdgpu_mes_ctx_data *ctx_data)
{
	struct amdgpu_bo_va *meta_data_va = NULL;
	uint64_t meta_data_addr = AMDGPU_VA_RESERVED_SIZE;
	int r;

	r = amdgpu_map_static_csa(adev, vm, ctx_data->meta_data_obj,
				  &meta_data_va, meta_data_addr,
				  sizeof(struct amdgpu_mes_ctx_meta_data));
	if (r)
		return r;

	r = amdgpu_vm_bo_update(adev, meta_data_va, false);
	if (r)
		goto error;

	r = amdgpu_vm_update_pdes(adev, vm, false);
	if (r)
		goto error;

	dma_fence_wait(vm->last_update, false);
	dma_fence_wait(meta_data_va->last_pt_update, false);

	ctx_data->meta_data_gpu_addr = meta_data_addr;
	ctx_data->meta_data_va = meta_data_va;

	return 0;

error:
	BUG_ON(amdgpu_bo_reserve(ctx_data->meta_data_obj, true));
	amdgpu_vm_bo_rmv(adev, meta_data_va);
	amdgpu_bo_unreserve(ctx_data->meta_data_obj);
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
