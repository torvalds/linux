// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
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
#include <drm/drm_drv.h>
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "mes_userqueue.h"
#include "amdgpu_userq_fence.h"

#define AMDGPU_USERQ_PROC_CTX_SZ PAGE_SIZE
#define AMDGPU_USERQ_GANG_CTX_SZ PAGE_SIZE

static int
mes_userq_create_wptr_mapping(struct amdgpu_device *adev,
			      struct amdgpu_userq_mgr *uq_mgr,
			      struct amdgpu_usermode_queue *queue,
			      uint64_t wptr)
{
	struct amdgpu_bo_va_mapping *wptr_mapping;
	struct amdgpu_userq_obj *wptr_obj = &queue->wptr_obj;
	struct amdgpu_bo *obj;
	struct amdgpu_vm *vm = queue->vm;
	struct drm_exec exec;
	int ret;

	wptr &= AMDGPU_GMC_HOLE_MASK;

	drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES, 2);
	drm_exec_until_all_locked(&exec) {
		ret = amdgpu_vm_lock_pd(vm, &exec, 1);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret))
			goto fail_lock;

		wptr_mapping = amdgpu_vm_bo_lookup_mapping(vm, wptr >> PAGE_SHIFT);
		if (!wptr_mapping) {
			ret = -EINVAL;
			goto fail_lock;
		}

		obj = wptr_mapping->bo_va->base.bo;
		ret = drm_exec_lock_obj(&exec, &obj->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret))
			goto fail_lock;
	}

	wptr_obj->obj = amdgpu_bo_ref(wptr_mapping->bo_va->base.bo);
	if (wptr_obj->obj->tbo.base.size > PAGE_SIZE) {
		ret = -EINVAL;
		goto fail_map;
	}

	/* TODO use eviction fence instead of pinning. */
	ret = amdgpu_bo_pin(wptr_obj->obj, AMDGPU_GEM_DOMAIN_GTT);
	if (ret) {
		DRM_ERROR("Failed to pin wptr bo. ret %d\n", ret);
		goto fail_map;
	}

	ret = amdgpu_ttm_alloc_gart(&wptr_obj->obj->tbo);
	if (ret) {
		DRM_ERROR("Failed to bind bo to GART. ret %d\n", ret);
		goto fail_alloc_gart;
	}

	queue->wptr_obj.gpu_addr = amdgpu_bo_gpu_offset(wptr_obj->obj);

	drm_exec_fini(&exec);
	return 0;

fail_alloc_gart:
	amdgpu_bo_unpin(wptr_obj->obj);
fail_map:
	amdgpu_bo_unref(&wptr_obj->obj);
fail_lock:
	drm_exec_fini(&exec);
	return ret;

}

static int convert_to_mes_priority(int priority)
{
	switch (priority) {
	case AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_NORMAL_LOW:
	default:
		return AMDGPU_MES_PRIORITY_LEVEL_NORMAL;
	case AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_LOW:
		return AMDGPU_MES_PRIORITY_LEVEL_LOW;
	case AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_NORMAL_HIGH:
		return AMDGPU_MES_PRIORITY_LEVEL_MEDIUM;
	case AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_HIGH:
		return AMDGPU_MES_PRIORITY_LEVEL_HIGH;
	}
}

