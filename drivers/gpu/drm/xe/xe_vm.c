// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_vm.h"

#include <linux/dma-fence-array.h>

#include <drm/ttm/ttm_execbuf_util.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/xe_drm.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include "xe_bo.h"
#include "xe_device.h"
#include "xe_engine.h"
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

#define TEST_VM_ASYNC_OPS_ERROR

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
	struct xe_vm *vm = vma->vm;
	struct xe_device *xe = vm->xe;
	const unsigned long num_pages =
		(vma->end - vma->start + 1) >> PAGE_SHIFT;
	struct page **pages;
	bool in_kthread = !current->mm;
	unsigned long notifier_seq;
	int pinned, ret, i;
	bool read_only = vma->pte_flags & XE_PTE_READ_ONLY;

	lockdep_assert_held(&vm->lock);
	XE_BUG_ON(!xe_vma_is_userptr(vma));
retry:
	if (vma->destroyed)
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
		ret = get_user_pages_fast(vma->userptr.ptr + pinned * PAGE_SIZE,
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

	ret = sg_alloc_table_from_pages(&vma->userptr.sgt, pages, pinned,
					0, (u64)pinned << PAGE_SHIFT,
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
	struct xe_engine *e;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(e, &vm->preempt.engines, compute.link) {
		if (!e->compute.pfence || (e->compute.pfence &&
		    test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			     &e->compute.pfence->flags))) {
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

	if (*count >= vm->preempt.num_engines)
		return 0;

	for (; *count < vm->preempt.num_engines; ++(*count)) {
		struct xe_preempt_fence *pfence = xe_preempt_fence_alloc();

		if (IS_ERR(pfence))
			return PTR_ERR(pfence);

		list_move_tail(xe_preempt_fence_link(pfence), list);
	}

	return 0;
}

static int wait_for_existing_preempt_fences(struct xe_vm *vm)
{
	struct xe_engine *e;

	xe_vm_assert_held(vm);

	list_for_each_entry(e, &vm->preempt.engines, compute.link) {
		if (e->compute.pfence) {
			long timeout = dma_fence_wait(e->compute.pfence, false);

			if (timeout < 0)
				return -ETIME;
			dma_fence_put(e->compute.pfence);
			e->compute.pfence = NULL;
		}
	}

	return 0;
}

static bool xe_vm_is_idle(struct xe_vm *vm)
{
	struct xe_engine *e;

	xe_vm_assert_held(vm);
	list_for_each_entry(e, &vm->preempt.engines, compute.link) {
		if (!xe_engine_is_idle(e))
			return false;
	}

	return true;
}

static void arm_preempt_fences(struct xe_vm *vm, struct list_head *list)
{
	struct list_head *link;
	struct xe_engine *e;

	list_for_each_entry(e, &vm->preempt.engines, compute.link) {
		struct dma_fence *fence;

		link = list->next;
		XE_BUG_ON(link == list);

		fence = xe_preempt_fence_arm(to_preempt_fence_from_link(link),
					     e, e->compute.context,
					     ++e->compute.seqno);
		dma_fence_put(e->compute.pfence);
		e->compute.pfence = fence;
	}
}

static int add_preempt_fences(struct xe_vm *vm, struct xe_bo *bo)
{
	struct xe_engine *e;
	struct ww_acquire_ctx ww;
	int err;

	err = xe_bo_lock(bo, &ww, vm->preempt.num_engines, true);
	if (err)
		return err;

	list_for_each_entry(e, &vm->preempt.engines, compute.link)
		if (e->compute.pfence) {
			dma_resv_add_fence(bo->ttm.base.resv,
					   e->compute.pfence,
					   DMA_RESV_USAGE_BOOKKEEP);
		}

	xe_bo_unlock(bo, &ww);
	return 0;
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
		dma_resv_add_fence(vma->bo->ttm.base.resv, fence, usage);
}

static void resume_and_reinstall_preempt_fences(struct xe_vm *vm)
{
	struct xe_engine *e;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(e, &vm->preempt.engines, compute.link) {
		e->ops->resume(e);

		dma_resv_add_fence(&vm->resv, e->compute.pfence,
				   DMA_RESV_USAGE_BOOKKEEP);
		xe_vm_fence_all_extobjs(vm, e->compute.pfence,
					DMA_RESV_USAGE_BOOKKEEP);
	}
}

int xe_vm_add_compute_engine(struct xe_vm *vm, struct xe_engine *e)
{
	struct ttm_validate_buffer tv_onstack[XE_ONSTACK_TV];
	struct ttm_validate_buffer *tv;
	struct ww_acquire_ctx ww;
	struct list_head objs;
	struct dma_fence *pfence;
	int err;
	bool wait;

	XE_BUG_ON(!xe_vm_in_compute_mode(vm));

	down_write(&vm->lock);

	err = xe_vm_lock_dma_resv(vm, &ww, tv_onstack, &tv, &objs, true, 1);
	if (err)
		goto out_unlock_outer;

	pfence = xe_preempt_fence_create(e, e->compute.context,
					 ++e->compute.seqno);
	if (!pfence) {
		err = -ENOMEM;
		goto out_unlock;
	}

	list_add(&e->compute.link, &vm->preempt.engines);
	++vm->preempt.num_engines;
	e->compute.pfence = pfence;

	down_read(&vm->userptr.notifier_lock);

	dma_resv_add_fence(&vm->resv, pfence,
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
	xe_vm_unlock_dma_resv(vm, tv_onstack, tv, &ww, &objs);
out_unlock_outer:
	up_write(&vm->lock);

	return err;
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
 * @ww: Pointer to a struct ww_acquire_ctx locking context.
 * @tv_onstack: Array size XE_ONSTACK_TV of storage for the struct
 * ttm_validate_buffers used for locking.
 * @tv: Pointer to a pointer that on output contains the actual storage used.
 * @objs: List head for the buffer objects locked.
 * @intr: Whether to lock interruptible.
 * @num_shared: Number of dma-fence slots to reserve in the locked objects.
 *
 * Locks the vm dma-resv objects and all the dma-resv objects of the
 * buffer objects on the vm external object list. The TTM utilities require
 * a list of struct ttm_validate_buffers pointing to the actual buffer
 * objects to lock. Storage for those struct ttm_validate_buffers should
 * be provided in @tv_onstack, and is typically reserved on the stack
 * of the caller. If the size of @tv_onstack isn't sufficient, then
 * storage will be allocated internally using kvmalloc().
 *
 * The function performs deadlock handling internally, and after a
 * successful return the ww locking transaction should be considered
 * sealed.
 *
 * Return: 0 on success, Negative error code on error. In particular if
 * @intr is set to true, -EINTR or -ERESTARTSYS may be returned. In case
 * of error, any locking performed has been reverted.
 */
int xe_vm_lock_dma_resv(struct xe_vm *vm, struct ww_acquire_ctx *ww,
			struct ttm_validate_buffer *tv_onstack,
			struct ttm_validate_buffer **tv,
			struct list_head *objs,
			bool intr,
			unsigned int num_shared)
{
	struct ttm_validate_buffer *tv_vm, *tv_bo;
	struct xe_vma *vma, *next;
	LIST_HEAD(dups);
	int err;

	lockdep_assert_held(&vm->lock);

	if (vm->extobj.entries < XE_ONSTACK_TV) {
		tv_vm = tv_onstack;
	} else {
		tv_vm = kvmalloc_array(vm->extobj.entries + 1, sizeof(*tv_vm),
				       GFP_KERNEL);
		if (!tv_vm)
			return -ENOMEM;
	}
	tv_bo = tv_vm + 1;

	INIT_LIST_HEAD(objs);
	list_for_each_entry(vma, &vm->extobj.list, extobj.link) {
		tv_bo->num_shared = num_shared;
		tv_bo->bo = &vma->bo->ttm;

		list_add_tail(&tv_bo->head, objs);
		tv_bo++;
	}
	tv_vm->num_shared = num_shared;
	tv_vm->bo = xe_vm_ttm_bo(vm);
	list_add_tail(&tv_vm->head, objs);
	err = ttm_eu_reserve_buffers(ww, objs, intr, &dups);
	if (err)
		goto out_err;

	spin_lock(&vm->notifier.list_lock);
	list_for_each_entry_safe(vma, next, &vm->notifier.rebind_list,
				 notifier.rebind_link) {
		xe_bo_assert_held(vma->bo);

		list_del_init(&vma->notifier.rebind_link);
		if (vma->gt_present && !vma->destroyed)
			list_move_tail(&vma->rebind_link, &vm->rebind_list);
	}
	spin_unlock(&vm->notifier.list_lock);

	*tv = tv_vm;
	return 0;

out_err:
	if (tv_vm != tv_onstack)
		kvfree(tv_vm);

	return err;
}

/**
 * xe_vm_unlock_dma_resv() - Unlock reservation objects locked by
 * xe_vm_lock_dma_resv()
 * @vm: The vm.
 * @tv_onstack: The @tv_onstack array given to xe_vm_lock_dma_resv().
 * @tv: The value of *@tv given by xe_vm_lock_dma_resv().
 * @ww: The ww_acquire_context used for locking.
 * @objs: The list returned from xe_vm_lock_dma_resv().
 *
 * Unlocks the reservation objects and frees any memory allocated by
 * xe_vm_lock_dma_resv().
 */
void xe_vm_unlock_dma_resv(struct xe_vm *vm,
			   struct ttm_validate_buffer *tv_onstack,
			   struct ttm_validate_buffer *tv,
			   struct ww_acquire_ctx *ww,
			   struct list_head *objs)
{
	/*
	 * Nothing should've been able to enter the list while we were locked,
	 * since we've held the dma-resvs of all the vm's external objects,
	 * and holding the dma_resv of an object is required for list
	 * addition, and we shouldn't add ourselves.
	 */
	XE_WARN_ON(!list_empty(&vm->notifier.rebind_list));

	ttm_eu_backoff_reservation(ww, objs);
	if (tv && tv != tv_onstack)
		kvfree(tv);
}

#define XE_VM_REBIND_RETRY_TIMEOUT_MS 1000

static void preempt_rebind_work_func(struct work_struct *w)
{
	struct xe_vm *vm = container_of(w, struct xe_vm, preempt.rebind_work);
	struct xe_vma *vma;
	struct ttm_validate_buffer tv_onstack[XE_ONSTACK_TV];
	struct ttm_validate_buffer *tv;
	struct ww_acquire_ctx ww;
	struct list_head objs;
	struct dma_fence *rebind_fence;
	unsigned int fence_count = 0;
	LIST_HEAD(preempt_fences);
	ktime_t end = 0;
	int err;
	long wait;
	int __maybe_unused tries = 0;

	XE_BUG_ON(!xe_vm_in_compute_mode(vm));
	trace_xe_vm_rebind_worker_enter(vm);

	if (xe_vm_is_closed(vm)) {
		trace_xe_vm_rebind_worker_exit(vm);
		return;
	}

	down_write(&vm->lock);

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

	err = xe_vm_lock_dma_resv(vm, &ww, tv_onstack, &tv, &objs,
				  false, vm->preempt.num_engines);
	if (err)
		goto out_unlock_outer;

	if (xe_vm_is_idle(vm)) {
		vm->preempt.rebind_deactivated = true;
		goto out_unlock;
	}

	/* Fresh preempt fences already installed. Everyting is running. */
	if (!preempt_fences_waiting(vm))
		goto out_unlock;

	/*
	 * This makes sure vm is completely suspended and also balances
	 * xe_engine suspend- and resume; we resume *all* vm engines below.
	 */
	err = wait_for_existing_preempt_fences(vm);
	if (err)
		goto out_unlock;

	err = alloc_preempt_fences(vm, &preempt_fences, &fence_count);
	if (err)
		goto out_unlock;

	list_for_each_entry(vma, &vm->rebind_list, rebind_link) {
		if (xe_vma_is_userptr(vma) || vma->destroyed)
			continue;

		err = xe_bo_validate(vma->bo, vm, false);
		if (err)
			goto out_unlock;
	}

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
	wait = dma_resv_wait_timeout(&vm->resv,
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

	/* Point of no return. */
	arm_preempt_fences(vm, &preempt_fences);
	resume_and_reinstall_preempt_fences(vm);
	up_read(&vm->userptr.notifier_lock);

out_unlock:
	xe_vm_unlock_dma_resv(vm, tv_onstack, tv, &ww, &objs);
out_unlock_outer:
	if (err == -EAGAIN) {
		trace_xe_vm_rebind_worker_retry(vm);
		goto retry;
	}

	/*
	 * With multiple active VMs, under memory pressure, it is possible that
	 * ttm_bo_validate() run into -EDEADLK and in such case returns -ENOMEM.
	 * Until ttm properly handles locking in such scenarios, best thing the
	 * driver can do is retry with a timeout. Killing the VM or putting it
	 * in error state after timeout or other error scenarios is still TBD.
	 */
	if (err == -ENOMEM) {
		ktime_t cur = ktime_get();

		end = end ? : ktime_add_ms(cur, XE_VM_REBIND_RETRY_TIMEOUT_MS);
		if (ktime_before(cur, end)) {
			msleep(20);
			trace_xe_vm_rebind_worker_retry(vm);
			goto retry;
		}
	}
	up_write(&vm->lock);

	free_preempt_fences(&preempt_fences);

	XE_WARN_ON(err < 0);	/* TODO: Kill VM or put in error state */
	trace_xe_vm_rebind_worker_exit(vm);
}

struct async_op_fence;
static int __xe_vm_bind(struct xe_vm *vm, struct xe_vma *vma,
			struct xe_engine *e, struct xe_sync_entry *syncs,
			u32 num_syncs, struct async_op_fence *afence);

static bool vma_userptr_invalidate(struct mmu_interval_notifier *mni,
				   const struct mmu_notifier_range *range,
				   unsigned long cur_seq)
{
	struct xe_vma *vma = container_of(mni, struct xe_vma, userptr.notifier);
	struct xe_vm *vm = vma->vm;
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	long err;

	XE_BUG_ON(!xe_vma_is_userptr(vma));
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
	if (!xe_vm_in_fault_mode(vm) && !vma->destroyed && vma->gt_present) {
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
	dma_resv_iter_begin(&cursor, &vm->resv,
			    DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, fence)
		dma_fence_enable_sw_signaling(fence);
	dma_resv_iter_end(&cursor);

	err = dma_resv_wait_timeout(&vm->resv,
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
		list_move_tail(&vma->userptr_link, &vm->userptr.repin_list);
	}
	spin_unlock(&vm->userptr.invalidated_lock);

	/* Pin and move to temporary list */
	list_for_each_entry_safe(vma, next, &vm->userptr.repin_list, userptr_link) {
		err = xe_vma_userptr_pin_pages(vma);
		if (err < 0)
			goto out_err;

		list_move_tail(&vma->userptr_link, &tmp_evict);
	}

	/* Take lock and move to rebind_list for rebinding. */
	err = dma_resv_lock_interruptible(&vm->resv, NULL);
	if (err)
		goto out_err;

	list_for_each_entry_safe(vma, next, &tmp_evict, userptr_link) {
		list_del_init(&vma->userptr_link);
		list_move_tail(&vma->rebind_link, &vm->rebind_list);
	}

	dma_resv_unlock(&vm->resv);

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
xe_vm_bind_vma(struct xe_vma *vma, struct xe_engine *e,
	       struct xe_sync_entry *syncs, u32 num_syncs);

struct dma_fence *xe_vm_rebind(struct xe_vm *vm, bool rebind_worker)
{
	struct dma_fence *fence = NULL;
	struct xe_vma *vma, *next;

	lockdep_assert_held(&vm->lock);
	if (xe_vm_no_dma_fences(vm) && !rebind_worker)
		return NULL;

	xe_vm_assert_held(vm);
	list_for_each_entry_safe(vma, next, &vm->rebind_list, rebind_link) {
		XE_WARN_ON(!vma->gt_present);

		list_del_init(&vma->rebind_link);
		dma_fence_put(fence);
		if (rebind_worker)
			trace_xe_vma_rebind_worker(vma);
		else
			trace_xe_vma_rebind_exec(vma);
		fence = xe_vm_bind_vma(vma, NULL, NULL, 0);
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
				    u64 gt_mask)
{
	struct xe_vma *vma;
	struct xe_gt *gt;
	u8 id;

	XE_BUG_ON(start >= end);
	XE_BUG_ON(end >= vm->size);

	vma = kzalloc(sizeof(*vma), GFP_KERNEL);
	if (!vma) {
		vma = ERR_PTR(-ENOMEM);
		return vma;
	}

	INIT_LIST_HEAD(&vma->rebind_link);
	INIT_LIST_HEAD(&vma->unbind_link);
	INIT_LIST_HEAD(&vma->userptr_link);
	INIT_LIST_HEAD(&vma->userptr.invalidate_link);
	INIT_LIST_HEAD(&vma->notifier.rebind_link);
	INIT_LIST_HEAD(&vma->extobj.link);

	vma->vm = vm;
	vma->start = start;
	vma->end = end;
	if (read_only)
		vma->pte_flags = XE_PTE_READ_ONLY;

	if (gt_mask) {
		vma->gt_mask = gt_mask;
	} else {
		for_each_gt(gt, vm->xe, id)
			if (!xe_gt_is_media_type(gt))
				vma->gt_mask |= 0x1 << id;
	}

	if (vm->xe->info.platform == XE_PVC)
		vma->use_atomic_access_pte_bit = true;

	if (bo) {
		xe_bo_assert_held(bo);
		vma->bo_offset = bo_offset_or_userptr;
		vma->bo = xe_bo_get(bo);
		list_add_tail(&vma->bo_link, &bo->vmas);
	} else /* userptr */ {
		u64 size = end - start + 1;
		int err;

		vma->userptr.ptr = bo_offset_or_userptr;

		err = mmu_interval_notifier_insert(&vma->userptr.notifier,
						   current->mm,
						   vma->userptr.ptr, size,
						   &vma_userptr_notifier_ops);
		if (err) {
			kfree(vma);
			vma = ERR_PTR(err);
			return vma;
		}

		vma->userptr.notifier_seq = LONG_MAX;
		xe_vm_get(vm);
	}

	return vma;
}

static bool vm_remove_extobj(struct xe_vma *vma)
{
	if (!list_empty(&vma->extobj.link)) {
		vma->vm->extobj.entries--;
		list_del_init(&vma->extobj.link);
		return true;
	}
	return false;
}

static void xe_vma_destroy_late(struct xe_vma *vma)
{
	struct xe_vm *vm = vma->vm;
	struct xe_device *xe = vm->xe;
	bool read_only = vma->pte_flags & XE_PTE_READ_ONLY;

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
	} else {
		xe_bo_put(vma->bo);
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
	struct xe_vma *vma;

	list_for_each_entry(vma, &bo->vmas, bo_link) {
		if (vma != ignore && vma->vm == vm && !vma->destroyed)
			return vma;
	}

	return NULL;
}

static bool bo_has_vm_references(struct xe_bo *bo, struct xe_vm *vm,
				 struct xe_vma *ignore)
{
	struct ww_acquire_ctx ww;
	bool ret;

	xe_bo_lock(bo, &ww, 0, false);
	ret = !!bo_has_vm_references_locked(bo, vm, ignore);
	xe_bo_unlock(bo, &ww);

	return ret;
}

static void __vm_insert_extobj(struct xe_vm *vm, struct xe_vma *vma)
{
	list_add(&vma->extobj.link, &vm->extobj.list);
	vm->extobj.entries++;
}

static void vm_insert_extobj(struct xe_vm *vm, struct xe_vma *vma)
{
	struct xe_bo *bo = vma->bo;

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
	struct xe_vm *vm = vma->vm;

	lockdep_assert_held_write(&vm->lock);
	XE_BUG_ON(!list_empty(&vma->unbind_link));

	if (xe_vma_is_userptr(vma)) {
		XE_WARN_ON(!vma->destroyed);
		spin_lock(&vm->userptr.invalidated_lock);
		list_del_init(&vma->userptr.invalidate_link);
		spin_unlock(&vm->userptr.invalidated_lock);
		list_del(&vma->userptr_link);
	} else {
		xe_bo_assert_held(vma->bo);
		list_del(&vma->bo_link);

		spin_lock(&vm->notifier.list_lock);
		list_del(&vma->notifier.rebind_link);
		spin_unlock(&vm->notifier.list_lock);

		if (!vma->bo->vm && vm_remove_extobj(vma)) {
			struct xe_vma *other;

			other = bo_has_vm_references_locked(vma->bo, vm, NULL);

			if (other)
				__vm_insert_extobj(vm, other);
		}
	}

	xe_vm_assert_held(vm);
	if (!list_empty(&vma->rebind_link))
		list_del(&vma->rebind_link);

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

static void xe_vma_destroy_unlocked(struct xe_vma *vma)
{
	struct ttm_validate_buffer tv[2];
	struct ww_acquire_ctx ww;
	struct xe_bo *bo = vma->bo;
	LIST_HEAD(objs);
	LIST_HEAD(dups);
	int err;

	memset(tv, 0, sizeof(tv));
	tv[0].bo = xe_vm_ttm_bo(vma->vm);
	list_add(&tv[0].head, &objs);

	if (bo) {
		tv[1].bo = &xe_bo_get(bo)->ttm;
		list_add(&tv[1].head, &objs);
	}
	err = ttm_eu_reserve_buffers(&ww, &objs, false, &dups);
	XE_WARN_ON(err);

	xe_vma_destroy(vma, NULL);

	ttm_eu_backoff_reservation(&ww, &objs);
	if (bo)
		xe_bo_put(bo);
}

static struct xe_vma *to_xe_vma(const struct rb_node *node)
{
	BUILD_BUG_ON(offsetof(struct xe_vma, vm_node) != 0);
	return (struct xe_vma *)node;
}

static int xe_vma_cmp(const struct xe_vma *a, const struct xe_vma *b)
{
	if (a->end < b->start) {
		return -1;
	} else if (b->end < a->start) {
		return 1;
	} else {
		return 0;
	}
}

static bool xe_vma_less_cb(struct rb_node *a, const struct rb_node *b)
{
	return xe_vma_cmp(to_xe_vma(a), to_xe_vma(b)) < 0;
}

int xe_vma_cmp_vma_cb(const void *key, const struct rb_node *node)
{
	struct xe_vma *cmp = to_xe_vma(node);
	const struct xe_vma *own = key;

	if (own->start > cmp->end)
		return 1;

	if (own->end < cmp->start)
		return -1;

	return 0;
}

struct xe_vma *
xe_vm_find_overlapping_vma(struct xe_vm *vm, const struct xe_vma *vma)
{
	struct rb_node *node;

	if (xe_vm_is_closed(vm))
		return NULL;

	XE_BUG_ON(vma->end >= vm->size);
	lockdep_assert_held(&vm->lock);

	node = rb_find(vma, &vm->vmas, xe_vma_cmp_vma_cb);

	return node ? to_xe_vma(node) : NULL;
}

static void xe_vm_insert_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	XE_BUG_ON(vma->vm != vm);
	lockdep_assert_held(&vm->lock);

	rb_add(&vma->vm_node, &vm->vmas, xe_vma_less_cb);
}

static void xe_vm_remove_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	XE_BUG_ON(vma->vm != vm);
	lockdep_assert_held(&vm->lock);

	rb_erase(&vma->vm_node, &vm->vmas);
	if (vm->usm.last_fault_vma == vma)
		vm->usm.last_fault_vma = NULL;
}

static void async_op_work_func(struct work_struct *w);
static void vm_destroy_work_func(struct work_struct *w);

struct xe_vm *xe_vm_create(struct xe_device *xe, u32 flags)
{
	struct xe_vm *vm;
	int err, i = 0, number_gts = 0;
	struct xe_gt *gt;
	u8 id;

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return ERR_PTR(-ENOMEM);

	vm->xe = xe;
	kref_init(&vm->refcount);
	dma_resv_init(&vm->resv);

	vm->size = 1ull << xe_pt_shift(xe->info.vm_max_level + 1);

	vm->vmas = RB_ROOT;
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
	INIT_WORK(&vm->async_ops.work, async_op_work_func);
	spin_lock_init(&vm->async_ops.lock);

	INIT_WORK(&vm->destroy_work, vm_destroy_work_func);

	INIT_LIST_HEAD(&vm->preempt.engines);
	vm->preempt.min_run_period_ms = 10;	/* FIXME: Wire up to uAPI */

	INIT_LIST_HEAD(&vm->extobj.list);

	if (!(flags & XE_VM_FLAG_MIGRATION)) {
		/* We need to immeditatelly exit from any D3 state */
		xe_pm_runtime_get(xe);
		xe_device_mem_access_get(xe);
	}

	err = dma_resv_lock_interruptible(&vm->resv, NULL);
	if (err)
		goto err_put;

	if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
		vm->flags |= XE_VM_FLAGS_64K;

	for_each_gt(gt, xe, id) {
		if (xe_gt_is_media_type(gt))
			continue;

		if (flags & XE_VM_FLAG_MIGRATION &&
		    gt->info.id != XE_VM_FLAG_GT_ID(flags))
			continue;

		vm->pt_root[id] = xe_pt_create(vm, gt, xe->info.vm_max_level);
		if (IS_ERR(vm->pt_root[id])) {
			err = PTR_ERR(vm->pt_root[id]);
			vm->pt_root[id] = NULL;
			goto err_destroy_root;
		}
	}

	if (flags & XE_VM_FLAG_SCRATCH_PAGE) {
		for_each_gt(gt, xe, id) {
			if (!vm->pt_root[id])
				continue;

			err = xe_pt_create_scratch(xe, gt, vm);
			if (err)
				goto err_scratch_pt;
		}
	}

	if (flags & DRM_XE_VM_CREATE_COMPUTE_MODE) {
		INIT_WORK(&vm->preempt.rebind_work, preempt_rebind_work_func);
		vm->flags |= XE_VM_FLAG_COMPUTE_MODE;
	}

	if (flags & DRM_XE_VM_CREATE_ASYNC_BIND_OPS) {
		vm->async_ops.fence.context = dma_fence_context_alloc(1);
		vm->flags |= XE_VM_FLAG_ASYNC_BIND_OPS;
	}

	/* Fill pt_root after allocating scratch tables */
	for_each_gt(gt, xe, id) {
		if (!vm->pt_root[id])
			continue;

		xe_pt_populate_empty(gt, vm, vm->pt_root[id]);
	}
	dma_resv_unlock(&vm->resv);

	/* Kernel migration VM shouldn't have a circular loop.. */
	if (!(flags & XE_VM_FLAG_MIGRATION)) {
		for_each_gt(gt, xe, id) {
			struct xe_vm *migrate_vm;
			struct xe_engine *eng;

			if (!vm->pt_root[id])
				continue;

			migrate_vm = xe_migrate_get_vm(gt->migrate);
			eng = xe_engine_create_class(xe, gt, migrate_vm,
						     XE_ENGINE_CLASS_COPY,
						     ENGINE_FLAG_VM);
			xe_vm_put(migrate_vm);
			if (IS_ERR(eng)) {
				xe_vm_close_and_put(vm);
				return ERR_CAST(eng);
			}
			vm->eng[id] = eng;
			number_gts++;
		}
	}

	if (number_gts > 1)
		vm->composite_fence_ctx = dma_fence_context_alloc(1);

	mutex_lock(&xe->usm.lock);
	if (flags & XE_VM_FLAG_FAULT_MODE)
		xe->usm.num_vm_in_fault_mode++;
	else if (!(flags & XE_VM_FLAG_MIGRATION))
		xe->usm.num_vm_in_non_fault_mode++;
	mutex_unlock(&xe->usm.lock);

	trace_xe_vm_create(vm);

	return vm;

err_scratch_pt:
	for_each_gt(gt, xe, id) {
		if (!vm->pt_root[id])
			continue;

		i = vm->pt_root[id]->level;
		while (i)
			if (vm->scratch_pt[id][--i])
				xe_pt_destroy(vm->scratch_pt[id][i],
					      vm->flags, NULL);
		xe_bo_unpin(vm->scratch_bo[id]);
		xe_bo_put(vm->scratch_bo[id]);
	}
err_destroy_root:
	for_each_gt(gt, xe, id) {
		if (vm->pt_root[id])
			xe_pt_destroy(vm->pt_root[id], vm->flags, NULL);
	}
	dma_resv_unlock(&vm->resv);
err_put:
	dma_resv_fini(&vm->resv);
	kfree(vm);
	if (!(flags & XE_VM_FLAG_MIGRATION)) {
		xe_device_mem_access_put(xe);
		xe_pm_runtime_put(xe);
	}
	return ERR_PTR(err);
}

static void flush_async_ops(struct xe_vm *vm)
{
	queue_work(system_unbound_wq, &vm->async_ops.work);
	flush_work(&vm->async_ops.work);
}

static void vm_error_capture(struct xe_vm *vm, int err,
			     u32 op, u64 addr, u64 size)
{
	struct drm_xe_vm_bind_op_error_capture capture;
	u64 __user *address =
		u64_to_user_ptr(vm->async_ops.error_capture.addr);
	bool in_kthread = !current->mm;

	capture.error = err;
	capture.op = op;
	capture.addr = addr;
	capture.size = size;

	if (in_kthread) {
		if (!mmget_not_zero(vm->async_ops.error_capture.mm))
			goto mm_closed;
		kthread_use_mm(vm->async_ops.error_capture.mm);
	}

	if (copy_to_user(address, &capture, sizeof(capture)))
		XE_WARN_ON("Copy to user failed");

	if (in_kthread) {
		kthread_unuse_mm(vm->async_ops.error_capture.mm);
		mmput(vm->async_ops.error_capture.mm);
	}

mm_closed:
	wake_up_all(&vm->async_ops.error_capture.wq);
}

void xe_vm_close_and_put(struct xe_vm *vm)
{
	struct rb_root contested = RB_ROOT;
	struct ww_acquire_ctx ww;
	struct xe_device *xe = vm->xe;
	struct xe_gt *gt;
	u8 id;

	XE_BUG_ON(vm->preempt.num_engines);

	vm->size = 0;
	smp_mb();
	flush_async_ops(vm);
	if (xe_vm_in_compute_mode(vm))
		flush_work(&vm->preempt.rebind_work);

	for_each_gt(gt, xe, id) {
		if (vm->eng[id]) {
			xe_engine_kill(vm->eng[id]);
			xe_engine_put(vm->eng[id]);
			vm->eng[id] = NULL;
		}
	}

	down_write(&vm->lock);
	xe_vm_lock(vm, &ww, 0, false);
	while (vm->vmas.rb_node) {
		struct xe_vma *vma = to_xe_vma(vm->vmas.rb_node);

		if (xe_vma_is_userptr(vma)) {
			down_read(&vm->userptr.notifier_lock);
			vma->destroyed = true;
			up_read(&vm->userptr.notifier_lock);
		}

		rb_erase(&vma->vm_node, &vm->vmas);

		/* easy case, remove from VMA? */
		if (xe_vma_is_userptr(vma) || vma->bo->vm) {
			xe_vma_destroy(vma, NULL);
			continue;
		}

		rb_add(&vma->vm_node, &contested, xe_vma_less_cb);
	}

	/*
	 * All vm operations will add shared fences to resv.
	 * The only exception is eviction for a shared object,
	 * but even so, the unbind when evicted would still
	 * install a fence to resv. Hence it's safe to
	 * destroy the pagetables immediately.
	 */
	for_each_gt(gt, xe, id) {
		if (vm->scratch_bo[id]) {
			u32 i;

			xe_bo_unpin(vm->scratch_bo[id]);
			xe_bo_put(vm->scratch_bo[id]);
			for (i = 0; i < vm->pt_root[id]->level; i++)
				xe_pt_destroy(vm->scratch_pt[id][i], vm->flags,
					      NULL);
		}
	}
	xe_vm_unlock(vm, &ww);

	if (contested.rb_node) {

		/*
		 * VM is now dead, cannot re-add nodes to vm->vmas if it's NULL
		 * Since we hold a refcount to the bo, we can remove and free
		 * the members safely without locking.
		 */
		while (contested.rb_node) {
			struct xe_vma *vma = to_xe_vma(contested.rb_node);

			rb_erase(&vma->vm_node, &contested);
			xe_vma_destroy_unlocked(vma);
		}
	}

	if (vm->async_ops.error_capture.addr)
		wake_up_all(&vm->async_ops.error_capture.wq);

	XE_WARN_ON(!list_empty(&vm->extobj.list));
	up_write(&vm->lock);

	mutex_lock(&xe->usm.lock);
	if (vm->flags & XE_VM_FLAG_FAULT_MODE)
		xe->usm.num_vm_in_fault_mode--;
	else if (!(vm->flags & XE_VM_FLAG_MIGRATION))
		xe->usm.num_vm_in_non_fault_mode--;
	mutex_unlock(&xe->usm.lock);

	xe_vm_put(vm);
}

static void vm_destroy_work_func(struct work_struct *w)
{
	struct xe_vm *vm =
		container_of(w, struct xe_vm, destroy_work);
	struct ww_acquire_ctx ww;
	struct xe_device *xe = vm->xe;
	struct xe_gt *gt;
	u8 id;
	void *lookup;

	/* xe_vm_close_and_put was not called? */
	XE_WARN_ON(vm->size);

	if (!(vm->flags & XE_VM_FLAG_MIGRATION)) {
		xe_device_mem_access_put(xe);
		xe_pm_runtime_put(xe);

		if (xe->info.has_asid) {
			mutex_lock(&xe->usm.lock);
			lookup = xa_erase(&xe->usm.asid_to_vm, vm->usm.asid);
			XE_WARN_ON(lookup != vm);
			mutex_unlock(&xe->usm.lock);
		}
	}

	/*
	 * XXX: We delay destroying the PT root until the VM if freed as PT root
	 * is needed for xe_vm_lock to work. If we remove that dependency this
	 * can be moved to xe_vm_close_and_put.
	 */
	xe_vm_lock(vm, &ww, 0, false);
	for_each_gt(gt, xe, id) {
		if (vm->pt_root[id]) {
			xe_pt_destroy(vm->pt_root[id], vm->flags, NULL);
			vm->pt_root[id] = NULL;
		}
	}
	xe_vm_unlock(vm, &ww);

	trace_xe_vm_free(vm);
	dma_fence_put(vm->rebind_fence);
	dma_resv_fini(&vm->resv);
	kfree(vm);
}

void xe_vm_free(struct kref *ref)
{
	struct xe_vm *vm = container_of(ref, struct xe_vm, refcount);

	/* To destroy the VM we need to be able to sleep */
	queue_work(system_unbound_wq, &vm->destroy_work);
}

struct xe_vm *xe_vm_lookup(struct xe_file *xef, u32 id)
{
	struct xe_vm *vm;

	mutex_lock(&xef->vm.lock);
	vm = xa_load(&xef->vm.xa, id);
	mutex_unlock(&xef->vm.lock);

	if (vm)
		xe_vm_get(vm);

	return vm;
}

u64 xe_vm_pdp4_descriptor(struct xe_vm *vm, struct xe_gt *full_gt)
{
	XE_BUG_ON(xe_gt_is_media_type(full_gt));

	return gen8_pde_encode(vm->pt_root[full_gt->info.id]->bo, 0,
			       XE_CACHE_WB);
}

static struct dma_fence *
xe_vm_unbind_vma(struct xe_vma *vma, struct xe_engine *e,
		 struct xe_sync_entry *syncs, u32 num_syncs)
{
	struct xe_gt *gt;
	struct dma_fence *fence = NULL;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	struct xe_vm *vm = vma->vm;
	int cur_fence = 0, i;
	int number_gts = hweight_long(vma->gt_present);
	int err;
	u8 id;

	trace_xe_vma_unbind(vma);

	if (number_gts > 1) {
		fences = kmalloc_array(number_gts, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences)
			return ERR_PTR(-ENOMEM);
	}

	for_each_gt(gt, vm->xe, id) {
		if (!(vma->gt_present & BIT(id)))
			goto next;

		XE_BUG_ON(xe_gt_is_media_type(gt));

		fence = __xe_pt_unbind_vma(gt, vma, e, syncs, num_syncs);
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto err_fences;
		}

		if (fences)
			fences[cur_fence++] = fence;

next:
		if (e && vm->pt_root[id] && !list_empty(&e->multi_gt_list))
			e = list_next_entry(e, multi_gt_list);
	}

	if (fences) {
		cf = dma_fence_array_create(number_gts, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}
	}

	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_signal(&syncs[i], NULL, cf ? &cf->base : fence);

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
xe_vm_bind_vma(struct xe_vma *vma, struct xe_engine *e,
	       struct xe_sync_entry *syncs, u32 num_syncs)
{
	struct xe_gt *gt;
	struct dma_fence *fence;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	struct xe_vm *vm = vma->vm;
	int cur_fence = 0, i;
	int number_gts = hweight_long(vma->gt_mask);
	int err;
	u8 id;

	trace_xe_vma_bind(vma);

	if (number_gts > 1) {
		fences = kmalloc_array(number_gts, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences)
			return ERR_PTR(-ENOMEM);
	}

	for_each_gt(gt, vm->xe, id) {
		if (!(vma->gt_mask & BIT(id)))
			goto next;

		XE_BUG_ON(xe_gt_is_media_type(gt));
		fence = __xe_pt_bind_vma(gt, vma, e, syncs, num_syncs,
					 vma->gt_present & BIT(id));
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			goto err_fences;
		}

		if (fences)
			fences[cur_fence++] = fence;

next:
		if (e && vm->pt_root[id] && !list_empty(&e->multi_gt_list))
			e = list_next_entry(e, multi_gt_list);
	}

	if (fences) {
		cf = dma_fence_array_create(number_gts, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			err = -ENOMEM;
			goto err_fences;
		}
	}

	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_signal(&syncs[i], NULL, cf ? &cf->base : fence);

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

		XE_BUG_ON(xe_vm_no_dma_fences(afence->vm));

		smp_rmb();
		return wait_event_interruptible(afence->wq, afence->started);
	}

	return 0;
}

static int __xe_vm_bind(struct xe_vm *vm, struct xe_vma *vma,
			struct xe_engine *e, struct xe_sync_entry *syncs,
			u32 num_syncs, struct async_op_fence *afence)
{
	struct dma_fence *fence;

	xe_vm_assert_held(vm);

	fence = xe_vm_bind_vma(vma, e, syncs, num_syncs);
	if (IS_ERR(fence))
		return PTR_ERR(fence);
	if (afence)
		add_async_op_fence_cb(vm, fence, afence);

	dma_fence_put(fence);
	return 0;
}

static int xe_vm_bind(struct xe_vm *vm, struct xe_vma *vma, struct xe_engine *e,
		      struct xe_bo *bo, struct xe_sync_entry *syncs,
		      u32 num_syncs, struct async_op_fence *afence)
{
	int err;

	xe_vm_assert_held(vm);
	xe_bo_assert_held(bo);

	if (bo) {
		err = xe_bo_validate(bo, vm, true);
		if (err)
			return err;
	}

	return __xe_vm_bind(vm, vma, e, syncs, num_syncs, afence);
}

static int xe_vm_unbind(struct xe_vm *vm, struct xe_vma *vma,
			struct xe_engine *e, struct xe_sync_entry *syncs,
			u32 num_syncs, struct async_op_fence *afence)
{
	struct dma_fence *fence;

	xe_vm_assert_held(vm);
	xe_bo_assert_held(vma->bo);

	fence = xe_vm_unbind_vma(vma, e, syncs, num_syncs);
	if (IS_ERR(fence))
		return PTR_ERR(fence);
	if (afence)
		add_async_op_fence_cb(vm, fence, afence);

	xe_vma_destroy(vma, fence);
	dma_fence_put(fence);

	return 0;
}

static int vm_set_error_capture_address(struct xe_device *xe, struct xe_vm *vm,
					u64 value)
{
	if (XE_IOCTL_ERR(xe, !value))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, !(vm->flags & XE_VM_FLAG_ASYNC_BIND_OPS)))
		return -ENOTSUPP;

	if (XE_IOCTL_ERR(xe, vm->async_ops.error_capture.addr))
		return -ENOTSUPP;

	vm->async_ops.error_capture.mm = current->mm;
	vm->async_ops.error_capture.addr = value;
	init_waitqueue_head(&vm->async_ops.error_capture.wq);

	return 0;
}

typedef int (*xe_vm_set_property_fn)(struct xe_device *xe, struct xe_vm *vm,
				     u64 value);

static const xe_vm_set_property_fn vm_set_property_funcs[] = {
	[XE_VM_PROPERTY_BIND_OP_ERROR_CAPTURE_ADDRESS] =
		vm_set_error_capture_address,
};

static int vm_user_ext_set_property(struct xe_device *xe, struct xe_vm *vm,
				    u64 extension)
{
	u64 __user *address = u64_to_user_ptr(extension);
	struct drm_xe_ext_vm_set_property ext;
	int err;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_ERR(xe, err))
		return -EFAULT;

	if (XE_IOCTL_ERR(xe, ext.property >=
			 ARRAY_SIZE(vm_set_property_funcs)))
		return -EINVAL;

	return vm_set_property_funcs[ext.property](xe, vm, ext.value);
}

typedef int (*xe_vm_user_extension_fn)(struct xe_device *xe, struct xe_vm *vm,
				       u64 extension);

static const xe_vm_set_property_fn vm_user_extension_funcs[] = {
	[XE_VM_EXTENSION_SET_PROPERTY] = vm_user_ext_set_property,
};

#define MAX_USER_EXTENSIONS	16
static int vm_user_extensions(struct xe_device *xe, struct xe_vm *vm,
			      u64 extensions, int ext_number)
{
	u64 __user *address = u64_to_user_ptr(extensions);
	struct xe_user_extension ext;
	int err;

	if (XE_IOCTL_ERR(xe, ext_number >= MAX_USER_EXTENSIONS))
		return -E2BIG;

	err = __copy_from_user(&ext, address, sizeof(ext));
	if (XE_IOCTL_ERR(xe, err))
		return -EFAULT;

	if (XE_IOCTL_ERR(xe, ext.name >=
			 ARRAY_SIZE(vm_user_extension_funcs)))
		return -EINVAL;

	err = vm_user_extension_funcs[ext.name](xe, vm, extensions);
	if (XE_IOCTL_ERR(xe, err))
		return err;

	if (ext.next_extension)
		return vm_user_extensions(xe, vm, ext.next_extension,
					  ++ext_number);

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
	struct xe_vm *vm;
	u32 id, asid;
	int err;
	u32 flags = 0;

	if (XE_IOCTL_ERR(xe, args->flags & ~ALL_DRM_XE_VM_CREATE_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags & DRM_XE_VM_CREATE_SCRATCH_PAGE &&
			 args->flags & DRM_XE_VM_CREATE_FAULT_MODE))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags & DRM_XE_VM_CREATE_COMPUTE_MODE &&
			 args->flags & DRM_XE_VM_CREATE_FAULT_MODE))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags & DRM_XE_VM_CREATE_FAULT_MODE &&
			 xe_device_in_non_fault_mode(xe)))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, !(args->flags & DRM_XE_VM_CREATE_FAULT_MODE) &&
			 xe_device_in_fault_mode(xe)))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags & DRM_XE_VM_CREATE_FAULT_MODE &&
			 !xe->info.supports_usm))
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

	if (args->extensions) {
		err = vm_user_extensions(xe, vm, args->extensions, 0);
		if (XE_IOCTL_ERR(xe, err)) {
			xe_vm_close_and_put(vm);
			return err;
		}
	}

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

	if (XE_IOCTL_ERR(xe, args->pad))
		return -EINVAL;

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_ERR(xe, !vm))
		return -ENOENT;
	xe_vm_put(vm);

	/* FIXME: Extend this check to non-compute mode VMs */
	if (XE_IOCTL_ERR(xe, vm->preempt.num_engines))
		return -EBUSY;

	mutex_lock(&xef->vm.lock);
	xa_erase(&xef->vm.xa, args->vm_id);
	mutex_unlock(&xef->vm.lock);

	xe_vm_close_and_put(vm);

	return 0;
}

