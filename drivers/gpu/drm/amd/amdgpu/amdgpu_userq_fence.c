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

#include <linux/kref.h>
#include <linux/slab.h>
#include <linux/dma-fence-unwrap.h>

#include <drm/drm_exec.h>
#include <drm/drm_syncobj.h>

#include "amdgpu.h"
#include "amdgpu_userq_fence.h"

#define AMDGPU_USERQ_MAX_HANDLES	(1U << 16)

static const struct dma_fence_ops amdgpu_userq_fence_ops;

static inline struct amdgpu_userq_fence *to_amdgpu_userq_fence(struct dma_fence *f)
{
	if (!f || f->ops != &amdgpu_userq_fence_ops)
		return NULL;

	return container_of(f, struct amdgpu_userq_fence, base);
}

static u64 amdgpu_userq_fence_read(struct amdgpu_userq_fence_driver *fence_drv)
{
	return le64_to_cpu(*fence_drv->cpu_addr);
}

static void
amdgpu_userq_fence_write(struct amdgpu_userq_fence_driver *fence_drv,
			 u64 seq)
{
	if (fence_drv->cpu_addr)
		*fence_drv->cpu_addr = cpu_to_le64(seq);
}

int amdgpu_userq_fence_driver_alloc(struct amdgpu_device *adev,
				    struct amdgpu_userq_fence_driver **fence_drv_req)
{
	struct amdgpu_userq_fence_driver *fence_drv;
	int r;

	if (!fence_drv_req)
		return -EINVAL;
	*fence_drv_req = NULL;

	fence_drv = kzalloc_obj(*fence_drv);
	if (!fence_drv)
		return -ENOMEM;

	/* Acquire seq64 memory */
	r = amdgpu_seq64_alloc(adev, &fence_drv->va, &fence_drv->gpu_addr,
			       &fence_drv->cpu_addr);
	if (r)
		goto free_fence_drv;

	memset(fence_drv->cpu_addr, 0, sizeof(u64));

	kref_init(&fence_drv->refcount);
	INIT_LIST_HEAD(&fence_drv->fences);
	spin_lock_init(&fence_drv->fence_list_lock);

	fence_drv->adev = adev;
	fence_drv->context = dma_fence_context_alloc(1);
	get_task_comm(fence_drv->timeline_name, current);

	*fence_drv_req = fence_drv;

	return 0;

free_fence_drv:
	kfree(fence_drv);

	return r;
}

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

void
amdgpu_userq_fence_driver_free(struct amdgpu_usermode_queue *userq)
{
	dma_fence_put(userq->last_fence);
	userq->last_fence = NULL;
	amdgpu_userq_walk_and_drop_fence_drv(&userq->fence_drv_xa);
	xa_destroy(&userq->fence_drv_xa);
	mutex_destroy(&userq->fence_drv_lock);
	/* Drop the queue's ownership reference to fence_drv explicitly */
	amdgpu_userq_fence_driver_put(userq->fence_drv);
}

static void
amdgpu_userq_fence_put_fence_drv_array(struct amdgpu_userq_fence *userq_fence)
{
	unsigned long i;
	for (i = 0; i < userq_fence->fence_drv_array_count; i++)
		amdgpu_userq_fence_driver_put(userq_fence->fence_drv_array[i]);
	userq_fence->fence_drv_array_count = 0;
}

/*
 * Returns:
 * -ENOENT when no fences were processes
 * 1 when more fences are pending
 * 0 when no fences are pending any more
 */
