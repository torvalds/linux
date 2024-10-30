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
#include "amdgpu_userq_fence.h"

static void amdgpu_userq_walk_and_drop_fence_drv(struct xarray *xa)
{
	struct amdgpu_userq_fence_driver *fence_drv;
	unsigned long index;

	if (xa_empty(xa))
		return;

	xa_lock(xa);
	xa_for_each(xa, index, fence_drv) {
		__xa_erase(xa, index);
		amdgpu_userq_fence_driver_put(fence_drv);
	}

	xa_unlock(xa);
}

static void
amdgpu_userq_fence_driver_free(struct amdgpu_usermode_queue *userq)
{
	amdgpu_userq_walk_and_drop_fence_drv(&userq->uq_fence_drv_xa);
	xa_destroy(&userq->uq_fence_drv_xa);
	/* Drop the fence_drv reference held by user queue */
	amdgpu_userq_fence_driver_put(userq->fence_drv);
}

static void
amdgpu_userqueue_cleanup(struct amdgpu_userq_mgr *uq_mgr,
			 struct amdgpu_usermode_queue *queue,
			 int queue_id)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *uq_funcs = adev->userq_funcs[queue->queue_type];

	uq_funcs->mqd_destroy(uq_mgr, queue);
	amdgpu_userq_fence_driver_free(queue);
	idr_remove(&uq_mgr->userq_idr, queue_id);
	kfree(queue);
}

#ifdef CONFIG_DRM_AMDGPU_NAVI3X_USERQ
static struct amdgpu_usermode_queue *
amdgpu_userqueue_find(struct amdgpu_userq_mgr *uq_mgr, int qid)
{
	return idr_find(&uq_mgr->userq_idr, qid);
}

int amdgpu_userqueue_create_object(struct amdgpu_userq_mgr *uq_mgr,
				   struct amdgpu_userq_obj *userq_obj,
				   int size)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_bo_param bp;
	int r;

	memset(&bp, 0, sizeof(bp));
	bp.byte_align = PAGE_SIZE;
	bp.domain = AMDGPU_GEM_DOMAIN_GTT;
	bp.flags = AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS |
		   AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	bp.type = ttm_bo_type_kernel;
	bp.size = size;
	bp.resv = NULL;
	bp.bo_ptr_size = sizeof(struct amdgpu_bo);

	r = amdgpu_bo_create(adev, &bp, &userq_obj->obj);
	if (r) {
		DRM_ERROR("Failed to allocate BO for userqueue (%d)", r);
		return r;
	}

	r = amdgpu_bo_reserve(userq_obj->obj, true);
	if (r) {
		DRM_ERROR("Failed to reserve BO to map (%d)", r);
		goto free_obj;
	}

	r = amdgpu_ttm_alloc_gart(&(userq_obj->obj)->tbo);
	if (r) {
		DRM_ERROR("Failed to alloc GART for userqueue object (%d)", r);
		goto unresv;
	}

	r = amdgpu_bo_kmap(userq_obj->obj, &userq_obj->cpu_ptr);
	if (r) {
		DRM_ERROR("Failed to map BO for userqueue (%d)", r);
		goto unresv;
	}

	userq_obj->gpu_addr = amdgpu_bo_gpu_offset(userq_obj->obj);
	amdgpu_bo_unreserve(userq_obj->obj);
	memset(userq_obj->cpu_ptr, 0, size);
	return 0;

unresv:
	amdgpu_bo_unreserve(userq_obj->obj);

free_obj:
	amdgpu_bo_unref(&userq_obj->obj);
	return r;
}

void amdgpu_userqueue_destroy_object(struct amdgpu_userq_mgr *uq_mgr,
				   struct amdgpu_userq_obj *userq_obj)
{
	amdgpu_bo_kunmap(userq_obj->obj);
	amdgpu_bo_unref(&userq_obj->obj);
}

