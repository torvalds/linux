// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_vm.h"

#include <linux/dma-fence-array.h>

#include <drm/drm_exec.h>
#include <drm/drm_print.h>
#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/xe_drm.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_drm_client.h"
#include "xe_exec_queue.h"
#include "xe_gt.h"
#include "xe_gt_pagefault.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_migrate.h"
#include "xe_pm.h"
#include "xe_preempt_fence.h"
#include "xe_pt.h"
#include "xe_res_cursor.h"
#include "xe_sync.h"
#include "xe_trace.h"
#include "generated/xe_wa_oob.h"
#include "xe_wa.h"

#define TEST_VM_ASYNC_OPS_ERROR

static struct drm_gem_object *xe_vm_obj(struct xe_vm *vm)
{
	return vm->gpuvm.r_obj;
}

/**
 * xe_vma_userptr_check_repin() - Advisory check for repin needed
 * @vma: The userptr vma
 *
 * Check if the userptr vma has been invalidated since last successful
 * repin. The check is advisory only and can the function can be called
 * without the vm->userptr.notifier_lock held. There is no guarantee that the
 * vma userptr will remain valid after a lockless check, so typically
 * the call needs to be followed by a proper check under the notifier_lock.
 *
 * Return: 0 if userptr vma is valid, -EAGAIN otherwise; repin recommended.
 */
int xe_vma_userptr_check_repin(struct xe_vma *vma)
{
	return mmu_interval_check_retry(&vma->userptr.notifier,
					vma->userptr.notifier_seq) ?
		-EAGAIN : 0;
}

int xe_vma_userptr_pin_pages(struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_device *xe = vm->xe;
	const unsigned long num_pages = xe_vma_size(vma) >> PAGE_SHIFT;
	struct page **pages;
	bool in_kthread = !current->mm;
	unsigned long notifier_seq;
	int pinned, ret, i;
	bool read_only = xe_vma_read_only(vma);

	lockdep_assert_held(&vm->lock);
	xe_assert(xe, xe_vma_is_userptr(vma));
retry:
	if (vma->gpuva.flags & XE_VMA_DESTROYED)
		return 0;

	notifier_seq = mmu_interval_read_begin(&vma->userptr.notifier);
	if (notifier_seq == vma->userptr.notifier_seq)
		return 0;

	pages = kvmalloc_array(num_pages, sizeof(*pages), GFP_KERNEL);
	if (!pages)
		return -ENOMEM;

	if (vma->userptr.sg) {
		dma_unmap_sgtable(xe->drm.dev,
				  vma->userptr.sg,
				  read_only ? DMA_TO_DEVICE :
				  DMA_BIDIRECTIONAL, 0);
		sg_free_table(vma->userptr.sg);
		vma->userptr.sg = NULL;
	}

	pinned = ret = 0;
	if (in_kthread) {
		if (!mmget_not_zero(vma->userptr.notifier.mm)) {
			ret = -EFAULT;
			goto mm_closed;
		}
		kthread_use_mm(vma->userptr.notifier.mm);
	}

	while (pinned < num_pages) {
		ret = get_user_pages_fast(xe_vma_userptr(vma) +
					  pinned * PAGE_SIZE,
					  num_pages - pinned,
					  read_only ? 0 : FOLL_WRITE,
					  &pages[pinned]);
		if (ret < 0) {
			if (in_kthread)
				ret = 0;
			break;
		}

		pinned += ret;
		ret = 0;
	}

	if (in_kthread) {
		kthread_unuse_mm(vma->userptr.notifier.mm);
		mmput(vma->userptr.notifier.mm);
	}
mm_closed:
	if (ret)
		goto out;

	ret = sg_alloc_table_from_pages_segment(&vma->userptr.sgt, pages,
						pinned, 0,
						(u64)pinned << PAGE_SHIFT,
						xe_sg_segment_size(xe->drm.dev),
						GFP_KERNEL);
	if (ret) {
		vma->userptr.sg = NULL;
		goto out;
	}
	vma->userptr.sg = &vma->userptr.sgt;

	ret = dma_map_sgtable(xe->drm.dev, vma->userptr.sg,
			      read_only ? DMA_TO_DEVICE :
			      DMA_BIDIRECTIONAL,
			      DMA_ATTR_SKIP_CPU_SYNC |
			      DMA_ATTR_NO_KERNEL_MAPPING);
	if (ret) {
		sg_free_table(vma->userptr.sg);
		vma->userptr.sg = NULL;
		goto out;
	}

	for (i = 0; i < pinned; ++i) {
		if (!read_only) {
			lock_page(pages[i]);
			set_page_dirty(pages[i]);
			unlock_page(pages[i]);
		}

		mark_page_accessed(pages[i]);
	}

out:
	release_pages(pages, pinned);
	kvfree(pages);

	if (!(ret < 0)) {
		vma->userptr.notifier_seq = notifier_seq;
		if (xe_vma_userptr_check_repin(vma) == -EAGAIN)
			goto retry;
	}

	return ret < 0 ? ret : 0;
}

static bool preempt_fences_waiting(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		if (!q->compute.pfence ||
		    (q->compute.pfence && test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
						   &q->compute.pfence->flags))) {
			return true;
		}
	}

	return false;
}

static void free_preempt_fences(struct list_head *list)
{
	struct list_head *link, *next;

	list_for_each_safe(link, next, list)
		xe_preempt_fence_free(to_preempt_fence_from_link(link));
}

static int alloc_preempt_fences(struct xe_vm *vm, struct list_head *list,
				unsigned int *count)
{
	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	if (*count >= vm->preempt.num_exec_queues)
		return 0;

	for (; *count < vm->preempt.num_exec_queues; ++(*count)) {
		struct xe_preempt_fence *pfence = xe_preempt_fence_alloc();

		if (IS_ERR(pfence))
			return PTR_ERR(pfence);

		list_move_tail(xe_preempt_fence_link(pfence), list);
	}

	return 0;
}

static int wait_for_existing_preempt_fences(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		if (q->compute.pfence) {
			long timeout = dma_fence_wait(q->compute.pfence, false);

			if (timeout < 0)
				return -ETIME;
			dma_fence_put(q->compute.pfence);
			q->compute.pfence = NULL;
		}
	}

	return 0;
}

static bool xe_vm_is_idle(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	xe_vm_assert_held(vm);
	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		if (!xe_exec_queue_is_idle(q))
			return false;
	}

	return true;
}

static void arm_preempt_fences(struct xe_vm *vm, struct list_head *list)
{
	struct list_head *link;
	struct xe_exec_queue *q;

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		struct dma_fence *fence;

		link = list->next;
		xe_assert(vm->xe, link != list);

		fence = xe_preempt_fence_arm(to_preempt_fence_from_link(link),
					     q, q->compute.context,
					     ++q->compute.seqno);
		dma_fence_put(q->compute.pfence);
		q->compute.pfence = fence;
	}
}

static int add_preempt_fences(struct xe_vm *vm, struct xe_bo *bo)
{
	struct xe_exec_queue *q;
	int err;

	err = xe_bo_lock(bo, true);
	if (err)
		return err;

	err = dma_resv_reserve_fences(bo->ttm.base.resv, vm->preempt.num_exec_queues);
	if (err)
		goto out_unlock;

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link)
		if (q->compute.pfence) {
			dma_resv_add_fence(bo->ttm.base.resv,
					   q->compute.pfence,
					   DMA_RESV_USAGE_BOOKKEEP);
		}

out_unlock:
	xe_bo_unlock(bo);
	return err;
}

/**
 * xe_vm_fence_all_extobjs() - Add a fence to vm's external objects' resv
 * @vm: The vm.
 * @fence: The fence to add.
 * @usage: The resv usage for the fence.
 *
 * Loops over all of the vm's external object bindings and adds a @fence
 * with the given @usage to all of the external object's reservation
 * objects.
 */
void xe_vm_fence_all_extobjs(struct xe_vm *vm, struct dma_fence *fence,
			     enum dma_resv_usage usage)
{
	struct xe_vma *vma;

	list_for_each_entry(vma, &vm->extobj.list, extobj.link)
		dma_resv_add_fence(xe_vma_bo(vma)->ttm.base.resv, fence, usage);
}

static void resume_and_reinstall_preempt_fences(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link) {
		q->ops->resume(q);

		dma_resv_add_fence(xe_vm_resv(vm), q->compute.pfence,
				   DMA_RESV_USAGE_BOOKKEEP);
		xe_vm_fence_all_extobjs(vm, q->compute.pfence,
					DMA_RESV_USAGE_BOOKKEEP);
	}
}

int xe_vm_add_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	struct drm_exec exec;
	struct dma_fence *pfence;
	int err;
	bool wait;

	xe_assert(vm->xe, xe_vm_in_compute_mode(vm));

	down_write(&vm->lock);
	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT);
	drm_exec_until_all_locked(&exec) {
		err = xe_vm_lock_dma_resv(vm, &exec, 1, true);
		drm_exec_retry_on_contention(&exec);
		if (err)
			goto out_unlock;
	}

	pfence = xe_preempt_fence_create(q, q->compute.context,
					 ++q->compute.seqno);
	if (!pfence) {
		err = -ENOMEM;
		goto out_unlock;
	}

	list_add(&q->compute.link, &vm->preempt.exec_queues);
	++vm->preempt.num_exec_queues;
	q->compute.pfence = pfence;

	down_read(&vm->userptr.notifier_lock);

	dma_resv_add_fence(xe_vm_resv(vm), pfence,
			   DMA_RESV_USAGE_BOOKKEEP);

	xe_vm_fence_all_extobjs(vm, pfence, DMA_RESV_USAGE_BOOKKEEP);

	/*
	 * Check to see if a preemption on VM is in flight or userptr
	 * invalidation, if so trigger this preempt fence to sync state with
	 * other preempt fences on the VM.
	 */
	wait = __xe_vm_userptr_needs_repin(vm) || preempt_fences_waiting(vm);
	if (wait)
		dma_fence_enable_sw_signaling(pfence);

	up_read(&vm->userptr.notifier_lock);

out_unlock:
	drm_exec_fini(&exec);
	up_write(&vm->lock);

	return err;
}

/**
 * xe_vm_remove_compute_exec_queue() - Remove compute exec queue from VM
 * @vm: The VM.
 * @q: The exec_queue
 */
void xe_vm_remove_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	if (!xe_vm_in_compute_mode(vm))
		return;

	down_write(&vm->lock);
	list_del(&q->compute.link);
	--vm->preempt.num_exec_queues;
	if (q->compute.pfence) {
		dma_fence_enable_sw_signaling(q->compute.pfence);
		dma_fence_put(q->compute.pfence);
		q->compute.pfence = NULL;
	}
	up_write(&vm->lock);
}

/**
 * __xe_vm_userptr_needs_repin() - Check whether the VM does have userptrs
 * that need repinning.
 * @vm: The VM.
 *
 * This function checks for whether the VM has userptrs that need repinning,
 * and provides a release-type barrier on the userptr.notifier_lock after
 * checking.
 *
 * Return: 0 if there are no userptrs needing repinning, -EAGAIN if there are.
 */
int __xe_vm_userptr_needs_repin(struct xe_vm *vm)
{
	lockdep_assert_held_read(&vm->userptr.notifier_lock);

	return (list_empty(&vm->userptr.repin_list) &&
		list_empty(&vm->userptr.invalidated)) ? 0 : -EAGAIN;
}

/**
 * xe_vm_lock_dma_resv() - Lock the vm dma_resv object and the dma_resv
 * objects of the vm's external buffer objects.
 * @vm: The vm.
 * @exec: Pointer to a struct drm_exec locking context.
 * @num_shared: Number of dma-fence slots to reserve in the locked objects.
 * @lock_vm: Lock also the vm's dma_resv.
 *
 * Locks the vm dma-resv objects and all the dma-resv objects of the
 * buffer objects on the vm external object list.
 *
 * Return: 0 on success, Negative error code on error. In particular if
 * @intr is set to true, -EINTR or -ERESTARTSYS may be returned.
 */
int xe_vm_lock_dma_resv(struct xe_vm *vm, struct drm_exec *exec,
			unsigned int num_shared, bool lock_vm)
{
	struct xe_vma *vma, *next;
	int err = 0;

	lockdep_assert_held(&vm->lock);

	if (lock_vm) {
		err = drm_exec_prepare_obj(exec, xe_vm_obj(vm), num_shared);
		if (err)
			return err;
	}

	list_for_each_entry(vma, &vm->extobj.list, extobj.link) {
		err = drm_exec_prepare_obj(exec, &xe_vma_bo(vma)->ttm.base, num_shared);
		if (err)
			return err;
	}

	spin_lock(&vm->notifier.list_lock);
	list_for_each_entry_safe(vma, next, &vm->notifier.rebind_list,
				 notifier.rebind_link) {
		xe_bo_assert_held(xe_vma_bo(vma));

		list_del_init(&vma->notifier.rebind_link);
		if (vma->tile_present && !(vma->gpuva.flags & XE_VMA_DESTROYED))
			list_move_tail(&vma->combined_links.rebind,
				       &vm->rebind_list);
	}
	spin_unlock(&vm->notifier.list_lock);

	return 0;
}

#define XE_VM_REBIND_RETRY_TIMEOUT_MS 1000

static void xe_vm_kill(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);

	xe_vm_lock(vm, false);
	vm->flags |= XE_VM_FLAG_BANNED;
	trace_xe_vm_kill(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, compute.link)
		q->ops->kill(q);
	xe_vm_unlock(vm);

	/* TODO: Inform user the VM is banned */
}