int
amdgpu_userq_fence_driver_process(struct amdgpu_userq_fence_driver *fence_drv)
{
	struct amdgpu_userq_fence *userq_fence, *tmp;
	LIST_HEAD(to_be_signaled);
	struct dma_fence *fence;
	unsigned long flags;
	u64 rptr;

	spin_lock_irqsave(&fence_drv->fence_list_lock, flags);
	rptr = amdgpu_userq_fence_read(fence_drv);

	list_for_each_entry(userq_fence, &fence_drv->fences, link) {
		if (rptr < userq_fence->base.seqno)
			break;
	}

	list_cut_before(&to_be_signaled, &fence_drv->fences,
				&userq_fence->link);
	spin_unlock_irqrestore(&fence_drv->fence_list_lock, flags);

	if (list_empty(&to_be_signaled))
		return -ENOENT;

	list_for_each_entry_safe(userq_fence, tmp, &to_be_signaled, link) {
		fence = &userq_fence->base;
		list_del_init(&userq_fence->link);
		dma_fence_signal(fence);
		/* Drop fence_drv_array outside fence_list_lock
		 * to avoid the recursion lock.
		 */
		amdgpu_userq_fence_put_fence_drv_array(userq_fence);
		dma_fence_put(fence);
	}

	/* That doesn't need to be accurate so no locking */
	return list_empty(&fence_drv->fences) ? 0 : 1;
}

void amdgpu_userq_fence_driver_destroy(struct kref *ref)
{
	struct amdgpu_userq_fence_driver *fence_drv = container_of(ref,
					 struct amdgpu_userq_fence_driver,
					 refcount);
	struct amdgpu_device *adev = fence_drv->adev;
	struct amdgpu_userq_fence *fence, *tmp;
	unsigned long flags;
	struct dma_fence *f;

	spin_lock_irqsave(&fence_drv->fence_list_lock, flags);
	list_for_each_entry_safe(fence, tmp, &fence_drv->fences, link) {
		f = &fence->base;

		if (!dma_fence_is_signaled(f)) {
			dma_fence_set_error(f, -ECANCELED);
			dma_fence_signal(f);
		}

		list_del(&fence->link);
		dma_fence_put(f);
	}
	spin_unlock_irqrestore(&fence_drv->fence_list_lock, flags);

	/* Free seq64 memory */
	amdgpu_seq64_free(adev, fence_drv->va);
	kfree(fence_drv);
}

void amdgpu_userq_fence_driver_get(struct amdgpu_userq_fence_driver *fence_drv)
{
	kref_get(&fence_drv->refcount);
}

void amdgpu_userq_fence_driver_put(struct amdgpu_userq_fence_driver *fence_drv)
{
	kref_put(&fence_drv->refcount, amdgpu_userq_fence_driver_destroy);
}

static int amdgpu_userq_fence_alloc(struct amdgpu_usermode_queue *userq,
				    struct amdgpu_userq_fence **pfence)
{
	struct amdgpu_userq_fence_driver *fence_drv = userq->fence_drv;
	struct amdgpu_userq_fence *userq_fence;
	void *entry;

	userq_fence = kmalloc(sizeof(*userq_fence), GFP_KERNEL);
	if (!userq_fence)
		return -ENOMEM;

	/*
	 * Get the next unused entry, since we fill from the start this can be
	 * used as size to allocate the array.
	 */
	mutex_lock(&userq->fence_drv_lock);
	XA_STATE(xas, &userq->fence_drv_xa, 0);

	rcu_read_lock();
	do {
		entry = xas_find_marked(&xas, ULONG_MAX, XA_FREE_MARK);
	} while (xas_retry(&xas, entry));
	rcu_read_unlock();

	userq_fence->fence_drv_array = kvmalloc_array(xas.xa_index,
						      sizeof(fence_drv),
						      GFP_KERNEL);
	if (!userq_fence->fence_drv_array) {
		mutex_unlock(&userq->fence_drv_lock);
		kfree(userq_fence);
		return -ENOMEM;
	}

	userq_fence->fence_drv_array_count = xas.xa_index;
	xa_extract(&userq->fence_drv_xa, (void **)userq_fence->fence_drv_array,
		   0, ULONG_MAX, xas.xa_index, XA_PRESENT);
	xa_destroy(&userq->fence_drv_xa);

	mutex_unlock(&userq->fence_drv_lock);

	amdgpu_userq_fence_driver_get(fence_drv);
	userq_fence->fence_drv = fence_drv;

	*pfence = userq_fence;
	return 0;
}