static const u32 region_to_mem_type[] = {
	XE_PL_TT,
	XE_PL_VRAM0,
	XE_PL_VRAM1,
};

static int xe_vm_prefetch(struct xe_vm *vm, struct xe_vma *vma,
			  struct xe_engine *e, u32 region,
			  struct xe_sync_entry *syncs, u32 num_syncs,
			  struct async_op_fence *afence)
{
	int err;

	XE_BUG_ON(region > ARRAY_SIZE(region_to_mem_type));

	if (!xe_vma_is_userptr(vma)) {
		err = xe_bo_migrate(vma->bo, region_to_mem_type[region]);
		if (err)
			return err;
	}

	if (vma->gt_mask != (vma->gt_present & ~vma->usm.gt_invalidated)) {
		return xe_vm_bind(vm, vma, e, vma->bo, syncs, num_syncs,
				  afence);
	} else {
		int i;

		/* Nothing to do, signal fences now */
		for (i = 0; i < num_syncs; i++)
			xe_sync_entry_signal(&syncs[i], NULL,
					     dma_fence_get_stub());
		if (afence)
			dma_fence_signal(&afence->fence);
		return 0;
	}
}

#define VM_BIND_OP(op)	(op & 0xffff)

static int __vm_bind_ioctl(struct xe_vm *vm, struct xe_vma *vma,
			   struct xe_engine *e, struct xe_bo *bo, u32 op,
			   u32 region, struct xe_sync_entry *syncs,
			   u32 num_syncs, struct async_op_fence *afence)
{
	switch (VM_BIND_OP(op)) {
	case XE_VM_BIND_OP_MAP:
		return xe_vm_bind(vm, vma, e, bo, syncs, num_syncs, afence);
	case XE_VM_BIND_OP_UNMAP:
	case XE_VM_BIND_OP_UNMAP_ALL:
		return xe_vm_unbind(vm, vma, e, syncs, num_syncs, afence);
	case XE_VM_BIND_OP_MAP_USERPTR:
		return xe_vm_bind(vm, vma, e, NULL, syncs, num_syncs, afence);
	case XE_VM_BIND_OP_PREFETCH:
		return xe_vm_prefetch(vm, vma, e, region, syncs, num_syncs,
				      afence);
		break;
	default:
		XE_BUG_ON("NOT POSSIBLE");
		return -EINVAL;
	}
}