/**
 * xe_vm_validate_should_retry() - Whether to retry after a validate error.
 * @exec: The drm_exec object used for locking before validation.
 * @err: The error returned from ttm_bo_validate().
 * @end: A ktime_t cookie that should be set to 0 before first use and
 * that should be reused on subsequent calls.
 *
 * With multiple active VMs, under memory pressure, it is possible that
 * ttm_bo_validate() run into -EDEADLK and in such case returns -ENOMEM.
 * Until ttm properly handles locking in such scenarios, best thing the
 * driver can do is retry with a timeout. Check if that is necessary, and
 * if so unlock the drm_exec's objects while keeping the ticket to prepare
 * for a rerun.
 *
 * Return: true if a retry after drm_exec_init() is recommended;
 * false otherwise.
 */
bool xe_vm_validate_should_retry(struct drm_exec *exec, int err, ktime_t *end)
{
	ktime_t cur;

	if (err != -ENOMEM)
		return false;

	cur = ktime_get();
	*end = *end ? : ktime_add_ms(cur, XE_VM_REBIND_RETRY_TIMEOUT_MS);
	if (!ktime_before(cur, *end))
		return false;

	/*
	 * We would like to keep the ticket here with
	 * drm_exec_unlock_all(), but WW mutex asserts currently
	 * stop us from that. In any case this function could go away
	 * with proper TTM -EDEADLK handling.
	 */
	drm_exec_fini(exec);

	msleep(20);
	return true;
}

static int xe_preempt_work_begin(struct drm_exec *exec, struct xe_vm *vm,
				 bool *done)
{
	struct xe_vma *vma;
	int err;

	/*
	 * 1 fence for each preempt fence plus a fence for each tile from a
	 * possible rebind
	 */
	err = drm_exec_prepare_obj(exec, xe_vm_obj(vm),
				   vm->preempt.num_exec_queues +
				   vm->xe->info.tile_count);
	if (err)
		return err;

	if (xe_vm_is_idle(vm)) {
		vm->preempt.rebind_deactivated = true;
		*done = true;
		return 0;
	}

	if (!preempt_fences_waiting(vm)) {
		*done = true;
		return 0;
	}

	err = xe_vm_lock_dma_resv(vm, exec, vm->preempt.num_exec_queues, false);
	if (err)
		return err;

	err = wait_for_existing_preempt_fences(vm);
	if (err)
		return err;

	list_for_each_entry(vma, &vm->rebind_list, combined_links.rebind) {
		if (xe_vma_has_no_bo(vma) ||
		    vma->gpuva.flags & XE_VMA_DESTROYED)
			continue;

		err = xe_bo_validate(xe_vma_bo(vma), vm, false);
		if (err)
			break;
	}

	return err;
}

static void preempt_rebind_work_func(struct work_struct *w)
{
	struct xe_vm *vm = container_of(w, struct xe_vm, preempt.rebind_work);
	struct drm_exec exec;
	struct dma_fence *rebind_fence;
	unsigned int fence_count = 0;
	LIST_HEAD(preempt_fences);
	ktime_t end = 0;
	int err;
	long wait;
	int __maybe_unused tries = 0;

	xe_assert(vm->xe, xe_vm_in_compute_mode(vm));
	trace_xe_vm_rebind_worker_enter(vm);

	down_write(&vm->lock);

	if (xe_vm_is_closed_or_banned(vm)) {
		up_write(&vm->lock);
		trace_xe_vm_rebind_worker_exit(vm);
		return;
	}

retry:
	if (vm->async_ops.error)
		goto out_unlock_outer;

	/*
	 * Extreme corner where we exit a VM error state with a munmap style VM
	 * unbind inflight which requires a rebind. In this case the rebind
	 * needs to install some fences into the dma-resv slots. The worker to
	 * do this queued, let that worker make progress by dropping vm->lock
	 * and trying this again.
	 */
	if (vm->async_ops.munmap_rebind_inflight) {
		up_write(&vm->lock);
		flush_work(&vm->async_ops.work);
		goto retry;
	}

	if (xe_vm_userptr_check_repin(vm)) {
		err = xe_vm_userptr_pin(vm);
		if (err)
			goto out_unlock_outer;
	}

	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT);

	drm_exec_until_all_locked(&exec) {
		bool done = false;

		err = xe_preempt_work_begin(&exec, vm, &done);
		drm_exec_retry_on_contention(&exec);
		if (err && xe_vm_validate_should_retry(&exec, err, &end)) {
			err = -EAGAIN;
			goto out_unlock_outer;
		}
		if (err || done)
			goto out_unlock;
	}

	err = alloc_preempt_fences(vm, &preempt_fences, &fence_count);
	if (err)
		goto out_unlock;

	rebind_fence = xe_vm_rebind(vm, true);
	if (IS_ERR(rebind_fence)) {
		err = PTR_ERR(rebind_fence);
		goto out_unlock;
	}

	if (rebind_fence) {
		dma_fence_wait(rebind_fence, false);
		dma_fence_put(rebind_fence);
	}

	/* Wait on munmap style VM unbinds */
	wait = dma_resv_wait_timeout(xe_vm_resv(vm),
				     DMA_RESV_USAGE_KERNEL,
				     false, MAX_SCHEDULE_TIMEOUT);
	if (wait <= 0) {
		err = -ETIME;
		goto out_unlock;
	}

#define retry_required(__tries, __vm) \
	(IS_ENABLED(CONFIG_DRM_XE_USERPTR_INVAL_INJECT) ? \
	(!(__tries)++ || __xe_vm_userptr_needs_repin(__vm)) : \
	__xe_vm_userptr_needs_repin(__vm))

	down_read(&vm->userptr.notifier_lock);
	if (retry_required(tries, vm)) {
		up_read(&vm->userptr.notifier_lock);
		err = -EAGAIN;
		goto out_unlock;
	}

#undef retry_required

	spin_lock(&vm->xe->ttm.lru_lock);
	ttm_lru_bulk_move_tail(&vm->lru_bulk_move);
	spin_unlock(&vm->xe->ttm.lru_lock);

	/* Point of no return. */
	arm_preempt_fences(vm, &preempt_fences);
	resume_and_reinstall_preempt_fences(vm);
	up_read(&vm->userptr.notifier_lock);

out_unlock:
	drm_exec_fini(&exec);
out_unlock_outer:
	if (err == -EAGAIN) {
		trace_xe_vm_rebind_worker_retry(vm);
		goto retry;
	}

	if (err) {
		drm_warn(&vm->xe->drm, "VM worker error: %d\n", err);
		xe_vm_kill(vm);
	}
	up_write(&vm->lock);

	free_preempt_fences(&preempt_fences);

	trace_xe_vm_rebind_worker_exit(vm);
}

static bool vma_userptr_invalidate(struct mmu_interval_notifier *mni,
				   const struct mmu_notifier_range *range,
				   unsigned long cur_seq)
{
	struct xe_vma *vma = container_of(mni, struct xe_vma, userptr.notifier);
	struct xe_vm *vm = xe_vma_vm(vma);
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	long err;

	xe_assert(vm->xe, xe_vma_is_userptr(vma));
	trace_xe_vma_userptr_invalidate(vma);

	if (!mmu_notifier_range_blockable(range))
		return false;

	down_write(&vm->userptr.notifier_lock);
	mmu_interval_set_seq(mni, cur_seq);

	/* No need to stop gpu access if the userptr is not yet bound. */
	if (!vma->userptr.initial_bind) {
		up_write(&vm->userptr.notifier_lock);
		return true;
	}

	/*
	 * Tell exec and rebind worker they need to repin and rebind this
	 * userptr.
	 */
	if (!xe_vm_in_fault_mode(vm) &&
	    !(vma->gpuva.flags & XE_VMA_DESTROYED) && vma->tile_present) {
		spin_lock(&vm->userptr.invalidated_lock);
		list_move_tail(&vma->userptr.invalidate_link,
			       &vm->userptr.invalidated);
		spin_unlock(&vm->userptr.invalidated_lock);
	}

	up_write(&vm->userptr.notifier_lock);

	/*
	 * Preempt fences turn into schedule disables, pipeline these.
	 * Note that even in fault mode, we need to wait for binds and
	 * unbinds to complete, and those are attached as BOOKMARK fences
	 * to the vm.
	 */
	dma_resv_iter_begin(&cursor, xe_vm_resv(vm),
			    DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, fence)
		dma_fence_enable_sw_signaling(fence);
	dma_resv_iter_end(&cursor);

	err = dma_resv_wait_timeout(xe_vm_resv(vm),
				    DMA_RESV_USAGE_BOOKKEEP,
				    false, MAX_SCHEDULE_TIMEOUT);
	XE_WARN_ON(err <= 0);

	if (xe_vm_in_fault_mode(vm)) {
		err = xe_vm_invalidate_vma(vma);
		XE_WARN_ON(err);
	}

	trace_xe_vma_userptr_invalidate_complete(vma);

	return true;
}

static const struct mmu_interval_notifier_ops vma_userptr_notifier_ops = {
	.invalidate = vma_userptr_invalidate,
};

int xe_vm_userptr_pin(struct xe_vm *vm)
{
	struct xe_vma *vma, *next;
	int err = 0;
	LIST_HEAD(tmp_evict);

	lockdep_assert_held_write(&vm->lock);

	/* Collect invalidated userptrs */
	spin_lock(&vm->userptr.invalidated_lock);
	list_for_each_entry_safe(vma, next, &vm->userptr.invalidated,
				 userptr.invalidate_link) {
		list_del_init(&vma->userptr.invalidate_link);
		if (list_empty(&vma->combined_links.userptr))
			list_move_tail(&vma->combined_links.userptr,
				       &vm->userptr.repin_list);
	}
	spin_unlock(&vm->userptr.invalidated_lock);

	/* Pin and move to temporary list */
	list_for_each_entry_safe(vma, next, &vm->userptr.repin_list,
				 combined_links.userptr) {
		err = xe_vma_userptr_pin_pages(vma);
		if (err < 0)
			goto out_err;

		list_move_tail(&vma->combined_links.userptr, &tmp_evict);
	}

	/* Take lock and move to rebind_list for rebinding. */
	err = dma_resv_lock_interruptible(xe_vm_resv(vm), NULL);
	if (err)
		goto out_err;

	list_for_each_entry_safe(vma, next, &tmp_evict, combined_links.userptr)
		list_move_tail(&vma->combined_links.rebind, &vm->rebind_list);

	dma_resv_unlock(xe_vm_resv(vm));

	return 0;

out_err:
	list_splice_tail(&tmp_evict, &vm->userptr.repin_list);

	return err;
}

/**
 * xe_vm_userptr_check_repin() - Check whether the VM might have userptrs
 * that need repinning.
 * @vm: The VM.
 *
 * This function does an advisory check for whether the VM has userptrs that
 * need repinning.
 *
 * Return: 0 if there are no indications of userptrs needing repinning,
 * -EAGAIN if there are.
 */
int xe_vm_userptr_check_repin(struct xe_vm *vm)
{
	return (list_empty_careful(&vm->userptr.repin_list) &&
		list_empty_careful(&vm->userptr.invalidated)) ? 0 : -EAGAIN;
}

static struct dma_fence *
xe_vm_bind_vma(struct xe_vma *vma, struct xe_exec_queue *q,
	       struct xe_sync_entry *syncs, u32 num_syncs,
	       bool first_op, bool last_op);

struct dma_fence *xe_vm_rebind(struct xe_vm *vm, bool rebind_worker)
{
	struct dma_fence *fence = NULL;
	struct xe_vma *vma, *next;

	lockdep_assert_held(&vm->lock);
	if (xe_vm_no_dma_fences(vm) && !rebind_worker)
		return NULL;

	xe_vm_assert_held(vm);
	list_for_each_entry_safe(vma, next, &vm->rebind_list,
				 combined_links.rebind) {
		xe_assert(vm->xe, vma->tile_present);

		list_del_init(&vma->combined_links.rebind);
		dma_fence_put(fence);
		if (rebind_worker)
			trace_xe_vma_rebind_worker(vma);
		else
			trace_xe_vma_rebind_exec(vma);
		fence = xe_vm_bind_vma(vma, NULL, NULL, 0, false, false);
		if (IS_ERR(fence))
			return fence;
	}

	return fence;
}

static struct xe_vma *xe_vma_create(struct xe_vm *vm,
				    struct xe_bo *bo,
				    u64 bo_offset_or_userptr,
				    u64 start, u64 end,
				    bool read_only,
				    bool is_null,
				    u8 tile_mask)
{
	struct xe_vma *vma;
	struct xe_tile *tile;
	u8 id;

	xe_assert(vm->xe, start < end);
	xe_assert(vm->xe, end < vm->size);

	if (!bo && !is_null)	/* userptr */
		vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	else
		vma = kzalloc(sizeof(*vma) - sizeof(struct xe_userptr),
			      GFP_KERNEL);
	if (!vma) {
		vma = ERR_PTR(-ENOMEM);
		return vma;
	}

	INIT_LIST_HEAD(&vma->combined_links.rebind);
	INIT_LIST_HEAD(&vma->notifier.rebind_link);
	INIT_LIST_HEAD(&vma->extobj.link);

	INIT_LIST_HEAD(&vma->gpuva.gem.entry);
	vma->gpuva.vm = &vm->gpuvm;
	vma->gpuva.va.addr = start;
	vma->gpuva.va.range = end - start + 1;
	if (read_only)
		vma->gpuva.flags |= XE_VMA_READ_ONLY;
	if (is_null)
		vma->gpuva.flags |= DRM_GPUVA_SPARSE;

	if (tile_mask) {
		vma->tile_mask = tile_mask;
	} else {
		for_each_tile(tile, vm->xe, id)
			vma->tile_mask |= 0x1 << id;
	}

	if (GRAPHICS_VER(vm->xe) >= 20 || vm->xe->info.platform == XE_PVC)
		vma->gpuva.flags |= XE_VMA_ATOMIC_PTE_BIT;

	if (bo) {
		struct drm_gpuvm_bo *vm_bo;

		xe_bo_assert_held(bo);

		vm_bo = drm_gpuvm_bo_obtain(vma->gpuva.vm, &bo->ttm.base);
		if (IS_ERR(vm_bo)) {
			kfree(vma);
			return ERR_CAST(vm_bo);
		}

		drm_gem_object_get(&bo->ttm.base);
		vma->gpuva.gem.obj = &bo->ttm.base;
		vma->gpuva.gem.offset = bo_offset_or_userptr;
		drm_gpuva_link(&vma->gpuva, vm_bo);
		drm_gpuvm_bo_put(vm_bo);
	} else /* userptr or null */ {
		if (!is_null) {
			u64 size = end - start + 1;
			int err;

			INIT_LIST_HEAD(&vma->userptr.invalidate_link);
			vma->gpuva.gem.offset = bo_offset_or_userptr;

			err = mmu_interval_notifier_insert(&vma->userptr.notifier,
							   current->mm,
							   xe_vma_userptr(vma), size,
							   &vma_userptr_notifier_ops);
			if (err) {
				kfree(vma);
				vma = ERR_PTR(err);
				return vma;
			}

			vma->userptr.notifier_seq = LONG_MAX;
		}

		xe_vm_get(vm);
	}

	return vma;
}