static int mes_userq_map(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	struct amdgpu_mqd_prop *userq_props = queue->userq_prop;
	struct mes_add_queue_input queue_input;
	int r;

	memset(&queue_input, 0x0, sizeof(struct mes_add_queue_input));

	queue_input.process_va_start = 0;
	queue_input.process_va_end = adev->vm_manager.max_pfn - 1;

	/* set process quantum to 10 ms and gang quantum to 1 ms as default */
	queue_input.process_quantum = 100000;
	queue_input.gang_quantum = 10000;
	queue_input.paging = false;

	queue_input.process_context_addr = ctx->gpu_addr;
	queue_input.gang_context_addr = ctx->gpu_addr + AMDGPU_USERQ_PROC_CTX_SZ;
	queue_input.inprocess_gang_priority = AMDGPU_MES_PRIORITY_LEVEL_NORMAL;
	queue_input.gang_global_priority_level = convert_to_mes_priority(queue->priority);

	queue_input.process_id = queue->vm->pasid;
	queue_input.queue_type = queue->queue_type;
	queue_input.mqd_addr = queue->mqd.gpu_addr;
	queue_input.wptr_addr = userq_props->wptr_gpu_addr;
	queue_input.queue_size = userq_props->queue_size >> 2;
	queue_input.doorbell_offset = userq_props->doorbell_index;
	queue_input.page_table_base_addr = amdgpu_gmc_pd_addr(queue->vm->root.bo);
	queue_input.wptr_mc_addr = queue->wptr_obj.gpu_addr;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->add_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r) {
		DRM_ERROR("Failed to map queue in HW, err (%d)\n", r);
		return r;
	}

	DRM_DEBUG_DRIVER("Queue (doorbell:%d) mapped successfully\n", userq_props->doorbell_index);
	return 0;
}

