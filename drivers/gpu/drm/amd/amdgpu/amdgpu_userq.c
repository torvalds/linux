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

#include <drm/drm_auth.h>
#include <drm/drm_exec.h>
#include <linux/pm_runtime.h>
#include <drm/drm_drv.h>

#include "amdgpu.h"
#include "amdgpu_reset.h"
#include "amdgpu_vm.h"
#include "amdgpu_userq.h"
#include "amdgpu_hmm.h"
#include "amdgpu_userq_fence.h"

u32 amdgpu_userq_get_supported_ip_mask(struct amdgpu_device *adev)
{
	int i;
	u32 userq_ip_mask = 0;

	for (i = 0; i < AMDGPU_HW_IP_NUM; i++) {
		if (adev->userq_funcs[i])
			userq_ip_mask |= (1 << i);
	}

	return userq_ip_mask;
}

static bool amdgpu_userq_is_reset_type_supported(struct amdgpu_device *adev,
				enum amdgpu_ring_type ring_type, int reset_type)
{

	if (ring_type < 0 || ring_type >= AMDGPU_RING_TYPE_MAX)
		return false;

	switch (ring_type) {
	case AMDGPU_RING_TYPE_GFX:
		if (adev->gfx.gfx_supported_reset & reset_type)
			return true;
		break;
	case AMDGPU_RING_TYPE_COMPUTE:
		if (adev->gfx.compute_supported_reset & reset_type)
			return true;
		break;
	case AMDGPU_RING_TYPE_SDMA:
		if (adev->sdma.supported_reset & reset_type)
			return true;
		break;
	case AMDGPU_RING_TYPE_VCN_DEC:
	case AMDGPU_RING_TYPE_VCN_ENC:
		if (adev->vcn.supported_reset & reset_type)
			return true;
		break;
	case AMDGPU_RING_TYPE_VCN_JPEG:
		if (adev->jpeg.supported_reset & reset_type)
			return true;
		break;
	default:
		break;
	}
	return false;
}

static void amdgpu_userq_gpu_reset(struct amdgpu_device *adev)
{
	if (amdgpu_device_should_recover_gpu(adev)) {
		amdgpu_reset_domain_schedule(adev->reset_domain,
					     &adev->userq_reset_work);
		/* Wait for the reset job to complete */
		flush_work(&adev->userq_reset_work);
	}
}

static int
amdgpu_userq_detect_and_reset_queues(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_device *adev = uq_mgr->adev;
	const int queue_types[] = {
		AMDGPU_RING_TYPE_COMPUTE,
		AMDGPU_RING_TYPE_GFX,
		AMDGPU_RING_TYPE_SDMA
	};
	const int num_queue_types = ARRAY_SIZE(queue_types);
	bool gpu_reset = false;
	int r = 0;
	int i;

	/* Warning if current process mutex is not held */
	WARN_ON(!mutex_is_locked(&uq_mgr->userq_mutex));

	if (unlikely(adev->debug_disable_gpu_ring_reset)) {
		dev_err(adev->dev, "userq reset disabled by debug mask\n");
		return 0;
	}

	/*
	 * If GPU recovery feature is disabled system-wide,
	 * skip all reset detection logic
	 */
	if (!amdgpu_gpu_recovery)
		return 0;

	/*
	 * Iterate through all queue types to detect and reset problematic queues
	 * Process each queue type in the defined order
	 */
	for (i = 0; i < num_queue_types; i++) {
		int ring_type = queue_types[i];
		const struct amdgpu_userq_funcs *funcs = adev->userq_funcs[ring_type];

		if (!amdgpu_userq_is_reset_type_supported(adev, ring_type, AMDGPU_RESET_TYPE_PER_QUEUE))
				continue;

		if (atomic_read(&uq_mgr->userq_count[ring_type]) > 0 &&
		    funcs && funcs->detect_and_reset) {
			r = funcs->detect_and_reset(adev, ring_type);
			if (r) {
				gpu_reset = true;
				break;
			}
		}
	}

	if (gpu_reset)
		amdgpu_userq_gpu_reset(adev);

	return r;
}

static void amdgpu_userq_hang_detect_work(struct work_struct *work)
{
	struct amdgpu_usermode_queue *queue = container_of(work,
							  struct amdgpu_usermode_queue,
							  hang_detect_work.work);
	struct dma_fence *fence;
	struct amdgpu_userq_mgr *uq_mgr;

	if (!queue || !queue->userq_mgr)
		return;

	uq_mgr = queue->userq_mgr;
	fence = READ_ONCE(queue->hang_detect_fence);
	/* Fence already signaled – no action needed */
	if (!fence || dma_fence_is_signaled(fence))
		return;

	mutex_lock(&uq_mgr->userq_mutex);
	amdgpu_userq_detect_and_reset_queues(uq_mgr);
	mutex_unlock(&uq_mgr->userq_mutex);
}

/*
 * Start hang detection for a user queue fence. A delayed work will be scheduled
 * to check if the fence is still pending after the timeout period.
*/
void amdgpu_userq_start_hang_detect_work(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_device *adev;
	unsigned long timeout_ms;

	if (!queue || !queue->userq_mgr || !queue->userq_mgr->adev)
		return;

	adev = queue->userq_mgr->adev;
	/* Determine timeout based on queue type */
	switch (queue->queue_type) {
	case AMDGPU_RING_TYPE_GFX:
		timeout_ms = adev->gfx_timeout;
		break;
	case AMDGPU_RING_TYPE_COMPUTE:
		timeout_ms = adev->compute_timeout;
		break;
	case AMDGPU_RING_TYPE_SDMA:
		timeout_ms = adev->sdma_timeout;
		break;
	default:
		timeout_ms = adev->gfx_timeout;
		break;
	}

	/* Store the fence to monitor and schedule hang detection */
	WRITE_ONCE(queue->hang_detect_fence, queue->last_fence);
	schedule_delayed_work(&queue->hang_detect_work,
		     msecs_to_jiffies(timeout_ms));
}

static void amdgpu_userq_init_hang_detect_work(struct amdgpu_usermode_queue *queue)
{
	INIT_DELAYED_WORK(&queue->hang_detect_work, amdgpu_userq_hang_detect_work);
	queue->hang_detect_fence = NULL;
}

static int amdgpu_userq_buffer_va_list_add(struct amdgpu_usermode_queue *queue,
					   struct amdgpu_bo_va_mapping *va_map, u64 addr)
{
	struct amdgpu_userq_va_cursor *va_cursor;
	struct userq_va_list;