static bool vm_remove_extobj(struct xe_vma *vma)
{
	if (!list_empty(&vma->extobj.link)) {
		xe_vma_vm(vma)->extobj.entries--;
		list_del_init(&vma->extobj.link);
		return true;
	}
	return false;
}

static void xe_vma_destroy_late(struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_device *xe = vm->xe;
	bool read_only = xe_vma_read_only(vma);

	if (xe_vma_is_userptr(vma)) {
		if (vma->userptr.sg) {
			dma_unmap_sgtable(xe->drm.dev,
					  vma->userptr.sg,
					  read_only ? DMA_TO_DEVICE :
					  DMA_BIDIRECTIONAL, 0);
			sg_free_table(vma->userptr.sg);
			vma->userptr.sg = NULL;
		}

		/*
		 * Since userptr pages are not pinned, we can't remove
		 * the notifer until we're sure the GPU is not accessing
		 * them anymore
		 */
		mmu_interval_notifier_remove(&vma->userptr.notifier);
		xe_vm_put(vm);
	} else if (xe_vma_is_null(vma)) {
		xe_vm_put(vm);
	} else {
		xe_bo_put(xe_vma_bo(vma));
	}

	kfree(vma);
}

static void vma_destroy_work_func(struct work_struct *w)
{
	struct xe_vma *vma =
		container_of(w, struct xe_vma, destroy_work);

	xe_vma_destroy_late(vma);
}

static struct xe_vma *
bo_has_vm_references_locked(struct xe_bo *bo, struct xe_vm *vm,
			    struct xe_vma *ignore)
{
	struct drm_gpuvm_bo *vm_bo;
	struct drm_gpuva *va;
	struct drm_gem_object *obj = &bo->ttm.base;

	xe_bo_assert_held(bo);

	drm_gem_for_each_gpuvm_bo(vm_bo, obj) {
		drm_gpuvm_bo_for_each_va(va, vm_bo) {
			struct xe_vma *vma = gpuva_to_vma(va);

			if (vma != ignore && xe_vma_vm(vma) == vm)
				return vma;
		}
	}

	return NULL;
}

static bool bo_has_vm_references(struct xe_bo *bo, struct xe_vm *vm,
				 struct xe_vma *ignore)
{
	bool ret;

	xe_bo_lock(bo, false);
	ret = !!bo_has_vm_references_locked(bo, vm, ignore);
	xe_bo_unlock(bo);

	return ret;
}

static void __vm_insert_extobj(struct xe_vm *vm, struct xe_vma *vma)
{
	lockdep_assert_held_write(&vm->lock);

	list_add(&vma->extobj.link, &vm->extobj.list);
	vm->extobj.entries++;
}

static void vm_insert_extobj(struct xe_vm *vm, struct xe_vma *vma)
{
	struct xe_bo *bo = xe_vma_bo(vma);

	lockdep_assert_held_write(&vm->lock);

	if (bo_has_vm_references(bo, vm, vma))
		return;

	__vm_insert_extobj(vm, vma);
}

static void vma_destroy_cb(struct dma_fence *fence,
			   struct dma_fence_cb *cb)
{
	struct xe_vma *vma = container_of(cb, struct xe_vma, destroy_cb);

	INIT_WORK(&vma->destroy_work, vma_destroy_work_func);
	queue_work(system_unbound_wq, &vma->destroy_work);
}

static void xe_vma_destroy(struct xe_vma *vma, struct dma_fence *fence)
{
	struct xe_vm *vm = xe_vma_vm(vma);

	lockdep_assert_held_write(&vm->lock);
	xe_assert(vm->xe, list_empty(&vma->combined_links.destroy));

	if (xe_vma_is_userptr(vma)) {
		xe_assert(vm->xe, vma->gpuva.flags & XE_VMA_DESTROYED);

		spin_lock(&vm->userptr.invalidated_lock);
		list_del(&vma->userptr.invalidate_link);
		spin_unlock(&vm->userptr.invalidated_lock);
	} else if (!xe_vma_is_null(vma)) {
		xe_bo_assert_held(xe_vma_bo(vma));

		spin_lock(&vm->notifier.list_lock);
		list_del(&vma->notifier.rebind_link);
		spin_unlock(&vm->notifier.list_lock);

		drm_gpuva_unlink(&vma->gpuva);

		if (!xe_vma_bo(vma)->vm && vm_remove_extobj(vma)) {
			struct xe_vma *other;

			other = bo_has_vm_references_locked(xe_vma_bo(vma), vm, NULL);

			if (other)
				__vm_insert_extobj(vm, other);
		}
	}

	xe_vm_assert_held(vm);
	if (fence) {
		int ret = dma_fence_add_callback(fence, &vma->destroy_cb,
						 vma_destroy_cb);

		if (ret) {
			XE_WARN_ON(ret != -ENOENT);
			xe_vma_destroy_late(vma);
		}
	} else {
		xe_vma_destroy_late(vma);
	}
}

/**
 * xe_vm_prepare_vma() - drm_exec utility to lock a vma
 * @exec: The drm_exec object we're currently locking for.
 * @vma: The vma for witch we want to lock the vm resv and any attached
 * object's resv.
 * @num_shared: The number of dma-fence slots to pre-allocate in the
 * objects' reservation objects.
 *
 * Return: 0 on success, negative error code on error. In particular
 * may return -EDEADLK on WW transaction contention and -EINTR if
 * an interruptible wait is terminated by a signal.
 */
int xe_vm_prepare_vma(struct drm_exec *exec, struct xe_vma *vma,
		      unsigned int num_shared)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_bo *bo = xe_vma_bo(vma);
	int err;

	XE_WARN_ON(!vm);
	err = drm_exec_prepare_obj(exec, xe_vm_obj(vm), num_shared);
	if (!err && bo && !bo->vm)
		err = drm_exec_prepare_obj(exec, &bo->ttm.base, num_shared);

	return err;
}

static void xe_vma_destroy_unlocked(struct xe_vma *vma)
{
	struct drm_exec exec;
	int err;

	drm_exec_init(&exec, 0);
	drm_exec_until_all_locked(&exec) {
		err = xe_vm_prepare_vma(&exec, vma, 0);
		drm_exec_retry_on_contention(&exec);
		if (XE_WARN_ON(err))
			break;
	}

	xe_vma_destroy(vma, NULL);

	drm_exec_fini(&exec);
}

struct xe_vma *
xe_vm_find_overlapping_vma(struct xe_vm *vm, u64 start, u64 range)
{
	struct drm_gpuva *gpuva;

	lockdep_assert_held(&vm->lock);

	if (xe_vm_is_closed_or_banned(vm))
		return NULL;

	xe_assert(vm->xe, start + range <= vm->size);

	gpuva = drm_gpuva_find_first(&vm->gpuvm, start, range);

	return gpuva ? gpuva_to_vma(gpuva) : NULL;
}

static int xe_vm_insert_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	int err;

	xe_assert(vm->xe, xe_vma_vm(vma) == vm);
	lockdep_assert_held(&vm->lock);

	err = drm_gpuva_insert(&vm->gpuvm, &vma->gpuva);
	XE_WARN_ON(err);	/* Shouldn't be possible */

	return err;
}

static void xe_vm_remove_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	xe_assert(vm->xe, xe_vma_vm(vma) == vm);
	lockdep_assert_held(&vm->lock);

	drm_gpuva_remove(&vma->gpuva);
	if (vm->usm.last_fault_vma == vma)
		vm->usm.last_fault_vma = NULL;
}

static struct drm_gpuva_op *xe_vm_op_alloc(void)
{
	struct xe_vma_op *op;

	op = kzalloc(sizeof(*op), GFP_KERNEL);

	if (unlikely(!op))
		return NULL;

	return &op->base;
}

static void xe_vm_free(struct drm_gpuvm *gpuvm);

static struct drm_gpuvm_ops gpuvm_ops = {
	.op_alloc = xe_vm_op_alloc,
	.vm_free = xe_vm_free,
};

static u64 pde_encode_pat_index(struct xe_device *xe, u16 pat_index)
{
	u64 pte = 0;

	if (pat_index & BIT(0))
		pte |= XE_PPGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XE_PPGTT_PTE_PAT1;

	return pte;
}

static u64 pte_encode_pat_index(struct xe_device *xe, u16 pat_index)
{
	u64 pte = 0;

	if (pat_index & BIT(0))
		pte |= XE_PPGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XE_PPGTT_PTE_PAT1;

	if (pat_index & BIT(2))
		pte |= XE_PPGTT_PTE_PAT2;

	if (pat_index & BIT(3))
		pte |= XELPG_PPGTT_PTE_PAT3;

	if (pat_index & (BIT(4)))
		pte |= XE2_PPGTT_PTE_PAT4;

	return pte;
}

static u64 pte_encode_ps(u32 pt_level)
{
	XE_WARN_ON(pt_level > 2);

	if (pt_level == 1)
		return XE_PDE_PS_2M;
	else if (pt_level == 2)
		return XE_PDPE_PS_1G;

	return 0;
}

static u64 xelp_pde_encode_bo(struct xe_bo *bo, u64 bo_offset,
			      const u16 pat_index)
{
	struct xe_device *xe = xe_bo_device(bo);
	u64 pde;

	pde = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pde |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pde |= pde_encode_pat_index(xe, pat_index);

	return pde;
}

static u64 xelp_pte_encode_bo(struct xe_bo *bo, u64 bo_offset,
			      u16 pat_index, u32 pt_level)
{
	struct xe_device *xe = xe_bo_device(bo);
	u64 pte;

	pte = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pte |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pte |= pte_encode_pat_index(xe, pat_index);
	pte |= pte_encode_ps(pt_level);

	if (xe_bo_is_vram(bo) || xe_bo_is_stolen_devmem(bo))
		pte |= XE_PPGTT_PTE_DM;

	return pte;
}

static u64 xelp_pte_encode_vma(u64 pte, struct xe_vma *vma,
			       u16 pat_index, u32 pt_level)
{
	struct xe_device *xe = xe_vma_vm(vma)->xe;

	pte |= XE_PAGE_PRESENT;

	if (likely(!xe_vma_read_only(vma)))
		pte |= XE_PAGE_RW;

	pte |= pte_encode_pat_index(xe, pat_index);
	pte |= pte_encode_ps(pt_level);

	if (unlikely(xe_vma_is_null(vma)))
		pte |= XE_PTE_NULL;

	return pte;
}

static u64 xelp_pte_encode_addr(struct xe_device *xe, u64 addr,
				u16 pat_index,
				u32 pt_level, bool devmem, u64 flags)
{
	u64 pte;

	/* Avoid passing random bits directly as flags */
	xe_assert(xe, !(flags & ~XE_PTE_PS64));

	pte = addr;
	pte |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pte |= pte_encode_pat_index(xe, pat_index);
	pte |= pte_encode_ps(pt_level);

	if (devmem)
		pte |= XE_PPGTT_PTE_DM;

	pte |= flags;

	return pte;
}

static const struct xe_pt_ops xelp_pt_ops = {
	.pte_encode_bo = xelp_pte_encode_bo,
	.pte_encode_vma = xelp_pte_encode_vma,
	.pte_encode_addr = xelp_pte_encode_addr,
	.pde_encode_bo = xelp_pde_encode_bo,
};

static void xe_vma_op_work_func(struct work_struct *w);
static void vm_destroy_work_func(struct work_struct *w);

struct xe_vm *xe_vm_create(struct xe_device *xe, u32 flags)
{
	struct drm_gem_object *vm_resv_obj;
	struct xe_vm *vm;
	int err, number_tiles = 0;
	struct xe_tile *tile;
	u8 id;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return ERR_PTR(-ENOMEM);

	vm->xe = xe;

	vm->size = 1ull << xe->info.va_bits;

	vm->flags = flags;

	init_rwsem(&vm->lock);

	INIT_LIST_HEAD(&vm->rebind_list);

	INIT_LIST_HEAD(&vm->userptr.repin_list);
	INIT_LIST_HEAD(&vm->userptr.invalidated);
	init_rwsem(&vm->userptr.notifier_lock);
	spin_lock_init(&vm->userptr.invalidated_lock);

	INIT_LIST_HEAD(&vm->notifier.rebind_list);
	spin_lock_init(&vm->notifier.list_lock);

	INIT_LIST_HEAD(&vm->async_ops.pending);
	INIT_WORK(&vm->async_ops.work, xe_vma_op_work_func);
	spin_lock_init(&vm->async_ops.lock);

