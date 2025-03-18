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

#include <drm/drm_exec.h>
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
	amdgpu_userq_walk_and_drop_fence_drv(&userq->fence_drv_xa);
	xa_destroy(&userq->fence_drv_xa);
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
	struct dma_fence *f = queue->last_fence;
	int ret;

	if (f && !dma_fence_is_signaled(f)) {
		ret = dma_fence_wait_timeout(f, true, msecs_to_jiffies(100));
		if (ret <= 0) {
			DRM_ERROR("Timed out waiting for fence f=%p\n", f);
			return;
		}
	}

	uq_funcs->mqd_destroy(uq_mgr, queue);
	queue->fence_drv->fence_drv_xa_ptr = NULL;
	amdgpu_userq_fence_driver_free(queue);
	idr_remove(&uq_mgr->userq_idr, queue_id);
	kfree(queue);
}

int
amdgpu_userqueue_active(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_usermode_queue *queue;
	int queue_id;
	int ret = 0;

	mutex_lock(&uq_mgr->userq_mutex);
	/* Resume all the queues for this process */
	idr_for_each_entry(&uq_mgr->userq_idr, queue, queue_id)
		ret += queue->queue_active;

	mutex_unlock(&uq_mgr->userq_mutex);
	return ret;
}

#ifdef CONFIG_DRM_AMDGPU_NAVI3X_USERQ
static struct amdgpu_usermode_queue *
amdgpu_userqueue_find(struct amdgpu_userq_mgr *uq_mgr, int qid)
{
	return idr_find(&uq_mgr->userq_idr, qid);
}

void
amdgpu_userqueue_ensure_ev_fence(struct amdgpu_userq_mgr *uq_mgr,
				 struct amdgpu_eviction_fence_mgr *evf_mgr)
{
	struct amdgpu_eviction_fence *ev_fence;

retry:
	/* Flush any pending resume work to create ev_fence */
	flush_delayed_work(&uq_mgr->resume_work);

	mutex_lock(&uq_mgr->userq_mutex);
	spin_lock(&evf_mgr->ev_fence_lock);
	ev_fence = evf_mgr->ev_fence;
	spin_unlock(&evf_mgr->ev_fence_lock);
	if (!ev_fence || dma_fence_is_signaled(&ev_fence->base)) {
		mutex_unlock(&uq_mgr->userq_mutex);
		/*
		 * Looks like there was no pending resume work,
		 * add one now to create a valid eviction fence
		 */
		schedule_delayed_work(&uq_mgr->resume_work, 0);
		goto retry;
	}
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

uint64_t
amdgpu_userqueue_get_doorbell_index(struct amdgpu_userq_mgr *uq_mgr,
				     struct amdgpu_db_info *db_info,
				     struct drm_file *filp)
{
	uint64_t index;
	struct drm_gem_object *gobj;
	struct amdgpu_userq_obj *db_obj = db_info->db_obj;
	int r, db_size;

	gobj = drm_gem_object_lookup(filp, db_info->doorbell_handle);
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

	switch (db_info->queue_type) {
	case AMDGPU_HW_IP_GFX:
	case AMDGPU_HW_IP_COMPUTE:
	case AMDGPU_HW_IP_DMA:
		db_size = sizeof(u64);
		break;

	case AMDGPU_HW_IP_VCN_ENC:
		db_size = sizeof(u32);
		db_info->doorbell_offset += AMDGPU_NAVI10_DOORBELL64_VCN0_1 << 1;
		break;

	case AMDGPU_HW_IP_VPE:
		db_size = sizeof(u32);
		db_info->doorbell_offset += AMDGPU_NAVI10_DOORBELL64_VPE << 1;
		break;

	default:
		DRM_ERROR("[Usermode queues] IP %d not support\n", db_info->queue_type);
		r = -EINVAL;
		goto unpin_bo;
	}

	index = amdgpu_doorbell_index_on_bar(uq_mgr->adev, db_obj->obj,
					     db_info->doorbell_offset, db_size);
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

	cancel_delayed_work(&uq_mgr->resume_work);
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
	struct amdgpu_db_info db_info;
	uint64_t index;
	int qid, r = 0;

	/* Usermode queues are only supported for GFX IP as of now */
	if (args->in.ip_type != AMDGPU_HW_IP_GFX &&
	    args->in.ip_type != AMDGPU_HW_IP_DMA &&
	    args->in.ip_type != AMDGPU_HW_IP_COMPUTE) {
		DRM_ERROR("Usermode queue doesn't support IP type %u\n", args->in.ip_type);
		return -EINVAL;
	}

	/*
	 * There could be a situation that we are creating a new queue while
	 * the other queues under this UQ_mgr are suspended. So if there is any
	 * resume work pending, wait for it to get done.
	 *
	 * This will also make sure we have a valid eviction fence ready to be used.
	 */
	amdgpu_userqueue_ensure_ev_fence(&fpriv->userq_mgr, &fpriv->evf_mgr);

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
	queue->queue_type = args->in.ip_type;
	queue->vm = &fpriv->vm;