struct ttm_buffer_object *xe_vm_ttm_bo(struct xe_vm *vm)
{
	int idx = vm->flags & XE_VM_FLAG_MIGRATION ?
		XE_VM_FLAG_GT_ID(vm->flags) : 0;

	/* Safe to use index 0 as all BO in the VM share a single dma-resv lock */
	return &vm->pt_root[idx]->bo->ttm;
}

static void xe_vm_tv_populate(struct xe_vm *vm, struct ttm_validate_buffer *tv)
{
	tv->num_shared = 1;
	tv->bo = xe_vm_ttm_bo(vm);
}

static bool is_map_op(u32 op)
{
	return VM_BIND_OP(op) == XE_VM_BIND_OP_MAP ||
		VM_BIND_OP(op) == XE_VM_BIND_OP_MAP_USERPTR;
}

static bool is_unmap_op(u32 op)
{
	return VM_BIND_OP(op) == XE_VM_BIND_OP_UNMAP ||
		VM_BIND_OP(op) == XE_VM_BIND_OP_UNMAP_ALL;
}

static int vm_bind_ioctl(struct xe_vm *vm, struct xe_vma *vma,
			 struct xe_engine *e, struct xe_bo *bo,
			 struct drm_xe_vm_bind_op *bind_op,
			 struct xe_sync_entry *syncs, u32 num_syncs,
			 struct async_op_fence *afence)
{
	LIST_HEAD(objs);
	LIST_HEAD(dups);
	struct ttm_validate_buffer tv_bo, tv_vm;
	struct ww_acquire_ctx ww;
	struct xe_bo *vbo;
	int err, i;