	INIT_WORK(&vm->destroy_work, vm_destroy_work_func);

	INIT_LIST_HEAD(&vm->preempt.exec_queues);
	vm->preempt.min_run_period_ms = 10;	/* FIXME: Wire up to uAPI */

	for_each_tile(tile, xe, id)
		xe_range_fence_tree_init(&vm->rftree[id]);

	INIT_LIST_HEAD(&vm->extobj.list);

	vm->pt_ops = &xelp_pt_ops;

	if (!(flags & XE_VM_FLAG_MIGRATION))
		xe_device_mem_access_get(xe);

	vm_resv_obj = drm_gpuvm_resv_object_alloc(&xe->drm);
	if (!vm_resv_obj) {
		err = -ENOMEM;
		goto err_no_resv;
	}

	drm_gpuvm_init(&vm->gpuvm, "Xe VM", 0, &xe->drm, vm_resv_obj,
		       0, vm->size, 0, 0, &gpuvm_ops);

	drm_gem_object_put(vm_resv_obj);

	err = dma_resv_lock_interruptible(xe_vm_resv(vm), NULL);
	if (err)
		goto err_close;

	if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		vm->flags |= XE_VM_FLAG_64K;

	for_each_tile(tile, xe, id) {
		if (flags & XE_VM_FLAG_MIGRATION &&
		    tile->id != XE_VM_FLAG_TILE_ID(flags))
			continue;

		vm->pt_root[id] = xe_pt_create(vm, tile, xe->info.vm_max_level);
		if (IS_ERR(vm->pt_root[id])) {
			err = PTR_ERR(vm->pt_root[id]);
			vm->pt_root[id] = NULL;
			goto err_unlock_close;
		}
	}

	if (flags & XE_VM_FLAG_SCRATCH_PAGE) {
		for_each_tile(tile, xe, id) {
			if (!vm->pt_root[id])
				continue;

			err = xe_pt_create_scratch(xe, tile, vm);
			if (err)
				goto err_unlock_close;
		}
		vm->batch_invalidate_tlb = true;
	}

	if (flags & XE_VM_FLAG_COMPUTE_MODE) {
		INIT_WORK(&vm->preempt.rebind_work, preempt_rebind_work_func);
		vm->flags |= XE_VM_FLAG_COMPUTE_MODE;
		vm->batch_invalidate_tlb = false;
	}

	if (flags & XE_VM_FLAG_ASYNC_BIND_OPS) {
		vm->async_ops.fence.context = dma_fence_context_alloc(1);
		vm->flags |= XE_VM_FLAG_ASYNC_BIND_OPS;
	}

	/* Fill pt_root after allocating scratch tables */
	for_each_tile(tile, xe, id) {
		if (!vm->pt_root[id])
			continue;

		xe_pt_populate_empty(tile, vm, vm->pt_root[id]);
	}
	dma_resv_unlock(xe_vm_resv(vm));

	/* Kernel migration VM shouldn't have a circular loop.. */
	if (!(flags & XE_VM_FLAG_MIGRATION)) {
		for_each_tile(tile, xe, id) {
			struct xe_gt *gt = tile->primary_gt;
			struct xe_vm *migrate_vm;
			struct xe_exec_queue *q;

			if (!vm->pt_root[id])
				continue;

			migrate_vm = xe_migrate_get_vm(tile->migrate);
			q = xe_exec_queue_create_class(xe, gt, migrate_vm,
						       XE_ENGINE_CLASS_COPY,
						       EXEC_QUEUE_FLAG_VM);
			xe_vm_put(migrate_vm);
			if (IS_ERR(q)) {
				err = PTR_ERR(q);
				goto err_close;
			}
			vm->q[id] = q;
			number_tiles++;
		}
	}

	if (number_tiles > 1)
		vm->composite_fence_ctx = dma_fence_context_alloc(1);

	mutex_lock(&xe->usm.lock);
	if (flags & XE_VM_FLAG_FAULT_MODE)
		xe->usm.num_vm_in_fault_mode++;
	else if (!(flags & XE_VM_FLAG_MIGRATION))
		xe->usm.num_vm_in_non_fault_mode++;
	mutex_unlock(&xe->usm.lock);

	trace_xe_vm_create(vm);

	return vm;

err_unlock_close:
	dma_resv_unlock(xe_vm_resv(vm));
err_close:
	xe_vm_close_and_put(vm);
	return ERR_PTR(err);

err_no_resv:
	for_each_tile(tile, xe, id)
		xe_range_fence_tree_fini(&vm->rftree[id]);
	kfree(vm);
	if (!(flags & XE_VM_FLAG_MIGRATION))
		xe_device_mem_access_put(xe);
	return ERR_PTR(err);
}

static void flush_async_ops(struct xe_vm *vm)
{
	queue_work(system_unbound_wq, &vm->async_ops.work);
	flush_work(&vm->async_ops.work);
}

static void xe_vm_close(struct xe_vm *vm)
{
	down_write(&vm->lock);
	vm->size = 0;
	up_write(&vm->lock);
}

void xe_vm_close_and_put(struct xe_vm *vm)
{
	LIST_HEAD(contested);
	struct xe_device *xe = vm->xe;
	struct xe_tile *tile;
	struct xe_vma *vma, *next_vma;
	struct drm_gpuva *gpuva, *next;
	u8 id;

	xe_assert(xe, !vm->preempt.num_exec_queues);

	xe_vm_close(vm);
	flush_async_ops(vm);
	if (xe_vm_in_compute_mode(vm))
		flush_work(&vm->preempt.rebind_work);

	for_each_tile(tile, xe, id) {
		if (vm->q[id]) {
			xe_exec_queue_kill(vm->q[id]);
			xe_exec_queue_put(vm->q[id]);
			vm->q[id] = NULL;
		}
	}

	down_write(&vm->lock);
	xe_vm_lock(vm, false);
	drm_gpuvm_for_each_va_safe(gpuva, next, &vm->gpuvm) {
		vma = gpuva_to_vma(gpuva);

		if (xe_vma_has_no_bo(vma)) {
			down_read(&vm->userptr.notifier_lock);
			vma->gpuva.flags |= XE_VMA_DESTROYED;
			up_read(&vm->userptr.notifier_lock);
		}

		xe_vm_remove_vma(vm, vma);

		/* easy case, remove from VMA? */
		if (xe_vma_has_no_bo(vma) || xe_vma_bo(vma)->vm) {
			list_del_init(&vma->combined_links.rebind);
			xe_vma_destroy(vma, NULL);
			continue;
		}

		list_move_tail(&vma->combined_links.destroy, &contested);
		vma->gpuva.flags |= XE_VMA_DESTROYED;
	}

	/*
	 * All vm operations will add shared fences to resv.
	 * The only exception is eviction for a shared object,
	 * but even so, the unbind when evicted would still
	 * install a fence to resv. Hence it's safe to
	 * destroy the pagetables immediately.
	 */
	for_each_tile(tile, xe, id) {
		if (vm->scratch_bo[id]) {
			u32 i;

			xe_bo_unpin(vm->scratch_bo[id]);
			xe_bo_put(vm->scratch_bo[id]);
			for (i = 0; i < vm->pt_root[id]->level; i++)
				xe_pt_destroy(vm->scratch_pt[id][i], vm->flags,
					      NULL);
		}
		if (vm->pt_root[id]) {
			xe_pt_destroy(vm->pt_root[id], vm->flags, NULL);
			vm->pt_root[id] = NULL;
		}
	}
	xe_vm_unlock(vm);

	/*
	 * VM is now dead, cannot re-add nodes to vm->vmas if it's NULL
	 * Since we hold a refcount to the bo, we can remove and free
	 * the members safely without locking.
	 */
	list_for_each_entry_safe(vma, next_vma, &contested,
				 combined_links.destroy) {
		list_del_init(&vma->combined_links.destroy);
		xe_vma_destroy_unlocked(vma);
	}

	xe_assert(xe, list_empty(&vm->extobj.list));
	up_write(&vm->lock);

	mutex_lock(&xe->usm.lock);
	if (vm->flags & XE_VM_FLAG_FAULT_MODE)
		xe->usm.num_vm_in_fault_mode--;
	else if (!(vm->flags & XE_VM_FLAG_MIGRATION))
		xe->usm.num_vm_in_non_fault_mode--;
	mutex_unlock(&xe->usm.lock);

	for_each_tile(tile, xe, id)
		xe_range_fence_tree_fini(&vm->rftree[id]);

	xe_vm_put(vm);
}

static void vm_destroy_work_func(struct work_struct *w)
{
	struct xe_vm *vm =
		container_of(w, struct xe_vm, destroy_work);
	struct xe_device *xe = vm->xe;
	struct xe_tile *tile;
	u8 id;
	void *lookup;

	/* xe_vm_close_and_put was not called? */
	xe_assert(xe, !vm->size);

	if (!(vm->flags & XE_VM_FLAG_MIGRATION)) {
		xe_device_mem_access_put(xe);

		if (xe->info.has_asid) {
			mutex_lock(&xe->usm.lock);
			lookup = xa_erase(&xe->usm.asid_to_vm, vm->usm.asid);
			xe_assert(xe, lookup == vm);
			mutex_unlock(&xe->usm.lock);
		}
	}

	for_each_tile(tile, xe, id)
		XE_WARN_ON(vm->pt_root[id]);

	trace_xe_vm_free(vm);
	dma_fence_put(vm->rebind_fence);
	kfree(vm);
}

static void xe_vm_free(struct drm_gpuvm *gpuvm)
{
	struct xe_vm *vm = container_of(gpuvm, struct xe_vm, gpuvm);

	/* To destroy the VM we need to be able to sleep */
	queue_work(system_unbound_wq, &vm->destroy_work);
}

struct xe_vm *xe_vm_lookup(struct xe_file *xef, u32 id)
{
	struct xe_vm *vm;

	mutex_lock(&xef->vm.lock);
	vm = xa_load(&xef->vm.xa, id);
	if (vm)
		xe_vm_get(vm);
	mutex_unlock(&xef->vm.lock);

	return vm;
}

u64 xe_vm_pdp4_descriptor(struct xe_vm *vm, struct xe_tile *tile)
{
	return vm->pt_ops->pde_encode_bo(vm->pt_root[tile->id]->bo, 0,
					 tile_to_xe(tile)->pat.idx[XE_CACHE_WB]);
}

static struct dma_fence *
xe_vm_unbind_vma(struct xe_vma *vma, struct xe_exec_queue *q,
		 struct xe_sync_entry *syncs, u32 num_syncs,
		 bool first_op, bool last_op)
{
	struct xe_tile *tile;
	struct dma_fence *fence = NULL;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	struct xe_vm *vm = xe_vma_vm(vma);
	int cur_fence = 0, i;
	int number_tiles = hweight8(vma->tile_present);
	int err;
	u8 id;

	trace_xe_vma_unbind(vma);

	if (number_tiles > 1) {
		fences = kmalloc_array(number_tiles, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences)
			return ERR_PTR(-ENOMEM);
	}

	for_each_tile(tile, vm->xe, id) {
		if (!(vma->tile_present & BIT(id)))
			goto next;

		fence = __xe_pt_unbind_vma(tile, vma, q ? q : vm->q[id],
					   first_op ? syncs : NULL,
					   first_op ? num_syncs : 0);
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto err_fences;
		}

		if (fences)
			fences[cur_fence++] = fence;

next:
		if (q && vm->pt_root[id] && !list_empty(&q->multi_gt_list))
			q = list_next_entry(q, multi_gt_list);
	}

	if (fences) {
		cf = dma_fence_array_create(number_tiles, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}
	}

	if (last_op) {
		for (i = 0; i < num_syncs; i++)
			xe_sync_entry_signal(&syncs[i], NULL,
					     cf ? &cf->base : fence);
	}

	return cf ? &cf->base : !fence ? dma_fence_get_stub() : fence;

err_fences:
	if (fences) {
		while (cur_fence) {
			/* FIXME: Rewind the previous binds? */
			dma_fence_put(fences[--cur_fence]);
		}
		kfree(fences);
	}

	return ERR_PTR(err);
}

static struct dma_fence *
xe_vm_bind_vma(struct xe_vma *vma, struct xe_exec_queue *q,
	       struct xe_sync_entry *syncs, u32 num_syncs,
	       bool first_op, bool last_op)
{
	struct xe_tile *tile;
	struct dma_fence *fence;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	struct xe_vm *vm = xe_vma_vm(vma);
	int cur_fence = 0, i;
	int number_tiles = hweight8(vma->tile_mask);
	int err;
	u8 id;

	trace_xe_vma_bind(vma);

	if (number_tiles > 1) {
		fences = kmalloc_array(number_tiles, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences)
			return ERR_PTR(-ENOMEM);
	}

	for_each_tile(tile, vm->xe, id) {
		if (!(vma->tile_mask & BIT(id)))
			goto next;

		fence = __xe_pt_bind_vma(tile, vma, q ? q : vm->q[id],
					 first_op ? syncs : NULL,
					 first_op ? num_syncs : 0,
					 vma->tile_present & BIT(id));
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto err_fences;
		}

		if (fences)
			fences[cur_fence++] = fence;

next:
		if (q && vm->pt_root[id] && !list_empty(&q->multi_gt_list))
			q = list_next_entry(q, multi_gt_list);
	}

	if (fences) {
		cf = dma_fence_array_create(number_tiles, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}
	}

	if (last_op) {
		for (i = 0; i < num_syncs; i++)
			xe_sync_entry_signal(&syncs[i], NULL,
					     cf ? &cf->base : fence);
	}

	return cf ? &cf->base : fence;