	va_cursor = kzalloc(sizeof(*va_cursor), GFP_KERNEL);
	if (!va_cursor)
		return -ENOMEM;

	INIT_LIST_HEAD(&va_cursor->list);
	va_cursor->gpu_addr = addr;
	atomic_set(&va_map->bo_va->userq_va_mapped, 1);
	list_add(&va_cursor->list, &queue->userq_va_list);

	return 0;
}

int amdgpu_userq_input_va_validate(struct amdgpu_device *adev,
				   struct amdgpu_usermode_queue *queue,
				   u64 addr, u64 expected_size)
{
	struct amdgpu_bo_va_mapping *va_map;
	struct amdgpu_vm *vm = queue->vm;
	u64 user_addr;
	u64 size;
	int r = 0;

	user_addr = (addr & AMDGPU_GMC_HOLE_MASK) >> AMDGPU_GPU_PAGE_SHIFT;
	size = expected_size >> AMDGPU_GPU_PAGE_SHIFT;

	r = amdgpu_bo_reserve(vm->root.bo, false);
	if (r)
		return r;

	va_map = amdgpu_vm_bo_lookup_mapping(vm, user_addr);
	if (!va_map) {
		r = -EINVAL;
		goto out_err;
	}
	/* Only validate the userq whether resident in the VM mapping range */
	if (user_addr >= va_map->start  &&
	    va_map->last - user_addr + 1 >= size) {
		amdgpu_userq_buffer_va_list_add(queue, va_map, user_addr);
		amdgpu_bo_unreserve(vm->root.bo);
		return 0;
	}

	r = -EINVAL;
out_err:
	amdgpu_bo_unreserve(vm->root.bo);
	return r;
}

static bool amdgpu_userq_buffer_va_mapped(struct amdgpu_vm *vm, u64 addr)
{
	struct amdgpu_bo_va_mapping *mapping;
	bool r;

	if (amdgpu_bo_reserve(vm->root.bo, false))
		return false;

	mapping = amdgpu_vm_bo_lookup_mapping(vm, addr);
	if (!IS_ERR_OR_NULL(mapping) && atomic_read(&mapping->bo_va->userq_va_mapped))
		r = true;
	else
		r = false;
	amdgpu_bo_unreserve(vm->root.bo);

	return r;
}

static bool amdgpu_userq_buffer_vas_mapped(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_va_cursor *va_cursor, *tmp;
	int r = 0;

	list_for_each_entry_safe(va_cursor, tmp, &queue->userq_va_list, list) {
		r += amdgpu_userq_buffer_va_mapped(queue->vm, va_cursor->gpu_addr);
		dev_dbg(queue->userq_mgr->adev->dev,
			"validate the userq mapping:%p va:%llx r:%d\n",
			queue, va_cursor->gpu_addr, r);
	}

	if (r != 0)
		return true;

	return false;
}

static void amdgpu_userq_buffer_va_list_del(struct amdgpu_bo_va_mapping *mapping,
					    struct amdgpu_userq_va_cursor *va_cursor)
{
	atomic_set(&mapping->bo_va->userq_va_mapped, 0);
	list_del(&va_cursor->list);
	kfree(va_cursor);
}

static int amdgpu_userq_buffer_vas_list_cleanup(struct amdgpu_device *adev,
						struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_va_cursor *va_cursor, *tmp;
	struct amdgpu_bo_va_mapping *mapping;
	int r;

	r = amdgpu_bo_reserve(queue->vm->root.bo, false);
	if (r)
		return r;

	list_for_each_entry_safe(va_cursor, tmp, &queue->userq_va_list, list) {
		mapping = amdgpu_vm_bo_lookup_mapping(queue->vm, va_cursor->gpu_addr);
		if (!mapping) {
			r = -EINVAL;
			goto err;
		}
		dev_dbg(adev->dev, "delete the userq:%p va:%llx\n",
			queue, va_cursor->gpu_addr);
		amdgpu_userq_buffer_va_list_del(mapping, va_cursor);
	}
err:
	amdgpu_bo_unreserve(queue->vm->root.bo);
	return r;
}

static int amdgpu_userq_preempt_helper(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *userq_funcs =
		adev->userq_funcs[queue->queue_type];
	bool found_hung_queue = false;
	int r = 0;

	if (queue->state == AMDGPU_USERQ_STATE_MAPPED) {
		r = userq_funcs->preempt(queue);
		if (r) {
			queue->state = AMDGPU_USERQ_STATE_HUNG;
			found_hung_queue = true;
		} else {
			queue->state = AMDGPU_USERQ_STATE_PREEMPTED;
		}
	}

	if (found_hung_queue)
		amdgpu_userq_detect_and_reset_queues(uq_mgr);

	return r;
}

static int amdgpu_userq_restore_helper(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *userq_funcs =
		adev->userq_funcs[queue->queue_type];
	int r = 0;

	if (queue->state == AMDGPU_USERQ_STATE_PREEMPTED) {
		r = userq_funcs->restore(queue);
		if (r) {
			queue->state = AMDGPU_USERQ_STATE_HUNG;
		} else {
			queue->state = AMDGPU_USERQ_STATE_MAPPED;
		}
	}

	return r;
}

static int amdgpu_userq_unmap_helper(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *userq_funcs =
		adev->userq_funcs[queue->queue_type];
	bool found_hung_queue = false;
	int r = 0;

	if ((queue->state == AMDGPU_USERQ_STATE_MAPPED) ||
		(queue->state == AMDGPU_USERQ_STATE_PREEMPTED)) {
		r = userq_funcs->unmap(queue);
		if (r) {
			queue->state = AMDGPU_USERQ_STATE_HUNG;
			found_hung_queue = true;
		} else {
			queue->state = AMDGPU_USERQ_STATE_UNMAPPED;
		}
	}

	if (found_hung_queue)
		amdgpu_userq_detect_and_reset_queues(uq_mgr);

	return r;
}

static int amdgpu_userq_map_helper(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *userq_funcs =
		adev->userq_funcs[queue->queue_type];
	int r = 0;

	if (queue->state == AMDGPU_USERQ_STATE_UNMAPPED) {
		r = userq_funcs->map(queue);
		if (r) {
			queue->state = AMDGPU_USERQ_STATE_HUNG;
			amdgpu_userq_detect_and_reset_queues(uq_mgr);
		} else {
			queue->state = AMDGPU_USERQ_STATE_MAPPED;
		}
	}

	return r;
}