	lockdep_assert_held(&vm->lock);
	XE_BUG_ON(!list_empty(&vma->unbind_link));

	/* Binds deferred to faults, signal fences now */
	if (xe_vm_in_fault_mode(vm) && is_map_op(bind_op->op) &&
	    !(bind_op->op & XE_VM_BIND_FLAG_IMMEDIATE)) {
		for (i = 0; i < num_syncs; i++)
			xe_sync_entry_signal(&syncs[i], NULL,
					     dma_fence_get_stub());
		if (afence)
			dma_fence_signal(&afence->fence);
		return 0;
	}

	xe_vm_tv_populate(vm, &tv_vm);
	list_add_tail(&tv_vm.head, &objs);
	vbo = vma->bo;
	if (vbo) {
		/*
		 * An unbind can drop the last reference to the BO and
		 * the BO is needed for ttm_eu_backoff_reservation so
		 * take a reference here.
		 */
		xe_bo_get(vbo);

		tv_bo.bo = &vbo->ttm;
		tv_bo.num_shared = 1;
		list_add(&tv_bo.head, &objs);
	}

again:
	err = ttm_eu_reserve_buffers(&ww, &objs, true, &dups);
	if (!err) {
		err = __vm_bind_ioctl(vm, vma, e, bo,
				      bind_op->op, bind_op->region, syncs,
				      num_syncs, afence);
		ttm_eu_backoff_reservation(&ww, &objs);
		if (err == -EAGAIN && xe_vma_is_userptr(vma)) {
			lockdep_assert_held_write(&vm->lock);
			err = xe_vma_userptr_pin_pages(vma);
			if (!err)
				goto again;
		}
	}
	xe_bo_put(vbo);