static int mes_userq_unmap(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	struct mes_remove_queue_input queue_input;
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	int r;

	memset(&queue_input, 0x0, sizeof(struct mes_remove_queue_input));
	queue_input.doorbell_offset = queue->doorbell_index;
	queue_input.gang_context_addr = ctx->gpu_addr + AMDGPU_USERQ_PROC_CTX_SZ;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->remove_hw_queue(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		DRM_ERROR("Failed to unmap queue in HW, err (%d)\n", r);
	return r;
}

static int mes_userq_create_ctx_space(struct amdgpu_userq_mgr *uq_mgr,
				      struct amdgpu_usermode_queue *queue,
				      struct drm_amdgpu_userq_in *mqd_user)
{
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	int r, size;

	/*
	 * The FW expects at least one page space allocated for
	 * process ctx and gang ctx each. Create an object
	 * for the same.
	 */
	size = AMDGPU_USERQ_PROC_CTX_SZ + AMDGPU_USERQ_GANG_CTX_SZ;
	r = amdgpu_bo_create_kernel(uq_mgr->adev, size, 0,
				    AMDGPU_GEM_DOMAIN_GTT,
				    &ctx->obj, &ctx->gpu_addr,
				    &ctx->cpu_ptr);
	if (r) {
		DRM_ERROR("Failed to allocate ctx space bo for userqueue, err:%d\n", r);
		return r;
	}

	memset(ctx->cpu_ptr, 0, size);
	return 0;
}

static int mes_userq_detect_and_reset(struct amdgpu_device *adev,
				      int queue_type)
{
	int db_array_size = amdgpu_mes_get_hung_queue_db_array_size(adev);
	struct mes_detect_and_reset_queue_input input;
	struct amdgpu_usermode_queue *queue;
	unsigned int hung_db_num = 0;
	unsigned long queue_id;
	u32 db_array[8];
	bool found_hung_queue = false;
	int r, i;

	if (db_array_size > 8) {
		dev_err(adev->dev, "DB array size (%d vs 8) too small\n",
			db_array_size);
		return -EINVAL;
	}

	memset(&input, 0x0, sizeof(struct mes_detect_and_reset_queue_input));

	input.queue_type = queue_type;

	amdgpu_mes_lock(&adev->mes);
	r = amdgpu_mes_detect_and_reset_hung_queues(adev, queue_type, false,
						    &hung_db_num, db_array, 0);
	amdgpu_mes_unlock(&adev->mes);
	if (r) {
		dev_err(adev->dev, "Failed to detect and reset queues, err (%d)\n", r);
	} else if (hung_db_num) {
		xa_for_each(&adev->userq_doorbell_xa, queue_id, queue) {
			if (queue->queue_type == queue_type) {
				for (i = 0; i < hung_db_num; i++) {
					if (queue->doorbell_index == db_array[i]) {
						queue->state = AMDGPU_USERQ_STATE_HUNG;
						found_hung_queue = true;
						atomic_inc(&adev->gpu_reset_counter);
						amdgpu_userq_fence_driver_force_completion(queue);
						drm_dev_wedged_event(adev_to_drm(adev), DRM_WEDGE_RECOVERY_NONE, NULL);
					}
				}
			}
		}
	}

	if (found_hung_queue) {
		/* Resume scheduling after hang recovery */
		r = amdgpu_mes_resume(adev, input.xcc_id);
	}

	return r;
}

static int mes_userq_mqd_create(struct amdgpu_usermode_queue *queue,
				struct drm_amdgpu_userq_in *args_in)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_mqd *mqd_hw_default = &adev->mqds[queue->queue_type];
	struct drm_amdgpu_userq_in *mqd_user = args_in;
	struct amdgpu_mqd_prop *userq_props;
	int r;

	/* Structure to initialize MQD for userqueue using generic MQD init function */
	userq_props = kzalloc_obj(struct amdgpu_mqd_prop);
	if (!userq_props) {
		DRM_ERROR("Failed to allocate memory for userq_props\n");
		return -ENOMEM;
	}

	r = amdgpu_bo_create_kernel(adev,
				    AMDGPU_MQD_SIZE_ALIGN(mqd_hw_default->mqd_size),
				    0, AMDGPU_GEM_DOMAIN_GTT,
				    &queue->mqd.obj, &queue->mqd.gpu_addr,
				    &queue->mqd.cpu_ptr);
	if (r) {
		DRM_ERROR("Failed to create MQD object for userqueue\n");
		goto free_props;
	}

	memset(queue->mqd.cpu_ptr, 0,
	       AMDGPU_MQD_SIZE_ALIGN(mqd_hw_default->mqd_size));

	/* Initialize the MQD BO with user given values */
	userq_props->wptr_gpu_addr = mqd_user->wptr_va;
	userq_props->rptr_gpu_addr = mqd_user->rptr_va;
	userq_props->queue_size = mqd_user->queue_size;
	userq_props->hqd_base_gpu_addr = mqd_user->queue_va;
	userq_props->mqd_gpu_addr = queue->mqd.gpu_addr;
	userq_props->use_doorbell = true;
	userq_props->doorbell_index = queue->doorbell_index;
	userq_props->fence_address = queue->fence_drv->gpu_addr;

	if (queue->queue_type == AMDGPU_HW_IP_COMPUTE) {
		struct drm_amdgpu_userq_mqd_compute_gfx11 *compute_mqd;

		if (mqd_user->mqd_size != sizeof(*compute_mqd)) {
			DRM_ERROR("Invalid compute IP MQD size\n");
			r = -EINVAL;
			goto free_mqd;
		}

		compute_mqd = memdup_user(u64_to_user_ptr(mqd_user->mqd), mqd_user->mqd_size);
		if (IS_ERR(compute_mqd)) {
			DRM_ERROR("Failed to read user MQD\n");
			r = -ENOMEM;
			goto free_mqd;
		}

		r = amdgpu_bo_reserve(queue->vm->root.bo, false);
		if (r) {
			kfree(compute_mqd);
			goto free_mqd;
		}
		r = amdgpu_userq_input_va_validate(adev, queue,
						   compute_mqd->eop_va, 2048,
						   &queue->userq_vas.va.eop);
		amdgpu_bo_unreserve(queue->vm->root.bo);
		if (r) {
			kfree(compute_mqd);
			goto free_mqd;
		}

		userq_props->eop_gpu_addr = compute_mqd->eop_va;
		userq_props->hqd_pipe_priority = AMDGPU_GFX_PIPE_PRIO_NORMAL;
		userq_props->hqd_queue_priority = AMDGPU_GFX_QUEUE_PRIORITY_MINIMUM;
		userq_props->hqd_active = false;
		userq_props->tmz_queue =
			mqd_user->flags & AMDGPU_USERQ_CREATE_FLAGS_QUEUE_SECURE;
		kfree(compute_mqd);
	} else if (queue->queue_type == AMDGPU_HW_IP_GFX) {
		struct drm_amdgpu_userq_mqd_gfx11 *mqd_gfx_v11;
		struct amdgpu_gfx_shadow_info shadow_info;

		if (adev->gfx.funcs->get_gfx_shadow_info) {
			adev->gfx.funcs->get_gfx_shadow_info(adev, &shadow_info, true);
		} else {
			r = -EINVAL;
			goto free_mqd;
		}

		if (mqd_user->mqd_size != sizeof(*mqd_gfx_v11) || !mqd_user->mqd) {
			DRM_ERROR("Invalid GFX MQD\n");
			r = -EINVAL;
			goto free_mqd;
		}

		mqd_gfx_v11 = memdup_user(u64_to_user_ptr(mqd_user->mqd), mqd_user->mqd_size);
		if (IS_ERR(mqd_gfx_v11)) {
			DRM_ERROR("Failed to read user MQD\n");
			r = -ENOMEM;
			goto free_mqd;
		}

		userq_props->shadow_addr = mqd_gfx_v11->shadow_va;
		userq_props->csa_addr = mqd_gfx_v11->csa_va;
		userq_props->tmz_queue =
			mqd_user->flags & AMDGPU_USERQ_CREATE_FLAGS_QUEUE_SECURE;

		r = amdgpu_bo_reserve(queue->vm->root.bo, false);
		if (r) {
			kfree(mqd_gfx_v11);
			goto free_mqd;
		}
		r = amdgpu_userq_input_va_validate(adev, queue, mqd_gfx_v11->shadow_va,
						   shadow_info.shadow_size,
						   &queue->userq_vas.va.shadow);
		if (r) {
			amdgpu_bo_unreserve(queue->vm->root.bo);
			kfree(mqd_gfx_v11);
			goto free_mqd;
		}

		r = amdgpu_userq_input_va_validate(adev, queue, mqd_gfx_v11->csa_va,
						   shadow_info.csa_size,
						   &queue->userq_vas.va.csa);
		amdgpu_bo_unreserve(queue->vm->root.bo);
		if (r) {
			kfree(mqd_gfx_v11);
			goto free_mqd;
		}

		kfree(mqd_gfx_v11);
	} else if (queue->queue_type == AMDGPU_HW_IP_DMA) {
		struct drm_amdgpu_userq_mqd_sdma_gfx11 *mqd_sdma_v11;

		if (mqd_user->mqd_size != sizeof(*mqd_sdma_v11) || !mqd_user->mqd) {
			DRM_ERROR("Invalid SDMA MQD\n");
			r = -EINVAL;
			goto free_mqd;
		}

		mqd_sdma_v11 = memdup_user(u64_to_user_ptr(mqd_user->mqd), mqd_user->mqd_size);
		if (IS_ERR(mqd_sdma_v11)) {
			DRM_ERROR("Failed to read sdma user MQD\n");
			r = -ENOMEM;
			goto free_mqd;
		}

		r = amdgpu_bo_reserve(queue->vm->root.bo, false);
		if (r) {
			kfree(mqd_sdma_v11);
			goto free_mqd;
		}
		r = amdgpu_userq_input_va_validate(adev, queue, mqd_sdma_v11->csa_va,
						   32,
						   &queue->userq_vas.va.csa);
		amdgpu_bo_unreserve(queue->vm->root.bo);
		if (r) {
			kfree(mqd_sdma_v11);
			goto free_mqd;
		}

		userq_props->csa_addr = mqd_sdma_v11->csa_va;
		kfree(mqd_sdma_v11);
	}

	queue->userq_prop = userq_props;

	r = mqd_hw_default->init_mqd(adev, (void *)queue->mqd.cpu_ptr, userq_props);
	if (r) {
		DRM_ERROR("Failed to initialize MQD for userqueue\n");
		goto free_mqd;
	}

	/* Create BO for FW operations */
	r = mes_userq_create_ctx_space(uq_mgr, queue, mqd_user);
	if (r) {
		DRM_ERROR("Failed to allocate BO for userqueue (%d)", r);
		goto free_mqd;
	}

	/* FW expects WPTR BOs to be mapped into GART */
	r = mes_userq_create_wptr_mapping(adev, uq_mgr, queue, userq_props->wptr_gpu_addr);
	if (r) {
		DRM_ERROR("Failed to create WPTR mapping\n");
		goto free_ctx;
	}

	return 0;

free_ctx:
	amdgpu_bo_free_kernel(&queue->fw_obj.obj, &queue->fw_obj.gpu_addr,
			      &queue->fw_obj.cpu_ptr);

free_mqd:
	amdgpu_bo_free_kernel(&queue->mqd.obj, &queue->mqd.gpu_addr,
			      &queue->mqd.cpu_ptr);

free_props:
	kfree(userq_props);

	return r;
}