static int amdgpu_userq_wait_for_last_fence(struct amdgpu_usermode_queue *queue)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct dma_fence *f = queue->last_fence;
	int ret = 0;

	if (f && !dma_fence_is_signaled(f)) {
		ret = dma_fence_wait_timeout(f, true, MAX_SCHEDULE_TIMEOUT);
		if (ret <= 0) {
			drm_file_err(uq_mgr->file, "Timed out waiting for fence=%llu:%llu\n",
				     f->context, f->seqno);
			queue->state = AMDGPU_USERQ_STATE_HUNG;
			return -ETIME;
		}
	}

	return ret;
}

static void amdgpu_userq_cleanup(struct amdgpu_usermode_queue *queue,
				 int queue_id)
{
	struct amdgpu_userq_mgr *uq_mgr = queue->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *uq_funcs = adev->userq_funcs[queue->queue_type];

	/* Wait for mode-1 reset to complete */
	down_read(&adev->reset_domain->sem);

	/* Drop the userq reference. */
	amdgpu_userq_buffer_vas_list_cleanup(adev, queue);
	uq_funcs->mqd_destroy(queue);
	amdgpu_userq_fence_driver_free(queue);
	/* Use interrupt-safe locking since IRQ handlers may access these XArrays */
	xa_erase_irq(&uq_mgr->userq_xa, (unsigned long)queue_id);
	xa_erase_irq(&adev->userq_doorbell_xa, queue->doorbell_index);
	queue->userq_mgr = NULL;
	list_del(&queue->userq_va_list);
	kfree(queue);

	up_read(&adev->reset_domain->sem);
}

static struct amdgpu_usermode_queue *
amdgpu_userq_find(struct amdgpu_userq_mgr *uq_mgr, int qid)
{
	return xa_load(&uq_mgr->userq_xa, qid);
}

void
amdgpu_userq_ensure_ev_fence(struct amdgpu_userq_mgr *uq_mgr,
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

int amdgpu_userq_create_object(struct amdgpu_userq_mgr *uq_mgr,
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
		drm_file_err(uq_mgr->file, "Failed to allocate BO for userqueue (%d)", r);
		return r;
	}

	r = amdgpu_bo_reserve(userq_obj->obj, true);
	if (r) {
		drm_file_err(uq_mgr->file, "Failed to reserve BO to map (%d)", r);
		goto free_obj;
	}

	r = amdgpu_ttm_alloc_gart(&(userq_obj->obj)->tbo);
	if (r) {
		drm_file_err(uq_mgr->file, "Failed to alloc GART for userqueue object (%d)", r);
		goto unresv;
	}

	r = amdgpu_bo_kmap(userq_obj->obj, &userq_obj->cpu_ptr);
	if (r) {
		drm_file_err(uq_mgr->file, "Failed to map BO for userqueue (%d)", r);
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

void amdgpu_userq_destroy_object(struct amdgpu_userq_mgr *uq_mgr,
				 struct amdgpu_userq_obj *userq_obj)
{
	amdgpu_bo_kunmap(userq_obj->obj);
	amdgpu_bo_unref(&userq_obj->obj);
}

uint64_t
amdgpu_userq_get_doorbell_index(struct amdgpu_userq_mgr *uq_mgr,
				struct amdgpu_db_info *db_info,
				struct drm_file *filp)
{
	uint64_t index;
	struct drm_gem_object *gobj;
	struct amdgpu_userq_obj *db_obj = db_info->db_obj;
	int r, db_size;

	gobj = drm_gem_object_lookup(filp, db_info->doorbell_handle);
	if (gobj == NULL) {
		drm_file_err(uq_mgr->file, "Can't find GEM object for doorbell\n");
		return -EINVAL;
	}

	db_obj->obj = amdgpu_bo_ref(gem_to_amdgpu_bo(gobj));
	drm_gem_object_put(gobj);

	r = amdgpu_bo_reserve(db_obj->obj, true);
	if (r) {
		drm_file_err(uq_mgr->file, "[Usermode queues] Failed to pin doorbell object\n");
		goto unref_bo;
	}

	/* Pin the BO before generating the index, unpin in queue destroy */
	r = amdgpu_bo_pin(db_obj->obj, AMDGPU_GEM_DOMAIN_DOORBELL);
	if (r) {
		drm_file_err(uq_mgr->file, "[Usermode queues] Failed to pin doorbell object\n");
		goto unresv_bo;
	}

	switch (db_info->queue_type) {
	case AMDGPU_HW_IP_GFX:
	case AMDGPU_HW_IP_COMPUTE:
	case AMDGPU_HW_IP_DMA:
		db_size = sizeof(u64);
		break;
	default:
		drm_file_err(uq_mgr->file, "[Usermode queues] IP %d not support\n",
			     db_info->queue_type);
		r = -EINVAL;
		goto unpin_bo;
	}

	index = amdgpu_doorbell_index_on_bar(uq_mgr->adev, db_obj->obj,
					     db_info->doorbell_offset, db_size);
	drm_dbg_driver(adev_to_drm(uq_mgr->adev),
		       "[Usermode queues] doorbell index=%lld\n", index);
	amdgpu_bo_unreserve(db_obj->obj);
	return index;

unpin_bo:
	amdgpu_bo_unpin(db_obj->obj);
unresv_bo:
	amdgpu_bo_unreserve(db_obj->obj);
unref_bo:
	amdgpu_bo_unref(&db_obj->obj);
	return r;
}

static int
amdgpu_userq_destroy(struct drm_file *filp, int queue_id)
{
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_userq_mgr *uq_mgr = &fpriv->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_usermode_queue *queue;
	int r = 0;

	cancel_delayed_work_sync(&uq_mgr->resume_work);
	mutex_lock(&uq_mgr->userq_mutex);
	queue = amdgpu_userq_find(uq_mgr, queue_id);
	if (!queue) {
		drm_dbg_driver(adev_to_drm(uq_mgr->adev), "Invalid queue id to destroy\n");
		mutex_unlock(&uq_mgr->userq_mutex);
		return -EINVAL;
	}
	amdgpu_userq_wait_for_last_fence(queue);
	/* Cancel any pending hang detection work and cleanup */
	if (queue->hang_detect_fence) {
		cancel_delayed_work_sync(&queue->hang_detect_work);
		queue->hang_detect_fence = NULL;
	}
	r = amdgpu_bo_reserve(queue->db_obj.obj, true);
	if (!r) {
		amdgpu_bo_unpin(queue->db_obj.obj);
		amdgpu_bo_unreserve(queue->db_obj.obj);
	}
	amdgpu_bo_unref(&queue->db_obj.obj);

	r = amdgpu_bo_reserve(queue->wptr_obj.obj, true);
	if (!r) {
		amdgpu_bo_unpin(queue->wptr_obj.obj);
		amdgpu_bo_unreserve(queue->wptr_obj.obj);
	}
	amdgpu_bo_unref(&queue->wptr_obj.obj);

	atomic_dec(&uq_mgr->userq_count[queue->queue_type]);
#if defined(CONFIG_DEBUG_FS)
	debugfs_remove_recursive(queue->debugfs_queue);
#endif
	amdgpu_userq_detect_and_reset_queues(uq_mgr);
	r = amdgpu_userq_unmap_helper(queue);
	/*TODO: It requires a reset for userq hw unmap error*/
	if (unlikely(r != AMDGPU_USERQ_STATE_UNMAPPED)) {
		drm_warn(adev_to_drm(uq_mgr->adev), "trying to destroy a HW mapping userq\n");
		queue->state = AMDGPU_USERQ_STATE_HUNG;
	}
	amdgpu_userq_cleanup(queue, queue_id);
	mutex_unlock(&uq_mgr->userq_mutex);

	pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);

	return r;
}

static int amdgpu_userq_priority_permit(struct drm_file *filp,
					int priority)
{
	if (priority < AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_HIGH)
		return 0;

	if (capable(CAP_SYS_NICE))
		return 0;

	if (drm_is_current_master(filp))
		return 0;

	return -EACCES;
}

#if defined(CONFIG_DEBUG_FS)
static int amdgpu_mqd_info_read(struct seq_file *m, void *unused)
{
	struct amdgpu_usermode_queue *queue = m->private;
	struct amdgpu_bo *bo;
	int r;

	if (!queue || !queue->mqd.obj)
		return -EINVAL;

	bo = amdgpu_bo_ref(queue->mqd.obj);
	r = amdgpu_bo_reserve(bo, true);
	if (r) {
		amdgpu_bo_unref(&bo);
		return -EINVAL;
	}

	seq_printf(m, "queue_type: %d\n", queue->queue_type);
	seq_printf(m, "mqd_gpu_address: 0x%llx\n", amdgpu_bo_gpu_offset(queue->mqd.obj));

	amdgpu_bo_unreserve(bo);
	amdgpu_bo_unref(&bo);

	return 0;
}

static int amdgpu_mqd_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, amdgpu_mqd_info_read, inode->i_private);
}