static uint64_t
amdgpu_userqueue_get_doorbell_index(struct amdgpu_userq_mgr *uq_mgr,
				     struct amdgpu_usermode_queue *queue,
				     struct drm_file *filp,
				     uint32_t doorbell_offset)
{
	uint64_t index;
	struct drm_gem_object *gobj;
	struct amdgpu_userq_obj *db_obj = &queue->db_obj;
	int r;

	gobj = drm_gem_object_lookup(filp, queue->doorbell_handle);
	if (gobj == NULL) {
		DRM_ERROR("Can't find GEM object for doorbell\n");
		return -EINVAL;
	}

	db_obj->obj = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
	drm_gem_object_put(gobj);

	/* Pin the BO before generating the index, unpin in queue destroy */
	r = amdgpu_bo_pin(db_obj->obj, AMDGPU_GEM_DOMAIN_DOORBELL);
	if (r) {
		DRM_ERROR("[Usermode queues] Failed to pin doorbell object\n");
		goto unref_bo;
	}

	r = amdgpu_bo_reserve(db_obj->obj, true);
	if (r) {
		DRM_ERROR("[Usermode queues] Failed to pin doorbell object\n");
		goto unpin_bo;
	}

	index = amdgpu_doorbell_index_on_bar(uq_mgr->adev, db_obj->obj,
					     doorbell_offset, sizeof(u64));
	DRM_DEBUG_DRIVER("[Usermode queues] doorbell index=%lld\n", index);
	amdgpu_bo_unreserve(db_obj->obj);
	return index;

unpin_bo:
	amdgpu_bo_unpin(db_obj->obj);

unref_bo:
	amdgpu_bo_unref(&db_obj->obj);
	return r;
}

static int
amdgpu_userqueue_destroy(struct drm_file *filp, int queue_id)
{
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_userq_mgr *uq_mgr = &fpriv->userq_mgr;
	struct amdgpu_usermode_queue *queue;

	mutex_lock(&uq_mgr->userq_mutex);

	queue = amdgpu_userqueue_find(uq_mgr, queue_id);
	if (!queue) {
		DRM_DEBUG_DRIVER("Invalid queue id to destroy\n");
		mutex_unlock(&uq_mgr->userq_mutex);
		return -EINVAL;
	}

	amdgpu_bo_unpin(queue->db_obj.obj);
	amdgpu_bo_unref(&queue->db_obj.obj);
	amdgpu_userqueue_cleanup(uq_mgr, queue, queue_id);
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
	uint64_t index;
	int qid, r = 0;

	/* Usermode queues are only supported for GFX IP as of now */
	if (args->in.ip_type != AMDGPU_HW_IP_GFX &&
	    args->in.ip_type != AMDGPU_HW_IP_DMA &&
	    args->in.ip_type != AMDGPU_HW_IP_COMPUTE) {
		DRM_ERROR("Usermode queue doesn't support IP type %u\n", args->in.ip_type);
		return -EINVAL;
	}

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

	/* Convert relative doorbell offset into absolute doorbell index */
	index = amdgpu_userqueue_get_doorbell_index(uq_mgr, queue, filp, args->in.doorbell_offset);
	if (index == (uint64_t)-EINVAL) {
		DRM_ERROR("Failed to get doorbell for queue\n");
		kfree(queue);
		goto unlock;
	}
	queue->doorbell_index = index;

	xa_init_flags(&queue->uq_fence_drv_xa, XA_FLAGS_ALLOC);
	r = amdgpu_userq_fence_driver_alloc(adev, queue);
	if (r) {
		DRM_ERROR("Failed to alloc fence driver\n");
		goto unlock;
	}

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
#else
int amdgpu_userq_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *filp)
{
	return 0;
}
#endif

int amdgpu_userq_mgr_init(struct amdgpu_userq_mgr *userq_mgr, struct amdgpu_device *adev)
{
	mutex_init(&userq_mgr->userq_mutex);
	idr_init_base(&userq_mgr->userq_idr, 1);
	userq_mgr->adev = adev;

	return 0;
}

void amdgpu_userq_mgr_fini(struct amdgpu_userq_mgr *userq_mgr)
{
	uint32_t queue_id;
	struct amdgpu_usermode_queue *queue;

	idr_for_each_entry(&userq_mgr->userq_idr, queue, queue_id)
		amdgpu_userqueue_cleanup(userq_mgr, queue, queue_id);

	idr_destroy(&userq_mgr->userq_idr);
	mutex_destroy(&userq_mgr->userq_mutex);
}