static void amdgpu_userq_fence_init(struct amdgpu_usermode_queue *userq,
				    struct amdgpu_userq_fence *fence,
				    u64 seq)
{
	struct amdgpu_userq_fence_driver *fence_drv = userq->fence_drv;
	unsigned long flags;
	bool signaled = false;

	spin_lock_init(&fence->lock);
	dma_fence_init64(&fence->base, &amdgpu_userq_fence_ops, &fence->lock,
			 fence_drv->context, seq);

	/* Make sure the fence is visible to the hang detect worker */
	dma_fence_put(userq->last_fence);
	userq->last_fence = dma_fence_get(&fence->base);

	/* Check if hardware has already processed the fence */
	spin_lock_irqsave(&fence_drv->fence_list_lock, flags);
	if (!dma_fence_is_signaled(&fence->base)) {
		dma_fence_get(&fence->base);
		list_add_tail(&fence->link, &fence_drv->fences);
	} else {
		INIT_LIST_HEAD(&fence->link);
		signaled = true;
	}
	spin_unlock_irqrestore(&fence_drv->fence_list_lock, flags);

	if (signaled)
		amdgpu_userq_fence_put_fence_drv_array(fence);
	else
		amdgpu_userq_start_hang_detect_work(userq);
}

static const char *amdgpu_userq_fence_get_driver_name(struct dma_fence *f)
{
	return "amdgpu_userq_fence";
}

static const char *amdgpu_userq_fence_get_timeline_name(struct dma_fence *f)
{
	struct amdgpu_userq_fence *fence = to_amdgpu_userq_fence(f);

	return fence->fence_drv->timeline_name;
}

static bool amdgpu_userq_fence_signaled(struct dma_fence *f)
{
	struct amdgpu_userq_fence *fence = to_amdgpu_userq_fence(f);
	struct amdgpu_userq_fence_driver *fence_drv = fence->fence_drv;
	u64 rptr, wptr;

	rptr = amdgpu_userq_fence_read(fence_drv);
	wptr = fence->base.seqno;

	if (rptr >= wptr)
		return true;

	return false;
}

static void amdgpu_userq_fence_free(struct rcu_head *rcu)
{
	struct dma_fence *fence = container_of(rcu, struct dma_fence, rcu);
	struct amdgpu_userq_fence *userq_fence = to_amdgpu_userq_fence(fence);
	struct amdgpu_userq_fence_driver *fence_drv = userq_fence->fence_drv;

	/* Release the fence driver reference */
	amdgpu_userq_fence_driver_put(fence_drv);

	kvfree(userq_fence->fence_drv_array);
	kfree(userq_fence);
}

static void amdgpu_userq_fence_release(struct dma_fence *f)
{
	call_rcu(&f->rcu, amdgpu_userq_fence_free);
}

static const struct dma_fence_ops amdgpu_userq_fence_ops = {
	.get_driver_name = amdgpu_userq_fence_get_driver_name,
	.get_timeline_name = amdgpu_userq_fence_get_timeline_name,
	.signaled = amdgpu_userq_fence_signaled,
	.release = amdgpu_userq_fence_release,
};

/**
 * amdgpu_userq_fence_read_wptr - Read the userq wptr value
 *
 * @adev: amdgpu_device pointer
 * @queue: user mode queue structure pointer
 * @wptr: write pointer value
 *
 * Read the wptr value from userq's MQD. The userq signal IOCTL
 * creates a dma_fence for the shared buffers that expects the
 * RPTR value written to seq64 memory >= WPTR.
 *
 * Returns wptr value on success, error on failure.
 */