static const struct file_operations amdgpu_mqd_info_fops = {
	.owner = THIS_MODULE,
	.open = amdgpu_mqd_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static int
amdgpu_userq_create(struct drm_file *filp, union drm_amdgpu_userq *args)
{
	struct amdgpu_fpriv *fpriv = filp->driver_priv;
	struct amdgpu_userq_mgr *uq_mgr = &fpriv->userq_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	const struct amdgpu_userq_funcs *uq_funcs;
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_db_info db_info;
	char *queue_name;
	bool skip_map_queue;
	u32 qid;
	uint64_t index;
	int r = 0;
	int priority =
		(args->in.flags & AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_MASK) >>
		AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_SHIFT;

	r = amdgpu_userq_priority_permit(filp, priority);
	if (r)
		return r;

	r = pm_runtime_get_sync(adev_to_drm(adev)->dev);
	if (r < 0) {
		drm_file_err(uq_mgr->file, "pm_runtime_get_sync() failed for userqueue create\n");
		pm_runtime_put_autosuspend(adev_to_drm(adev)->dev);
		return r;
	}

	/*
	 * There could be a situation that we are creating a new queue while
	 * the other queues under this UQ_mgr are suspended. So if there is any
	 * resume work pending, wait for it to get done.
	 *
	 * This will also make sure we have a valid eviction fence ready to be used.
	 */
	amdgpu_userq_ensure_ev_fence(&fpriv->userq_mgr, &fpriv->evf_mgr);

	uq_funcs = adev->userq_funcs[args->in.ip_type];
	if (!uq_funcs) {
		drm_file_err(uq_mgr->file, "Usermode queue is not supported for this IP (%u)\n",
			     args->in.ip_type);
		r = -EINVAL;
		goto unlock;
	}

	queue = kzalloc(sizeof(struct amdgpu_usermode_queue), GFP_KERNEL);
	if (!queue) {
		drm_file_err(uq_mgr->file, "Failed to allocate memory for queue\n");
		r = -ENOMEM;
		goto unlock;
	}

	INIT_LIST_HEAD(&queue->userq_va_list);
	queue->doorbell_handle = args->in.doorbell_handle;
	queue->queue_type = args->in.ip_type;
	queue->vm = &fpriv->vm;
	queue->priority = priority;

	db_info.queue_type = queue->queue_type;
	db_info.doorbell_handle = queue->doorbell_handle;
	db_info.db_obj = &queue->db_obj;
	db_info.doorbell_offset = args->in.doorbell_offset;

	queue->userq_mgr = uq_mgr;
	/* Validate the userq virtual address.*/
	if (amdgpu_userq_input_va_validate(adev, queue, args->in.queue_va, args->in.queue_size) ||
	    amdgpu_userq_input_va_validate(adev, queue, args->in.rptr_va, AMDGPU_GPU_PAGE_SIZE) ||
	    amdgpu_userq_input_va_validate(adev, queue, args->in.wptr_va, AMDGPU_GPU_PAGE_SIZE)) {
		r = -EINVAL;
		kfree(queue);
		goto unlock;
	}

	/* Convert relative doorbell offset into absolute doorbell index */
	index = amdgpu_userq_get_doorbell_index(uq_mgr, &db_info, filp);
	if (index == (uint64_t)-EINVAL) {
		drm_file_err(uq_mgr->file, "Failed to get doorbell for queue\n");
		kfree(queue);
		r = -EINVAL;
		goto unlock;
	}

	queue->doorbell_index = index;
	xa_init_flags(&queue->fence_drv_xa, XA_FLAGS_ALLOC);
	r = amdgpu_userq_fence_driver_alloc(adev, queue);
	if (r) {
		drm_file_err(uq_mgr->file, "Failed to alloc fence driver\n");
		goto unlock;
	}

	r = uq_funcs->mqd_create(queue, &args->in);
	if (r) {
		drm_file_err(uq_mgr->file, "Failed to create Queue\n");
		amdgpu_userq_fence_driver_free(queue);
		kfree(queue);
		goto unlock;
	}

	/* Wait for mode-1 reset to complete */
	down_read(&adev->reset_domain->sem);
	r = xa_err(xa_store_irq(&adev->userq_doorbell_xa, index, queue, GFP_KERNEL));
	if (r) {
		kfree(queue);
		up_read(&adev->reset_domain->sem);
		goto unlock;
	}

	r = xa_alloc(&uq_mgr->userq_xa, &qid, queue,
		     XA_LIMIT(1, AMDGPU_MAX_USERQ_COUNT), GFP_KERNEL);
	if (r) {
		drm_file_err(uq_mgr->file, "Failed to allocate a queue id\n");
		amdgpu_userq_fence_driver_free(queue);
		uq_funcs->mqd_destroy(queue);
		kfree(queue);
		r = -ENOMEM;
		up_read(&adev->reset_domain->sem);
		goto unlock;
	}
	up_read(&adev->reset_domain->sem);

	/* don't map the queue if scheduling is halted */
	if (adev->userq_halt_for_enforce_isolation &&
	    ((queue->queue_type == AMDGPU_HW_IP_GFX) ||
	     (queue->queue_type == AMDGPU_HW_IP_COMPUTE)))
		skip_map_queue = true;
	else
		skip_map_queue = false;
	if (!skip_map_queue) {
		r = amdgpu_userq_map_helper(queue);
		if (r) {
			drm_file_err(uq_mgr->file, "Failed to map Queue\n");
			xa_erase(&uq_mgr->userq_xa, qid);
			amdgpu_userq_fence_driver_free(queue);
			uq_funcs->mqd_destroy(queue);
			kfree(queue);
			goto unlock;
		}
	}

	queue_name = kasprintf(GFP_KERNEL, "queue-%d", qid);
	if (!queue_name) {
		r = -ENOMEM;
		goto unlock;
	}

#if defined(CONFIG_DEBUG_FS)
	/* Queue dentry per client to hold MQD information   */
	queue->debugfs_queue = debugfs_create_dir(queue_name, filp->debugfs_client);
	debugfs_create_file("mqd_info", 0444, queue->debugfs_queue, queue, &amdgpu_mqd_info_fops);
#endif
	amdgpu_userq_init_hang_detect_work(queue);
	kfree(queue_name);

	args->out.queue_id = qid;
	atomic_inc(&uq_mgr->userq_count[queue->queue_type]);

unlock:
	mutex_unlock(&uq_mgr->userq_mutex);

	return r;
}

static int amdgpu_userq_input_args_validate(struct drm_device *dev,
					union drm_amdgpu_userq *args,
					struct drm_file *filp)
{
	struct amdgpu_device *adev = drm_to_adev(dev);

	switch (args->in.op) {
	case AMDGPU_USERQ_OP_CREATE:
		if (args->in.flags & ~(AMDGPU_USERQ_CREATE_FLAGS_QUEUE_PRIORITY_MASK |
				       AMDGPU_USERQ_CREATE_FLAGS_QUEUE_SECURE))
			return -EINVAL;
		/* Usermode queues are only supported for GFX IP as of now */
		if (args->in.ip_type != AMDGPU_HW_IP_GFX &&
		    args->in.ip_type != AMDGPU_HW_IP_DMA &&
		    args->in.ip_type != AMDGPU_HW_IP_COMPUTE) {
			drm_file_err(filp, "Usermode queue doesn't support IP type %u\n",
				     args->in.ip_type);
			return -EINVAL;
		}

		if ((args->in.flags & AMDGPU_USERQ_CREATE_FLAGS_QUEUE_SECURE) &&
		    (args->in.ip_type != AMDGPU_HW_IP_GFX) &&
		    (args->in.ip_type != AMDGPU_HW_IP_COMPUTE) &&
		    !amdgpu_is_tmz(adev)) {
			drm_file_err(filp, "Secure only supported on GFX/Compute queues\n");
			return -EINVAL;
		}

		if (args->in.queue_va == AMDGPU_BO_INVALID_OFFSET ||
		    args->in.queue_va == 0 ||
		    args->in.queue_size == 0) {
			drm_file_err(filp, "invalidate userq queue va or size\n");
			return -EINVAL;
		}

		if (!is_power_of_2(args->in.queue_size)) {
			drm_file_err(filp, "Queue size must be a power of 2\n");
			return -EINVAL;
		}

		if (args->in.queue_size < AMDGPU_GPU_PAGE_SIZE) {
			drm_file_err(filp, "Queue size smaller than AMDGPU_GPU_PAGE_SIZE\n");
			return -EINVAL;
		}

		if (!args->in.wptr_va || !args->in.rptr_va) {
			drm_file_err(filp, "invalidate userq queue rptr or wptr\n");
			return -EINVAL;
		}
		break;
	case AMDGPU_USERQ_OP_FREE:
		if (args->in.ip_type ||
		    args->in.doorbell_handle ||
		    args->in.doorbell_offset ||
		    args->in.flags ||
		    args->in.queue_va ||
		    args->in.queue_size ||
		    args->in.rptr_va ||
		    args->in.wptr_va ||
		    args->in.mqd ||
		    args->in.mqd_size)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

bool amdgpu_userq_enabled(struct drm_device *dev)
{
	struct amdgpu_device *adev = drm_to_adev(dev);
	int i;

	for (i = 0; i < AMDGPU_HW_IP_NUM; i++) {
		if (adev->userq_funcs[i])
			return true;
	}

	return false;
}

int amdgpu_userq_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *filp)
{
	union drm_amdgpu_userq *args = data;
	int r;

	if (!amdgpu_userq_enabled(dev))
		return -ENOTSUPP;

	if (amdgpu_userq_input_args_validate(dev, args, filp) < 0)
		return -EINVAL;

	switch (args->in.op) {
	case AMDGPU_USERQ_OP_CREATE:
		r = amdgpu_userq_create(filp, args);
		if (r)
			drm_file_err(filp, "Failed to create usermode queue\n");
		break;

	case AMDGPU_USERQ_OP_FREE:
		r = amdgpu_userq_destroy(filp, args->in.queue_id);
		if (r)
			drm_file_err(filp, "Failed to destroy usermode queue\n");
		break;

	default:
		drm_dbg_driver(dev, "Invalid user queue op specified: %d\n", args->in.op);
		return -EINVAL;
	}

	return r;
}

static int
amdgpu_userq_restore_all(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_usermode_queue *queue;
	unsigned long queue_id;
	int ret = 0, r;

	/* Resume all the queues for this process */
	xa_for_each(&uq_mgr->userq_xa, queue_id, queue) {
		if (!amdgpu_userq_buffer_vas_mapped(queue)) {
			drm_file_err(uq_mgr->file,
				     "trying restore queue without va mapping\n");
			queue->state = AMDGPU_USERQ_STATE_INVALID_VA;
			continue;
		}

		r = amdgpu_userq_restore_helper(queue);
		if (r)
			ret = r;
	}

	if (ret)
		drm_file_err(uq_mgr->file, "Failed to map all the queues\n");
	return ret;
}

static int amdgpu_userq_validate_vm(void *param, struct amdgpu_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };

	amdgpu_bo_placement_from_domain(bo, bo->allowed_domains);
	return ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
}

/* Handle all BOs on the invalidated list, validate them and update the PTs */
static int
amdgpu_userq_bo_validate(struct amdgpu_device *adev, struct drm_exec *exec,
			 struct amdgpu_vm *vm)
{
	struct ttm_operation_ctx ctx = { false, false };
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;
	int ret;

	spin_lock(&vm->status_lock);
	while (!list_empty(&vm->invalidated)) {
		bo_va = list_first_entry(&vm->invalidated,
					 struct amdgpu_bo_va,
					 base.vm_status);
		spin_unlock(&vm->status_lock);

		bo = bo_va->base.bo;
		ret = drm_exec_prepare_obj(exec, &bo->tbo.base, 2);
		if (unlikely(ret))
			return ret;

		amdgpu_bo_placement_from_domain(bo, bo->allowed_domains);
		ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
		if (ret)
			return ret;

		/* This moves the bo_va to the done list */
		ret = amdgpu_vm_bo_update(adev, bo_va, false);
		if (ret)
			return ret;

		spin_lock(&vm->status_lock);
	}
	spin_unlock(&vm->status_lock);

	return 0;
}

/* Make sure the whole VM is ready to be used */
static int
amdgpu_userq_vm_validate(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_fpriv *fpriv = uq_mgr_to_fpriv(uq_mgr);
	bool invalidated = false, new_addition = false;
	struct ttm_operation_ctx ctx = { true, false };
	struct amdgpu_device *adev = uq_mgr->adev;
	struct amdgpu_hmm_range *range;
	struct amdgpu_vm *vm = &fpriv->vm;
	unsigned long key, tmp_key;
	struct amdgpu_bo_va *bo_va;
	struct amdgpu_bo *bo;
	struct drm_exec exec;
	struct xarray xa;
	int ret;

	xa_init(&xa);

retry_lock:
	drm_exec_init(&exec, DRM_EXEC_IGNORE_DUPLICATES, 0);
	drm_exec_until_all_locked(&exec) {
		ret = amdgpu_vm_lock_pd(vm, &exec, 1);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret))
			goto unlock_all;

		ret = amdgpu_vm_lock_done_list(vm, &exec, 1);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret))
			goto unlock_all;

		/* This validates PDs, PTs and per VM BOs */
		ret = amdgpu_vm_validate(adev, vm, NULL,
					 amdgpu_userq_validate_vm,
					 NULL);
		if (unlikely(ret))
			goto unlock_all;

		/* This locks and validates the remaining evicted BOs */
		ret = amdgpu_userq_bo_validate(adev, &exec, vm);
		drm_exec_retry_on_contention(&exec);
		if (unlikely(ret))
			goto unlock_all;
	}

	if (invalidated) {
		xa_for_each(&xa, tmp_key, range) {
			bo = range->bo;
			amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (ret)
				goto unlock_all;

			amdgpu_ttm_tt_set_user_pages(bo->tbo.ttm, range);

			amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_GTT);
			ret = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
			if (ret)
				goto unlock_all;
		}
		invalidated = false;
	}

	ret = amdgpu_vm_handle_moved(adev, vm, NULL);
	if (ret)
		goto unlock_all;

	key = 0;
	/* Validate User Ptr BOs */
	list_for_each_entry(bo_va, &vm->done, base.vm_status) {
		bo = bo_va->base.bo;
		if (!bo)
			continue;

		if (!amdgpu_ttm_tt_is_userptr(bo->tbo.ttm))
			continue;

		range = xa_load(&xa, key);
		if (range && range->bo != bo) {
			xa_erase(&xa, key);
			amdgpu_hmm_range_free(range);
			range = NULL;
		}

		if (!range) {
			range = amdgpu_hmm_range_alloc(bo);
			if (!range) {
				ret = -ENOMEM;
				goto unlock_all;
			}

			xa_store(&xa, key, range, GFP_KERNEL);
			new_addition = true;
		}
		key++;
	}

	if (new_addition) {
		drm_exec_fini(&exec);
		xa_for_each(&xa, tmp_key, range) {
			if (!range)
				continue;
			bo = range->bo;
			ret = amdgpu_ttm_tt_get_user_pages(bo, range);
			if (ret)
				goto unlock_all;
		}

		invalidated = true;
		new_addition = false;
		goto retry_lock;
	}

	ret = amdgpu_vm_update_pdes(adev, vm, false);
	if (ret)
		goto unlock_all;

	/*
	 * We need to wait for all VM updates to finish before restarting the
	 * queues. Using the done list like that is now ok since everything is
	 * locked in place.
	 */
	list_for_each_entry(bo_va, &vm->done, base.vm_status)
		dma_fence_wait(bo_va->last_pt_update, false);
	dma_fence_wait(vm->last_update, false);

	ret = amdgpu_eviction_fence_replace_fence(&fpriv->evf_mgr, &exec);
	if (ret)
		drm_file_err(uq_mgr->file, "Failed to replace eviction fence\n");