	db_info.queue_type = queue->queue_type;
	db_info.doorbell_handle = queue->doorbell_handle;
	db_info.db_obj = &queue->db_obj;
	db_info.doorbell_offset = args->in.doorbell_offset;

	/* Convert relative doorbell offset into absolute doorbell index */
	index = amdgpu_userqueue_get_doorbell_index(uq_mgr, &db_info, filp);
	if (index == (uint64_t)-EINVAL) {
		DRM_ERROR("Failed to get doorbell for queue\n");
		kfree(queue);
		goto unlock;
	}

	queue->doorbell_index = index;
	xa_init_flags(&queue->fence_drv_xa, XA_FLAGS_ALLOC);
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
		if (args->in._pad)
			return -EINVAL;
		r = amdgpu_userqueue_create(filp, args);
		if (r)
			DRM_ERROR("Failed to create usermode queue\n");
		break;

	case AMDGPU_USERQ_OP_FREE:
		if (args->in.ip_type ||
		    args->in.doorbell_handle ||
		    args->in.doorbell_offset ||
		    args->in._pad ||
		    args->in.queue_va ||
		    args->in.queue_size ||
		    args->in.rptr_va ||
		    args->in.wptr_va ||
		    args->in.wptr_va ||
		    args->in.mqd ||
		    args->in.mqd_size)
			return -EINVAL;
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
	return -ENOTSUPP;
}
#endif

static int
amdgpu_userqueue_resume_all(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *userq_funcs;
	struct amdgpu_usermode_queue *queue;
	int queue_id;
	int ret = 0;

	/* Resume all the queues for this process */
	idr_for_each_entry(&uq_mgr->userq_idr, queue, queue_id) {
		userq_funcs = adev->userq_funcs[queue->queue_type];
		ret = userq_funcs->resume(uq_mgr, queue);
	}

	if (ret)
		DRM_ERROR("Failed to resume all the queue\n");
	return ret;
}

static int
amdgpu_userqueue_validate_vm_bo(void *_unused, struct amdgpu_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };
	int ret;

	amdgpu_bo_placement_from_domain(bo, bo->allowed_domains);

	ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (ret)
		DRM_ERROR("Fail to validate\n");

	return ret;
}

static int
amdgpu_userqueue_validate_bos(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_fpriv *fpriv = uq_mgr_to_fpriv(uq_mgr);
	struct amdgpu_vm *vm = &fpriv->vm;
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_bo_va *bo_va;
	struct ww_acquire_ctx *ticket;
	struct drm_exec exec;
	struct amdgpu_bo *bo;
	struct dma_resv *resv;
	bool clear, unlock;
	int ret = 0;

	drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES, 0);
	drm_exec_until_all_locked(&exec) {
		ret = amdgpu_vm_lock_pd(vm, &exec, 2);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret)) {
			DRM_ERROR("Failed to lock PD\n");
			goto unlock_all;
		}

		/* Lock the done list */
		list_for_each_entry(bo_va, &vm->done, base.vm_status) {
			bo = bo_va->base.bo;
			if (!bo)
				continue;

			ret = drm_exec_lock_obj(&exec, &bo->tbo.base);
			drm_exec_retry_on_contention(&exec);
			if (unlikely(ret))
				goto unlock_all;
		}
	}

	spin_lock(&vm->status_lock);
	while (!list_empty(&vm->moved)) {
		bo_va = list_first_entry(&vm->moved, struct amdgpu_bo_va,
					 base.vm_status);
		spin_unlock(&vm->status_lock);

		/* Per VM BOs never need to bo cleared in the page tables */
		ret = amdgpu_vm_bo_update(adev, bo_va, false);
		if (ret)
			goto unlock_all;
		spin_lock(&vm->status_lock);
	}

	ticket = &exec.ticket;
	while (!list_empty(&vm->invalidated)) {
		bo_va = list_first_entry(&vm->invalidated, struct amdgpu_bo_va,
					 base.vm_status);
		resv = bo_va->base.bo->tbo.base.resv;
		spin_unlock(&vm->status_lock);

		bo = bo_va->base.bo;
		ret = amdgpu_userqueue_validate_vm_bo(NULL, bo);
		if (ret) {
			DRM_ERROR("Failed to validate BO\n");
			goto unlock_all;
		}

		/* Try to reserve the BO to avoid clearing its ptes */
		if (!adev->debug_vm && dma_resv_trylock(resv)) {
			clear = false;
			unlock = true;
		/* The caller is already holding the reservation lock */
		} else if (ticket && dma_resv_locking_ctx(resv) == ticket) {
			clear = false;
			unlock = false;
		/* Somebody else is using the BO right now */
		} else {
			clear = true;
			unlock = false;
		}

		ret = amdgpu_vm_bo_update(adev, bo_va, clear);

		if (unlock)
			dma_resv_unlock(resv);
		if (ret)
			goto unlock_all;

		spin_lock(&vm->status_lock);
	}
	spin_unlock(&vm->status_lock);

	ret = amdgpu_eviction_fence_replace_fence(&fpriv->evf_mgr, &exec);
	if (ret)
		DRM_ERROR("Failed to replace eviction fence\n");