static int amdgpu_userq_fence_read_wptr(struct amdgpu_device *adev,
					struct amdgpu_usermode_queue *queue,
					u64 *wptr)
{
	struct amdgpu_bo_va_mapping *mapping;
	struct amdgpu_bo *bo;
	struct drm_exec exec;
	u64 addr, *ptr;
	int ret;

	addr = queue->userq_prop->wptr_gpu_addr;
	addr &= AMDGPU_GMC_HOLE_MASK;

	drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES, 2);
	drm_exec_until_all_locked(&exec) {
		ret = amdgpu_vm_lock_pd(queue->vm, &exec, 1);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret))
			goto lock_error;

		mapping = amdgpu_vm_bo_lookup_mapping(queue->vm, addr >> PAGE_SHIFT);
		if (!mapping) {
			ret = -EINVAL;
			goto lock_error;
		}

		ret = drm_exec_lock_obj(&exec, &mapping->bo_va->base.bo->tbo.base);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret))
			goto lock_error;
	}

	bo = mapping->bo_va->base.bo;
	ret = amdgpu_bo_kmap(bo, (void **)&ptr);
	if (ret) {
		DRM_ERROR("Failed mapping the userqueue wptr bo");
		goto lock_error;
	}

	*wptr = le64_to_cpu(*ptr);

	amdgpu_bo_kunmap(bo);
	drm_exec_fini(&exec);
	return 0;

lock_error:
	drm_exec_fini(&exec);
	return ret;
}

static void
amdgpu_userq_fence_driver_set_error(struct amdgpu_userq_fence *fence,
				    int error)
{
	struct amdgpu_userq_fence_driver *fence_drv = fence->fence_drv;
	unsigned long flags;
	struct dma_fence *f;

	spin_lock_irqsave(&fence_drv->fence_list_lock, flags);

	f = rcu_dereference_protected(&fence->base,
				      lockdep_is_held(&fence_drv->fence_list_lock));
	if (f && !dma_fence_is_signaled_locked(f))
		dma_fence_set_error(f, error);
	spin_unlock_irqrestore(&fence_drv->fence_list_lock, flags);
}

void
amdgpu_userq_fence_driver_force_completion(struct amdgpu_usermode_queue *userq)
{
	struct dma_fence *f = userq->last_fence;

	if (f) {
		struct amdgpu_userq_fence *fence = to_amdgpu_userq_fence(f);
		struct amdgpu_userq_fence_driver *fence_drv = fence->fence_drv;
		u64 wptr = fence->base.seqno;

		amdgpu_userq_fence_driver_set_error(fence, -ECANCELED);
		amdgpu_userq_fence_write(fence_drv, wptr);
		amdgpu_userq_fence_driver_process(fence_drv);

	}
}