static void mes_userq_mqd_destroy(struct amdgpu_usermode_queue *queue)
{

	amdgpu_bo_free_kernel(&queue->fw_obj.obj, &queue->fw_obj.gpu_addr,
			      &queue->fw_obj.cpu_ptr);
	kfree(queue->userq_prop);
	amdgpu_bo_free_kernel(&queue->mqd.obj, &queue->mqd.gpu_addr,
			      &queue->mqd.cpu_ptr);

	amdgpu_bo_reserve(queue->wptr_obj.obj, true);
	amdgpu_bo_unpin(queue->wptr_obj.obj);
	amdgpu_bo_unreserve(queue->wptr_obj.obj);
	amdgpu_bo_unref(&queue->wptr_obj.obj);
}

static int mes_userq_preempt(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	struct mes_suspend_gang_input queue_input;
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	signed long timeout = 2100000; /* 2100 ms */
	u64 fence_gpu_addr;
	u32 fence_offset;
	u64 *fence_ptr;
	int i, r;

	if (queue->state != AMDGPU_USERQ_STATE_MAPPED)
		return 0;
	r = amdgpu_device_wb_get(adev, &fence_offset);
	if (r)
		return r;

	fence_gpu_addr = adev->wb.gpu_addr + (fence_offset * 4);
	fence_ptr = (u64 *)&adev->wb.wb[fence_offset];
	*fence_ptr = 0;

	memset(&queue_input, 0x0, sizeof(struct mes_suspend_gang_input));
	queue_input.gang_context_addr = ctx->gpu_addr + AMDGPU_USERQ_PROC_CTX_SZ;
	queue_input.suspend_fence_addr = fence_gpu_addr;
	queue_input.suspend_fence_value = 1;
	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->suspend_gang(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r) {
		DRM_ERROR("Failed to suspend gang: %d\n", r);
		goto out;
	}

	for (i = 0; i < timeout; i++) {
		if (*fence_ptr == 1)
			goto out;
		udelay(1);
	}
	r = -ETIMEDOUT;

out:
	amdgpu_device_wb_free(adev, fence_offset);
	return r;
}