err_fences:
	if (fences) {
		while (cur_fence) {
			/* FIXME: Rewind the previous binds? */
			dma_fence_put(fences[--cur_fence]);
		}
		kfree(fences);
	}

	return ERR_PTR(err);
}

struct async_op_fence {
	struct dma_fence fence;
	struct dma_fence *wait_fence;
	struct dma_fence_cb cb;
	struct xe_vm *vm;
	wait_queue_head_t wq;
	bool started;
};

static const char *async_op_fence_get_driver_name(struct dma_fence *dma_fence)
{
	return "xe";
}

static const char *
async_op_fence_get_timeline_name(struct dma_fence *dma_fence)
{
	return "async_op_fence";
}

static const struct dma_fence_ops async_op_fence_ops = {
	.get_driver_name = async_op_fence_get_driver_name,
	.get_timeline_name = async_op_fence_get_timeline_name,
};

static void async_op_fence_cb(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct async_op_fence *afence =
		container_of(cb, struct async_op_fence, cb);

	afence->fence.error = afence->wait_fence->error;
	dma_fence_signal(&afence->fence);
	xe_vm_put(afence->vm);
	dma_fence_put(afence->wait_fence);
	dma_fence_put(&afence->fence);
}

static void add_async_op_fence_cb(struct xe_vm *vm,
				  struct dma_fence *fence,
				  struct async_op_fence *afence)
{
	int ret;

	if (!xe_vm_no_dma_fences(vm)) {
		afence->started = true;
		smp_wmb();
		wake_up_all(&afence->wq);
	}

	afence->wait_fence = dma_fence_get(fence);
	afence->vm = xe_vm_get(vm);
	dma_fence_get(&afence->fence);
	ret = dma_fence_add_callback(fence, &afence->cb, async_op_fence_cb);
	if (ret == -ENOENT) {
		afence->fence.error = afence->wait_fence->error;
		dma_fence_signal(&afence->fence);
	}
	if (ret) {
		xe_vm_put(vm);
		dma_fence_put(afence->wait_fence);
		dma_fence_put(&afence->fence);
	}
	XE_WARN_ON(ret && ret != -ENOENT);
}

int xe_vm_async_fence_wait_start(struct dma_fence *fence)
{
	if (fence->ops == &async_op_fence_ops) {
		struct async_op_fence *afence =
			container_of(fence, struct async_op_fence, fence);

		xe_assert(afence->vm->xe, !xe_vm_no_dma_fences(afence->vm));

		smp_rmb();
		return wait_event_interruptible(afence->wq, afence->started);
	}

	return 0;
}

static int __xe_vm_bind(struct xe_vm *vm, struct xe_vma *vma,
			struct xe_exec_queue *q, struct xe_sync_entry *syncs,
			u32 num_syncs, struct async_op_fence *afence,
			bool immediate, bool first_op, bool last_op)
{
	struct dma_fence *fence;

	xe_vm_assert_held(vm);

	if (immediate) {
		fence = xe_vm_bind_vma(vma, q, syncs, num_syncs, first_op,
				       last_op);
		if (IS_ERR(fence))
			return PTR_ERR(fence);
	} else {
		int i;

		xe_assert(vm->xe, xe_vm_in_fault_mode(vm));

		fence = dma_fence_get_stub();
		if (last_op) {
			for (i = 0; i < num_syncs; i++)
				xe_sync_entry_signal(&syncs[i], NULL, fence);
		}
	}
	if (afence)
		add_async_op_fence_cb(vm, fence, afence);

	dma_fence_put(fence);
	return 0;
}

static int xe_vm_bind(struct xe_vm *vm, struct xe_vma *vma, struct xe_exec_queue *q,
		      struct xe_bo *bo, struct xe_sync_entry *syncs,
		      u32 num_syncs, struct async_op_fence *afence,
		      bool immediate, bool first_op, bool last_op)
{
	int err;

	xe_vm_assert_held(vm);
	xe_bo_assert_held(bo);

	if (bo && immediate) {
		err = xe_bo_validate(bo, vm, true);
		if (err)
			return err;
	}

	return __xe_vm_bind(vm, vma, q, syncs, num_syncs, afence, immediate,
			    first_op, last_op);
}

static int xe_vm_unbind(struct xe_vm *vm, struct xe_vma *vma,
			struct xe_exec_queue *q, struct xe_sync_entry *syncs,
			u32 num_syncs, struct async_op_fence *afence,
			bool first_op, bool last_op)
{
	struct dma_fence *fence;

	xe_vm_assert_held(vm);
	xe_bo_assert_held(xe_vma_bo(vma));

	fence = xe_vm_unbind_vma(vma, q, syncs, num_syncs, first_op, last_op);
	if (IS_ERR(fence))
		return PTR_ERR(fence);
	if (afence)
		add_async_op_fence_cb(vm, fence, afence);

	xe_vma_destroy(vma, fence);
	dma_fence_put(fence);

	return 0;
}

#define ALL_DRM_XE_VM_CREATE_FLAGS (DRM_XE_VM_CREATE_SCRATCH_PAGE | \
				    DRM_XE_VM_CREATE_COMPUTE_MODE | \
				    DRM_XE_VM_CREATE_ASYNC_BIND_OPS | \
				    DRM_XE_VM_CREATE_FAULT_MODE)

int xe_vm_create_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_create *args = data;
	struct xe_tile *tile;
	struct xe_vm *vm;
	u32 id, asid;
	int err;
	u32 flags = 0;

	if (XE_IOCTL_DBG(xe, args->extensions))
		return -EINVAL;

	if (XE_WA(xe_root_mmio_gt(xe), 14016763929))
		args->flags |= DRM_XE_VM_CREATE_SCRATCH_PAGE;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_FAULT_MODE &&
			 !xe->info.supports_usm))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & ~ALL_DRM_XE_VM_CREATE_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_SCRATCH_PAGE &&
			 args->flags & DRM_XE_VM_CREATE_FAULT_MODE))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_COMPUTE_MODE &&
			 args->flags & DRM_XE_VM_CREATE_FAULT_MODE))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_FAULT_MODE &&
			 xe_device_in_non_fault_mode(xe)))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !(args->flags & DRM_XE_VM_CREATE_FAULT_MODE) &&
			 xe_device_in_fault_mode(xe)))
		return -EINVAL;

	if (args->flags & DRM_XE_VM_CREATE_SCRATCH_PAGE)
		flags |= XE_VM_FLAG_SCRATCH_PAGE;
	if (args->flags & DRM_XE_VM_CREATE_COMPUTE_MODE)
		flags |= XE_VM_FLAG_COMPUTE_MODE;
	if (args->flags & DRM_XE_VM_CREATE_ASYNC_BIND_OPS)
		flags |= XE_VM_FLAG_ASYNC_BIND_OPS;
	if (args->flags & DRM_XE_VM_CREATE_FAULT_MODE)
		flags |= XE_VM_FLAG_FAULT_MODE;

	vm = xe_vm_create(xe, flags);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

	mutex_lock(&xef->vm.lock);
	err = xa_alloc(&xef->vm.xa, &id, vm, xa_limit_32b, GFP_KERNEL);
	mutex_unlock(&xef->vm.lock);
	if (err) {
		xe_vm_close_and_put(vm);
		return err;
	}

	if (xe->info.has_asid) {
		mutex_lock(&xe->usm.lock);
		err = xa_alloc_cyclic(&xe->usm.asid_to_vm, &asid, vm,
				      XA_LIMIT(0, XE_MAX_ASID - 1),
				      &xe->usm.next_asid, GFP_KERNEL);
		mutex_unlock(&xe->usm.lock);
		if (err) {
			xe_vm_close_and_put(vm);
			return err;
		}
		vm->usm.asid = asid;
	}

	args->vm_id = id;
	vm->xef = xef;

	/* Record BO memory for VM pagetable created against client */
	for_each_tile(tile, xe, id)
		if (vm->pt_root[id])
			xe_drm_client_add_bo(vm->xef->client, vm->pt_root[id]->bo);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_MEM)
	/* Warning: Security issue - never enable by default */
	args->reserved[0] = xe_bo_main_addr(vm->pt_root[0]->bo, XE_PAGE_SIZE);
#endif

	return 0;
}

int xe_vm_destroy_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_destroy *args = data;
	struct xe_vm *vm;
	int err = 0;

	if (XE_IOCTL_DBG(xe, args->pad) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	mutex_lock(&xef->vm.lock);
	vm = xa_load(&xef->vm.xa, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm))
		err = -ENOENT;
	else if (XE_IOCTL_DBG(xe, vm->preempt.num_exec_queues))
		err = -EBUSY;
	else
		xa_erase(&xef->vm.xa, args->vm_id);
	mutex_unlock(&xef->vm.lock);

	if (!err)
		xe_vm_close_and_put(vm);

	return err;
}

static const u32 region_to_mem_type[] = {
	XE_PL_TT,
	XE_PL_VRAM0,
	XE_PL_VRAM1,
};

static int xe_vm_prefetch(struct xe_vm *vm, struct xe_vma *vma,
			  struct xe_exec_queue *q, u32 region,
			  struct xe_sync_entry *syncs, u32 num_syncs,
			  struct async_op_fence *afence, bool first_op,
			  bool last_op)
{
	int err;

	xe_assert(vm->xe, region <= ARRAY_SIZE(region_to_mem_type));

	if (!xe_vma_has_no_bo(vma)) {
		err = xe_bo_migrate(xe_vma_bo(vma), region_to_mem_type[region]);
		if (err)
			return err;
	}

	if (vma->tile_mask != (vma->tile_present & ~vma->usm.tile_invalidated)) {
		return xe_vm_bind(vm, vma, q, xe_vma_bo(vma), syncs, num_syncs,
				  afence, true, first_op, last_op);
	} else {
		int i;

		/* Nothing to do, signal fences now */
		if (last_op) {
			for (i = 0; i < num_syncs; i++)
				xe_sync_entry_signal(&syncs[i], NULL,
						     dma_fence_get_stub());
		}
		if (afence)
			dma_fence_signal(&afence->fence);
		return 0;
	}
}

static void vm_set_async_error(struct xe_vm *vm, int err)
{
	lockdep_assert_held(&vm->lock);
	vm->async_ops.error = err;
}

static int vm_bind_ioctl_lookup_vma(struct xe_vm *vm, struct xe_bo *bo,
				    u64 addr, u64 range, u32 op, u32 flags)
{
	struct xe_device *xe = vm->xe;
	struct xe_vma *vma;
	bool async = !!(flags & XE_VM_BIND_FLAG_ASYNC);

	lockdep_assert_held(&vm->lock);

	switch (op) {
	case XE_VM_BIND_OP_MAP:
	case XE_VM_BIND_OP_MAP_USERPTR:
		vma = xe_vm_find_overlapping_vma(vm, addr, range);
		if (XE_IOCTL_DBG(xe, vma && !async))
			return -EBUSY;
		break;
	case XE_VM_BIND_OP_UNMAP:
	case XE_VM_BIND_OP_PREFETCH:
		vma = xe_vm_find_overlapping_vma(vm, addr, range);
		if (XE_IOCTL_DBG(xe, !vma))
			/* Not an actual error, IOCTL cleans up returns and 0 */
			return -ENODATA;
		if (XE_IOCTL_DBG(xe, (xe_vma_start(vma) != addr ||
				      xe_vma_end(vma) != addr + range) && !async))
			return -EINVAL;
		break;
	case XE_VM_BIND_OP_UNMAP_ALL:
		if (XE_IOCTL_DBG(xe, list_empty(&bo->ttm.base.gpuva.list)))
			/* Not an actual error, IOCTL cleans up returns and 0 */
			return -ENODATA;
		break;
	default:
		drm_warn(&xe->drm, "NOT POSSIBLE");
		return -EINVAL;
	}

	return 0;
}

static void prep_vma_destroy(struct xe_vm *vm, struct xe_vma *vma,
			     bool post_commit)
{
	down_read(&vm->userptr.notifier_lock);
	vma->gpuva.flags |= XE_VMA_DESTROYED;
	up_read(&vm->userptr.notifier_lock);
	if (post_commit)
		xe_vm_remove_vma(vm, vma);
}

#undef ULL
#define ULL	unsigned long long

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_VM)
static void print_op(struct xe_device *xe, struct drm_gpuva_op *op)
{
	struct xe_vma *vma;

	switch (op->op) {
	case DRM_GPUVA_OP_MAP:
		vm_dbg(&xe->drm, "MAP: addr=0x%016llx, range=0x%016llx",
		       (ULL)op->map.va.addr, (ULL)op->map.va.range);
		break;
	case DRM_GPUVA_OP_REMAP:
		vma = gpuva_to_vma(op->remap.unmap->va);
		vm_dbg(&xe->drm, "REMAP:UNMAP: addr=0x%016llx, range=0x%016llx, keep=%d",
		       (ULL)xe_vma_start(vma), (ULL)xe_vma_size(vma),
		       op->remap.unmap->keep ? 1 : 0);
		if (op->remap.prev)
			vm_dbg(&xe->drm,
			       "REMAP:PREV: addr=0x%016llx, range=0x%016llx",
			       (ULL)op->remap.prev->va.addr,
			       (ULL)op->remap.prev->va.range);
		if (op->remap.next)
			vm_dbg(&xe->drm,
			       "REMAP:NEXT: addr=0x%016llx, range=0x%016llx",
			       (ULL)op->remap.next->va.addr,
			       (ULL)op->remap.next->va.range);
		break;
	case DRM_GPUVA_OP_UNMAP:
		vma = gpuva_to_vma(op->unmap.va);
		vm_dbg(&xe->drm, "UNMAP: addr=0x%016llx, range=0x%016llx, keep=%d",
		       (ULL)xe_vma_start(vma), (ULL)xe_vma_size(vma),
		       op->unmap.keep ? 1 : 0);
		break;
	case DRM_GPUVA_OP_PREFETCH:
		vma = gpuva_to_vma(op->prefetch.va);
		vm_dbg(&xe->drm, "PREFETCH: addr=0x%016llx, range=0x%016llx",
		       (ULL)xe_vma_start(vma), (ULL)xe_vma_size(vma));
		break;
	default:
		drm_warn(&xe->drm, "NOT POSSIBLE");
	}
}
#else
static void print_op(struct xe_device *xe, struct drm_gpuva_op *op)
{
}
#endif

