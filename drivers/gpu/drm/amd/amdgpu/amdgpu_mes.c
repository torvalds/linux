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