	return err;
}

struct async_op {
	struct xe_vma *vma;
	struct xe_engine *engine;
	struct xe_bo *bo;
	struct drm_xe_vm_bind_op bind_op;
	struct xe_sync_entry *syncs;
	u32 num_syncs;
	struct list_head link;
	struct async_op_fence *fence;
};

static void async_op_cleanup(struct xe_vm *vm, struct async_op *op)
{
	while (op->num_syncs--)
		xe_sync_entry_cleanup(&op->syncs[op->num_syncs]);
	kfree(op->syncs);
	xe_bo_put(op->bo);
	if (op->engine)
		xe_engine_put(op->engine);
	xe_vm_put(vm);
	if (op->fence)
		dma_fence_put(&op->fence->fence);
	kfree(op);
}

static struct async_op *next_async_op(struct xe_vm *vm)
{
	return list_first_entry_or_null(&vm->async_ops.pending,
					struct async_op, link);
}

static void vm_set_async_error(struct xe_vm *vm, int err)
{
	lockdep_assert_held(&vm->lock);
	vm->async_ops.error = err;
}

static void async_op_work_func(struct work_struct *w)
{
	struct xe_vm *vm = container_of(w, struct xe_vm, async_ops.work);

	for (;;) {
		struct async_op *op;
		int err;

		if (vm->async_ops.error && !xe_vm_is_closed(vm))
			break;

		spin_lock_irq(&vm->async_ops.lock);
		op = next_async_op(vm);
		if (op)
			list_del_init(&op->link);
		spin_unlock_irq(&vm->async_ops.lock);

		if (!op)
			break;

		if (!xe_vm_is_closed(vm)) {
			bool first, last;

			down_write(&vm->lock);
again:
			first = op->vma->first_munmap_rebind;
			last = op->vma->last_munmap_rebind;
#ifdef TEST_VM_ASYNC_OPS_ERROR
#define FORCE_ASYNC_OP_ERROR	BIT(31)
			if (!(op->bind_op.op & FORCE_ASYNC_OP_ERROR)) {
				err = vm_bind_ioctl(vm, op->vma, op->engine,
						    op->bo, &op->bind_op,
						    op->syncs, op->num_syncs,
						    op->fence);
			} else {
				err = -ENOMEM;
				op->bind_op.op &= ~FORCE_ASYNC_OP_ERROR;
			}
#else
			err = vm_bind_ioctl(vm, op->vma, op->engine, op->bo,
					    &op->bind_op, op->syncs,
					    op->num_syncs, op->fence);
#endif
			/*
			 * In order for the fencing to work (stall behind
			 * existing jobs / prevent new jobs from running) all
			 * the dma-resv slots need to be programmed in a batch
			 * relative to execs / the rebind worker. The vm->lock
			 * ensure this.
			 */
			if (!err && ((first && VM_BIND_OP(op->bind_op.op) ==
				      XE_VM_BIND_OP_UNMAP) ||
				     vm->async_ops.munmap_rebind_inflight)) {
				if (last) {
					op->vma->last_munmap_rebind = false;
					vm->async_ops.munmap_rebind_inflight =
						false;
				} else {
					vm->async_ops.munmap_rebind_inflight =
						true;

					async_op_cleanup(vm, op);

					spin_lock_irq(&vm->async_ops.lock);
					op = next_async_op(vm);
					XE_BUG_ON(!op);
					list_del_init(&op->link);
					spin_unlock_irq(&vm->async_ops.lock);

					goto again;
				}
			}
			if (err) {
				trace_xe_vma_fail(op->vma);
				drm_warn(&vm->xe->drm, "Async VM op(%d) failed with %d",
					 VM_BIND_OP(op->bind_op.op),
					 err);

				spin_lock_irq(&vm->async_ops.lock);
				list_add(&op->link, &vm->async_ops.pending);
				spin_unlock_irq(&vm->async_ops.lock);

				vm_set_async_error(vm, err);
				up_write(&vm->lock);

				if (vm->async_ops.error_capture.addr)
					vm_error_capture(vm, err,
							 op->bind_op.op,
							 op->bind_op.addr,
							 op->bind_op.range);
				break;
			}
			up_write(&vm->lock);
		} else {
			trace_xe_vma_flush(op->vma);

			if (is_unmap_op(op->bind_op.op)) {
				down_write(&vm->lock);
				xe_vma_destroy_unlocked(op->vma);
				up_write(&vm->lock);
			}

			if (op->fence && !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
						   &op->fence->fence.flags)) {
				if (!xe_vm_no_dma_fences(vm)) {
					op->fence->started = true;
					smp_wmb();
					wake_up_all(&op->fence->wq);
				}
				dma_fence_signal(&op->fence->fence);
			}
		}

		async_op_cleanup(vm, op);
	}
}

static int __vm_bind_ioctl_async(struct xe_vm *vm, struct xe_vma *vma,
				 struct xe_engine *e, struct xe_bo *bo,
				 struct drm_xe_vm_bind_op *bind_op,
				 struct xe_sync_entry *syncs, u32 num_syncs)
{
	struct async_op *op;
	bool installed = false;
	u64 seqno;
	int i;