int amdgpu_userq_signal_ioctl(struct drm_device *dev, void *data,
			      struct drm_file *filp)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	struct drm_amdgpu_userq_signal *args = data;
	const unsigned int num_write_bo_handles = args->num_bo_write_handles;
	const unsigned int num_read_bo_handles = args->num_bo_read_handles;
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_userq_mgr *userq_mgr = &fpriv->userq_mgr;

	struct drm_gem_object **gobj_write, **gobj_read;
	u32 *syncobj_handles, num_syncobj_handles;
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_userq_fence *fence;
	struct drm_syncobj **syncobj;
	struct drm_exec exec;
	void __user *ptr;
	int r, i, entry;
	u64 wptr;

	if (!amdgpu_userq_enabled(dev))
		return -ENOTSUPP;

	if (args->num_bo_write_handles > AMDGPU_USERQ_MAX_HANDLES ||
	    args->num_bo_read_handles > AMDGPU_USERQ_MAX_HANDLES)
		return -EINVAL;

	num_syncobj_handles = args->num_syncobj_handles;
	ptr = u64_to_user_ptr(args->syncobj_handles);
	syncobj_handles = memdup_array_user(ptr, num_syncobj_handles,
					    sizeof(u32));
	if (IS_ERR(syncobj_handles))
		return PTR_ERR(syncobj_handles);

	syncobj = kmalloc_array(num_syncobj_handles, sizeof(*syncobj),
				GFP_KERNEL);
	if (!syncobj) {
		r = -ENOMEM;
		goto free_syncobj_handles;
	}

	for (entry = 0; entry < num_syncobj_handles; entry++) {
		syncobj[entry] = drm_syncobj_find(filp, syncobj_handles[entry]);
		if (!syncobj[entry]) {
			r = -ENOENT;
			goto free_syncobj;
		}
	}

	ptr = u64_to_user_ptr(args->bo_read_handles);
	r = drm_gem_objects_lookup(filp, ptr, num_read_bo_handles, &gobj_read);
	if (r)
		goto free_syncobj;

	ptr = u64_to_user_ptr(args->bo_write_handles);
	r = drm_gem_objects_lookup(filp, ptr, num_write_bo_handles,
				   &gobj_write);
	if (r)
		goto put_gobj_read;

	queue = amdgpu_userq_get(userq_mgr, args->queue_id);
	if (!queue) {
		r = -ENOENT;
		goto put_gobj_write;
	}

	r = amdgpu_userq_fence_read_wptr(adev, queue, &wptr);
	if (r)
		goto put_queue;

	r = amdgpu_userq_fence_alloc(queue, &fence);
	if (r)
		goto put_queue;

	/* We are here means UQ is active, make sure the eviction fence is valid */
	amdgpu_userq_ensure_ev_fence(&fpriv->userq_mgr, &fpriv->evf_mgr);

	/* Create the new fence */
	amdgpu_userq_fence_init(queue, fence, wptr);

	mutex_unlock(&userq_mgr->userq_mutex);

	/*
	 * This needs to come after the fence is created since
	 * amdgpu_userq_ensure_ev_fence() can't be called while holding the resv
	 * locks.
	 */
	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT,
		      (num_read_bo_handles + num_write_bo_handles));

	drm_exec_until_all_locked(&exec) {
		r = drm_exec_prepare_array(&exec, gobj_read,
					   num_read_bo_handles, 1);
		drm_exec_retry_on_contention(&exec);
		if (r)
			goto exec_fini;

		r = drm_exec_prepare_array(&exec, gobj_write,
					   num_write_bo_handles, 1);
		drm_exec_retry_on_contention(&exec);
		if (r)
			goto exec_fini;
	}

	/* And publish the new fence in the BOs and syncobj */
	for (i = 0; i < num_read_bo_handles; i++)
		dma_resv_add_fence(gobj_read[i]->resv, &fence->base,
				   DMA_RESV_USAGE_READ);

	for (i = 0; i < num_write_bo_handles; i++)
		dma_resv_add_fence(gobj_write[i]->resv, &fence->base,
				   DMA_RESV_USAGE_WRITE);

	for (i = 0; i < num_syncobj_handles; i++)
		drm_syncobj_replace_fence(syncobj[i], &fence->base);

exec_fini:
	/* drop the reference acquired in fence creation function */
	dma_fence_put(&fence->base);

	drm_exec_fini(&exec);
put_queue:
	amdgpu_userq_put(queue);
put_gobj_write:
	for (i = 0; i < num_write_bo_handles; i++)
		drm_gem_object_put(gobj_write[i]);
	kvfree(gobj_write);
put_gobj_read:
	for (i = 0; i < num_read_bo_handles; i++)
		drm_gem_object_put(gobj_read[i]);
	kvfree(gobj_read);
free_syncobj:
	while (entry-- > 0)
		drm_syncobj_put(syncobj[entry]);
	kfree(syncobj);
free_syncobj_handles:
	kfree(syncobj_handles);

	return r;
}

/* Count the number of expected fences so userspace can alloc a buffer */
static int
amdgpu_userq_wait_count_fences(struct drm_file *filp,
			       struct drm_amdgpu_userq_wait *wait_info,
			       u32 *syncobj_handles, u64 *timeline_points,
			       u32 *timeline_handles,
			       struct drm_gem_object **gobj_write,
			       struct drm_gem_object **gobj_read)
{
	int num_read_bo_handles, num_write_bo_handles;
	struct dma_fence_unwrap iter;
	struct dma_fence *fence, *f;
	unsigned int num_fences = 0;
	struct drm_exec exec;
	int i, r;

	/*
	 * This needs to be outside of the lock provided by drm_exec for
	 * DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT to work correctly.
	 */

	/* Count timeline fences */
	for (i = 0; i < wait_info->num_syncobj_timeline_handles; i++) {
		r = drm_syncobj_find_fence(filp, timeline_handles[i],
					   timeline_points[i],
					   DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
					   &fence);
		if (r)
			return r;

		dma_fence_unwrap_for_each(f, &iter, fence)
			num_fences++;

		dma_fence_put(fence);
	}