unlock_all:
	drm_exec_fini(&exec);
	return ret;
}

static void amdgpu_userqueue_resume_worker(struct work_struct *work)
{
	struct amdgpu_userq_mgr *uq_mgr = work_to_uq_mgr(work, resume_work.work);
	struct amdgpu_fpriv *fpriv = uq_mgr_to_fpriv(uq_mgr);
	int ret;

	flush_work(&fpriv->evf_mgr.suspend_work.work);

	mutex_lock(&uq_mgr->userq_mutex);

	ret = amdgpu_userqueue_validate_bos(uq_mgr);
	if (ret) {
		DRM_ERROR("Failed to validate BOs to restore\n");
		goto unlock;
	}

	ret = amdgpu_userqueue_resume_all(uq_mgr);
	if (ret) {
		DRM_ERROR("Failed to resume all queues\n");
		goto unlock;
	}

unlock:
	mutex_unlock(&uq_mgr->userq_mutex);
}

static int
amdgpu_userqueue_suspend_all(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *userq_funcs;
	struct amdgpu_usermode_queue *queue;
	int queue_id;
	int ret = 0;

	/* Try to suspend all the queues in this process ctx */
	idr_for_each_entry(&uq_mgr->userq_idr, queue, queue_id) {
		userq_funcs = adev->userq_funcs[queue->queue_type];
		ret += userq_funcs->suspend(uq_mgr, queue);
	}

	if (ret)
		DRM_ERROR("Couldn't suspend all the queues\n");
	return ret;
}

static int
amdgpu_userqueue_wait_for_signal(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_usermode_queue *queue;
	int queue_id, ret;

	idr_for_each_entry(&uq_mgr->userq_idr, queue, queue_id) {
		struct dma_fence *f = queue->last_fence;

		if (!f || dma_fence_is_signaled(f))
			continue;
		ret = dma_fence_wait_timeout(f, true, msecs_to_jiffies(100));
		if (ret <= 0) {
			DRM_ERROR("Timed out waiting for fence f=%p\n", f);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

void
amdgpu_userqueue_suspend(struct amdgpu_userq_mgr *uq_mgr,
			 struct amdgpu_eviction_fence *ev_fence)
{
	int ret;
	struct amdgpu_fpriv *fpriv = uq_mgr_to_fpriv(uq_mgr);
	struct amdgpu_eviction_fence_mgr *evf_mgr = &fpriv->evf_mgr;

	/* Wait for any pending userqueue fence work to finish */
	ret = amdgpu_userqueue_wait_for_signal(uq_mgr);
	if (ret) {
		DRM_ERROR("Not suspending userqueue, timeout waiting for work\n");
		return;
	}

	ret = amdgpu_userqueue_suspend_all(uq_mgr);
	if (ret) {
		DRM_ERROR("Failed to evict userqueue\n");
		return;
	}

	/* Signal current eviction fence */
	amdgpu_eviction_fence_signal(evf_mgr, ev_fence);

	if (evf_mgr->fd_closing) {
		cancel_delayed_work(&uq_mgr->resume_work);
		return;
	}

	/* Schedule a resume work */
	schedule_delayed_work(&uq_mgr->resume_work, 0);
}

int amdgpu_userq_mgr_init(struct amdgpu_userq_mgr *userq_mgr, struct amdgpu_device *adev)
{
	mutex_init(&userq_mgr->userq_mutex);
	idr_init_base(&userq_mgr->userq_idr, 1);
	userq_mgr->adev = adev;

	INIT_DELAYED_WORK(&userq_mgr->resume_work, amdgpu_userqueue_resume_worker);
	return 0;
}

void amdgpu_userq_mgr_fini(struct amdgpu_userq_mgr *userq_mgr)
{
	uint32_t queue_id;
	struct amdgpu_usermode_queue *queue;

	cancel_delayed_work(&userq_mgr->resume_work);

	mutex_lock(&userq_mgr->userq_mutex);
	idr_for_each_entry(&userq_mgr->userq_idr, queue, queue_id)
		amdgpu_userqueue_cleanup(userq_mgr, queue, queue_id);
	idr_destroy(&userq_mgr->userq_idr);
	mutex_unlock(&userq_mgr->userq_mutex);
	mutex_destroy(&userq_mgr->userq_mutex);
}