/*
 * Create operations list from IOCTL arguments, setup operations fields so parse
 * and commit steps are decoupled from IOCTL arguments. This step can fail.
 */
static struct drm_gpuva_ops *
vm_bind_ioctl_ops_create(struct xe_vm *vm, struct xe_bo *bo,
			 u64 bo_offset_or_userptr, u64 addr, u64 range,
			 u32 operation, u32 flags, u8 tile_mask, u32 region)
{
	struct drm_gem_object *obj = bo ? &bo->ttm.base : NULL;
	struct drm_gpuva_ops *ops;
	struct drm_gpuva_op *__op;
	struct xe_vma_op *op;
	struct drm_gpuvm_bo *vm_bo;
	int err;

	lockdep_assert_held_write(&vm->lock);

	vm_dbg(&vm->xe->drm,
	       "op=%d, addr=0x%016llx, range=0x%016llx, bo_offset_or_userptr=0x%016llx",
	       operation, (ULL)addr, (ULL)range,
	       (ULL)bo_offset_or_userptr);

	switch (operation) {
	case XE_VM_BIND_OP_MAP:
	case XE_VM_BIND_OP_MAP_USERPTR:
		ops = drm_gpuvm_sm_map_ops_create(&vm->gpuvm, addr, range,
						  obj, bo_offset_or_userptr);
		if (IS_ERR(ops))
			return ops;

		drm_gpuva_for_each_op(__op, ops) {
			struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

			op->tile_mask = tile_mask;
			op->map.immediate =
				flags & XE_VM_BIND_FLAG_IMMEDIATE;
			op->map.read_only =
				flags & XE_VM_BIND_FLAG_READONLY;
			op->map.is_null = flags & XE_VM_BIND_FLAG_NULL;
		}
		break;
	case XE_VM_BIND_OP_UNMAP:
		ops = drm_gpuvm_sm_unmap_ops_create(&vm->gpuvm, addr, range);
		if (IS_ERR(ops))
			return ops;

		drm_gpuva_for_each_op(__op, ops) {
			struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

			op->tile_mask = tile_mask;
		}
		break;
	case XE_VM_BIND_OP_PREFETCH:
		ops = drm_gpuvm_prefetch_ops_create(&vm->gpuvm, addr, range);
		if (IS_ERR(ops))
			return ops;

		drm_gpuva_for_each_op(__op, ops) {
			struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

			op->tile_mask = tile_mask;
			op->prefetch.region = region;
		}
		break;
	case XE_VM_BIND_OP_UNMAP_ALL:
		xe_assert(vm->xe, bo);

		err = xe_bo_lock(bo, true);
		if (err)
			return ERR_PTR(err);

		vm_bo = drm_gpuvm_bo_find(&vm->gpuvm, obj);
		if (!vm_bo)
			break;

		ops = drm_gpuvm_bo_unmap_ops_create(vm_bo);
		drm_gpuvm_bo_put(vm_bo);
		xe_bo_unlock(bo);
		if (IS_ERR(ops))
			return ops;

		drm_gpuva_for_each_op(__op, ops) {
			struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

			op->tile_mask = tile_mask;
		}
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
		ops = ERR_PTR(-EINVAL);
	}

#ifdef TEST_VM_ASYNC_OPS_ERROR
	if (operation & FORCE_ASYNC_OP_ERROR) {
		op = list_first_entry_or_null(&ops->list, struct xe_vma_op,
					      base.entry);
		if (op)
			op->inject_error = true;
	}
#endif

	if (!IS_ERR(ops))
		drm_gpuva_for_each_op(__op, ops)
			print_op(vm->xe, __op);

	return ops;
}

static struct xe_vma *new_vma(struct xe_vm *vm, struct drm_gpuva_op_map *op,
			      u8 tile_mask, bool read_only, bool is_null)
{
	struct xe_bo *bo = op->gem.obj ? gem_to_xe_bo(op->gem.obj) : NULL;
	struct xe_vma *vma;
	int err;

	lockdep_assert_held_write(&vm->lock);

	if (bo) {
		err = xe_bo_lock(bo, true);
		if (err)
			return ERR_PTR(err);
	}
	vma = xe_vma_create(vm, bo, op->gem.offset,
			    op->va.addr, op->va.addr +
			    op->va.range - 1, read_only, is_null,
			    tile_mask);
	if (bo)
		xe_bo_unlock(bo);

	if (xe_vma_is_userptr(vma)) {
		err = xe_vma_userptr_pin_pages(vma);
		if (err) {
			prep_vma_destroy(vm, vma, false);
			xe_vma_destroy_unlocked(vma);
			return ERR_PTR(err);
		}
	} else if (!xe_vma_has_no_bo(vma) && !bo->vm) {
		vm_insert_extobj(vm, vma);
		err = add_preempt_fences(vm, bo);
		if (err) {
			prep_vma_destroy(vm, vma, false);
			xe_vma_destroy_unlocked(vma);
			return ERR_PTR(err);
		}
	}

	return vma;
}

static u64 xe_vma_max_pte_size(struct xe_vma *vma)
{
	if (vma->gpuva.flags & XE_VMA_PTE_1G)
		return SZ_1G;
	else if (vma->gpuva.flags & XE_VMA_PTE_2M)
		return SZ_2M;

	return SZ_4K;
}

static u64 xe_vma_set_pte_size(struct xe_vma *vma, u64 size)
{
	switch (size) {
	case SZ_1G:
		vma->gpuva.flags |= XE_VMA_PTE_1G;
		break;
	case SZ_2M:
		vma->gpuva.flags |= XE_VMA_PTE_2M;
		break;
	}

	return SZ_4K;
}

static int xe_vma_op_commit(struct xe_vm *vm, struct xe_vma_op *op)
{
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		err |= xe_vm_insert_vma(vm, op->map.vma);
		if (!err)
			op->flags |= XE_VMA_OP_COMMITTED;
		break;
	case DRM_GPUVA_OP_REMAP:
		prep_vma_destroy(vm, gpuva_to_vma(op->base.remap.unmap->va),
				 true);
		op->flags |= XE_VMA_OP_COMMITTED;

		if (op->remap.prev) {
			err |= xe_vm_insert_vma(vm, op->remap.prev);
			if (!err)
				op->flags |= XE_VMA_OP_PREV_COMMITTED;
			if (!err && op->remap.skip_prev)
				op->remap.prev = NULL;
		}
		if (op->remap.next) {
			err |= xe_vm_insert_vma(vm, op->remap.next);
			if (!err)
				op->flags |= XE_VMA_OP_NEXT_COMMITTED;
			if (!err && op->remap.skip_next)
				op->remap.next = NULL;
		}

		/* Adjust for partial unbind after removin VMA from VM */
		if (!err) {
			op->base.remap.unmap->va->va.addr = op->remap.start;
			op->base.remap.unmap->va->va.range = op->remap.range;
		}
		break;
	case DRM_GPUVA_OP_UNMAP:
		prep_vma_destroy(vm, gpuva_to_vma(op->base.unmap.va), true);
		op->flags |= XE_VMA_OP_COMMITTED;
		break;
	case DRM_GPUVA_OP_PREFETCH:
		op->flags |= XE_VMA_OP_COMMITTED;
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return err;
}


static int vm_bind_ioctl_ops_parse(struct xe_vm *vm, struct xe_exec_queue *q,
				   struct drm_gpuva_ops *ops,
				   struct xe_sync_entry *syncs, u32 num_syncs,
				   struct list_head *ops_list, bool last,
				   bool async)
{
	struct xe_vma_op *last_op = NULL;
	struct async_op_fence *fence = NULL;
	struct drm_gpuva_op *__op;
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	if (last && num_syncs && async) {
		u64 seqno;

		fence = kmalloc(sizeof(*fence), GFP_KERNEL);
		if (!fence)
			return -ENOMEM;

		seqno = q ? ++q->bind.fence_seqno : ++vm->async_ops.fence.seqno;
		dma_fence_init(&fence->fence, &async_op_fence_ops,
			       &vm->async_ops.lock, q ? q->bind.fence_ctx :
			       vm->async_ops.fence.context, seqno);

		if (!xe_vm_no_dma_fences(vm)) {
			fence->vm = vm;
			fence->started = false;
			init_waitqueue_head(&fence->wq);
		}
	}

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);
		bool first = list_empty(ops_list);

		xe_assert(vm->xe, first || async);

		INIT_LIST_HEAD(&op->link);
		list_add_tail(&op->link, ops_list);

		if (first) {
			op->flags |= XE_VMA_OP_FIRST;
			op->num_syncs = num_syncs;
			op->syncs = syncs;
		}

		op->q = q;

		switch (op->base.op) {
		case DRM_GPUVA_OP_MAP:
		{
			struct xe_vma *vma;

			vma = new_vma(vm, &op->base.map,
				      op->tile_mask, op->map.read_only,
				      op->map.is_null);
			if (IS_ERR(vma)) {
				err = PTR_ERR(vma);
				goto free_fence;
			}

			op->map.vma = vma;
			break;
		}
		case DRM_GPUVA_OP_REMAP:
		{
			struct xe_vma *old =
				gpuva_to_vma(op->base.remap.unmap->va);

			op->remap.start = xe_vma_start(old);
			op->remap.range = xe_vma_size(old);

			if (op->base.remap.prev) {
				struct xe_vma *vma;
				bool read_only =
					op->base.remap.unmap->va->flags &
					XE_VMA_READ_ONLY;
				bool is_null =
					op->base.remap.unmap->va->flags &
					DRM_GPUVA_SPARSE;

				vma = new_vma(vm, op->base.remap.prev,
					      op->tile_mask, read_only,
					      is_null);
				if (IS_ERR(vma)) {
					err = PTR_ERR(vma);
					goto free_fence;
				}

				op->remap.prev = vma;

				/*
				 * Userptr creates a new SG mapping so
				 * we must also rebind.
				 */
				op->remap.skip_prev = !xe_vma_is_userptr(old) &&
					IS_ALIGNED(xe_vma_end(vma),
						   xe_vma_max_pte_size(old));
				if (op->remap.skip_prev) {
					xe_vma_set_pte_size(vma, xe_vma_max_pte_size(old));
					op->remap.range -=
						xe_vma_end(vma) -
						xe_vma_start(old);
					op->remap.start = xe_vma_end(vma);
				}
			}

			if (op->base.remap.next) {
				struct xe_vma *vma;
				bool read_only =
					op->base.remap.unmap->va->flags &
					XE_VMA_READ_ONLY;

				bool is_null =
					op->base.remap.unmap->va->flags &
					DRM_GPUVA_SPARSE;

				vma = new_vma(vm, op->base.remap.next,
					      op->tile_mask, read_only,
					      is_null);
				if (IS_ERR(vma)) {
					err = PTR_ERR(vma);
					goto free_fence;
				}

				op->remap.next = vma;

				/*
				 * Userptr creates a new SG mapping so
				 * we must also rebind.
				 */
				op->remap.skip_next = !xe_vma_is_userptr(old) &&
					IS_ALIGNED(xe_vma_start(vma),
						   xe_vma_max_pte_size(old));
				if (op->remap.skip_next) {
					xe_vma_set_pte_size(vma, xe_vma_max_pte_size(old));
					op->remap.range -=
						xe_vma_end(old) -
						xe_vma_start(vma);
				}
			}
			break;
		}
		case DRM_GPUVA_OP_UNMAP:
		case DRM_GPUVA_OP_PREFETCH:
			/* Nothing to do */
			break;
		default:
			drm_warn(&vm->xe->drm, "NOT POSSIBLE");
		}

		last_op = op;

		err = xe_vma_op_commit(vm, op);
		if (err)
			goto free_fence;
	}

	/* FIXME: Unhandled corner case */
	XE_WARN_ON(!last_op && last && !list_empty(ops_list));

	if (!last_op)
		goto free_fence;
	last_op->ops = ops;
	if (last) {
		last_op->flags |= XE_VMA_OP_LAST;
		last_op->num_syncs = num_syncs;
		last_op->syncs = syncs;
		last_op->fence = fence;
	}

	return 0;

free_fence:
	kfree(fence);
	return err;
}

static int op_execute(struct drm_exec *exec, struct xe_vm *vm,
		      struct xe_vma *vma, struct xe_vma_op *op)
{
	int err;

	lockdep_assert_held_write(&vm->lock);

	err = xe_vm_prepare_vma(exec, vma, 1);
	if (err)
		return err;