	/* Count boolean fences */
	for (i = 0; i < wait_info->num_syncobj_handles; i++) {
		r = drm_syncobj_find_fence(filp, syncobj_handles[i], 0,
					   DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
					   &fence);
		if (r)
			return r;

		num_fences++;
		dma_fence_put(fence);
	}

	/* Lock all the GEM objects */
	/* TODO: It is actually not necessary to lock them */
	num_read_bo_handles = wait_info->num_bo_read_handles;
	num_write_bo_handles = wait_info->num_bo_write_handles;
	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT,
		      num_read_bo_handles + num_write_bo_handles);

	drm_exec_until_all_locked(&exec) {
		r = drm_exec_prepare_array(&exec, gobj_read,
					   num_read_bo_handles, 1);
		drm_exec_retry_on_contention(&exec);
		if (r)
			goto error_unlock;

		r = drm_exec_prepare_array(&exec, gobj_write,
					   num_write_bo_handles, 1);
		drm_exec_retry_on_contention(&exec);
		if (r)
			goto error_unlock;
	}

	/* Count read fences */
	for (i = 0; i < num_read_bo_handles; i++) {
		struct dma_resv_iter resv_cursor;
		struct dma_fence *fence;

		dma_resv_for_each_fence(&resv_cursor, gobj_read[i]->resv,
					DMA_RESV_USAGE_READ, fence)
			num_fences++;
	}

	/* Count write fences */
	for (i = 0; i < num_write_bo_handles; i++) {
		struct dma_resv_iter resv_cursor;
		struct dma_fence *fence;

		dma_resv_for_each_fence(&resv_cursor, gobj_write[i]->resv,
					DMA_RESV_USAGE_WRITE, fence)
			num_fences++;
	}

	wait_info->num_fences = min(num_fences, USHRT_MAX);
	r = 0;

error_unlock:
	/* Unlock all the GEM objects */
	drm_exec_fini(&exec);
	return r;
}

static int
amdgpu_userq_wait_add_fence(struct drm_amdgpu_userq_wait *wait_info,
			    struct dma_fence **fences, unsigned int *num_fences,
			    struct dma_fence *fence)
{
	/* As fallback shouldn't userspace allocate enough space */
	if (*num_fences >= wait_info->num_fences)
		return dma_fence_wait(fence, true);

	fences[(*num_fences)++] = dma_fence_get(fence);
	return 0;
}

static int
amdgpu_userq_wait_return_fence_info(struct drm_file *filp,
				    struct drm_amdgpu_userq_wait *wait_info,
				    u32 *syncobj_handles, u64 *timeline_points,
				    u32 *timeline_handles,
				    struct drm_gem_object **gobj_write,
				    struct drm_gem_object **gobj_read)
{
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_userq_mgr *userq_mgr = &fpriv->userq_mgr;
	struct drm_amdgpu_userq_fence_info *fence_info;
	int num_read_bo_handles, num_write_bo_handles;
	struct amdgpu_usermode_queue *waitq;
	struct dma_fence **fences, *fence, *f;
	struct dma_fence_unwrap iter;
	int num_points, num_syncobj;
	unsigned int num_fences = 0;
	struct drm_exec exec;
	int i, cnt, r;

	fence_info = kmalloc_array(wait_info->num_fences, sizeof(*fence_info),
				   GFP_KERNEL);
	if (!fence_info)
		return -ENOMEM;

	fences = kmalloc_array(wait_info->num_fences, sizeof(*fences),
			       GFP_KERNEL);
	if (!fences) {
		r = -ENOMEM;
		goto free_fence_info;
	}

	/* Retrieve timeline fences */
	num_points = wait_info->num_syncobj_timeline_handles;
	for (i = 0; i < num_points; i++) {
		r = drm_syncobj_find_fence(filp, timeline_handles[i],
					   timeline_points[i],
					   DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
					   &fence);
		if (r)
			goto free_fences;

		dma_fence_unwrap_for_each(f, &iter, fence) {
			r = amdgpu_userq_wait_add_fence(wait_info, fences,
							&num_fences, f);
			if (r) {
				dma_fence_put(fence);
				goto free_fences;
			}
		}

		dma_fence_put(fence);
	}