	lockdep_assert_held(&vm->lock);

	op = kmalloc(sizeof(*op), GFP_KERNEL);
	if (!op) {
		return -ENOMEM;
	}

	if (num_syncs) {
		op->fence = kmalloc(sizeof(*op->fence), GFP_KERNEL);
		if (!op->fence) {
			kfree(op);
			return -ENOMEM;
		}

		seqno = e ? ++e->bind.fence_seqno : ++vm->async_ops.fence.seqno;
		dma_fence_init(&op->fence->fence, &async_op_fence_ops,
			       &vm->async_ops.lock, e ? e->bind.fence_ctx :
			       vm->async_ops.fence.context, seqno);

		if (!xe_vm_no_dma_fences(vm)) {
			op->fence->vm = vm;
			op->fence->started = false;
			init_waitqueue_head(&op->fence->wq);
		}
	} else {
		op->fence = NULL;
	}
	op->vma = vma;
	op->engine = e;
	op->bo = bo;
	op->bind_op = *bind_op;
	op->syncs = syncs;
	op->num_syncs = num_syncs;
	INIT_LIST_HEAD(&op->link);

	for (i = 0; i < num_syncs; i++)
		installed |= xe_sync_entry_signal(&syncs[i], NULL,
						  &op->fence->fence);

	if (!installed && op->fence)
		dma_fence_signal(&op->fence->fence);

	spin_lock_irq(&vm->async_ops.lock);
	list_add_tail(&op->link, &vm->async_ops.pending);
	spin_unlock_irq(&vm->async_ops.lock);

	if (!vm->async_ops.error)
		queue_work(system_unbound_wq, &vm->async_ops.work);

	return 0;
}

static int vm_bind_ioctl_async(struct xe_vm *vm, struct xe_vma *vma,
			       struct xe_engine *e, struct xe_bo *bo,
			       struct drm_xe_vm_bind_op *bind_op,
			       struct xe_sync_entry *syncs, u32 num_syncs)
{
	struct xe_vma *__vma, *next;
	struct list_head rebind_list;
	struct xe_sync_entry *in_syncs = NULL, *out_syncs = NULL;
	u32 num_in_syncs = 0, num_out_syncs = 0;
	bool first = true, last;
	int err;
	int i;

	lockdep_assert_held(&vm->lock);

	/* Not a linked list of unbinds + rebinds, easy */
	if (list_empty(&vma->unbind_link))
		return __vm_bind_ioctl_async(vm, vma, e, bo, bind_op,
					     syncs, num_syncs);

	/*
	 * Linked list of unbinds + rebinds, decompose syncs into 'in / out'
	 * passing the 'in' to the first operation and 'out' to the last. Also
	 * the reference counting is a little tricky, increment the VM / bind
	 * engine ref count on all but the last operation and increment the BOs
	 * ref count on each rebind.
	 */

	XE_BUG_ON(VM_BIND_OP(bind_op->op) != XE_VM_BIND_OP_UNMAP &&
		  VM_BIND_OP(bind_op->op) != XE_VM_BIND_OP_UNMAP_ALL &&
		  VM_BIND_OP(bind_op->op) != XE_VM_BIND_OP_PREFETCH);

	/* Decompose syncs */
	if (num_syncs) {
		in_syncs = kmalloc(sizeof(*in_syncs) * num_syncs, GFP_KERNEL);
		out_syncs = kmalloc(sizeof(*out_syncs) * num_syncs, GFP_KERNEL);
		if (!in_syncs || !out_syncs) {
			err = -ENOMEM;
			goto out_error;
		}

		for (i = 0; i < num_syncs; ++i) {
			bool signal = syncs[i].flags & DRM_XE_SYNC_SIGNAL;

			if (signal)
				out_syncs[num_out_syncs++] = syncs[i];
			else
				in_syncs[num_in_syncs++] = syncs[i];
		}
	}

	/* Do unbinds + move rebinds to new list */
	INIT_LIST_HEAD(&rebind_list);
	list_for_each_entry_safe(__vma, next, &vma->unbind_link, unbind_link) {
		if (__vma->destroyed ||
		    VM_BIND_OP(bind_op->op) == XE_VM_BIND_OP_PREFETCH) {
			list_del_init(&__vma->unbind_link);
			xe_bo_get(bo);
			err = __vm_bind_ioctl_async(xe_vm_get(vm), __vma,
						    e ? xe_engine_get(e) : NULL,
						    bo, bind_op, first ?
						    in_syncs : NULL,
						    first ? num_in_syncs : 0);
			if (err) {
				xe_bo_put(bo);
				xe_vm_put(vm);
				if (e)
					xe_engine_put(e);
				goto out_error;
			}
			in_syncs = NULL;
			first = false;
		} else {
			list_move_tail(&__vma->unbind_link, &rebind_list);
		}
	}
	last = list_empty(&rebind_list);
	if (!last) {
		xe_vm_get(vm);
		if (e)
			xe_engine_get(e);
	}
	err = __vm_bind_ioctl_async(vm, vma, e,
				    bo, bind_op,
				    first ? in_syncs :
				    last ? out_syncs : NULL,
				    first ? num_in_syncs :
				    last ? num_out_syncs : 0);
	if (err) {
		if (!last) {
			xe_vm_put(vm);
			if (e)
				xe_engine_put(e);
		}
		goto out_error;
	}
	in_syncs = NULL;

	/* Do rebinds */
	list_for_each_entry_safe(__vma, next, &rebind_list, unbind_link) {
		list_del_init(&__vma->unbind_link);
		last = list_empty(&rebind_list);

		if (xe_vma_is_userptr(__vma)) {
			bind_op->op = XE_VM_BIND_FLAG_ASYNC |
				XE_VM_BIND_OP_MAP_USERPTR;
		} else {
			bind_op->op = XE_VM_BIND_FLAG_ASYNC |
				XE_VM_BIND_OP_MAP;
			xe_bo_get(__vma->bo);
		}

		if (!last) {
			xe_vm_get(vm);
			if (e)
				xe_engine_get(e);
		}

		err = __vm_bind_ioctl_async(vm, __vma, e,
					    __vma->bo, bind_op, last ?
					    out_syncs : NULL,
					    last ? num_out_syncs : 0);
		if (err) {
			if (!last) {
				xe_vm_put(vm);
				if (e)
					xe_engine_put(e);
			}
			goto out_error;
		}
	}

	kfree(syncs);
	return 0;

out_error:
	kfree(in_syncs);
	kfree(out_syncs);
	kfree(syncs);

	return err;
}

static int __vm_bind_ioctl_lookup_vma(struct xe_vm *vm, struct xe_bo *bo,
				      u64 addr, u64 range, u32 op)
{
	struct xe_device *xe = vm->xe;
	struct xe_vma *vma, lookup;
	bool async = !!(op & XE_VM_BIND_FLAG_ASYNC);

	lockdep_assert_held(&vm->lock);

	lookup.start = addr;
	lookup.end = addr + range - 1;

	switch (VM_BIND_OP(op)) {
	case XE_VM_BIND_OP_MAP:
	case XE_VM_BIND_OP_MAP_USERPTR:
		vma = xe_vm_find_overlapping_vma(vm, &lookup);
		if (XE_IOCTL_ERR(xe, vma))
			return -EBUSY;
		break;
	case XE_VM_BIND_OP_UNMAP:
	case XE_VM_BIND_OP_PREFETCH:
		vma = xe_vm_find_overlapping_vma(vm, &lookup);
		if (XE_IOCTL_ERR(xe, !vma) ||
		    XE_IOCTL_ERR(xe, (vma->start != addr ||
				 vma->end != addr + range - 1) && !async))
			return -EINVAL;
		break;
	case XE_VM_BIND_OP_UNMAP_ALL:
		break;
	default:
		XE_BUG_ON("NOT POSSIBLE");
		return -EINVAL;
	}

	return 0;
}

static void prep_vma_destroy(struct xe_vm *vm, struct xe_vma *vma)
{
	down_read(&vm->userptr.notifier_lock);
	vma->destroyed = true;
	up_read(&vm->userptr.notifier_lock);
	xe_vm_remove_vma(vm, vma);
}

static int prep_replacement_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	int err;

	if (vma->bo && !vma->bo->vm) {
		vm_insert_extobj(vm, vma);
		err = add_preempt_fences(vm, vma->bo);
		if (err)
			return err;
	}

	return 0;
}

/*
 * Find all overlapping VMAs in lookup range and add to a list in the returned
 * VMA, all of VMAs found will be unbound. Also possibly add 2 new VMAs that
 * need to be bound if first / last VMAs are not fully unbound. This is akin to
 * how munmap works.
 */
static struct xe_vma *vm_unbind_lookup_vmas(struct xe_vm *vm,
					    struct xe_vma *lookup)
{
	struct xe_vma *vma = xe_vm_find_overlapping_vma(vm, lookup);
	struct rb_node *node;
	struct xe_vma *first = vma, *last = vma, *new_first = NULL,
		      *new_last = NULL, *__vma, *next;
	int err = 0;
	bool first_munmap_rebind = false;

	lockdep_assert_held(&vm->lock);
	XE_BUG_ON(!vma);

	node = &vma->vm_node;
	while ((node = rb_next(node))) {
		if (!xe_vma_cmp_vma_cb(lookup, node)) {
			__vma = to_xe_vma(node);
			list_add_tail(&__vma->unbind_link, &vma->unbind_link);
			last = __vma;
		} else {
			break;
		}
	}

	node = &vma->vm_node;
	while ((node = rb_prev(node))) {
		if (!xe_vma_cmp_vma_cb(lookup, node)) {
			__vma = to_xe_vma(node);
			list_add(&__vma->unbind_link, &vma->unbind_link);
			first = __vma;
		} else {
			break;
		}
	}

	if (first->start != lookup->start) {
		struct ww_acquire_ctx ww;

		if (first->bo)
			err = xe_bo_lock(first->bo, &ww, 0, true);
		if (err)
			goto unwind;
		new_first = xe_vma_create(first->vm, first->bo,
					  first->bo ? first->bo_offset :
					  first->userptr.ptr,
					  first->start,
					  lookup->start - 1,
					  (first->pte_flags & XE_PTE_READ_ONLY),
					  first->gt_mask);
		if (first->bo)
			xe_bo_unlock(first->bo, &ww);
		if (!new_first) {
			err = -ENOMEM;
			goto unwind;
		}
		if (!first->bo) {
			err = xe_vma_userptr_pin_pages(new_first);
			if (err)
				goto unwind;
		}
		err = prep_replacement_vma(vm, new_first);
		if (err)
			goto unwind;
	}