unlock_all:
	drm_exec_fini(&exec);
	xa_for_each(&xa, tmp_key, range) {
		if (!range)
			continue;
		bo = range->bo;
		amdgpu_hmm_range_free(range);
	}
	xa_destroy(&xa);
	return ret;
}

static void amdgpu_userq_restore_worker(struct work_struct *work)
{
	struct amdgpu_userq_mgr *uq_mgr = work_to_uq_mgr(work, resume_work.work);
	struct amdgpu_fpriv *fpriv = uq_mgr_to_fpriv(uq_mgr);
	int ret;

	flush_delayed_work(&fpriv->evf_mgr.suspend_work);

	mutex_lock(&uq_mgr->userq_mutex);

	ret = amdgpu_userq_vm_validate(uq_mgr);
	if (ret) {
		drm_file_err(uq_mgr->file, "Failed to validate BOs to restore\n");
		goto unlock;
	}

	ret = amdgpu_userq_restore_all(uq_mgr);
	if (ret) {
		drm_file_err(uq_mgr->file, "Failed to restore all queues\n");
		goto unlock;
	}

unlock:
	mutex_unlock(&uq_mgr->userq_mutex);
}

static int
amdgpu_userq_evict_all(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_usermode_queue *queue;
	unsigned long queue_id;
	int ret = 0, r;

	amdgpu_userq_detect_and_reset_queues(uq_mgr);
	/* Try to unmap all the queues in this process ctx */
	xa_for_each(&uq_mgr->userq_xa, queue_id, queue) {
		r = amdgpu_userq_preempt_helper(queue);
		if (r)
			ret = r;
	}

	if (ret)
		drm_file_err(uq_mgr->file, "Couldn't unmap all the queues\n");
	return ret;
}