	/* Retrieve boolean fences */
	num_syncobj = wait_info->num_syncobj_handles;
	for (i = 0; i < num_syncobj; i++) {
		struct dma_fence *fence;

		r = drm_syncobj_find_fence(filp, syncobj_handles[i], 0,
					   DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
					   &fence);
		if (r)
			goto free_fences;

		r = amdgpu_userq_wait_add_fence(wait_info, fences,
						&num_fences, fence);
		dma_fence_put(fence);
		if (r)
			goto free_fences;

	}

	/* Lock all the GEM objects */
	num_read_bo_handles = wait_info->num_bo_read_handles;
	num_write_bo_handles = wait_info->num_bo_write_handles;
	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT,
		      num_read_bo_handles + num_write_bo_handles);

	drm_exec_until_all_locked(&exec) {
		r = drm_exec_prepare_array(&exec, gobj_read,
					   num_read_bo_handles, 1);
		drm_exec_retry_on_contention(&exec);
		if (r)
			goto error_unlock;

		r = drm_exec_prepare_array(&exec, gobj_write,
					   num_write_bo_handles, 1);
		drm_exec_retry_on_contention(&exec);
		if (r)
			goto error_unlock;
	}

	/* Retrieve GEM read objects fence */
	for (i = 0; i < num_read_bo_handles; i++) {
		struct dma_resv_iter resv_cursor;
		struct dma_fence *fence;

		dma_resv_for_each_fence(&resv_cursor, gobj_read[i]->resv,
					DMA_RESV_USAGE_READ, fence) {
			r = amdgpu_userq_wait_add_fence(wait_info, fences,
							&num_fences, fence);
			if (r)
				goto error_unlock;
		}
	}

	/* Retrieve GEM write objects fence */
	for (i = 0; i < num_write_bo_handles; i++) {
		struct dma_resv_iter resv_cursor;
		struct dma_fence *fence;

		dma_resv_for_each_fence(&resv_cursor, gobj_write[i]->resv,
					DMA_RESV_USAGE_WRITE, fence) {
			r = amdgpu_userq_wait_add_fence(wait_info, fences,
							&num_fences, fence);
			if (r)
				goto error_unlock;
		}
	}

	drm_exec_fini(&exec);

	/*
	 * Keep only the latest fences to reduce the number of values
	 * given back to userspace.
	 */
	num_fences = dma_fence_dedup_array(fences, num_fences);

	waitq = amdgpu_userq_get(userq_mgr, wait_info->waitq_id);
	if (!waitq) {
		r = -EINVAL;
		goto free_fences;
	}

	for (i = 0, cnt = 0; i < num_fences; i++) {
		struct amdgpu_userq_fence_driver *fence_drv;
		struct amdgpu_userq_fence *userq_fence;
		u32 index;

		userq_fence = to_amdgpu_userq_fence(fences[i]);
		if (!userq_fence) {
			/*
			 * Just waiting on other driver fences should
			 * be good for now
			 */
			r = dma_fence_wait(fences[i], true);
			if (r)
				goto put_waitq;

			continue;
		}

		fence_drv = userq_fence->fence_drv;
		/*
		 * We need to make sure the user queue release their reference
		 * to the fence drivers at some point before queue destruction.
		 * Otherwise, we would gather those references until we don't
		 * have any more space left and crash.
		 */
		mutex_lock(&waitq->fence_drv_lock);
		r = xa_alloc(&waitq->fence_drv_xa, &index, fence_drv,
			     xa_limit_32b, GFP_KERNEL);
		mutex_unlock(&waitq->fence_drv_lock);
		if (r)
			goto put_waitq;

		amdgpu_userq_fence_driver_get(fence_drv);

		/* Store drm syncobj's gpu va address and value */
		fence_info[cnt].va = fence_drv->va;
		fence_info[cnt].value = fences[i]->seqno;

		/* Increment the actual userq fence count */
		cnt++;
	}
	wait_info->num_fences = cnt;

	/* Copy userq fence info to user space */
	if (copy_to_user(u64_to_user_ptr(wait_info->out_fences),
			 fence_info, cnt * sizeof(*fence_info)))
		r = -EFAULT;
	else
		r = 0;