	if (last->end != lookup->end) {
		struct ww_acquire_ctx ww;
		u64 chunk = lookup->end + 1 - last->start;

		if (last->bo)
			err = xe_bo_lock(last->bo, &ww, 0, true);
		if (err)
			goto unwind;
		new_last = xe_vma_create(last->vm, last->bo,
					 last->bo ? last->bo_offset + chunk :
					 last->userptr.ptr + chunk,
					 last->start + chunk,
					 last->end,
					 (last->pte_flags & XE_PTE_READ_ONLY),
					 last->gt_mask);
		if (last->bo)
			xe_bo_unlock(last->bo, &ww);
		if (!new_last) {
			err = -ENOMEM;
			goto unwind;
		}
		if (!last->bo) {
			err = xe_vma_userptr_pin_pages(new_last);
			if (err)
				goto unwind;
		}
		err = prep_replacement_vma(vm, new_last);
		if (err)
			goto unwind;
	}

	prep_vma_destroy(vm, vma);
	if (list_empty(&vma->unbind_link) && (new_first || new_last))
		vma->first_munmap_rebind = true;
	list_for_each_entry(__vma, &vma->unbind_link, unbind_link) {
		if ((new_first || new_last) && !first_munmap_rebind) {
			__vma->first_munmap_rebind = true;
			first_munmap_rebind = true;
		}
		prep_vma_destroy(vm, __vma);
	}
	if (new_first) {
		xe_vm_insert_vma(vm, new_first);
		list_add_tail(&new_first->unbind_link, &vma->unbind_link);
		if (!new_last)
			new_first->last_munmap_rebind = true;
	}
	if (new_last) {
		xe_vm_insert_vma(vm, new_last);
		list_add_tail(&new_last->unbind_link, &vma->unbind_link);
		new_last->last_munmap_rebind = true;
	}

	return vma;

unwind:
	list_for_each_entry_safe(__vma, next, &vma->unbind_link, unbind_link)
		list_del_init(&__vma->unbind_link);
	if (new_last) {
		prep_vma_destroy(vm, new_last);
		xe_vma_destroy_unlocked(new_last);
	}
	if (new_first) {
		prep_vma_destroy(vm, new_first);
		xe_vma_destroy_unlocked(new_first);
	}

	return ERR_PTR(err);
}

/*
 * Similar to vm_unbind_lookup_vmas, find all VMAs in lookup range to prefetch
 */
static struct xe_vma *vm_prefetch_lookup_vmas(struct xe_vm *vm,
					      struct xe_vma *lookup,
					      u32 region)
{
	struct xe_vma *vma = xe_vm_find_overlapping_vma(vm, lookup), *__vma,
		      *next;
	struct rb_node *node;

	if (!xe_vma_is_userptr(vma)) {
		if (!xe_bo_can_migrate(vma->bo, region_to_mem_type[region]))
			return ERR_PTR(-EINVAL);
	}

	node = &vma->vm_node;
	while ((node = rb_next(node))) {
		if (!xe_vma_cmp_vma_cb(lookup, node)) {
			__vma = to_xe_vma(node);
			if (!xe_vma_is_userptr(__vma)) {
				if (!xe_bo_can_migrate(__vma->bo, region_to_mem_type[region]))
					goto flush_list;
			}
			list_add_tail(&__vma->unbind_link, &vma->unbind_link);
		} else {
			break;
		}
	}

	node = &vma->vm_node;
	while ((node = rb_prev(node))) {
		if (!xe_vma_cmp_vma_cb(lookup, node)) {
			__vma = to_xe_vma(node);
			if (!xe_vma_is_userptr(__vma)) {
				if (!xe_bo_can_migrate(__vma->bo, region_to_mem_type[region]))
					goto flush_list;
			}
			list_add(&__vma->unbind_link, &vma->unbind_link);
		} else {
			break;
		}
	}

	return vma;

flush_list:
	list_for_each_entry_safe(__vma, next, &vma->unbind_link,
				 unbind_link)
		list_del_init(&__vma->unbind_link);

	return ERR_PTR(-EINVAL);
}

static struct xe_vma *vm_unbind_all_lookup_vmas(struct xe_vm *vm,
						struct xe_bo *bo)
{
	struct xe_vma *first = NULL, *vma;

	lockdep_assert_held(&vm->lock);
	xe_bo_assert_held(bo);

	list_for_each_entry(vma, &bo->vmas, bo_link) {
		if (vma->vm != vm)
			continue;

		prep_vma_destroy(vm, vma);
		if (!first)
			first = vma;
		else
			list_add_tail(&vma->unbind_link, &first->unbind_link);
	}

	return first;
}

static struct xe_vma *vm_bind_ioctl_lookup_vma(struct xe_vm *vm,
					       struct xe_bo *bo,
					       u64 bo_offset_or_userptr,
					       u64 addr, u64 range, u32 op,
					       u64 gt_mask, u32 region)
{
	struct ww_acquire_ctx ww;
	struct xe_vma *vma, lookup;
	int err;

	lockdep_assert_held(&vm->lock);

	lookup.start = addr;
	lookup.end = addr + range - 1;

	switch (VM_BIND_OP(op)) {
	case XE_VM_BIND_OP_MAP:
		XE_BUG_ON(!bo);

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return ERR_PTR(err);
		vma = xe_vma_create(vm, bo, bo_offset_or_userptr, addr,
				    addr + range - 1,
				    op & XE_VM_BIND_FLAG_READONLY,
				    gt_mask);
		xe_bo_unlock(bo, &ww);
		if (!vma)
			return ERR_PTR(-ENOMEM);

		xe_vm_insert_vma(vm, vma);
		if (!bo->vm) {
			vm_insert_extobj(vm, vma);
			err = add_preempt_fences(vm, bo);
			if (err) {
				prep_vma_destroy(vm, vma);
				xe_vma_destroy_unlocked(vma);

				return ERR_PTR(err);
			}
		}
		break;
	case XE_VM_BIND_OP_UNMAP:
		vma = vm_unbind_lookup_vmas(vm, &lookup);
		break;
	case XE_VM_BIND_OP_PREFETCH:
		vma = vm_prefetch_lookup_vmas(vm, &lookup, region);
		break;
	case XE_VM_BIND_OP_UNMAP_ALL:
		XE_BUG_ON(!bo);

		err = xe_bo_lock(bo, &ww, 0, true);
		if (err)
			return ERR_PTR(err);
		vma = vm_unbind_all_lookup_vmas(vm, bo);
		if (!vma)
			vma = ERR_PTR(-EINVAL);
		xe_bo_unlock(bo, &ww);
		break;
	case XE_VM_BIND_OP_MAP_USERPTR:
		XE_BUG_ON(bo);

		vma = xe_vma_create(vm, NULL, bo_offset_or_userptr, addr,
				    addr + range - 1,
				    op & XE_VM_BIND_FLAG_READONLY,
				    gt_mask);
		if (!vma)
			return ERR_PTR(-ENOMEM);

		err = xe_vma_userptr_pin_pages(vma);
		if (err) {
			prep_vma_destroy(vm, vma);
			xe_vma_destroy_unlocked(vma);

			return ERR_PTR(err);
		} else {
			xe_vm_insert_vma(vm, vma);
		}
		break;
	default:
		XE_BUG_ON("NOT POSSIBLE");
		vma = ERR_PTR(-EINVAL);
	}

	return vma;
}

#ifdef TEST_VM_ASYNC_OPS_ERROR
#define SUPPORTED_FLAGS	\
	(FORCE_ASYNC_OP_ERROR | XE_VM_BIND_FLAG_ASYNC | \
	 XE_VM_BIND_FLAG_READONLY | XE_VM_BIND_FLAG_IMMEDIATE | 0xffff)