void amdgpu_userq_reset_work(struct work_struct *work)
{
	struct amdgpu_device *adev = container_of(work, struct amdgpu_device,
						  userq_reset_work);
	struct amdgpu_reset_context reset_context;

	memset(&reset_context, 0, sizeof(reset_context));

	reset_context.method = AMD_RESET_METHOD_NONE;
	reset_context.reset_req_dev = adev;
	reset_context.src = AMDGPU_RESET_SRC_USERQ;
	set_bit(AMDGPU_NEED_FULL_RESET, &reset_context.flags);
	/*set_bit(AMDGPU_SKIP_COREDUMP, &reset_context.flags);*/

	amdgpu_device_gpu_recover(adev, NULL, &reset_context);
}

static int
amdgpu_userq_wait_for_signal(struct amdgpu_userq_mgr *uq_mgr)
{
	struct amdgpu_usermode_queue *queue;
	unsigned long queue_id;
	int ret;

	xa_for_each(&uq_mgr->userq_xa, queue_id, queue) {
		struct dma_fence *f = queue->last_fence;

		if (!f || dma_fence_is_signaled(f))
			continue;
		ret = dma_fence_wait_timeout(f, true, msecs_to_jiffies(100));
		if (ret <= 0) {
			drm_file_err(uq_mgr->file, "Timed out waiting for fence=%llu:%llu\n",
				     f->context, f->seqno);
			return -ETIMEDOUT;
		}
	}

	return 0;
}