put_waitq:
	amdgpu_userq_put(waitq);

free_fences:
	while (num_fences--)
		dma_fence_put(fences[num_fences]);
	kfree(fences);

free_fence_info:
	kfree(fence_info);
	return r;

error_unlock:
	drm_exec_fini(&exec);
	goto free_fences;
}

int amdgpu_userq_wait_ioctl(struct drm_device *dev, void *data,
			    struct drm_file *filp)
{
	int num_points, num_syncobj, num_read_bo_handles, num_write_bo_handles;
	u32 *syncobj_handles, *timeline_handles;
	u64 *timeline_points;
	struct drm_amdgpu_userq_wait *wait_info = data;
	struct drm_gem_object **gobj_write;
	struct drm_gem_object **gobj_read;
	void __user *ptr;
	int r;

	if (!amdgpu_userq_enabled(dev))
		return -ENOTSUPP;

	if (wait_info->num_bo_write_handles > AMDGPU_USERQ_MAX_HANDLES ||
	    wait_info->num_bo_read_handles > AMDGPU_USERQ_MAX_HANDLES)
		return -EINVAL;

	num_syncobj = wait_info->num_syncobj_handles;
	ptr = u64_to_user_ptr(wait_info->syncobj_handles);
	syncobj_handles = memdup_array_user(ptr, num_syncobj, sizeof(u32));
	if (IS_ERR(syncobj_handles))
		return PTR_ERR(syncobj_handles);

	num_points = wait_info->num_syncobj_timeline_handles;
	ptr = u64_to_user_ptr(wait_info->syncobj_timeline_handles);
	timeline_handles = memdup_array_user(ptr, num_points, sizeof(u32));
	if (IS_ERR(timeline_handles)) {
		r = PTR_ERR(timeline_handles);
		goto free_syncobj_handles;
	}

	ptr = u64_to_user_ptr(wait_info->syncobj_timeline_points);
	timeline_points = memdup_array_user(ptr, num_points, sizeof(u64));
	if (IS_ERR(timeline_points)) {
		r = PTR_ERR(timeline_points);
		goto free_timeline_handles;
	}

	num_read_bo_handles = wait_info->num_bo_read_handles;
	ptr = u64_to_user_ptr(wait_info->bo_read_handles);
	r = drm_gem_objects_lookup(filp, ptr, num_read_bo_handles, &gobj_read);
	if (r)
		goto free_timeline_points;

	num_write_bo_handles = wait_info->num_bo_write_handles;
	ptr = u64_to_user_ptr(wait_info->bo_write_handles);
	r = drm_gem_objects_lookup(filp, ptr, num_write_bo_handles,
				   &gobj_write);
	if (r)
		goto put_gobj_read;

	/*
	 * Passing num_fences = 0 means that userspace doesn't want to
	 * retrieve userq_fence_info. If num_fences = 0 we skip filling
	 * userq_fence_info and return the actual number of fences on
	 * args->num_fences.
	 */
	if (!wait_info->num_fences) {
		r = amdgpu_userq_wait_count_fences(filp, wait_info,
						   syncobj_handles,
						   timeline_points,
						   timeline_handles,
						   gobj_write,
						   gobj_read);
	} else {
		r = amdgpu_userq_wait_return_fence_info(filp, wait_info,
							syncobj_handles,
							timeline_points,
							timeline_handles,
							gobj_write,
							gobj_read);
	}

	while (num_write_bo_handles--)
		drm_gem_object_put(gobj_write[num_write_bo_handles]);
	kvfree(gobj_write);

put_gobj_read:
	while (num_read_bo_handles--)
		drm_gem_object_put(gobj_read[num_read_bo_handles]);
	kvfree(gobj_read);

free_timeline_points:
	kfree(timeline_points);
free_timeline_handles:
	kfree(timeline_handles);
free_syncobj_handles:
	kfree(syncobj_handles);
	return r;
}