	xe_vm_assert_held(vm);
	xe_bo_assert_held(xe_vma_bo(vma));

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		err = xe_vm_bind(vm, vma, op->q, xe_vma_bo(vma),
				 op->syncs, op->num_syncs, op->fence,
				 op->map.immediate || !xe_vm_in_fault_mode(vm),
				 op->flags & XE_VMA_OP_FIRST,
				 op->flags & XE_VMA_OP_LAST);
		break;
	case DRM_GPUVA_OP_REMAP:
	{
		bool prev = !!op->remap.prev;
		bool next = !!op->remap.next;

		if (!op->remap.unmap_done) {
			if (prev || next) {
				vm->async_ops.munmap_rebind_inflight = true;
				vma->gpuva.flags |= XE_VMA_FIRST_REBIND;
			}
			err = xe_vm_unbind(vm, vma, op->q, op->syncs,
					   op->num_syncs,
					   !prev && !next ? op->fence : NULL,
					   op->flags & XE_VMA_OP_FIRST,
					   op->flags & XE_VMA_OP_LAST && !prev &&
					   !next);
			if (err)
				break;
			op->remap.unmap_done = true;
		}

		if (prev) {
			op->remap.prev->gpuva.flags |= XE_VMA_LAST_REBIND;
			err = xe_vm_bind(vm, op->remap.prev, op->q,
					 xe_vma_bo(op->remap.prev), op->syncs,
					 op->num_syncs,
					 !next ? op->fence : NULL, true, false,
					 op->flags & XE_VMA_OP_LAST && !next);
			op->remap.prev->gpuva.flags &= ~XE_VMA_LAST_REBIND;
			if (err)
				break;
			op->remap.prev = NULL;
		}

		if (next) {
			op->remap.next->gpuva.flags |= XE_VMA_LAST_REBIND;
			err = xe_vm_bind(vm, op->remap.next, op->q,
					 xe_vma_bo(op->remap.next),
					 op->syncs, op->num_syncs,
					 op->fence, true, false,
					 op->flags & XE_VMA_OP_LAST);
			op->remap.next->gpuva.flags &= ~XE_VMA_LAST_REBIND;
			if (err)
				break;
			op->remap.next = NULL;
		}
		vm->async_ops.munmap_rebind_inflight = false;

		break;
	}
	case DRM_GPUVA_OP_UNMAP:
		err = xe_vm_unbind(vm, vma, op->q, op->syncs,
				   op->num_syncs, op->fence,
				   op->flags & XE_VMA_OP_FIRST,
				   op->flags & XE_VMA_OP_LAST);
		break;
	case DRM_GPUVA_OP_PREFETCH:
		err = xe_vm_prefetch(vm, vma, op->q, op->prefetch.region,
				     op->syncs, op->num_syncs, op->fence,
				     op->flags & XE_VMA_OP_FIRST,
				     op->flags & XE_VMA_OP_LAST);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	if (err)
		trace_xe_vma_fail(vma);

	return err;
}

static int __xe_vma_op_execute(struct xe_vm *vm, struct xe_vma *vma,
			       struct xe_vma_op *op)
{
	struct drm_exec exec;
	int err;

retry_userptr:
	drm_exec_init(&exec, DRM_EXEC_INTERRUPTIBLE_WAIT);
	drm_exec_until_all_locked(&exec) {
		err = op_execute(&exec, vm, vma, op);
		drm_exec_retry_on_contention(&exec);
		if (err)
			break;
	}
	drm_exec_fini(&exec);

	if (err == -EAGAIN && xe_vma_is_userptr(vma)) {
		lockdep_assert_held_write(&vm->lock);
		err = xe_vma_userptr_pin_pages(vma);
		if (!err)
			goto retry_userptr;

		trace_xe_vma_fail(vma);
	}

	return err;
}

static int xe_vma_op_execute(struct xe_vm *vm, struct xe_vma_op *op)
{
	int ret = 0;

	lockdep_assert_held_write(&vm->lock);

#ifdef TEST_VM_ASYNC_OPS_ERROR
	if (op->inject_error) {
		op->inject_error = false;
		return -ENOMEM;
	}
#endif

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		ret = __xe_vma_op_execute(vm, op->map.vma, op);
		break;
	case DRM_GPUVA_OP_REMAP:
	{
		struct xe_vma *vma;

		if (!op->remap.unmap_done)
			vma = gpuva_to_vma(op->base.remap.unmap->va);
		else if (op->remap.prev)
			vma = op->remap.prev;
		else
			vma = op->remap.next;

		ret = __xe_vma_op_execute(vm, vma, op);
		break;
	}
	case DRM_GPUVA_OP_UNMAP:
		ret = __xe_vma_op_execute(vm, gpuva_to_vma(op->base.unmap.va),
					  op);
		break;
	case DRM_GPUVA_OP_PREFETCH:
		ret = __xe_vma_op_execute(vm,
					  gpuva_to_vma(op->base.prefetch.va),
					  op);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return ret;
}

static void xe_vma_op_cleanup(struct xe_vm *vm, struct xe_vma_op *op)
{
	bool last = op->flags & XE_VMA_OP_LAST;

	if (last) {
		while (op->num_syncs--)
			xe_sync_entry_cleanup(&op->syncs[op->num_syncs]);
		kfree(op->syncs);
		if (op->q)
			xe_exec_queue_put(op->q);
		if (op->fence)
			dma_fence_put(&op->fence->fence);
	}
	if (!list_empty(&op->link)) {
		spin_lock_irq(&vm->async_ops.lock);
		list_del(&op->link);
		spin_unlock_irq(&vm->async_ops.lock);
	}
	if (op->ops)
		drm_gpuva_ops_free(&vm->gpuvm, op->ops);
	if (last)
		xe_vm_put(vm);
}

static void xe_vma_op_unwind(struct xe_vm *vm, struct xe_vma_op *op,
			     bool post_commit, bool prev_post_commit,
			     bool next_post_commit)
{
	lockdep_assert_held_write(&vm->lock);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		if (op->map.vma) {
			prep_vma_destroy(vm, op->map.vma, post_commit);
			xe_vma_destroy_unlocked(op->map.vma);
		}
		break;
	case DRM_GPUVA_OP_UNMAP:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.unmap.va);

		if (vma) {
			down_read(&vm->userptr.notifier_lock);
			vma->gpuva.flags &= ~XE_VMA_DESTROYED;
			up_read(&vm->userptr.notifier_lock);
			if (post_commit)
				xe_vm_insert_vma(vm, vma);
		}
		break;
	}
	case DRM_GPUVA_OP_REMAP:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.remap.unmap->va);

		if (op->remap.prev) {
			prep_vma_destroy(vm, op->remap.prev, prev_post_commit);
			xe_vma_destroy_unlocked(op->remap.prev);
		}
		if (op->remap.next) {
			prep_vma_destroy(vm, op->remap.next, next_post_commit);
			xe_vma_destroy_unlocked(op->remap.next);
		}
		if (vma) {
			down_read(&vm->userptr.notifier_lock);
			vma->gpuva.flags &= ~XE_VMA_DESTROYED;
			up_read(&vm->userptr.notifier_lock);
			if (post_commit)
				xe_vm_insert_vma(vm, vma);
		}
		break;
	}
	case DRM_GPUVA_OP_PREFETCH:
		/* Nothing to do */
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}
}

static struct xe_vma_op *next_vma_op(struct xe_vm *vm)
{
	return list_first_entry_or_null(&vm->async_ops.pending,
					struct xe_vma_op, link);
}

static void xe_vma_op_work_func(struct work_struct *w)
{
	struct xe_vm *vm = container_of(w, struct xe_vm, async_ops.work);

	for (;;) {
		struct xe_vma_op *op;
		int err;

		if (vm->async_ops.error && !xe_vm_is_closed(vm))
			break;

		spin_lock_irq(&vm->async_ops.lock);
		op = next_vma_op(vm);
		spin_unlock_irq(&vm->async_ops.lock);

		if (!op)
			break;

		if (!xe_vm_is_closed(vm)) {
			down_write(&vm->lock);
			err = xe_vma_op_execute(vm, op);
			if (err) {
				drm_warn(&vm->xe->drm,
					 "Async VM op(%d) failed with %d",
					 op->base.op, err);
				vm_set_async_error(vm, err);
				up_write(&vm->lock);

				break;
			}
			up_write(&vm->lock);
		} else {
			struct xe_vma *vma;

			switch (op->base.op) {
			case DRM_GPUVA_OP_REMAP:
				vma = gpuva_to_vma(op->base.remap.unmap->va);
				trace_xe_vma_flush(vma);

				down_write(&vm->lock);
				xe_vma_destroy_unlocked(vma);
				up_write(&vm->lock);
				break;
			case DRM_GPUVA_OP_UNMAP:
				vma = gpuva_to_vma(op->base.unmap.va);
				trace_xe_vma_flush(vma);

				down_write(&vm->lock);
				xe_vma_destroy_unlocked(vma);
				up_write(&vm->lock);
				break;
			default:
				/* Nothing to do */
				break;
			}

			if (op->fence && !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
						   &op->fence->fence.flags)) {
				if (!xe_vm_no_dma_fences(vm)) {
					op->fence->started = true;
					wake_up_all(&op->fence->wq);
				}
				dma_fence_signal(&op->fence->fence);
			}
		}

		xe_vma_op_cleanup(vm, op);
	}
}

static int vm_bind_ioctl_ops_execute(struct xe_vm *vm,
				     struct list_head *ops_list, bool async)
{
	struct xe_vma_op *op, *last_op, *next;
	int err;

	lockdep_assert_held_write(&vm->lock);

	last_op = list_last_entry(ops_list, struct xe_vma_op, link);

	if (!async) {
		err = xe_vma_op_execute(vm, last_op);
		if (err)
			goto unwind;
		xe_vma_op_cleanup(vm, last_op);
	} else {
		int i;
		bool installed = false;

		for (i = 0; i < last_op->num_syncs; i++)
			installed |= xe_sync_entry_signal(&last_op->syncs[i],
							  NULL,
							  &last_op->fence->fence);
		if (!installed && last_op->fence)
			dma_fence_signal(&last_op->fence->fence);

		spin_lock_irq(&vm->async_ops.lock);
		list_splice_tail(ops_list, &vm->async_ops.pending);
		spin_unlock_irq(&vm->async_ops.lock);

		if (!vm->async_ops.error)
			queue_work(system_unbound_wq, &vm->async_ops.work);
	}

	return 0;

unwind:
	list_for_each_entry_reverse(op, ops_list, link)
		xe_vma_op_unwind(vm, op, op->flags & XE_VMA_OP_COMMITTED,
				 op->flags & XE_VMA_OP_PREV_COMMITTED,
				 op->flags & XE_VMA_OP_NEXT_COMMITTED);
	list_for_each_entry_safe(op, next, ops_list, link)
		xe_vma_op_cleanup(vm, op);

	return err;
}

static void vm_bind_ioctl_ops_unwind(struct xe_vm *vm,
				     struct drm_gpuva_ops **ops,
				     int num_ops_list)
{
	int i;

	for (i = num_ops_list - 1; i; ++i) {
		struct drm_gpuva_ops *__ops = ops[i];
		struct drm_gpuva_op *__op;

		if (!__ops)
			continue;

		drm_gpuva_for_each_op_reverse(__op, __ops) {
			struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

			xe_vma_op_unwind(vm, op,
					 op->flags & XE_VMA_OP_COMMITTED,
					 op->flags & XE_VMA_OP_PREV_COMMITTED,
					 op->flags & XE_VMA_OP_NEXT_COMMITTED);
		}

		drm_gpuva_ops_free(&vm->gpuvm, __ops);
	}
}

#ifdef TEST_VM_ASYNC_OPS_ERROR
#define SUPPORTED_FLAGS	\
	(FORCE_ASYNC_OP_ERROR | XE_VM_BIND_FLAG_ASYNC | \
	 XE_VM_BIND_FLAG_READONLY | XE_VM_BIND_FLAG_IMMEDIATE | \
	 XE_VM_BIND_FLAG_NULL | 0xffff)
#else
#define SUPPORTED_FLAGS	\
	(XE_VM_BIND_FLAG_ASYNC | XE_VM_BIND_FLAG_READONLY | \
	 XE_VM_BIND_FLAG_IMMEDIATE | XE_VM_BIND_FLAG_NULL | 0xffff)
#endif
#define XE_64K_PAGE_MASK 0xffffull

#define MAX_BINDS	512	/* FIXME: Picking random upper limit */

static int vm_bind_ioctl_check_args(struct xe_device *xe,
				    struct drm_xe_vm_bind *args,
				    struct drm_xe_vm_bind_op **bind_ops,
				    bool *async)
{
	int err;
	int i;

	if (XE_IOCTL_DBG(xe, args->extensions) ||
	    XE_IOCTL_DBG(xe, !args->num_binds) ||
	    XE_IOCTL_DBG(xe, args->num_binds > MAX_BINDS))
		return -EINVAL;

	if (args->num_binds > 1) {
		u64 __user *bind_user =
			u64_to_user_ptr(args->vector_of_binds);

		*bind_ops = kmalloc(sizeof(struct drm_xe_vm_bind_op) *
				    args->num_binds, GFP_KERNEL);
		if (!*bind_ops)
			return -ENOMEM;

		err = __copy_from_user(*bind_ops, bind_user,
				       sizeof(struct drm_xe_vm_bind_op) *
				       args->num_binds);
		if (XE_IOCTL_DBG(xe, err)) {
			err = -EFAULT;
			goto free_bind_ops;
		}
	} else {
		*bind_ops = &args->bind;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = (*bind_ops)[i].range;
		u64 addr = (*bind_ops)[i].addr;
		u32 op = (*bind_ops)[i].op;
		u32 flags = (*bind_ops)[i].flags;
		u32 obj = (*bind_ops)[i].obj;
		u64 obj_offset = (*bind_ops)[i].obj_offset;
		u32 region = (*bind_ops)[i].region;
		bool is_null = flags & XE_VM_BIND_FLAG_NULL;

		if (i == 0) {
			*async = !!(flags & XE_VM_BIND_FLAG_ASYNC);
		} else if (XE_IOCTL_DBG(xe, !*async) ||
			   XE_IOCTL_DBG(xe, !(flags & XE_VM_BIND_FLAG_ASYNC)) ||
			   XE_IOCTL_DBG(xe, op == XE_VM_BIND_OP_RESTART)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, !*async &&
				 op == XE_VM_BIND_OP_UNMAP_ALL)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, !*async &&
				 op == XE_VM_BIND_OP_PREFETCH)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, op > XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, flags & ~SUPPORTED_FLAGS) ||
		    XE_IOCTL_DBG(xe, obj && is_null) ||
		    XE_IOCTL_DBG(xe, obj_offset && is_null) ||
		    XE_IOCTL_DBG(xe, op != XE_VM_BIND_OP_MAP &&
				 is_null) ||
		    XE_IOCTL_DBG(xe, !obj &&
				 op == XE_VM_BIND_OP_MAP &&
				 !is_null) ||
		    XE_IOCTL_DBG(xe, !obj &&
				 op == XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, addr &&
				 op == XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, range &&
				 op == XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == XE_VM_BIND_OP_MAP_USERPTR) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, region &&
				 op != XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, !(BIT(region) &
				       xe->info.mem_region_mask)) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == XE_VM_BIND_OP_UNMAP)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, obj_offset & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, addr & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, range & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, !range && op !=
				 XE_VM_BIND_OP_RESTART &&
				 op != XE_VM_BIND_OP_UNMAP_ALL)) {
			err = -EINVAL;
			goto free_bind_ops;
		}
	}

	return 0;

