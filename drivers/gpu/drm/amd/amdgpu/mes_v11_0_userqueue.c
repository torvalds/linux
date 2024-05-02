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
#include "amdgpu.h"
#include "amdgpu_gfx.h"
#include "v11_structs.h"
#include "mes_v11_0.h"
#include "mes_v11_0_userqueue.h"

#define AMDGPU_USERQ_PROC_CTX_SZ PAGE_SIZE
#define AMDGPU_USERQ_GANG_CTX_SZ PAGE_SIZE

static int mes_v11_0_userq_create_ctx_space(struct amdgpu_userq_mgr *uq_mgr,
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
	r = amdgpu_userqueue_create_object(uq_mgr, ctx, size);
	if (r) {
		DRM_ERROR("Failed to allocate ctx space bo for userqueue, err:%d\n", r);
		return r;
	}

	return 0;
}

static int mes_v11_0_userq_mqd_create(struct amdgpu_userq_mgr *uq_mgr,
				      struct drm_amdgpu_userq_in *args_in,
				      struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_mqd *mqd_hw_default = &adev->mqds[queue->queue_type];
	struct drm_amdgpu_userq_in *mqd_user = args_in;
	struct amdgpu_mqd_prop *userq_props;
	int r;

	/* Structure to initialize MQD for userqueue using generic MQD init function */
	userq_props = kzalloc(sizeof(struct amdgpu_mqd_prop), GFP_KERNEL);
	if (!userq_props) {
		DRM_ERROR("Failed to allocate memory for userq_props\n");
		return -ENOMEM;
	}

	if (!mqd_user->wptr_va || !mqd_user->rptr_va ||
	    !mqd_user->queue_va || mqd_user->queue_size == 0) {
		DRM_ERROR("Invalid MQD parameters for userqueue\n");
		r = -EINVAL;
		goto free_props;
	}

	r = amdgpu_userqueue_create_object(uq_mgr, &queue->mqd, mqd_hw_default->mqd_size);
	if (r) {
		DRM_ERROR("Failed to create MQD object for userqueue\n");
		goto free_props;
	}

	/* Initialize the MQD BO with user given values */
	userq_props->wptr_gpu_addr = mqd_user->wptr_va;
	userq_props->rptr_gpu_addr = mqd_user->rptr_va;
	userq_props->queue_size = mqd_user->queue_size;
	userq_props->hqd_base_gpu_addr = mqd_user->queue_va;
	userq_props->mqd_gpu_addr = queue->mqd.gpu_addr;
	userq_props->use_doorbell = true;

	queue->userq_prop = userq_props;

	r = mqd_hw_default->init_mqd(adev, (void *)queue->mqd.cpu_ptr, userq_props);
	if (r) {
		DRM_ERROR("Failed to initialize MQD for userqueue\n");
		goto free_mqd;
	}

	/* Create BO for FW operations */
	r = mes_v11_0_userq_create_ctx_space(uq_mgr, queue, mqd_user);
	if (r) {
		DRM_ERROR("Failed to allocate BO for userqueue (%d)", r);
		goto free_mqd;
	}

	return 0;

free_mqd:
	amdgpu_userqueue_destroy_object(uq_mgr, &queue->mqd);

free_props:
	kfree(userq_props);

	return r;
}

static void
mes_v11_0_userq_mqd_destroy(struct amdgpu_userq_mgr *uq_mgr,
			    struct amdgpu_usermode_queue *queue)
{
	amdgpu_userqueue_destroy_object(uq_mgr, &queue->fw_obj);
	kfree(queue->userq_prop);
	amdgpu_userqueue_destroy_object(uq_mgr, &queue->mqd);
}

const struct amdgpu_userq_funcs userq_mes_v11_0_funcs = {
	.mqd_create = mes_v11_0_userq_mqd_create,
	.mqd_destroy = mes_v11_0_userq_mqd_destroy,
};