#else
#define SUPPORTED_FLAGS	\
	(XE_VM_BIND_FLAG_ASYNC | XE_VM_BIND_FLAG_READONLY | \
	 XE_VM_BIND_FLAG_IMMEDIATE | 0xffff)
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

	if (XE_IOCTL_ERR(xe, args->extensions) ||
	    XE_IOCTL_ERR(xe, !args->num_binds) ||
	    XE_IOCTL_ERR(xe, args->num_binds > MAX_BINDS))
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
		if (XE_IOCTL_ERR(xe, err)) {
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
		u32 obj = (*bind_ops)[i].obj;
		u64 obj_offset = (*bind_ops)[i].obj_offset;
		u32 region = (*bind_ops)[i].region;

		if (i == 0) {
			*async = !!(op & XE_VM_BIND_FLAG_ASYNC);
		} else if (XE_IOCTL_ERR(xe, !*async) ||
			   XE_IOCTL_ERR(xe, !(op & XE_VM_BIND_FLAG_ASYNC)) ||
			   XE_IOCTL_ERR(xe, VM_BIND_OP(op) ==
					XE_VM_BIND_OP_RESTART)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_ERR(xe, !*async &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_UNMAP_ALL)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_ERR(xe, !*async &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_PREFETCH)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_ERR(xe, VM_BIND_OP(op) >
				 XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_ERR(xe, op & ~SUPPORTED_FLAGS) ||
		    XE_IOCTL_ERR(xe, !obj &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_MAP) ||
		    XE_IOCTL_ERR(xe, !obj &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_ERR(xe, addr &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_ERR(xe, range &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_ERR(xe, obj &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_MAP_USERPTR) ||
		    XE_IOCTL_ERR(xe, obj &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_ERR(xe, region &&
				 VM_BIND_OP(op) != XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_ERR(xe, !(BIT(region) &
				       xe->info.mem_region_mask)) ||
		    XE_IOCTL_ERR(xe, obj &&
				 VM_BIND_OP(op) == XE_VM_BIND_OP_UNMAP)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_ERR(xe, obj_offset & ~PAGE_MASK) ||
		    XE_IOCTL_ERR(xe, addr & ~PAGE_MASK) ||
		    XE_IOCTL_ERR(xe, range & ~PAGE_MASK) ||
		    XE_IOCTL_ERR(xe, !range && VM_BIND_OP(op) !=
				 XE_VM_BIND_OP_RESTART &&
				 VM_BIND_OP(op) != XE_VM_BIND_OP_UNMAP_ALL)) {
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
	struct xe_vma **vmas = NULL;
	struct xe_vm *vm;
	struct xe_engine *e = NULL;
	u32 num_syncs;
	struct xe_sync_entry *syncs = NULL;
	struct drm_xe_vm_bind_op *bind_ops;
	bool async;
	int err;
	int i, j = 0;

	err = vm_bind_ioctl_check_args(xe, args, &bind_ops, &async);
	if (err)
		return err;

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_ERR(xe, !vm)) {
		err = -EINVAL;
		goto free_objs;
	}

	if (XE_IOCTL_ERR(xe, xe_vm_is_closed(vm))) {
		DRM_ERROR("VM closed while we began looking up?\n");
		err = -ENOENT;
		goto put_vm;
	}

	if (args->engine_id) {
		e = xe_engine_lookup(xef, args->engine_id);
		if (XE_IOCTL_ERR(xe, !e)) {
			err = -ENOENT;
			goto put_vm;
		}
		if (XE_IOCTL_ERR(xe, !(e->flags & ENGINE_FLAG_VM))) {
			err = -EINVAL;
			goto put_engine;
		}
	}

	if (VM_BIND_OP(bind_ops[0].op) == XE_VM_BIND_OP_RESTART) {
		if (XE_IOCTL_ERR(xe, !(vm->flags & XE_VM_FLAG_ASYNC_BIND_OPS)))
			err = -ENOTSUPP;
		if (XE_IOCTL_ERR(xe, !err && args->num_syncs))
			err = EINVAL;
		if (XE_IOCTL_ERR(xe, !err && !vm->async_ops.error))
			err = -EPROTO;

		if (!err) {
			down_write(&vm->lock);
			trace_xe_vm_restart(vm);
			vm_set_async_error(vm, 0);
			up_write(&vm->lock);

			queue_work(system_unbound_wq, &vm->async_ops.work);

			/* Rebinds may have been blocked, give worker a kick */
			if (xe_vm_in_compute_mode(vm))
				queue_work(vm->xe->ordered_wq,
					   &vm->preempt.rebind_work);
		}

		goto put_engine;
	}

	if (XE_IOCTL_ERR(xe, !vm->async_ops.error &&
			 async != !!(vm->flags & XE_VM_FLAG_ASYNC_BIND_OPS))) {
		err = -ENOTSUPP;
		goto put_engine;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;

		if (XE_IOCTL_ERR(xe, range > vm->size) ||
		    XE_IOCTL_ERR(xe, addr > vm->size - range)) {
			err = -EINVAL;
			goto put_engine;
		}

		if (bind_ops[i].gt_mask) {
			u64 valid_gts = BIT(xe->info.tile_count) - 1;

			if (XE_IOCTL_ERR(xe, bind_ops[i].gt_mask &
					 ~valid_gts)) {
				err = -EINVAL;
				goto put_engine;
			}
		}
	}

	bos = kzalloc(sizeof(*bos) * args->num_binds, GFP_KERNEL);
	if (!bos) {
		err = -ENOMEM;
		goto put_engine;
	}

	vmas = kzalloc(sizeof(*vmas) * args->num_binds, GFP_KERNEL);
	if (!vmas) {
		err = -ENOMEM;
		goto put_engine;
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
		if (XE_IOCTL_ERR(xe, !gem_obj)) {
			err = -ENOENT;
			goto put_obj;
		}
		bos[i] = gem_to_xe_bo(gem_obj);

		if (XE_IOCTL_ERR(xe, range > bos[i]->size) ||
		    XE_IOCTL_ERR(xe, obj_offset >
				 bos[i]->size - range)) {
			err = -EINVAL;
			goto put_obj;
		}

		if (bos[i]->flags & XE_BO_INTERNAL_64K) {
			if (XE_IOCTL_ERR(xe, obj_offset &
					 XE_64K_PAGE_MASK) ||
			    XE_IOCTL_ERR(xe, addr & XE_64K_PAGE_MASK) ||
			    XE_IOCTL_ERR(xe, range & XE_64K_PAGE_MASK)) {
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
					  xe_vm_in_fault_mode(vm));
		if (err)
			goto free_syncs;
	}

	err = down_write_killable(&vm->lock);
	if (err)
		goto free_syncs;

	/* Do some error checking first to make the unwind easier */
	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 op = bind_ops[i].op;

		err = __vm_bind_ioctl_lookup_vma(vm, bos[i], addr, range, op);
		if (err)
			goto release_vm_lock;
	}

	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 op = bind_ops[i].op;
		u64 obj_offset = bind_ops[i].obj_offset;
		u64 gt_mask = bind_ops[i].gt_mask;
		u32 region = bind_ops[i].region;

		vmas[i] = vm_bind_ioctl_lookup_vma(vm, bos[i], obj_offset,
						   addr, range, op, gt_mask,
						   region);
		if (IS_ERR(vmas[i])) {
			err = PTR_ERR(vmas[i]);
			vmas[i] = NULL;
			goto destroy_vmas;
		}
	}

	for (j = 0; j < args->num_binds; ++j) {
		struct xe_sync_entry *__syncs;
		u32 __num_syncs = 0;
		bool first_or_last = j == 0 || j == args->num_binds - 1;

		if (args->num_binds == 1) {
			__num_syncs = num_syncs;
			__syncs = syncs;
		} else if (first_or_last && num_syncs) {
			bool first = j == 0;

			__syncs = kmalloc(sizeof(*__syncs) * num_syncs,
					  GFP_KERNEL);
			if (!__syncs) {
				err = ENOMEM;
				break;
			}

			/* in-syncs on first bind, out-syncs on last bind */
			for (i = 0; i < num_syncs; ++i) {
				bool signal = syncs[i].flags &
					DRM_XE_SYNC_SIGNAL;

				if ((first && !signal) || (!first && signal))
					__syncs[__num_syncs++] = syncs[i];
			}
		} else {
			__num_syncs = 0;
			__syncs = NULL;
		}

		if (async) {
			bool last = j == args->num_binds - 1;

			/*
			 * Each pass of async worker drops the ref, take a ref
			 * here, 1 set of refs taken above
			 */
			if (!last) {
				if (e)
					xe_engine_get(e);
				xe_vm_get(vm);
			}

			err = vm_bind_ioctl_async(vm, vmas[j], e, bos[j],
						  bind_ops + j, __syncs,
						  __num_syncs);
			if (err && !last) {
				if (e)
					xe_engine_put(e);
				xe_vm_put(vm);
			}
			if (err)
				break;
		} else {
			XE_BUG_ON(j != 0);	/* Not supported */
			err = vm_bind_ioctl(vm, vmas[j], e, bos[j],
					    bind_ops + j, __syncs,
					    __num_syncs, NULL);
			break;	/* Needed so cleanup loops work */
		}
	}

	/* Most of cleanup owned by the async bind worker */
	if (async && !err) {
		up_write(&vm->lock);
		if (args->num_binds > 1)
			kfree(syncs);
		goto free_objs;
	}

destroy_vmas:
	for (i = j; err && i < args->num_binds; ++i) {
		u32 op = bind_ops[i].op;
		struct xe_vma *vma, *next;

		if (!vmas[i])
			break;

		list_for_each_entry_safe(vma, next, &vma->unbind_link,
					 unbind_link) {
			list_del_init(&vma->unbind_link);
			if (!vma->destroyed) {
				prep_vma_destroy(vm, vma);
				xe_vma_destroy_unlocked(vma);
			}
		}

		switch (VM_BIND_OP(op)) {
		case XE_VM_BIND_OP_MAP:
			prep_vma_destroy(vm, vmas[i]);
			xe_vma_destroy_unlocked(vmas[i]);
			break;
		case XE_VM_BIND_OP_MAP_USERPTR:
			prep_vma_destroy(vm, vmas[i]);
			xe_vma_destroy_unlocked(vmas[i]);
			break;
		}
	}
release_vm_lock:
	up_write(&vm->lock);
free_syncs:
	while (num_syncs--) {
		if (async && j &&
		    !(syncs[num_syncs].flags & DRM_XE_SYNC_SIGNAL))
			continue;	/* Still in async worker */
		xe_sync_entry_cleanup(&syncs[num_syncs]);
	}

	kfree(syncs);
put_obj:
	for (i = j; i < args->num_binds; ++i)
		xe_bo_put(bos[i]);
put_engine:
	if (e)
		xe_engine_put(e);
put_vm:
	xe_vm_put(vm);
free_objs:
	kfree(bos);
	kfree(vmas);
	if (args->num_binds > 1)
		kfree(bind_ops);
	return err;
}

/*
 * XXX: Using the TTM wrappers for now, likely can call into dma-resv code
 * directly to optimize. Also this likely should be an inline function.
 */
int xe_vm_lock(struct xe_vm *vm, struct ww_acquire_ctx *ww,
	       int num_resv, bool intr)
{
	struct ttm_validate_buffer tv_vm;
	LIST_HEAD(objs);
	LIST_HEAD(dups);

	XE_BUG_ON(!ww);

	tv_vm.num_shared = num_resv;
	tv_vm.bo = xe_vm_ttm_bo(vm);;
	list_add_tail(&tv_vm.head, &objs);

	return ttm_eu_reserve_buffers(ww, &objs, intr, &dups);
}

void xe_vm_unlock(struct xe_vm *vm, struct ww_acquire_ctx *ww)
{
	dma_resv_unlock(&vm->resv);
	ww_acquire_fini(ww);
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
	struct xe_device *xe = vma->vm->xe;
	struct xe_gt *gt;
	u32 gt_needs_invalidate = 0;
	int seqno[XE_MAX_GT];
	u8 id;
	int ret;

	XE_BUG_ON(!xe_vm_in_fault_mode(vma->vm));
	trace_xe_vma_usm_invalidate(vma);

	/* Check that we don't race with page-table updates */
	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		if (xe_vma_is_userptr(vma)) {
			WARN_ON_ONCE(!mmu_interval_check_retry
				     (&vma->userptr.notifier,
				      vma->userptr.notifier_seq));
			WARN_ON_ONCE(!dma_resv_test_signaled(&vma->vm->resv,
							     DMA_RESV_USAGE_BOOKKEEP));

		} else {
			xe_bo_assert_held(vma->bo);
		}
	}

	for_each_gt(gt, xe, id) {
		if (xe_pt_zap_ptes(gt, vma)) {
			gt_needs_invalidate |= BIT(id);
			xe_device_wmb(xe);
			seqno[id] = xe_gt_tlb_invalidation_vma(gt, NULL, vma);
			if (seqno[id] < 0)
				return seqno[id];
		}
	}

	for_each_gt(gt, xe, id) {
		if (gt_needs_invalidate & BIT(id)) {
			ret = xe_gt_tlb_invalidation_wait(gt, seqno[id]);
			if (ret < 0)
				return ret;
		}
	}

	vma->usm.gt_invalidated = vma->gt_mask;

	return 0;
}

int xe_analyze_vm(struct drm_printer *p, struct xe_vm *vm, int gt_id)
{
	struct rb_node *node;
	bool is_vram;
	uint64_t addr;

	if (!down_read_trylock(&vm->lock)) {
		drm_printf(p, " Failed to acquire VM lock to dump capture");
		return 0;
	}
	if (vm->pt_root[gt_id]) {
		addr = xe_bo_addr(vm->pt_root[gt_id]->bo, 0, XE_PAGE_SIZE,
				  &is_vram);
		drm_printf(p, " VM root: A:0x%llx %s\n", addr, is_vram ? "VRAM" : "SYS");
	}

	for (node = rb_first(&vm->vmas); node; node = rb_next(node)) {
		struct xe_vma *vma = to_xe_vma(node);
		bool is_userptr = xe_vma_is_userptr(vma);

		if (is_userptr) {
			struct xe_res_cursor cur;

			xe_res_first_sg(vma->userptr.sg, 0, XE_PAGE_SIZE,
					&cur);
			addr = xe_res_dma(&cur);
		} else {
			addr = __xe_bo_addr(vma->bo, 0, XE_PAGE_SIZE, &is_vram);
		}
		drm_printf(p, " [%016llx-%016llx] S:0x%016llx A:%016llx %s\n",
			   vma->start, vma->end, vma->end - vma->start + 1ull,
			   addr, is_userptr ? "USR" : is_vram ? "VRAM" : "SYS");
	}
	up_read(&vm->lock);

	return 0;
}