void
amdgpu_userq_evict(struct amdgpu_userq_mgr *uq_mgr,
		   struct amdgpu_eviction_fence *ev_fence)
{
	struct amdgpu_fpriv *fpriv = uq_mgr_to_fpriv(uq_mgr);
	struct amdgpu_eviction_fence_mgr *evf_mgr = &fpriv->evf_mgr;
	struct amdgpu_device *adev = uq_mgr->adev;
	int ret;

	/* Wait for any pending userqueue fence work to finish */
	ret = amdgpu_userq_wait_for_signal(uq_mgr);
	if (ret)
		dev_err(adev->dev, "Not evicting userqueue, timeout waiting for work\n");

	ret = amdgpu_userq_evict_all(uq_mgr);
	if (ret)
		dev_err(adev->dev, "Failed to evict userqueue\n");

	/* Signal current eviction fence */
	amdgpu_eviction_fence_signal(evf_mgr, ev_fence);

	if (evf_mgr->fd_closing) {
		cancel_delayed_work_sync(&uq_mgr->resume_work);
		return;
	}

	/* Schedule a resume work */
	schedule_delayed_work(&uq_mgr->resume_work, 0);
}

int amdgpu_userq_mgr_init(struct amdgpu_userq_mgr *userq_mgr, struct drm_file *file_priv,
			  struct amdgpu_device *adev)
{
	mutex_init(&userq_mgr->userq_mutex);
	xa_init_flags(&userq_mgr->userq_xa, XA_FLAGS_ALLOC);
	userq_mgr->adev = adev;
	userq_mgr->file = file_priv;

	INIT_DELAYED_WORK(&userq_mgr->resume_work, amdgpu_userq_restore_worker);
	return 0;
}

void amdgpu_userq_mgr_fini(struct amdgpu_userq_mgr *userq_mgr)
{
	struct amdgpu_usermode_queue *queue;
	unsigned long queue_id;

	cancel_delayed_work_sync(&userq_mgr->resume_work);

	mutex_lock(&userq_mgr->userq_mutex);
	amdgpu_userq_detect_and_reset_queues(userq_mgr);
	xa_for_each(&userq_mgr->userq_xa, queue_id, queue) {
		amdgpu_userq_wait_for_last_fence(queue);
		amdgpu_userq_unmap_helper(queue);
		amdgpu_userq_cleanup(queue, queue_id);
	}

	xa_destroy(&userq_mgr->userq_xa);
	mutex_unlock(&userq_mgr->userq_mutex);
	mutex_destroy(&userq_mgr->userq_mutex);
}

int amdgpu_userq_suspend(struct amdgpu_device *adev)
{
	u32 ip_mask = amdgpu_userq_get_supported_ip_mask(adev);
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_userq_mgr *uqm;
	unsigned long queue_id;
	int r;

	if (!ip_mask)
		return 0;

	xa_for_each(&adev->userq_doorbell_xa, queue_id, queue) {
		uqm = queue->userq_mgr;
		cancel_delayed_work_sync(&uqm->resume_work);
		guard(mutex)(&uqm->userq_mutex);
		amdgpu_userq_detect_and_reset_queues(uqm);
		if (adev->in_s0ix)
			r = amdgpu_userq_preempt_helper(queue);
		else
			r = amdgpu_userq_unmap_helper(queue);
		if (r)
			return r;
	}
	return 0;
}

