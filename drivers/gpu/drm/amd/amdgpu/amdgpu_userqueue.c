// SPDX-License-Identifier: MIT
/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "amdgpu_vm.h"
#include "amdgpu_userqueue.h"

static struct amdgpu_usermode_queue *
amdgpu_userqueue_find(struct amdgpu_userq_mgr *uq_mgr, int qid)
{
	return idr_find(&uq_mgr->userq_idr, qid);
}

static int
amdgpu_userqueue_destroy(struct drm_file *filp, int queue_id)
{
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_userq_mgr *uq_mgr = &fpriv->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *uq_funcs;
	struct amdgpu_usermode_queue *queue;

	mutex_lock(&uq_mgr->userq_mutex);

	queue = amdgpu_userqueue_find(uq_mgr, queue_id);
	if (!queue) {
		DRM_DEBUG_DRIVER("Invalid queue id to destroy\n");
		mutex_unlock(&uq_mgr->userq_mutex);
		return -EINVAL;
	}

	uq_funcs = adev->userq_funcs[queue->queue_type];
	uq_funcs->mqd_destroy(uq_mgr, queue);
	idr_remove(&uq_mgr->userq_idr, queue_id);
	kfree(queue);

	mutex_unlock(&uq_mgr->userq_mutex);
	return 0;
}

static int
amdgpu_userqueue_create(struct drm_file *filp, union drm_amdgpu_userq *args)
{
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_userq_mgr *uq_mgr = &fpriv->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *uq_funcs;
	struct amdgpu_usermode_queue *queue;
	int qid, r = 0;

	if (args->in.flags) {
		DRM_ERROR("Usermode queue flags not supported yet\n");
		return -EINVAL;
	}

	mutex_lock(&uq_mgr->userq_mutex);

	uq_funcs = adev->userq_funcs[args->in.ip_type];
	if (!uq_funcs) {
		DRM_ERROR("Usermode queue is not supported for this IP (%u)\n", args->in.ip_type);
		r = -EINVAL;
		goto unlock;
	}

	queue = kzalloc(sizeof(struct amdgpu_usermode_queue), GFP_KERNEL);
	if (!queue) {
		DRM_ERROR("Failed to allocate memory for queue\n");
		r = -ENOMEM;
		goto unlock;
	}
	queue->doorbell_handle = args->in.doorbell_handle;
	queue->doorbell_index = args->in.doorbell_offset;
	queue->queue_type = args->in.ip_type;
	queue->flags = args->in.flags;
	queue->vm = &fpriv->vm;

	r = uq_funcs->mqd_create(uq_mgr, &args->in, queue);
	if (r) {
		DRM_ERROR("Failed to create Queue\n");
		kfree(queue);
		goto unlock;
	}

	qid = idr_alloc(&uq_mgr->userq_idr, queue, 1, AMDGPU_MAX_USERQ_COUNT, GFP_KERNEL);
	if (qid < 0) {
		DRM_ERROR("Failed to allocate a queue id\n");
		uq_funcs->mqd_destroy(uq_mgr, queue);
		kfree(queue);
		r = -ENOMEM;
		goto unlock;
	}
	args->out.queue_id = qid;

unlock:
	mutex_unlock(&uq_mgr->userq_mutex);
	return r;
}

int amdgpu_userq_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *filp)
{
	union drm_amdgpu_userq *args = data;
	int r;

	switch (args->in.op) {
	case AMDGPU_USERQ_OP_CREATE:
		r = amdgpu_userqueue_create(filp, args);
		if (r)
			DRM_ERROR("Failed to create usermode queue\n");
		break;

	case AMDGPU_USERQ_OP_FREE:
		r = amdgpu_userqueue_destroy(filp, args->in.queue_id);
		if (r)
			DRM_ERROR("Failed to destroy usermode queue\n");
		break;

	default:
		DRM_DEBUG_DRIVER("Invalid user queue op specified: %d\n", args->in.op);
		return -EINVAL;
	}

	return r;
}

int amdgpu_userq_mgr_init(struct amdgpu_userq_mgr *userq_mgr, struct amdgpu_device *adev)
{
	mutex_init(&userq_mgr->userq_mutex);
	idr_init_base(&userq_mgr->userq_idr, 1);
	userq_mgr->adev = adev;

	return 0;
}

void amdgpu_userq_mgr_fini(struct amdgpu_userq_mgr *userq_mgr)
{
	idr_destroy(&userq_mgr->userq_idr);
	mutex_destroy(&userq_mgr->userq_mutex);
}