free_bind_ops:
	if (args->num_binds > 1)
		kfree(*bind_ops);
	return err;
}

int xe_vm_bind_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_bind *args = data;
	struct drm_xe_sync __user *syncs_user;
	struct xe_bo **bos = NULL;
	struct drm_gpuva_ops **ops = NULL;
	struct xe_vm *vm;
	struct xe_exec_queue *q = NULL;
	u32 num_syncs;
	struct xe_sync_entry *syncs = NULL;
	struct drm_xe_vm_bind_op *bind_ops;
	LIST_HEAD(ops_list);
	bool async;
	int err;
	int i;

	err = vm_bind_ioctl_check_args(xe, args, &bind_ops, &async);
	if (err)
		return err;

	if (args->exec_queue_id) {
		q = xe_exec_queue_lookup(xef, args->exec_queue_id);
		if (XE_IOCTL_DBG(xe, !q)) {
			err = -ENOENT;
			goto free_objs;
		}

		if (XE_IOCTL_DBG(xe, !(q->flags & EXEC_QUEUE_FLAG_VM))) {
			err = -EINVAL;
			goto put_exec_queue;
		}
	}

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm)) {
		err = -EINVAL;
		goto put_exec_queue;
	}

	err = down_write_killable(&vm->lock);
	if (err)
		goto put_vm;

	if (XE_IOCTL_DBG(xe, xe_vm_is_closed_or_banned(vm))) {
		err = -ENOENT;
		goto release_vm_lock;
	}

	if (bind_ops[0].op == XE_VM_BIND_OP_RESTART) {
		if (XE_IOCTL_DBG(xe, !(vm->flags & XE_VM_FLAG_ASYNC_BIND_OPS)))
			err = -EOPNOTSUPP;
		if (XE_IOCTL_DBG(xe, !err && args->num_syncs))
			err = EINVAL;
		if (XE_IOCTL_DBG(xe, !err && !vm->async_ops.error))
			err = -EPROTO;

		if (!err) {
			trace_xe_vm_restart(vm);
			vm_set_async_error(vm, 0);

			queue_work(system_unbound_wq, &vm->async_ops.work);

			/* Rebinds may have been blocked, give worker a kick */
			if (xe_vm_in_compute_mode(vm))
				xe_vm_queue_rebind_worker(vm);
		}

		goto release_vm_lock;
	}

	if (XE_IOCTL_DBG(xe, !vm->async_ops.error &&
			 async != !!(vm->flags & XE_VM_FLAG_ASYNC_BIND_OPS))) {
		err = -EOPNOTSUPP;
		goto release_vm_lock;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;

		if (XE_IOCTL_DBG(xe, range > vm->size) ||
		    XE_IOCTL_DBG(xe, addr > vm->size - range)) {
			err = -EINVAL;
			goto release_vm_lock;
		}

		if (bind_ops[i].tile_mask) {
			u64 valid_tiles = BIT(xe->info.tile_count) - 1;

			if (XE_IOCTL_DBG(xe, bind_ops[i].tile_mask &
					 ~valid_tiles)) {
				err = -EINVAL;
				goto release_vm_lock;
			}
		}
	}

	bos = kzalloc(sizeof(*bos) * args->num_binds, GFP_KERNEL);
	if (!bos) {
		err = -ENOMEM;
		goto release_vm_lock;
	}

	ops = kzalloc(sizeof(*ops) * args->num_binds, GFP_KERNEL);
	if (!ops) {
		err = -ENOMEM;
		goto release_vm_lock;
	}

	for (i = 0; i < args->num_binds; ++i) {
		struct drm_gem_object *gem_obj;
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 obj = bind_ops[i].obj;
		u64 obj_offset = bind_ops[i].obj_offset;

		if (!obj)
			continue;

		gem_obj = drm_gem_object_lookup(file, obj);
		if (XE_IOCTL_DBG(xe, !gem_obj)) {
			err = -ENOENT;
			goto put_obj;
		}
		bos[i] = gem_to_xe_bo(gem_obj);

		if (XE_IOCTL_DBG(xe, range > bos[i]->size) ||
		    XE_IOCTL_DBG(xe, obj_offset >
				 bos[i]->size - range)) {
			err = -EINVAL;
			goto put_obj;
		}

		if (bos[i]->flags & XE_BO_INTERNAL_64K) {
			if (XE_IOCTL_DBG(xe, obj_offset &
					 XE_64K_PAGE_MASK) ||
			    XE_IOCTL_DBG(xe, addr & XE_64K_PAGE_MASK) ||
			    XE_IOCTL_DBG(xe, range & XE_64K_PAGE_MASK)) {
				err = -EINVAL;
				goto put_obj;
			}
		}
	}

	if (args->num_syncs) {
		syncs = kcalloc(args->num_syncs, sizeof(*syncs), GFP_KERNEL);
		if (!syncs) {
			err = -ENOMEM;
			goto put_obj;
		}
	}

	syncs_user = u64_to_user_ptr(args->syncs);
	for (num_syncs = 0; num_syncs < args->num_syncs; num_syncs++) {
		err = xe_sync_entry_parse(xe, xef, &syncs[num_syncs],
					  &syncs_user[num_syncs], false,
					  xe_vm_no_dma_fences(vm));
		if (err)
			goto free_syncs;
	}

	/* Do some error checking first to make the unwind easier */
	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 op = bind_ops[i].op;
		u32 flags = bind_ops[i].flags;

		err = vm_bind_ioctl_lookup_vma(vm, bos[i], addr, range, op, flags);
		if (err)
			goto free_syncs;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 op = bind_ops[i].op;
		u32 flags = bind_ops[i].flags;
		u64 obj_offset = bind_ops[i].obj_offset;
		u8 tile_mask = bind_ops[i].tile_mask;
		u32 region = bind_ops[i].region;

		ops[i] = vm_bind_ioctl_ops_create(vm, bos[i], obj_offset,
						  addr, range, op, flags,
						  tile_mask, region);
		if (IS_ERR(ops[i])) {
			err = PTR_ERR(ops[i]);
			ops[i] = NULL;
			goto unwind_ops;
		}

		err = vm_bind_ioctl_ops_parse(vm, q, ops[i], syncs, num_syncs,
					      &ops_list,
					      i == args->num_binds - 1,
					      async);
		if (err)
			goto unwind_ops;
	}

	/* Nothing to do */
	if (list_empty(&ops_list)) {
		err = -ENODATA;
		goto unwind_ops;
	}

	err = vm_bind_ioctl_ops_execute(vm, &ops_list, async);
	up_write(&vm->lock);

	for (i = 0; i < args->num_binds; ++i)
		xe_bo_put(bos[i]);

	kfree(bos);
	kfree(ops);
	if (args->num_binds > 1)
		kfree(bind_ops);

	return err;

unwind_ops:
	vm_bind_ioctl_ops_unwind(vm, ops, args->num_binds);
free_syncs:
	for (i = 0; err == -ENODATA && i < num_syncs; i++)
		xe_sync_entry_signal(&syncs[i], NULL, dma_fence_get_stub());
	while (num_syncs--)
		xe_sync_entry_cleanup(&syncs[num_syncs]);

	kfree(syncs);
put_obj:
	for (i = 0; i < args->num_binds; ++i)
		xe_bo_put(bos[i]);
release_vm_lock:
	up_write(&vm->lock);
put_vm:
	xe_vm_put(vm);
put_exec_queue:
	if (q)
		xe_exec_queue_put(q);
free_objs:
	kfree(bos);
	kfree(ops);
	if (args->num_binds > 1)
		kfree(bind_ops);
	return err == -ENODATA ? 0 : err;
}

/**
 * xe_vm_lock() - Lock the vm's dma_resv object
 * @vm: The struct xe_vm whose lock is to be locked
 * @intr: Whether to perform any wait interruptible
 *
 * Return: 0 on success, -EINTR if @intr is true and the wait for a
 * contended lock was interrupted. If @intr is false, the function
 * always returns 0.
 */
int xe_vm_lock(struct xe_vm *vm, bool intr)
{
	if (intr)
		return dma_resv_lock_interruptible(xe_vm_resv(vm), NULL);

	return dma_resv_lock(xe_vm_resv(vm), NULL);
}

/**
 * xe_vm_unlock() - Unlock the vm's dma_resv object
 * @vm: The struct xe_vm whose lock is to be released.
 *
 * Unlock a buffer object lock that was locked by xe_vm_lock().
 */
void xe_vm_unlock(struct xe_vm *vm)
{
	dma_resv_unlock(xe_vm_resv(vm));
}

/**
 * xe_vm_invalidate_vma - invalidate GPU mappings for VMA without a lock
 * @vma: VMA to invalidate
 *
 * Walks a list of page tables leaves which it memset the entries owned by this
 * VMA to zero, invalidates the TLBs, and block until TLBs invalidation is
 * complete.
 *
 * Returns 0 for success, negative error code otherwise.
 */
int xe_vm_invalidate_vma(struct xe_vma *vma)
{
	struct xe_device *xe = xe_vma_vm(vma)->xe;
	struct xe_tile *tile;
	u32 tile_needs_invalidate = 0;
	int seqno[XE_MAX_TILES_PER_DEVICE];
	u8 id;
	int ret;

	xe_assert(xe, xe_vm_in_fault_mode(xe_vma_vm(vma)));
	xe_assert(xe, !xe_vma_is_null(vma));
	trace_xe_vma_usm_invalidate(vma);

	/* Check that we don't race with page-table updates */
	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		if (xe_vma_is_userptr(vma)) {
			WARN_ON_ONCE(!mmu_interval_check_retry
				     (&vma->userptr.notifier,
				      vma->userptr.notifier_seq));
			WARN_ON_ONCE(!dma_resv_test_signaled(xe_vm_resv(xe_vma_vm(vma)),
							     DMA_RESV_USAGE_BOOKKEEP));

		} else {
			xe_bo_assert_held(xe_vma_bo(vma));
		}
	}

	for_each_tile(tile, xe, id) {
		if (xe_pt_zap_ptes(tile, vma)) {
			tile_needs_invalidate |= BIT(id);
			xe_device_wmb(xe);
			/*
			 * FIXME: We potentially need to invalidate multiple
			 * GTs within the tile
			 */
			seqno[id] = xe_gt_tlb_invalidation_vma(tile->primary_gt, NULL, vma);
			if (seqno[id] < 0)
				return seqno[id];
		}
	}

	for_each_tile(tile, xe, id) {
		if (tile_needs_invalidate & BIT(id)) {
			ret = xe_gt_tlb_invalidation_wait(tile->primary_gt, seqno[id]);
			if (ret < 0)
				return ret;
		}
	}

	vma->usm.tile_invalidated = vma->tile_mask;

	return 0;
}

int xe_analyze_vm(struct drm_printer *p, struct xe_vm *vm, int gt_id)
{
	struct drm_gpuva *gpuva;
	bool is_vram;
	uint64_t addr;

	if (!down_read_trylock(&vm->lock)) {
		drm_printf(p, " Failed to acquire VM lock to dump capture");
		return 0;
	}
	if (vm->pt_root[gt_id]) {
		addr = xe_bo_addr(vm->pt_root[gt_id]->bo, 0, XE_PAGE_SIZE);
		is_vram = xe_bo_is_vram(vm->pt_root[gt_id]->bo);
		drm_printf(p, " VM root: A:0x%llx %s\n", addr,
			   is_vram ? "VRAM" : "SYS");
	}

	drm_gpuvm_for_each_va(gpuva, &vm->gpuvm) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);
		bool is_userptr = xe_vma_is_userptr(vma);
		bool is_null = xe_vma_is_null(vma);

		if (is_null) {
			addr = 0;
		} else if (is_userptr) {
			struct xe_res_cursor cur;

			if (vma->userptr.sg) {
				xe_res_first_sg(vma->userptr.sg, 0, XE_PAGE_SIZE,
						&cur);
				addr = xe_res_dma(&cur);
			} else {
				addr = 0;
			}
		} else {
			addr = __xe_bo_addr(xe_vma_bo(vma), 0, XE_PAGE_SIZE);
			is_vram = xe_bo_is_vram(xe_vma_bo(vma));
		}
		drm_printf(p, " [%016llx-%016llx] S:0x%016llx A:%016llx %s\n",
			   xe_vma_start(vma), xe_vma_end(vma) - 1,
			   xe_vma_size(vma),
			   addr, is_null ? "NULL" : is_userptr ? "USR" :
			   is_vram ? "VRAM" : "SYS");
	}
	up_read(&vm->lock);

	return 0;
}