int amdgpu_userq_resume(struct amdgpu_device *adev)
{
	u32 ip_mask = amdgpu_userq_get_supported_ip_mask(adev);
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_userq_mgr *uqm;
	unsigned long queue_id;
	int r;

	if (!ip_mask)
		return 0;

	xa_for_each(&adev->userq_doorbell_xa, queue_id, queue) {
		uqm = queue->userq_mgr;
		guard(mutex)(&uqm->userq_mutex);
		if (adev->in_s0ix)
			r = amdgpu_userq_restore_helper(queue);
		else
			r = amdgpu_userq_map_helper(queue);
		if (r)
			return r;
	}

	return 0;
}

int amdgpu_userq_stop_sched_for_enforce_isolation(struct amdgpu_device *adev,
						  u32 idx)
{
	u32 ip_mask = amdgpu_userq_get_supported_ip_mask(adev);
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_userq_mgr *uqm;
	unsigned long queue_id;
	int ret = 0, r;

	/* only need to stop gfx/compute */
	if (!(ip_mask & ((1 << AMDGPU_HW_IP_GFX) | (1 << AMDGPU_HW_IP_COMPUTE))))
		return 0;

	if (adev->userq_halt_for_enforce_isolation)
		dev_warn(adev->dev, "userq scheduling already stopped!\n");
	adev->userq_halt_for_enforce_isolation = true;
	xa_for_each(&adev->userq_doorbell_xa, queue_id, queue) {
		uqm = queue->userq_mgr;
		cancel_delayed_work_sync(&uqm->resume_work);
		mutex_lock(&uqm->userq_mutex);
		if (((queue->queue_type == AMDGPU_HW_IP_GFX) ||
		     (queue->queue_type == AMDGPU_HW_IP_COMPUTE)) &&
		    (queue->xcp_id == idx)) {
			amdgpu_userq_detect_and_reset_queues(uqm);
			r = amdgpu_userq_preempt_helper(queue);
			if (r)
				ret = r;
		}
		mutex_unlock(&uqm->userq_mutex);
	}

	return ret;
}

int amdgpu_userq_start_sched_for_enforce_isolation(struct amdgpu_device *adev,
						   u32 idx)
{
	u32 ip_mask = amdgpu_userq_get_supported_ip_mask(adev);
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_userq_mgr *uqm;
	unsigned long queue_id;
	int ret = 0, r;

	/* only need to stop gfx/compute */
	if (!(ip_mask & ((1 << AMDGPU_HW_IP_GFX) | (1 << AMDGPU_HW_IP_COMPUTE))))
		return 0;

	if (!adev->userq_halt_for_enforce_isolation)
		dev_warn(adev->dev, "userq scheduling already started!\n");
	adev->userq_halt_for_enforce_isolation = false;
	xa_for_each(&adev->userq_doorbell_xa, queue_id, queue) {
		uqm = queue->userq_mgr;
		mutex_lock(&uqm->userq_mutex);
			if (((queue->queue_type == AMDGPU_HW_IP_GFX) ||
			     (queue->queue_type == AMDGPU_HW_IP_COMPUTE)) &&
			    (queue->xcp_id == idx)) {
			r = amdgpu_userq_restore_helper(queue);
			if (r)
				ret = r;
			}
		mutex_unlock(&uqm->userq_mutex);
	}

	return ret;
}

int amdgpu_userq_gem_va_unmap_validate(struct amdgpu_device *adev,
				       struct amdgpu_bo_va_mapping *mapping,
				       uint64_t saddr)
{
	u32 ip_mask = amdgpu_userq_get_supported_ip_mask(adev);
	struct amdgpu_bo_va *bo_va = mapping->bo_va;
	struct dma_resv *resv = bo_va->base.bo->tbo.base.resv;
	int ret = 0;

	if (!ip_mask)
		return 0;

	dev_warn_once(adev->dev, "now unmapping a vital queue va:%llx\n", saddr);
	/**
	 * The userq VA mapping reservation should include the eviction fence,
	 * if the eviction fence can't signal successfully during unmapping,
	 * then driver will warn to flag this improper unmap of the userq VA.
	 * Note: The eviction fence may be attached to different BOs, and this
	 * unmap is only for one kind of userq VAs, so at this point suppose
	 * the eviction fence is always unsignaled.
	 */
	if (!dma_resv_test_signaled(resv, DMA_RESV_USAGE_BOOKKEEP)) {
		ret = dma_resv_wait_timeout(resv, DMA_RESV_USAGE_BOOKKEEP, true,
					    MAX_SCHEDULE_TIMEOUT);
		if (ret <= 0)
			return -EBUSY;
	}

	return 0;
}

void amdgpu_userq_pre_reset(struct amdgpu_device *adev)
{
	const struct amdgpu_userq_funcs *userq_funcs;
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_userq_mgr *uqm;
	unsigned long queue_id;

	xa_for_each(&adev->userq_doorbell_xa, queue_id, queue) {
		uqm = queue->userq_mgr;
		cancel_delayed_work_sync(&uqm->resume_work);
		if (queue->state == AMDGPU_USERQ_STATE_MAPPED) {
			amdgpu_userq_wait_for_last_fence(queue);
			userq_funcs = adev->userq_funcs[queue->queue_type];
			userq_funcs->unmap(queue);
			/* just mark all queues as hung at this point.
			 * if unmap succeeds, we could map again
			 * in amdgpu_userq_post_reset() if vram is not lost
			 */
			queue->state = AMDGPU_USERQ_STATE_HUNG;
			amdgpu_userq_fence_driver_force_completion(queue);
		}
	}
}

int amdgpu_userq_post_reset(struct amdgpu_device *adev, bool vram_lost)
{
	/* if any queue state is AMDGPU_USERQ_STATE_UNMAPPED
	 * at this point, we should be able to map it again
	 * and continue if vram is not lost.
	 */
	struct amdgpu_usermode_queue *queue;
	const struct amdgpu_userq_funcs *userq_funcs;
	unsigned long queue_id;
	int r = 0;

	xa_for_each(&adev->userq_doorbell_xa, queue_id, queue) {
		if (queue->state == AMDGPU_USERQ_STATE_HUNG && !vram_lost) {
			userq_funcs = adev->userq_funcs[queue->queue_type];
			/* Re-map queue */
			r = userq_funcs->map(queue);
			if (r) {
				dev_err(adev->dev, "Failed to remap queue %ld\n", queue_id);
				continue;
			}
			queue->state = AMDGPU_USERQ_STATE_MAPPED;
		}
	}

	return r;
}