static int mes_userq_restore(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	struct mes_resume_gang_input queue_input;
	struct amdgpu_userq_obj *ctx = &queue->fw_obj;
	int r;

	if (queue->state == AMDGPU_USERQ_STATE_HUNG)
		return -EINVAL;
	if (queue->state != AMDGPU_USERQ_STATE_PREEMPTED)
		return 0;

	memset(&queue_input, 0x0, sizeof(struct mes_resume_gang_input));
	queue_input.gang_context_addr = ctx->gpu_addr + AMDGPU_USERQ_PROC_CTX_SZ;

	amdgpu_mes_lock(&adev->mes);
	r = adev->mes.funcs->resume_gang(&adev->mes, &queue_input);
	amdgpu_mes_unlock(&adev->mes);
	if (r)
		dev_err(adev->dev, "Failed to resume queue, err (%d)\n", r);
	return r;
}

const struct amdgpu_userq_funcs userq_mes_funcs = {
	.mqd_create = mes_userq_mqd_create,
	.mqd_destroy = mes_userq_mqd_destroy,
	.unmap = mes_userq_unmap,
	.map = mes_userq_map,
	.detect_and_reset = mes_userq_detect_and_reset,
	.preempt = mes_userq_preempt,
	.restore = mes_userq_restore,
};
