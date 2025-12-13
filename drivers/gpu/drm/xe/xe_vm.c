// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_vm.h"

#include <linux/dma-fence-array.h>
#include <linux/nospec.h>

#include <drm/drm_drv.h>
#include <drm/drm_exec.h>
#include <drm/drm_print.h>
#include <drm/ttm/ttm_tt.h>
#include <uapi/drm/xe_drm.h>
#include <linux/ascii85.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include <generated/xe_wa_oob.h>

#include "regs/xe_gtt_defs.h"
#include "xe_assert.h"
#include "xe_bo.h"
#include "xe_device.h"
#include "xe_drm_client.h"
#include "xe_exec_queue.h"
#include "xe_gt_pagefault.h"
#include "xe_migrate.h"
#include "xe_pat.h"
#include "xe_pm.h"
#include "xe_preempt_fence.h"
#include "xe_pt.h"
#include "xe_pxp.h"
#include "xe_res_cursor.h"
#include "xe_svm.h"
#include "xe_sync.h"
#include "xe_tile.h"
#include "xe_tlb_inval.h"
#include "xe_trace_bo.h"
#include "xe_wa.h"

static struct drm_gem_object *xe_vm_obj(struct xe_vm *vm)
{
	return vm->gpuvm.r_obj;
}

/**
 * xe_vm_drm_exec_lock() - Lock the vm's resv with a drm_exec transaction
 * @vm: The vm whose resv is to be locked.
 * @exec: The drm_exec transaction.
 *
 * Helper to lock the vm's resv as part of a drm_exec transaction.
 *
 * Return: %0 on success. See drm_exec_lock_obj() for error codes.
 */
int xe_vm_drm_exec_lock(struct xe_vm *vm, struct drm_exec *exec)
{
	return drm_exec_lock_obj(exec, xe_vm_obj(vm));
}

static bool preempt_fences_waiting(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		if (!q->lr.pfence ||
		    test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT,
			     &q->lr.pfence->flags)) {
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

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		if (q->lr.pfence) {
			long timeout = dma_fence_wait(q->lr.pfence, false);

			/* Only -ETIME on fence indicates VM needs to be killed */
			if (timeout < 0 || q->lr.pfence->error == -ETIME)
				return -ETIME;

			dma_fence_put(q->lr.pfence);
			q->lr.pfence = NULL;
		}
	}

	return 0;
}

static bool xe_vm_is_idle(struct xe_vm *vm)
{
	struct xe_exec_queue *q;

	xe_vm_assert_held(vm);
	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		if (!xe_exec_queue_is_idle(q))
			return false;
	}

	return true;
}

static void arm_preempt_fences(struct xe_vm *vm, struct list_head *list)
{
	struct list_head *link;
	struct xe_exec_queue *q;

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		struct dma_fence *fence;

		link = list->next;
		xe_assert(vm->xe, link != list);

		fence = xe_preempt_fence_arm(to_preempt_fence_from_link(link),
					     q, q->lr.context,
					     ++q->lr.seqno);
		dma_fence_put(q->lr.pfence);
		q->lr.pfence = fence;
	}
}

static int add_preempt_fences(struct xe_vm *vm, struct xe_bo *bo)
{
	struct xe_exec_queue *q;
	int err;

	xe_bo_assert_held(bo);

	if (!vm->preempt.num_exec_queues)
		return 0;

	err = dma_resv_reserve_fences(bo->ttm.base.resv, vm->preempt.num_exec_queues);
	if (err)
		return err;

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link)
		if (q->lr.pfence) {
			dma_resv_add_fence(bo->ttm.base.resv,
					   q->lr.pfence,
					   DMA_RESV_USAGE_BOOKKEEP);
		}

	return 0;
}

static void resume_and_reinstall_preempt_fences(struct xe_vm *vm,
						struct drm_exec *exec)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link) {
		q->ops->resume(q);

		drm_gpuvm_resv_add_fence(&vm->gpuvm, exec, q->lr.pfence,
					 DMA_RESV_USAGE_BOOKKEEP, DMA_RESV_USAGE_BOOKKEEP);
	}
}

int xe_vm_add_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	struct drm_gpuvm_exec vm_exec = {
		.vm = &vm->gpuvm,
		.flags = DRM_EXEC_INTERRUPTIBLE_WAIT,
		.num_fences = 1,
	};
	struct drm_exec *exec = &vm_exec.exec;
	struct xe_validation_ctx ctx;
	struct dma_fence *pfence;
	int err;
	bool wait;

	xe_assert(vm->xe, xe_vm_in_preempt_fence_mode(vm));

	down_write(&vm->lock);
	err = xe_validation_exec_lock(&ctx, &vm_exec, &vm->xe->val);
	if (err)
		goto out_up_write;

	pfence = xe_preempt_fence_create(q, q->lr.context,
					 ++q->lr.seqno);
	if (IS_ERR(pfence)) {
		err = PTR_ERR(pfence);
		goto out_fini;
	}

	list_add(&q->lr.link, &vm->preempt.exec_queues);
	++vm->preempt.num_exec_queues;
	q->lr.pfence = pfence;

	xe_svm_notifier_lock(vm);

	drm_gpuvm_resv_add_fence(&vm->gpuvm, exec, pfence,
				 DMA_RESV_USAGE_BOOKKEEP, DMA_RESV_USAGE_BOOKKEEP);

	/*
	 * Check to see if a preemption on VM is in flight or userptr
	 * invalidation, if so trigger this preempt fence to sync state with
	 * other preempt fences on the VM.
	 */
	wait = __xe_vm_userptr_needs_repin(vm) || preempt_fences_waiting(vm);
	if (wait)
		dma_fence_enable_sw_signaling(pfence);

	xe_svm_notifier_unlock(vm);

out_fini:
	xe_validation_ctx_fini(&ctx);
out_up_write:
	up_write(&vm->lock);

	return err;
}
ALLOW_ERROR_INJECTION(xe_vm_add_compute_exec_queue, ERRNO);

/**
 * xe_vm_remove_compute_exec_queue() - Remove compute exec queue from VM
 * @vm: The VM.
 * @q: The exec_queue
 *
 * Note that this function might be called multiple times on the same queue.
 */
void xe_vm_remove_compute_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	if (!xe_vm_in_preempt_fence_mode(vm))
		return;

	down_write(&vm->lock);
	if (!list_empty(&q->lr.link)) {
		list_del_init(&q->lr.link);
		--vm->preempt.num_exec_queues;
	}
	if (q->lr.pfence) {
		dma_fence_enable_sw_signaling(q->lr.pfence);
		dma_fence_put(q->lr.pfence);
		q->lr.pfence = NULL;
	}
	up_write(&vm->lock);
}

#define XE_VM_REBIND_RETRY_TIMEOUT_MS 1000

/**
 * xe_vm_kill() - VM Kill
 * @vm: The VM.
 * @unlocked: Flag indicates the VM's dma-resv is not held
 *
 * Kill the VM by setting banned flag indicated VM is no longer available for
 * use. If in preempt fence mode, also kill all exec queue attached to the VM.
 */
void xe_vm_kill(struct xe_vm *vm, bool unlocked)
{
	struct xe_exec_queue *q;

	lockdep_assert_held(&vm->lock);

	if (unlocked)
		xe_vm_lock(vm, false);

	vm->flags |= XE_VM_FLAG_BANNED;
	trace_xe_vm_kill(vm);

	list_for_each_entry(q, &vm->preempt.exec_queues, lr.link)
		q->ops->kill(q);

	if (unlocked)
		xe_vm_unlock(vm);

	/* TODO: Inform user the VM is banned */
}

static int xe_gpuvm_validate(struct drm_gpuvm_bo *vm_bo, struct drm_exec *exec)
{
	struct xe_vm *vm = gpuvm_to_vm(vm_bo->vm);
	struct drm_gpuva *gpuva;
	int ret;

	lockdep_assert_held(&vm->lock);
	drm_gpuvm_bo_for_each_va(gpuva, vm_bo)
		list_move_tail(&gpuva_to_vma(gpuva)->combined_links.rebind,
			       &vm->rebind_list);

	if (!try_wait_for_completion(&vm->xe->pm_block))
		return -EAGAIN;

	ret = xe_bo_validate(gem_to_xe_bo(vm_bo->obj), vm, false, exec);
	if (ret)
		return ret;

	vm_bo->evicted = false;
	return 0;
}

/**
 * xe_vm_validate_rebind() - Validate buffer objects and rebind vmas
 * @vm: The vm for which we are rebinding.
 * @exec: The struct drm_exec with the locked GEM objects.
 * @num_fences: The number of fences to reserve for the operation, not
 * including rebinds and validations.
 *
 * Validates all evicted gem objects and rebinds their vmas. Note that
 * rebindings may cause evictions and hence the validation-rebind
 * sequence is rerun until there are no more objects to validate.
 *
 * Return: 0 on success, negative error code on error. In particular,
 * may return -EINTR or -ERESTARTSYS if interrupted, and -EDEADLK if
 * the drm_exec transaction needs to be restarted.
 */
int xe_vm_validate_rebind(struct xe_vm *vm, struct drm_exec *exec,
			  unsigned int num_fences)
{
	struct drm_gem_object *obj;
	unsigned long index;
	int ret;

	do {
		ret = drm_gpuvm_validate(&vm->gpuvm, exec);
		if (ret)
			return ret;

		ret = xe_vm_rebind(vm, false);
		if (ret)
			return ret;
	} while (!list_empty(&vm->gpuvm.evict.list));

	drm_exec_for_each_locked_object(exec, index, obj) {
		ret = dma_resv_reserve_fences(obj->resv, num_fences);
		if (ret)
			return ret;
	}

	return 0;
}

static int xe_preempt_work_begin(struct drm_exec *exec, struct xe_vm *vm,
				 bool *done)
{
	int err;

	err = drm_gpuvm_prepare_vm(&vm->gpuvm, exec, 0);
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

	err = drm_gpuvm_prepare_objects(&vm->gpuvm, exec, 0);
	if (err)
		return err;

	err = wait_for_existing_preempt_fences(vm);
	if (err)
		return err;

	/*
	 * Add validation and rebinding to the locking loop since both can
	 * cause evictions which may require blocing dma_resv locks.
	 * The fence reservation here is intended for the new preempt fences
	 * we attach at the end of the rebind work.
	 */
	return xe_vm_validate_rebind(vm, exec, vm->preempt.num_exec_queues);
}

static bool vm_suspend_rebind_worker(struct xe_vm *vm)
{
	struct xe_device *xe = vm->xe;
	bool ret = false;

	mutex_lock(&xe->rebind_resume_lock);
	if (!try_wait_for_completion(&vm->xe->pm_block)) {
		ret = true;
		list_move_tail(&vm->preempt.pm_activate_link, &xe->rebind_resume_list);
	}
	mutex_unlock(&xe->rebind_resume_lock);

	return ret;
}

/**
 * xe_vm_resume_rebind_worker() - Resume the rebind worker.
 * @vm: The vm whose preempt worker to resume.
 *
 * Resume a preempt worker that was previously suspended by
 * vm_suspend_rebind_worker().
 */
void xe_vm_resume_rebind_worker(struct xe_vm *vm)
{
	queue_work(vm->xe->ordered_wq, &vm->preempt.rebind_work);
}

static void preempt_rebind_work_func(struct work_struct *w)
{
	struct xe_vm *vm = container_of(w, struct xe_vm, preempt.rebind_work);
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	unsigned int fence_count = 0;
	LIST_HEAD(preempt_fences);
	int err = 0;
	long wait;
	int __maybe_unused tries = 0;

	xe_assert(vm->xe, xe_vm_in_preempt_fence_mode(vm));
	trace_xe_vm_rebind_worker_enter(vm);

	down_write(&vm->lock);

	if (xe_vm_is_closed_or_banned(vm)) {
		up_write(&vm->lock);
		trace_xe_vm_rebind_worker_exit(vm);
		return;
	}

retry:
	if (!try_wait_for_completion(&vm->xe->pm_block) && vm_suspend_rebind_worker(vm)) {
		up_write(&vm->lock);
		return;
	}

	if (xe_vm_userptr_check_repin(vm)) {
		err = xe_vm_userptr_pin(vm);
		if (err)
			goto out_unlock_outer;
	}

	err = xe_validation_ctx_init(&ctx, &vm->xe->val, &exec,
				     (struct xe_val_flags) {.interruptible = true});
	if (err)
		goto out_unlock_outer;

	drm_exec_until_all_locked(&exec) {
		bool done = false;

		err = xe_preempt_work_begin(&exec, vm, &done);
		drm_exec_retry_on_contention(&exec);
		xe_validation_retry_on_oom(&ctx, &err);
		if (err || done) {
			xe_validation_ctx_fini(&ctx);
			goto out_unlock_outer;
		}
	}

	err = alloc_preempt_fences(vm, &preempt_fences, &fence_count);
	if (err)
		goto out_unlock;

	xe_vm_set_validation_exec(vm, &exec);
	err = xe_vm_rebind(vm, true);
	xe_vm_set_validation_exec(vm, NULL);
	if (err)
		goto out_unlock;

	/* Wait on rebinds and munmap style VM unbinds */
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

	xe_svm_notifier_lock(vm);
	if (retry_required(tries, vm)) {
		xe_svm_notifier_unlock(vm);
		err = -EAGAIN;
		goto out_unlock;
	}

#undef retry_required

	spin_lock(&vm->xe->ttm.lru_lock);
	ttm_lru_bulk_move_tail(&vm->lru_bulk_move);
	spin_unlock(&vm->xe->ttm.lru_lock);

	/* Point of no return. */
	arm_preempt_fences(vm, &preempt_fences);
	resume_and_reinstall_preempt_fences(vm, &exec);
	xe_svm_notifier_unlock(vm);

out_unlock:
	xe_validation_ctx_fini(&ctx);
out_unlock_outer:
	if (err == -EAGAIN) {
		trace_xe_vm_rebind_worker_retry(vm);
		goto retry;
	}

	if (err) {
		drm_warn(&vm->xe->drm, "VM worker error: %d\n", err);
		xe_vm_kill(vm, true);
	}
	up_write(&vm->lock);

	free_preempt_fences(&preempt_fences);

	trace_xe_vm_rebind_worker_exit(vm);
}

static int xe_vma_ops_alloc(struct xe_vma_ops *vops, bool array_of_binds)
{
	int i;

	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i) {
		if (!vops->pt_update_ops[i].num_ops)
			continue;

		vops->pt_update_ops[i].ops =
			kmalloc_array(vops->pt_update_ops[i].num_ops,
				      sizeof(*vops->pt_update_ops[i].ops),
				      GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!vops->pt_update_ops[i].ops)
			return array_of_binds ? -ENOBUFS : -ENOMEM;
	}

	return 0;
}
ALLOW_ERROR_INJECTION(xe_vma_ops_alloc, ERRNO);

static void xe_vma_svm_prefetch_op_fini(struct xe_vma_op *op)
{
	struct xe_vma *vma;

	vma = gpuva_to_vma(op->base.prefetch.va);

	if (op->base.op == DRM_GPUVA_OP_PREFETCH && xe_vma_is_cpu_addr_mirror(vma))
		xa_destroy(&op->prefetch_range.range);
}

static void xe_vma_svm_prefetch_ops_fini(struct xe_vma_ops *vops)
{
	struct xe_vma_op *op;

	if (!(vops->flags & XE_VMA_OPS_FLAG_HAS_SVM_PREFETCH))
		return;

	list_for_each_entry(op, &vops->list, link)
		xe_vma_svm_prefetch_op_fini(op);
}

static void xe_vma_ops_fini(struct xe_vma_ops *vops)
{
	int i;

	xe_vma_svm_prefetch_ops_fini(vops);

	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i)
		kfree(vops->pt_update_ops[i].ops);
}

static void xe_vma_ops_incr_pt_update_ops(struct xe_vma_ops *vops, u8 tile_mask, int inc_val)
{
	int i;

	if (!inc_val)
		return;

	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i)
		if (BIT(i) & tile_mask)
			vops->pt_update_ops[i].num_ops += inc_val;
}

#define XE_VMA_CREATE_MASK (		    \
	XE_VMA_READ_ONLY |		    \
	XE_VMA_DUMPABLE |		    \
	XE_VMA_SYSTEM_ALLOCATOR |           \
	DRM_GPUVA_SPARSE |		    \
	XE_VMA_MADV_AUTORESET)

static void xe_vm_populate_rebind(struct xe_vma_op *op, struct xe_vma *vma,
				  u8 tile_mask)
{
	INIT_LIST_HEAD(&op->link);
	op->tile_mask = tile_mask;
	op->base.op = DRM_GPUVA_OP_MAP;
	op->base.map.va.addr = vma->gpuva.va.addr;
	op->base.map.va.range = vma->gpuva.va.range;
	op->base.map.gem.obj = vma->gpuva.gem.obj;
	op->base.map.gem.offset = vma->gpuva.gem.offset;
	op->map.vma = vma;
	op->map.immediate = true;
	op->map.vma_flags = vma->gpuva.flags & XE_VMA_CREATE_MASK;
}

static int xe_vm_ops_add_rebind(struct xe_vma_ops *vops, struct xe_vma *vma,
				u8 tile_mask)
{
	struct xe_vma_op *op;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	xe_vm_populate_rebind(op, vma, tile_mask);
	list_add_tail(&op->link, &vops->list);
	xe_vma_ops_incr_pt_update_ops(vops, tile_mask, 1);

	return 0;
}

static struct dma_fence *ops_execute(struct xe_vm *vm,
				     struct xe_vma_ops *vops);
static void xe_vma_ops_init(struct xe_vma_ops *vops, struct xe_vm *vm,
			    struct xe_exec_queue *q,
			    struct xe_sync_entry *syncs, u32 num_syncs);

int xe_vm_rebind(struct xe_vm *vm, bool rebind_worker)
{
	struct dma_fence *fence;
	struct xe_vma *vma, *next;
	struct xe_vma_ops vops;
	struct xe_vma_op *op, *next_op;
	int err, i;

	lockdep_assert_held(&vm->lock);
	if ((xe_vm_in_lr_mode(vm) && !rebind_worker) ||
	    list_empty(&vm->rebind_list))
		return 0;

	xe_vma_ops_init(&vops, vm, NULL, NULL, 0);
	for (i = 0; i < XE_MAX_TILES_PER_DEVICE; ++i)
		vops.pt_update_ops[i].wait_vm_bookkeep = true;

	xe_vm_assert_held(vm);
	list_for_each_entry(vma, &vm->rebind_list, combined_links.rebind) {
		xe_assert(vm->xe, vma->tile_present);

		if (rebind_worker)
			trace_xe_vma_rebind_worker(vma);
		else
			trace_xe_vma_rebind_exec(vma);

		err = xe_vm_ops_add_rebind(&vops, vma,
					   vma->tile_present);
		if (err)
			goto free_ops;
	}

	err = xe_vma_ops_alloc(&vops, false);
	if (err)
		goto free_ops;

	fence = ops_execute(vm, &vops);
	if (IS_ERR(fence)) {
		err = PTR_ERR(fence);
	} else {
		dma_fence_put(fence);
		list_for_each_entry_safe(vma, next, &vm->rebind_list,
					 combined_links.rebind)
			list_del_init(&vma->combined_links.rebind);
	}
free_ops:
	list_for_each_entry_safe(op, next_op, &vops.list, link) {
		list_del(&op->link);
		kfree(op);
	}
	xe_vma_ops_fini(&vops);

	return err;
}

struct dma_fence *xe_vma_rebind(struct xe_vm *vm, struct xe_vma *vma, u8 tile_mask)
{
	struct dma_fence *fence = NULL;
	struct xe_vma_ops vops;
	struct xe_vma_op *op, *next_op;
	struct xe_tile *tile;
	u8 id;
	int err;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);
	xe_assert(vm->xe, xe_vm_in_fault_mode(vm));

	xe_vma_ops_init(&vops, vm, NULL, NULL, 0);
	for_each_tile(tile, vm->xe, id) {
		vops.pt_update_ops[id].wait_vm_bookkeep = true;
		vops.pt_update_ops[tile->id].q =
			xe_migrate_exec_queue(tile->migrate);
	}

	err = xe_vm_ops_add_rebind(&vops, vma, tile_mask);
	if (err)
		return ERR_PTR(err);

	err = xe_vma_ops_alloc(&vops, false);
	if (err) {
		fence = ERR_PTR(err);
		goto free_ops;
	}

	fence = ops_execute(vm, &vops);

free_ops:
	list_for_each_entry_safe(op, next_op, &vops.list, link) {
		list_del(&op->link);
		kfree(op);
	}
	xe_vma_ops_fini(&vops);

	return fence;
}

static void xe_vm_populate_range_rebind(struct xe_vma_op *op,
					struct xe_vma *vma,
					struct xe_svm_range *range,
					u8 tile_mask)
{
	INIT_LIST_HEAD(&op->link);
	op->tile_mask = tile_mask;
	op->base.op = DRM_GPUVA_OP_DRIVER;
	op->subop = XE_VMA_SUBOP_MAP_RANGE;
	op->map_range.vma = vma;
	op->map_range.range = range;
}

static int
xe_vm_ops_add_range_rebind(struct xe_vma_ops *vops,
			   struct xe_vma *vma,
			   struct xe_svm_range *range,
			   u8 tile_mask)
{
	struct xe_vma_op *op;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	xe_vm_populate_range_rebind(op, vma, range, tile_mask);
	list_add_tail(&op->link, &vops->list);
	xe_vma_ops_incr_pt_update_ops(vops, tile_mask, 1);

	return 0;
}

/**
 * xe_vm_range_rebind() - VM range (re)bind
 * @vm: The VM which the range belongs to.
 * @vma: The VMA which the range belongs to.
 * @range: SVM range to rebind.
 * @tile_mask: Tile mask to bind the range to.
 *
 * (re)bind SVM range setting up GPU page tables for the range.
 *
 * Return: dma fence for rebind to signal completion on succees, ERR_PTR on
 * failure
 */
struct dma_fence *xe_vm_range_rebind(struct xe_vm *vm,
				     struct xe_vma *vma,
				     struct xe_svm_range *range,
				     u8 tile_mask)
{
	struct dma_fence *fence = NULL;
	struct xe_vma_ops vops;
	struct xe_vma_op *op, *next_op;
	struct xe_tile *tile;
	u8 id;
	int err;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);
	xe_assert(vm->xe, xe_vm_in_fault_mode(vm));
	xe_assert(vm->xe, xe_vma_is_cpu_addr_mirror(vma));

	xe_vma_ops_init(&vops, vm, NULL, NULL, 0);
	for_each_tile(tile, vm->xe, id) {
		vops.pt_update_ops[id].wait_vm_bookkeep = true;
		vops.pt_update_ops[tile->id].q =
			xe_migrate_exec_queue(tile->migrate);
	}

	err = xe_vm_ops_add_range_rebind(&vops, vma, range, tile_mask);
	if (err)
		return ERR_PTR(err);

	err = xe_vma_ops_alloc(&vops, false);
	if (err) {
		fence = ERR_PTR(err);
		goto free_ops;
	}

	fence = ops_execute(vm, &vops);

free_ops:
	list_for_each_entry_safe(op, next_op, &vops.list, link) {
		list_del(&op->link);
		kfree(op);
	}
	xe_vma_ops_fini(&vops);

	return fence;
}

static void xe_vm_populate_range_unbind(struct xe_vma_op *op,
					struct xe_svm_range *range)
{
	INIT_LIST_HEAD(&op->link);
	op->tile_mask = range->tile_present;
	op->base.op = DRM_GPUVA_OP_DRIVER;
	op->subop = XE_VMA_SUBOP_UNMAP_RANGE;
	op->unmap_range.range = range;
}

static int
xe_vm_ops_add_range_unbind(struct xe_vma_ops *vops,
			   struct xe_svm_range *range)
{
	struct xe_vma_op *op;

	op = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -ENOMEM;

	xe_vm_populate_range_unbind(op, range);
	list_add_tail(&op->link, &vops->list);
	xe_vma_ops_incr_pt_update_ops(vops, range->tile_present, 1);

	return 0;
}

/**
 * xe_vm_range_unbind() - VM range unbind
 * @vm: The VM which the range belongs to.
 * @range: SVM range to rebind.
 *
 * Unbind SVM range removing the GPU page tables for the range.
 *
 * Return: dma fence for unbind to signal completion on succees, ERR_PTR on
 * failure
 */
struct dma_fence *xe_vm_range_unbind(struct xe_vm *vm,
				     struct xe_svm_range *range)
{
	struct dma_fence *fence = NULL;
	struct xe_vma_ops vops;
	struct xe_vma_op *op, *next_op;
	struct xe_tile *tile;
	u8 id;
	int err;

	lockdep_assert_held(&vm->lock);
	xe_vm_assert_held(vm);
	xe_assert(vm->xe, xe_vm_in_fault_mode(vm));

	if (!range->tile_present)
		return dma_fence_get_stub();

	xe_vma_ops_init(&vops, vm, NULL, NULL, 0);
	for_each_tile(tile, vm->xe, id) {
		vops.pt_update_ops[id].wait_vm_bookkeep = true;
		vops.pt_update_ops[tile->id].q =
			xe_migrate_exec_queue(tile->migrate);
	}

	err = xe_vm_ops_add_range_unbind(&vops, range);
	if (err)
		return ERR_PTR(err);

	err = xe_vma_ops_alloc(&vops, false);
	if (err) {
		fence = ERR_PTR(err);
		goto free_ops;
	}

	fence = ops_execute(vm, &vops);

free_ops:
	list_for_each_entry_safe(op, next_op, &vops.list, link) {
		list_del(&op->link);
		kfree(op);
	}
	xe_vma_ops_fini(&vops);

	return fence;
}

static void xe_vma_free(struct xe_vma *vma)
{
	if (xe_vma_is_userptr(vma))
		kfree(to_userptr_vma(vma));
	else
		kfree(vma);
}

static struct xe_vma *xe_vma_create(struct xe_vm *vm,
				    struct xe_bo *bo,
				    u64 bo_offset_or_userptr,
				    u64 start, u64 end,
				    struct xe_vma_mem_attr *attr,
				    unsigned int flags)
{
	struct xe_vma *vma;
	struct xe_tile *tile;
	u8 id;
	bool is_null = (flags & DRM_GPUVA_SPARSE);
	bool is_cpu_addr_mirror = (flags & XE_VMA_SYSTEM_ALLOCATOR);

	xe_assert(vm->xe, start < end);
	xe_assert(vm->xe, end < vm->size);

	/*
	 * Allocate and ensure that the xe_vma_is_userptr() return
	 * matches what was allocated.
	 */
	if (!bo && !is_null && !is_cpu_addr_mirror) {
		struct xe_userptr_vma *uvma = kzalloc(sizeof(*uvma), GFP_KERNEL);

		if (!uvma)
			return ERR_PTR(-ENOMEM);

		vma = &uvma->vma;
	} else {
		vma = kzalloc(sizeof(*vma), GFP_KERNEL);
		if (!vma)
			return ERR_PTR(-ENOMEM);

		if (bo)
			vma->gpuva.gem.obj = &bo->ttm.base;
	}

	INIT_LIST_HEAD(&vma->combined_links.rebind);

	INIT_LIST_HEAD(&vma->gpuva.gem.entry);
	vma->gpuva.vm = &vm->gpuvm;
	vma->gpuva.va.addr = start;
	vma->gpuva.va.range = end - start + 1;
	vma->gpuva.flags = flags;

	for_each_tile(tile, vm->xe, id)
		vma->tile_mask |= 0x1 << id;

	if (vm->xe->info.has_atomic_enable_pte_bit)
		vma->gpuva.flags |= XE_VMA_ATOMIC_PTE_BIT;

	vma->attr = *attr;

	if (bo) {
		struct drm_gpuvm_bo *vm_bo;

		xe_bo_assert_held(bo);

		vm_bo = drm_gpuvm_bo_obtain(vma->gpuva.vm, &bo->ttm.base);
		if (IS_ERR(vm_bo)) {
			xe_vma_free(vma);
			return ERR_CAST(vm_bo);
		}

		drm_gpuvm_bo_extobj_add(vm_bo);
		drm_gem_object_get(&bo->ttm.base);
		vma->gpuva.gem.offset = bo_offset_or_userptr;
		drm_gpuva_link(&vma->gpuva, vm_bo);
		drm_gpuvm_bo_put(vm_bo);
	} else /* userptr or null */ {
		if (!is_null && !is_cpu_addr_mirror) {
			struct xe_userptr_vma *uvma = to_userptr_vma(vma);
			u64 size = end - start + 1;
			int err;

			vma->gpuva.gem.offset = bo_offset_or_userptr;

			err = xe_userptr_setup(uvma, xe_vma_userptr(vma), size);
			if (err) {
				xe_vma_free(vma);
				return ERR_PTR(err);
			}
		}

		xe_vm_get(vm);
	}

	return vma;
}

static void xe_vma_destroy_late(struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);

	if (vma->ufence) {
		xe_sync_ufence_put(vma->ufence);
		vma->ufence = NULL;
	}

	if (xe_vma_is_userptr(vma)) {
		struct xe_userptr_vma *uvma = to_userptr_vma(vma);

		xe_userptr_remove(uvma);
		xe_vm_put(vm);
	} else if (xe_vma_is_null(vma) || xe_vma_is_cpu_addr_mirror(vma)) {
		xe_vm_put(vm);
	} else {
		xe_bo_put(xe_vma_bo(vma));
	}

	xe_vma_free(vma);
}

static void vma_destroy_work_func(struct work_struct *w)
{
	struct xe_vma *vma =
		container_of(w, struct xe_vma, destroy_work);

	xe_vma_destroy_late(vma);
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
		xe_userptr_destroy(to_userptr_vma(vma));
	} else if (!xe_vma_is_null(vma) && !xe_vma_is_cpu_addr_mirror(vma)) {
		xe_bo_assert_held(xe_vma_bo(vma));

		drm_gpuva_unlink(&vma->gpuva);
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
 * xe_vm_lock_vma() - drm_exec utility to lock a vma
 * @exec: The drm_exec object we're currently locking for.
 * @vma: The vma for witch we want to lock the vm resv and any attached
 * object's resv.
 *
 * Return: 0 on success, negative error code on error. In particular
 * may return -EDEADLK on WW transaction contention and -EINTR if
 * an interruptible wait is terminated by a signal.
 */
int xe_vm_lock_vma(struct drm_exec *exec, struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_bo *bo = xe_vma_bo(vma);
	int err;

	XE_WARN_ON(!vm);

	err = drm_exec_lock_obj(exec, xe_vm_obj(vm));
	if (!err && bo && !bo->vm)
		err = drm_exec_lock_obj(exec, &bo->ttm.base);

	return err;
}

static void xe_vma_destroy_unlocked(struct xe_vma *vma)
{
	struct xe_device *xe = xe_vma_vm(vma)->xe;
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	int err = 0;

	xe_validation_guard(&ctx, &xe->val, &exec, (struct xe_val_flags) {}, err) {
		err = xe_vm_lock_vma(&exec, vma);
		drm_exec_retry_on_contention(&exec);
		if (XE_WARN_ON(err))
			break;
		xe_vma_destroy(vma, NULL);
	}
	xe_assert(xe, !err);
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

	mutex_lock(&vm->snap_mutex);
	err = drm_gpuva_insert(&vm->gpuvm, &vma->gpuva);
	mutex_unlock(&vm->snap_mutex);
	XE_WARN_ON(err);	/* Shouldn't be possible */

	return err;
}

static void xe_vm_remove_vma(struct xe_vm *vm, struct xe_vma *vma)
{
	xe_assert(vm->xe, xe_vma_vm(vma) == vm);
	lockdep_assert_held(&vm->lock);

	mutex_lock(&vm->snap_mutex);
	drm_gpuva_remove(&vma->gpuva);
	mutex_unlock(&vm->snap_mutex);
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

static const struct drm_gpuvm_ops gpuvm_ops = {
	.op_alloc = xe_vm_op_alloc,
	.vm_bo_validate = xe_gpuvm_validate,
	.vm_free = xe_vm_free,
};

static u64 pde_encode_pat_index(u16 pat_index)
{
	u64 pte = 0;

	if (pat_index & BIT(0))
		pte |= XE_PPGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XE_PPGTT_PTE_PAT1;

	return pte;
}

static u64 pte_encode_pat_index(u16 pat_index, u32 pt_level)
{
	u64 pte = 0;

	if (pat_index & BIT(0))
		pte |= XE_PPGTT_PTE_PAT0;

	if (pat_index & BIT(1))
		pte |= XE_PPGTT_PTE_PAT1;

	if (pat_index & BIT(2)) {
		if (pt_level)
			pte |= XE_PPGTT_PDE_PDPE_PAT2;
		else
			pte |= XE_PPGTT_PTE_PAT2;
	}

	if (pat_index & BIT(3))
		pte |= XELPG_PPGTT_PTE_PAT3;

	if (pat_index & (BIT(4)))
		pte |= XE2_PPGTT_PTE_PAT4;

	return pte;
}

static u64 pte_encode_ps(u32 pt_level)
{
	XE_WARN_ON(pt_level > MAX_HUGEPTE_LEVEL);

	if (pt_level == 1)
		return XE_PDE_PS_2M;
	else if (pt_level == 2)
		return XE_PDPE_PS_1G;

	return 0;
}

static u16 pde_pat_index(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);
	u16 pat_index;

	/*
	 * We only have two bits to encode the PAT index in non-leaf nodes, but
	 * these only point to other paging structures so we only need a minimal
	 * selection of options. The user PAT index is only for encoding leaf
	 * nodes, where we have use of more bits to do the encoding. The
	 * non-leaf nodes are instead under driver control so the chosen index
	 * here should be distict from the user PAT index. Also the
	 * corresponding coherency of the PAT index should be tied to the
	 * allocation type of the page table (or at least we should pick
	 * something which is always safe).
	 */
	if (!xe_bo_is_vram(bo) && bo->ttm.ttm->caching == ttm_cached)
		pat_index = xe->pat.idx[XE_CACHE_WB];
	else
		pat_index = xe->pat.idx[XE_CACHE_NONE];

	xe_assert(xe, pat_index <= 3);

	return pat_index;
}

static u64 xelp_pde_encode_bo(struct xe_bo *bo, u64 bo_offset)
{
	u64 pde;

	pde = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pde |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pde |= pde_encode_pat_index(pde_pat_index(bo));

	return pde;
}

static u64 xelp_pte_encode_bo(struct xe_bo *bo, u64 bo_offset,
			      u16 pat_index, u32 pt_level)
{
	u64 pte;

	pte = xe_bo_addr(bo, bo_offset, XE_PAGE_SIZE);
	pte |= XE_PAGE_PRESENT | XE_PAGE_RW;
	pte |= pte_encode_pat_index(pat_index, pt_level);
	pte |= pte_encode_ps(pt_level);

	if (xe_bo_is_vram(bo) || xe_bo_is_stolen_devmem(bo))
		pte |= XE_PPGTT_PTE_DM;

	return pte;
}

static u64 xelp_pte_encode_vma(u64 pte, struct xe_vma *vma,
			       u16 pat_index, u32 pt_level)
{
	pte |= XE_PAGE_PRESENT;

	if (likely(!xe_vma_read_only(vma)))
		pte |= XE_PAGE_RW;

	pte |= pte_encode_pat_index(pat_index, pt_level);
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
	pte |= pte_encode_pat_index(pat_index, pt_level);
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

static void vm_destroy_work_func(struct work_struct *w);

/**
 * xe_vm_create_scratch() - Setup a scratch memory pagetable tree for the
 * given tile and vm.
 * @xe: xe device.
 * @tile: tile to set up for.
 * @vm: vm to set up for.
 * @exec: The struct drm_exec object used to lock the vm resv.
 *
 * Sets up a pagetable tree with one page-table per level and a single
 * leaf PTE. All pagetable entries point to the single page-table or,
 * for MAX_HUGEPTE_LEVEL, a NULL huge PTE returning 0 on read and
 * writes become NOPs.
 *
 * Return: 0 on success, negative error code on error.
 */
static int xe_vm_create_scratch(struct xe_device *xe, struct xe_tile *tile,
				struct xe_vm *vm, struct drm_exec *exec)
{
	u8 id = tile->id;
	int i;

	for (i = MAX_HUGEPTE_LEVEL; i < vm->pt_root[id]->level; i++) {
		vm->scratch_pt[id][i] = xe_pt_create(vm, tile, i, exec);
		if (IS_ERR(vm->scratch_pt[id][i])) {
			int err = PTR_ERR(vm->scratch_pt[id][i]);

			vm->scratch_pt[id][i] = NULL;
			return err;
		}
		xe_pt_populate_empty(tile, vm, vm->scratch_pt[id][i]);
	}

	return 0;
}
ALLOW_ERROR_INJECTION(xe_vm_create_scratch, ERRNO);

static void xe_vm_free_scratch(struct xe_vm *vm)
{
	struct xe_tile *tile;
	u8 id;

	if (!xe_vm_has_scratch(vm))
		return;

	for_each_tile(tile, vm->xe, id) {
		u32 i;

		if (!vm->pt_root[id])
			continue;

		for (i = MAX_HUGEPTE_LEVEL; i < vm->pt_root[id]->level; ++i)
			if (vm->scratch_pt[id][i])
				xe_pt_destroy(vm->scratch_pt[id][i], vm->flags, NULL);
	}
}

static void xe_vm_pt_destroy(struct xe_vm *vm)
{
	struct xe_tile *tile;
	u8 id;

	xe_vm_assert_held(vm);

	for_each_tile(tile, vm->xe, id) {
		if (vm->pt_root[id]) {
			xe_pt_destroy(vm->pt_root[id], vm->flags, NULL);
			vm->pt_root[id] = NULL;
		}
	}
}

struct xe_vm *xe_vm_create(struct xe_device *xe, u32 flags, struct xe_file *xef)
{
	struct drm_gem_object *vm_resv_obj;
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	struct xe_vm *vm;
	int err, number_tiles = 0;
	struct xe_tile *tile;
	u8 id;

	/*
	 * Since the GSCCS is not user-accessible, we don't expect a GSC VM to
	 * ever be in faulting mode.
	 */
	xe_assert(xe, !((flags & XE_VM_FLAG_GSC) && (flags & XE_VM_FLAG_FAULT_MODE)));

	vm = kzalloc(sizeof(*vm), GFP_KERNEL);
	if (!vm)
		return ERR_PTR(-ENOMEM);

	vm->xe = xe;

	vm->size = 1ull << xe->info.va_bits;
	vm->flags = flags;

	if (xef)
		vm->xef = xe_file_get(xef);
	/**
	 * GSC VMs are kernel-owned, only used for PXP ops and can sometimes be
	 * manipulated under the PXP mutex. However, the PXP mutex can be taken
	 * under a user-VM lock when the PXP session is started at exec_queue
	 * creation time. Those are different VMs and therefore there is no risk
	 * of deadlock, but we need to tell lockdep that this is the case or it
	 * will print a warning.
	 */
	if (flags & XE_VM_FLAG_GSC) {
		static struct lock_class_key gsc_vm_key;

		__init_rwsem(&vm->lock, "gsc_vm", &gsc_vm_key);
	} else {
		init_rwsem(&vm->lock);
	}
	mutex_init(&vm->snap_mutex);

	INIT_LIST_HEAD(&vm->rebind_list);

	INIT_LIST_HEAD(&vm->userptr.repin_list);
	INIT_LIST_HEAD(&vm->userptr.invalidated);
	spin_lock_init(&vm->userptr.invalidated_lock);

	ttm_lru_bulk_move_init(&vm->lru_bulk_move);

	INIT_WORK(&vm->destroy_work, vm_destroy_work_func);

	INIT_LIST_HEAD(&vm->preempt.exec_queues);
	vm->preempt.min_run_period_ms = 10;	/* FIXME: Wire up to uAPI */

	for_each_tile(tile, xe, id)
		xe_range_fence_tree_init(&vm->rftree[id]);

	vm->pt_ops = &xelp_pt_ops;

	/*
	 * Long-running workloads are not protected by the scheduler references.
	 * By design, run_job for long-running workloads returns NULL and the
	 * scheduler drops all the references of it, hence protecting the VM
	 * for this case is necessary.
	 */
	if (flags & XE_VM_FLAG_LR_MODE) {
		INIT_WORK(&vm->preempt.rebind_work, preempt_rebind_work_func);
		xe_pm_runtime_get_noresume(xe);
		INIT_LIST_HEAD(&vm->preempt.pm_activate_link);
	}

	err = xe_svm_init(vm);
	if (err)
		goto err_no_resv;

	vm_resv_obj = drm_gpuvm_resv_object_alloc(&xe->drm);
	if (!vm_resv_obj) {
		err = -ENOMEM;
		goto err_svm_fini;
	}

	drm_gpuvm_init(&vm->gpuvm, "Xe VM", DRM_GPUVM_RESV_PROTECTED, &xe->drm,
		       vm_resv_obj, 0, vm->size, 0, 0, &gpuvm_ops);

	drm_gem_object_put(vm_resv_obj);

	err = 0;
	xe_validation_guard(&ctx, &xe->val, &exec, (struct xe_val_flags) {.interruptible = true},
			    err) {
		err = xe_vm_drm_exec_lock(vm, &exec);
		drm_exec_retry_on_contention(&exec);

		if (IS_DGFX(xe) && xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)
			vm->flags |= XE_VM_FLAG_64K;

		for_each_tile(tile, xe, id) {
			if (flags & XE_VM_FLAG_MIGRATION &&
			    tile->id != XE_VM_FLAG_TILE_ID(flags))
				continue;

			vm->pt_root[id] = xe_pt_create(vm, tile, xe->info.vm_max_level,
						       &exec);
			if (IS_ERR(vm->pt_root[id])) {
				err = PTR_ERR(vm->pt_root[id]);
				vm->pt_root[id] = NULL;
				xe_vm_pt_destroy(vm);
				drm_exec_retry_on_contention(&exec);
				xe_validation_retry_on_oom(&ctx, &err);
				break;
			}
		}
		if (err)
			break;

		if (xe_vm_has_scratch(vm)) {
			for_each_tile(tile, xe, id) {
				if (!vm->pt_root[id])
					continue;

				err = xe_vm_create_scratch(xe, tile, vm, &exec);
				if (err) {
					xe_vm_free_scratch(vm);
					xe_vm_pt_destroy(vm);
					drm_exec_retry_on_contention(&exec);
					xe_validation_retry_on_oom(&ctx, &err);
					break;
				}
			}
			if (err)
				break;
			vm->batch_invalidate_tlb = true;
		}

		if (vm->flags & XE_VM_FLAG_LR_MODE) {
			INIT_WORK(&vm->preempt.rebind_work, preempt_rebind_work_func);
			vm->batch_invalidate_tlb = false;
		}

		/* Fill pt_root after allocating scratch tables */
		for_each_tile(tile, xe, id) {
			if (!vm->pt_root[id])
				continue;

			xe_pt_populate_empty(tile, vm, vm->pt_root[id]);
		}
	}
	if (err)
		goto err_close;

	/* Kernel migration VM shouldn't have a circular loop.. */
	if (!(flags & XE_VM_FLAG_MIGRATION)) {
		for_each_tile(tile, xe, id) {
			struct xe_exec_queue *q;
			u32 create_flags = EXEC_QUEUE_FLAG_VM;

			if (!vm->pt_root[id])
				continue;

			q = xe_exec_queue_create_bind(xe, tile, create_flags, 0);
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

	if (xef && xe->info.has_asid) {
		u32 asid;

		down_write(&xe->usm.lock);
		err = xa_alloc_cyclic(&xe->usm.asid_to_vm, &asid, vm,
				      XA_LIMIT(1, XE_MAX_ASID - 1),
				      &xe->usm.next_asid, GFP_KERNEL);
		up_write(&xe->usm.lock);
		if (err < 0)
			goto err_close;

		vm->usm.asid = asid;
	}

	trace_xe_vm_create(vm);

	return vm;

err_close:
	xe_vm_close_and_put(vm);
	return ERR_PTR(err);

err_svm_fini:
	if (flags & XE_VM_FLAG_FAULT_MODE) {
		vm->size = 0; /* close the vm */
		xe_svm_fini(vm);
	}
err_no_resv:
	mutex_destroy(&vm->snap_mutex);
	for_each_tile(tile, xe, id)
		xe_range_fence_tree_fini(&vm->rftree[id]);
	ttm_lru_bulk_move_fini(&xe->ttm, &vm->lru_bulk_move);
	if (vm->xef)
		xe_file_put(vm->xef);
	kfree(vm);
	if (flags & XE_VM_FLAG_LR_MODE)
		xe_pm_runtime_put(xe);
	return ERR_PTR(err);
}

static void xe_vm_close(struct xe_vm *vm)
{
	struct xe_device *xe = vm->xe;
	bool bound;
	int idx;

	bound = drm_dev_enter(&xe->drm, &idx);

	down_write(&vm->lock);
	if (xe_vm_in_fault_mode(vm))
		xe_svm_notifier_lock(vm);

	vm->size = 0;

	if (!((vm->flags & XE_VM_FLAG_MIGRATION))) {
		struct xe_tile *tile;
		struct xe_gt *gt;
		u8 id;

		/* Wait for pending binds */
		dma_resv_wait_timeout(xe_vm_resv(vm),
				      DMA_RESV_USAGE_BOOKKEEP,
				      false, MAX_SCHEDULE_TIMEOUT);

		if (bound) {
			for_each_tile(tile, xe, id)
				if (vm->pt_root[id])
					xe_pt_clear(xe, vm->pt_root[id]);

			for_each_gt(gt, xe, id)
				xe_tlb_inval_vm(&gt->tlb_inval, vm);
		}
	}

	if (xe_vm_in_fault_mode(vm))
		xe_svm_notifier_unlock(vm);
	up_write(&vm->lock);

	if (bound)
		drm_dev_exit(idx);
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
	if (xe_vm_in_preempt_fence_mode(vm)) {
		mutex_lock(&xe->rebind_resume_lock);
		list_del_init(&vm->preempt.pm_activate_link);
		mutex_unlock(&xe->rebind_resume_lock);
		flush_work(&vm->preempt.rebind_work);
	}
	if (xe_vm_in_fault_mode(vm))
		xe_svm_close(vm);

	down_write(&vm->lock);
	for_each_tile(tile, xe, id) {
		if (vm->q[id])
			xe_exec_queue_last_fence_put(vm->q[id], vm);
	}
	up_write(&vm->lock);

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
			xe_svm_notifier_lock(vm);
			vma->gpuva.flags |= XE_VMA_DESTROYED;
			xe_svm_notifier_unlock(vm);
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
	xe_vm_free_scratch(vm);
	xe_vm_pt_destroy(vm);
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

	xe_svm_fini(vm);

	up_write(&vm->lock);

	down_write(&xe->usm.lock);
	if (vm->usm.asid) {
		void *lookup;

		xe_assert(xe, xe->info.has_asid);
		xe_assert(xe, !(vm->flags & XE_VM_FLAG_MIGRATION));

		lookup = xa_erase(&xe->usm.asid_to_vm, vm->usm.asid);
		xe_assert(xe, lookup == vm);
	}
	up_write(&xe->usm.lock);

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

	/* xe_vm_close_and_put was not called? */
	xe_assert(xe, !vm->size);

	if (xe_vm_in_preempt_fence_mode(vm))
		flush_work(&vm->preempt.rebind_work);

	mutex_destroy(&vm->snap_mutex);

	if (vm->flags & XE_VM_FLAG_LR_MODE)
		xe_pm_runtime_put(xe);

	for_each_tile(tile, xe, id)
		XE_WARN_ON(vm->pt_root[id]);

	trace_xe_vm_free(vm);

	ttm_lru_bulk_move_fini(&xe->ttm, &vm->lru_bulk_move);

	if (vm->xef)
		xe_file_put(vm->xef);

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
	return vm->pt_ops->pde_encode_bo(vm->pt_root[tile->id]->bo, 0);
}

static struct xe_exec_queue *
to_wait_exec_queue(struct xe_vm *vm, struct xe_exec_queue *q)
{
	return q ? q : vm->q[0];
}

static struct xe_user_fence *
find_ufence_get(struct xe_sync_entry *syncs, u32 num_syncs)
{
	unsigned int i;

	for (i = 0; i < num_syncs; i++) {
		struct xe_sync_entry *e = &syncs[i];

		if (xe_sync_is_ufence(e))
			return xe_sync_ufence_get(e);
	}

	return NULL;
}

#define ALL_DRM_XE_VM_CREATE_FLAGS (DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE | \
				    DRM_XE_VM_CREATE_FLAG_LR_MODE | \
				    DRM_XE_VM_CREATE_FLAG_FAULT_MODE)

int xe_vm_create_ioctl(struct drm_device *dev, void *data,
		       struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_vm_create *args = data;
	struct xe_vm *vm;
	u32 id;
	int err;
	u32 flags = 0;

	if (XE_IOCTL_DBG(xe, args->extensions))
		return -EINVAL;

	if (XE_GT_WA(xe_root_mmio_gt(xe), 14016763929))
		args->flags |= DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE &&
			 !xe->info.has_usm))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & ~ALL_DRM_XE_VM_CREATE_FLAGS))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags & DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE &&
			 args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE &&
			 !xe->info.needs_scratch))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !(args->flags & DRM_XE_VM_CREATE_FLAG_LR_MODE) &&
			 args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE))
		return -EINVAL;

	if (args->flags & DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE)
		flags |= XE_VM_FLAG_SCRATCH_PAGE;
	if (args->flags & DRM_XE_VM_CREATE_FLAG_LR_MODE)
		flags |= XE_VM_FLAG_LR_MODE;
	if (args->flags & DRM_XE_VM_CREATE_FLAG_FAULT_MODE)
		flags |= XE_VM_FLAG_FAULT_MODE;

	vm = xe_vm_create(xe, flags, xef);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

#if IS_ENABLED(CONFIG_DRM_XE_DEBUG_MEM)
	/* Warning: Security issue - never enable by default */
	args->reserved[0] = xe_bo_main_addr(vm->pt_root[0]->bo, XE_PAGE_SIZE);
#endif

	/* user id alloc must always be last in ioctl to prevent UAF */
	err = xa_alloc(&xef->vm.xa, &id, vm, xa_limit_32b, GFP_KERNEL);
	if (err)
		goto err_close_and_put;

	args->vm_id = id;

	return 0;

err_close_and_put:
	xe_vm_close_and_put(vm);

	return err;
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

static int xe_vm_query_vmas(struct xe_vm *vm, u64 start, u64 end)
{
	struct drm_gpuva *gpuva;
	u32 num_vmas = 0;

	lockdep_assert_held(&vm->lock);
	drm_gpuvm_for_each_va_range(gpuva, &vm->gpuvm, start, end)
		num_vmas++;

	return num_vmas;
}

static int get_mem_attrs(struct xe_vm *vm, u32 *num_vmas, u64 start,
			 u64 end, struct drm_xe_mem_range_attr *attrs)
{
	struct drm_gpuva *gpuva;
	int i = 0;

	lockdep_assert_held(&vm->lock);

	drm_gpuvm_for_each_va_range(gpuva, &vm->gpuvm, start, end) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);

		if (i == *num_vmas)
			return -ENOSPC;

		attrs[i].start = xe_vma_start(vma);
		attrs[i].end = xe_vma_end(vma);
		attrs[i].atomic.val = vma->attr.atomic_access;
		attrs[i].pat_index.val = vma->attr.pat_index;
		attrs[i].preferred_mem_loc.devmem_fd = vma->attr.preferred_loc.devmem_fd;
		attrs[i].preferred_mem_loc.migration_policy =
		vma->attr.preferred_loc.migration_policy;

		i++;
	}

	*num_vmas = i;
	return 0;
}

int xe_vm_query_vmas_attrs_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_mem_range_attr *mem_attrs;
	struct drm_xe_vm_query_mem_range_attr *args = data;
	u64 __user *attrs_user = u64_to_user_ptr(args->vector_of_mem_attr);
	struct xe_vm *vm;
	int err = 0;

	if (XE_IOCTL_DBG(xe,
			 ((args->num_mem_ranges == 0 &&
			  (attrs_user || args->sizeof_mem_range_attr != 0)) ||
			 (args->num_mem_ranges > 0 &&
			  (!attrs_user ||
			   args->sizeof_mem_range_attr !=
			   sizeof(struct drm_xe_mem_range_attr))))))
		return -EINVAL;

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm))
		return -EINVAL;

	err = down_read_interruptible(&vm->lock);
	if (err)
		goto put_vm;

	attrs_user = u64_to_user_ptr(args->vector_of_mem_attr);

	if (args->num_mem_ranges == 0 && !attrs_user) {
		args->num_mem_ranges = xe_vm_query_vmas(vm, args->start, args->start + args->range);
		args->sizeof_mem_range_attr = sizeof(struct drm_xe_mem_range_attr);
		goto unlock_vm;
	}

	mem_attrs = kvmalloc_array(args->num_mem_ranges, args->sizeof_mem_range_attr,
				   GFP_KERNEL | __GFP_ACCOUNT |
				   __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (!mem_attrs) {
		err = args->num_mem_ranges > 1 ? -ENOBUFS : -ENOMEM;
		goto unlock_vm;
	}

	memset(mem_attrs, 0, args->num_mem_ranges * args->sizeof_mem_range_attr);
	err = get_mem_attrs(vm, &args->num_mem_ranges, args->start,
			    args->start + args->range, mem_attrs);
	if (err)
		goto free_mem_attrs;

	err = copy_to_user(attrs_user, mem_attrs,
			   args->sizeof_mem_range_attr * args->num_mem_ranges);
	if (err)
		err = -EFAULT;

free_mem_attrs:
	kvfree(mem_attrs);
unlock_vm:
	up_read(&vm->lock);
put_vm:
	xe_vm_put(vm);
	return err;
}

static bool vma_matches(struct xe_vma *vma, u64 page_addr)
{
	if (page_addr > xe_vma_end(vma) - 1 ||
	    page_addr + SZ_4K - 1 < xe_vma_start(vma))
		return false;

	return true;
}

/**
 * xe_vm_find_vma_by_addr() - Find a VMA by its address
 *
 * @vm: the xe_vm the vma belongs to
 * @page_addr: address to look up
 */
struct xe_vma *xe_vm_find_vma_by_addr(struct xe_vm *vm, u64 page_addr)
{
	struct xe_vma *vma = NULL;

	if (vm->usm.last_fault_vma) {   /* Fast lookup */
		if (vma_matches(vm->usm.last_fault_vma, page_addr))
			vma = vm->usm.last_fault_vma;
	}
	if (!vma)
		vma = xe_vm_find_overlapping_vma(vm, page_addr, SZ_4K);

	return vma;
}

static const u32 region_to_mem_type[] = {
	XE_PL_TT,
	XE_PL_VRAM0,
	XE_PL_VRAM1,
};

static void prep_vma_destroy(struct xe_vm *vm, struct xe_vma *vma,
			     bool post_commit)
{
	xe_svm_notifier_lock(vm);
	vma->gpuva.flags |= XE_VMA_DESTROYED;
	xe_svm_notifier_unlock(vm);
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

static bool __xe_vm_needs_clear_scratch_pages(struct xe_vm *vm, u32 bind_flags)
{
	if (!xe_vm_in_fault_mode(vm))
		return false;

	if (!xe_vm_has_scratch(vm))
		return false;

	if (bind_flags & DRM_XE_VM_BIND_FLAG_IMMEDIATE)
		return false;

	return true;
}

static void xe_svm_prefetch_gpuva_ops_fini(struct drm_gpuva_ops *ops)
{
	struct drm_gpuva_op *__op;

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

		xe_vma_svm_prefetch_op_fini(op);
	}
}

/*
 * Create operations list from IOCTL arguments, setup operations fields so parse
 * and commit steps are decoupled from IOCTL arguments. This step can fail.
 */
static struct drm_gpuva_ops *
vm_bind_ioctl_ops_create(struct xe_vm *vm, struct xe_vma_ops *vops,
			 struct xe_bo *bo, u64 bo_offset_or_userptr,
			 u64 addr, u64 range,
			 u32 operation, u32 flags,
			 u32 prefetch_region, u16 pat_index)
{
	struct drm_gem_object *obj = bo ? &bo->ttm.base : NULL;
	struct drm_gpuva_ops *ops;
	struct drm_gpuva_op *__op;
	struct drm_gpuvm_bo *vm_bo;
	u64 range_end = addr + range;
	int err;

	lockdep_assert_held_write(&vm->lock);

	vm_dbg(&vm->xe->drm,
	       "op=%d, addr=0x%016llx, range=0x%016llx, bo_offset_or_userptr=0x%016llx",
	       operation, (ULL)addr, (ULL)range,
	       (ULL)bo_offset_or_userptr);

	switch (operation) {
	case DRM_XE_VM_BIND_OP_MAP:
	case DRM_XE_VM_BIND_OP_MAP_USERPTR: {
		struct drm_gpuvm_map_req map_req = {
			.map.va.addr = addr,
			.map.va.range = range,
			.map.gem.obj = obj,
			.map.gem.offset = bo_offset_or_userptr,
		};

		ops = drm_gpuvm_sm_map_ops_create(&vm->gpuvm, &map_req);
		break;
	}
	case DRM_XE_VM_BIND_OP_UNMAP:
		ops = drm_gpuvm_sm_unmap_ops_create(&vm->gpuvm, addr, range);
		break;
	case DRM_XE_VM_BIND_OP_PREFETCH:
		ops = drm_gpuvm_prefetch_ops_create(&vm->gpuvm, addr, range);
		break;
	case DRM_XE_VM_BIND_OP_UNMAP_ALL:
		xe_assert(vm->xe, bo);

		err = xe_bo_lock(bo, true);
		if (err)
			return ERR_PTR(err);

		vm_bo = drm_gpuvm_bo_obtain(&vm->gpuvm, obj);
		if (IS_ERR(vm_bo)) {
			xe_bo_unlock(bo);
			return ERR_CAST(vm_bo);
		}

		ops = drm_gpuvm_bo_unmap_ops_create(vm_bo);
		drm_gpuvm_bo_put(vm_bo);
		xe_bo_unlock(bo);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
		ops = ERR_PTR(-EINVAL);
	}
	if (IS_ERR(ops))
		return ops;

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);

		if (__op->op == DRM_GPUVA_OP_MAP) {
			op->map.immediate =
				flags & DRM_XE_VM_BIND_FLAG_IMMEDIATE;
			if (flags & DRM_XE_VM_BIND_FLAG_READONLY)
				op->map.vma_flags |= XE_VMA_READ_ONLY;
			if (flags & DRM_XE_VM_BIND_FLAG_NULL)
				op->map.vma_flags |= DRM_GPUVA_SPARSE;
			if (flags & DRM_XE_VM_BIND_FLAG_CPU_ADDR_MIRROR)
				op->map.vma_flags |= XE_VMA_SYSTEM_ALLOCATOR;
			if (flags & DRM_XE_VM_BIND_FLAG_DUMPABLE)
				op->map.vma_flags |= XE_VMA_DUMPABLE;
			if (flags & DRM_XE_VM_BIND_FLAG_MADVISE_AUTORESET)
				op->map.vma_flags |= XE_VMA_MADV_AUTORESET;
			op->map.pat_index = pat_index;
			op->map.invalidate_on_bind =
				__xe_vm_needs_clear_scratch_pages(vm, flags);
		} else if (__op->op == DRM_GPUVA_OP_PREFETCH) {
			struct xe_vma *vma = gpuva_to_vma(op->base.prefetch.va);
			struct xe_tile *tile;
			struct xe_svm_range *svm_range;
			struct drm_gpusvm_ctx ctx = {};
			struct drm_pagemap *dpagemap;
			u8 id, tile_mask = 0;
			u32 i;

			if (!xe_vma_is_cpu_addr_mirror(vma)) {
				op->prefetch.region = prefetch_region;
				break;
			}

			ctx.read_only = xe_vma_read_only(vma);
			ctx.devmem_possible = IS_DGFX(vm->xe) &&
					      IS_ENABLED(CONFIG_DRM_XE_PAGEMAP);

			for_each_tile(tile, vm->xe, id)
				tile_mask |= 0x1 << id;

			xa_init_flags(&op->prefetch_range.range, XA_FLAGS_ALLOC);
			op->prefetch_range.ranges_count = 0;
			tile = NULL;

			if (prefetch_region == DRM_XE_CONSULT_MEM_ADVISE_PREF_LOC) {
				dpagemap = xe_vma_resolve_pagemap(vma,
								  xe_device_get_root_tile(vm->xe));
				/*
				 * TODO: Once multigpu support is enabled will need
				 * something to dereference tile from dpagemap.
				 */
				if (dpagemap)
					tile = xe_device_get_root_tile(vm->xe);
			} else if (prefetch_region) {
				tile = &vm->xe->tiles[region_to_mem_type[prefetch_region] -
						      XE_PL_VRAM0];
			}

			op->prefetch_range.tile = tile;
alloc_next_range:
			svm_range = xe_svm_range_find_or_insert(vm, addr, vma, &ctx);

			if (PTR_ERR(svm_range) == -ENOENT) {
				u64 ret = xe_svm_find_vma_start(vm, addr, range_end, vma);

				addr = ret == ULONG_MAX ? 0 : ret;
				if (addr)
					goto alloc_next_range;
				else
					goto print_op_label;
			}

			if (IS_ERR(svm_range)) {
				err = PTR_ERR(svm_range);
				goto unwind_prefetch_ops;
			}

			if (xe_svm_range_validate(vm, svm_range, tile_mask, !!tile)) {
				xe_svm_range_debug(svm_range, "PREFETCH - RANGE IS VALID");
				goto check_next_range;
			}

			err = xa_alloc(&op->prefetch_range.range,
				       &i, svm_range, xa_limit_32b,
				       GFP_KERNEL);

			if (err)
				goto unwind_prefetch_ops;

			op->prefetch_range.ranges_count++;
			vops->flags |= XE_VMA_OPS_FLAG_HAS_SVM_PREFETCH;
			xe_svm_range_debug(svm_range, "PREFETCH - RANGE CREATED");
check_next_range:
			if (range_end > xe_svm_range_end(svm_range) &&
			    xe_svm_range_end(svm_range) < xe_vma_end(vma)) {
				addr = xe_svm_range_end(svm_range);
				goto alloc_next_range;
			}
		}
print_op_label:
		print_op(vm->xe, __op);
	}

	return ops;

unwind_prefetch_ops:
	xe_svm_prefetch_gpuva_ops_fini(ops);
	drm_gpuva_ops_free(&vm->gpuvm, ops);
	return ERR_PTR(err);
}

ALLOW_ERROR_INJECTION(vm_bind_ioctl_ops_create, ERRNO);

static struct xe_vma *new_vma(struct xe_vm *vm, struct drm_gpuva_op_map *op,
			      struct xe_vma_mem_attr *attr, unsigned int flags)
{
	struct xe_bo *bo = op->gem.obj ? gem_to_xe_bo(op->gem.obj) : NULL;
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	struct xe_vma *vma;
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	if (bo) {
		err = 0;
		xe_validation_guard(&ctx, &vm->xe->val, &exec,
				    (struct xe_val_flags) {.interruptible = true}, err) {
			if (!bo->vm) {
				err = drm_exec_lock_obj(&exec, xe_vm_obj(vm));
				drm_exec_retry_on_contention(&exec);
			}
			if (!err) {
				err = drm_exec_lock_obj(&exec, &bo->ttm.base);
				drm_exec_retry_on_contention(&exec);
			}
			if (err)
				return ERR_PTR(err);

			vma = xe_vma_create(vm, bo, op->gem.offset,
					    op->va.addr, op->va.addr +
					    op->va.range - 1, attr, flags);
			if (IS_ERR(vma))
				return vma;

			if (!bo->vm) {
				err = add_preempt_fences(vm, bo);
				if (err) {
					prep_vma_destroy(vm, vma, false);
					xe_vma_destroy(vma, NULL);
				}
			}
		}
		if (err)
			return ERR_PTR(err);
	} else {
		vma = xe_vma_create(vm, NULL, op->gem.offset,
				    op->va.addr, op->va.addr +
				    op->va.range - 1, attr, flags);
		if (IS_ERR(vma))
			return vma;

		if (xe_vma_is_userptr(vma))
			err = xe_vma_userptr_pin_pages(to_userptr_vma(vma));
	}
	if (err) {
		prep_vma_destroy(vm, vma, false);
		xe_vma_destroy_unlocked(vma);
		vma = ERR_PTR(err);
	}

	return vma;
}

static u64 xe_vma_max_pte_size(struct xe_vma *vma)
{
	if (vma->gpuva.flags & XE_VMA_PTE_1G)
		return SZ_1G;
	else if (vma->gpuva.flags & (XE_VMA_PTE_2M | XE_VMA_PTE_COMPACT))
		return SZ_2M;
	else if (vma->gpuva.flags & XE_VMA_PTE_64K)
		return SZ_64K;
	else if (vma->gpuva.flags & XE_VMA_PTE_4K)
		return SZ_4K;

	return SZ_1G;	/* Uninitialized, used max size */
}

static void xe_vma_set_pte_size(struct xe_vma *vma, u64 size)
{
	switch (size) {
	case SZ_1G:
		vma->gpuva.flags |= XE_VMA_PTE_1G;
		break;
	case SZ_2M:
		vma->gpuva.flags |= XE_VMA_PTE_2M;
		break;
	case SZ_64K:
		vma->gpuva.flags |= XE_VMA_PTE_64K;
		break;
	case SZ_4K:
		vma->gpuva.flags |= XE_VMA_PTE_4K;
		break;
	}
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
	{
		u8 tile_present =
			gpuva_to_vma(op->base.remap.unmap->va)->tile_present;

		prep_vma_destroy(vm, gpuva_to_vma(op->base.remap.unmap->va),
				 true);
		op->flags |= XE_VMA_OP_COMMITTED;

		if (op->remap.prev) {
			err |= xe_vm_insert_vma(vm, op->remap.prev);
			if (!err)
				op->flags |= XE_VMA_OP_PREV_COMMITTED;
			if (!err && op->remap.skip_prev) {
				op->remap.prev->tile_present =
					tile_present;
				op->remap.prev = NULL;
			}
		}
		if (op->remap.next) {
			err |= xe_vm_insert_vma(vm, op->remap.next);
			if (!err)
				op->flags |= XE_VMA_OP_NEXT_COMMITTED;
			if (!err && op->remap.skip_next) {
				op->remap.next->tile_present =
					tile_present;
				op->remap.next = NULL;
			}
		}

		/* Adjust for partial unbind after removing VMA from VM */
		if (!err) {
			op->base.remap.unmap->va->va.addr = op->remap.start;
			op->base.remap.unmap->va->va.range = op->remap.range;
		}
		break;
	}
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

/**
 * xe_vma_has_default_mem_attrs - Check if a VMA has default memory attributes
 * @vma: Pointer to the xe_vma structure to check
 *
 * This function determines whether the given VMA (Virtual Memory Area)
 * has its memory attributes set to their default values. Specifically,
 * it checks the following conditions:
 *
 * - `atomic_access` is `DRM_XE_VMA_ATOMIC_UNDEFINED`
 * - `pat_index` is equal to `default_pat_index`
 * - `preferred_loc.devmem_fd` is `DRM_XE_PREFERRED_LOC_DEFAULT_DEVICE`
 * - `preferred_loc.migration_policy` is `DRM_XE_MIGRATE_ALL_PAGES`
 *
 * Return: true if all attributes are at their default values, false otherwise.
 */
bool xe_vma_has_default_mem_attrs(struct xe_vma *vma)
{
	return (vma->attr.atomic_access == DRM_XE_ATOMIC_UNDEFINED &&
		vma->attr.pat_index ==  vma->attr.default_pat_index &&
		vma->attr.preferred_loc.devmem_fd == DRM_XE_PREFERRED_LOC_DEFAULT_DEVICE &&
		vma->attr.preferred_loc.migration_policy == DRM_XE_MIGRATE_ALL_PAGES);
}

static int vm_bind_ioctl_ops_parse(struct xe_vm *vm, struct drm_gpuva_ops *ops,
				   struct xe_vma_ops *vops)
{
	struct xe_device *xe = vm->xe;
	struct drm_gpuva_op *__op;
	struct xe_tile *tile;
	u8 id, tile_mask = 0;
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	for_each_tile(tile, vm->xe, id)
		tile_mask |= 0x1 << id;

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);
		struct xe_vma *vma;
		unsigned int flags = 0;

		INIT_LIST_HEAD(&op->link);
		list_add_tail(&op->link, &vops->list);
		op->tile_mask = tile_mask;

		switch (op->base.op) {
		case DRM_GPUVA_OP_MAP:
		{
			struct xe_vma_mem_attr default_attr = {
				.preferred_loc = {
					.devmem_fd = DRM_XE_PREFERRED_LOC_DEFAULT_DEVICE,
					.migration_policy = DRM_XE_MIGRATE_ALL_PAGES,
				},
				.atomic_access = DRM_XE_ATOMIC_UNDEFINED,
				.default_pat_index = op->map.pat_index,
				.pat_index = op->map.pat_index,
			};

			flags |= op->map.vma_flags & XE_VMA_CREATE_MASK;

			vma = new_vma(vm, &op->base.map, &default_attr,
				      flags);
			if (IS_ERR(vma))
				return PTR_ERR(vma);

			op->map.vma = vma;
			if (((op->map.immediate || !xe_vm_in_fault_mode(vm)) &&
			     !(op->map.vma_flags & XE_VMA_SYSTEM_ALLOCATOR)) ||
			    op->map.invalidate_on_bind)
				xe_vma_ops_incr_pt_update_ops(vops,
							      op->tile_mask, 1);
			break;
		}
		case DRM_GPUVA_OP_REMAP:
		{
			struct xe_vma *old =
				gpuva_to_vma(op->base.remap.unmap->va);
			bool skip = xe_vma_is_cpu_addr_mirror(old);
			u64 start = xe_vma_start(old), end = xe_vma_end(old);
			int num_remap_ops = 0;

			if (op->base.remap.prev)
				start = op->base.remap.prev->va.addr +
					op->base.remap.prev->va.range;
			if (op->base.remap.next)
				end = op->base.remap.next->va.addr;

			if (xe_vma_is_cpu_addr_mirror(old) &&
			    xe_svm_has_mapping(vm, start, end)) {
				if (vops->flags & XE_VMA_OPS_FLAG_MADVISE)
					xe_svm_unmap_address_range(vm, start, end);
				else
					return -EBUSY;
			}

			op->remap.start = xe_vma_start(old);
			op->remap.range = xe_vma_size(old);

			flags |= op->base.remap.unmap->va->flags & XE_VMA_CREATE_MASK;
			if (op->base.remap.prev) {
				vma = new_vma(vm, op->base.remap.prev,
					      &old->attr, flags);
				if (IS_ERR(vma))
					return PTR_ERR(vma);

				op->remap.prev = vma;

				/*
				 * Userptr creates a new SG mapping so
				 * we must also rebind.
				 */
				op->remap.skip_prev = skip ||
					(!xe_vma_is_userptr(old) &&
					IS_ALIGNED(xe_vma_end(vma),
						   xe_vma_max_pte_size(old)));
				if (op->remap.skip_prev) {
					xe_vma_set_pte_size(vma, xe_vma_max_pte_size(old));
					op->remap.range -=
						xe_vma_end(vma) -
						xe_vma_start(old);
					op->remap.start = xe_vma_end(vma);
					vm_dbg(&xe->drm, "REMAP:SKIP_PREV: addr=0x%016llx, range=0x%016llx",
					       (ULL)op->remap.start,
					       (ULL)op->remap.range);
				} else {
					num_remap_ops++;
				}
			}

			if (op->base.remap.next) {
				vma = new_vma(vm, op->base.remap.next,
					      &old->attr, flags);
				if (IS_ERR(vma))
					return PTR_ERR(vma);

				op->remap.next = vma;

				/*
				 * Userptr creates a new SG mapping so
				 * we must also rebind.
				 */
				op->remap.skip_next = skip ||
					(!xe_vma_is_userptr(old) &&
					IS_ALIGNED(xe_vma_start(vma),
						   xe_vma_max_pte_size(old)));
				if (op->remap.skip_next) {
					xe_vma_set_pte_size(vma, xe_vma_max_pte_size(old));
					op->remap.range -=
						xe_vma_end(old) -
						xe_vma_start(vma);
					vm_dbg(&xe->drm, "REMAP:SKIP_NEXT: addr=0x%016llx, range=0x%016llx",
					       (ULL)op->remap.start,
					       (ULL)op->remap.range);
				} else {
					num_remap_ops++;
				}
			}
			if (!skip)
				num_remap_ops++;

			xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask, num_remap_ops);
			break;
		}
		case DRM_GPUVA_OP_UNMAP:
			vma = gpuva_to_vma(op->base.unmap.va);

			if (xe_vma_is_cpu_addr_mirror(vma) &&
			    xe_svm_has_mapping(vm, xe_vma_start(vma),
					       xe_vma_end(vma)))
				return -EBUSY;

			if (!xe_vma_is_cpu_addr_mirror(vma))
				xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask, 1);
			break;
		case DRM_GPUVA_OP_PREFETCH:
			vma = gpuva_to_vma(op->base.prefetch.va);

			if (xe_vma_is_userptr(vma)) {
				err = xe_vma_userptr_pin_pages(to_userptr_vma(vma));
				if (err)
					return err;
			}

			if (xe_vma_is_cpu_addr_mirror(vma))
				xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask,
							      op->prefetch_range.ranges_count);
			else
				xe_vma_ops_incr_pt_update_ops(vops, op->tile_mask, 1);

			break;
		default:
			drm_warn(&vm->xe->drm, "NOT POSSIBLE");
		}

		err = xe_vma_op_commit(vm, op);
		if (err)
			return err;
	}

	return 0;
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
			xe_svm_notifier_lock(vm);
			vma->gpuva.flags &= ~XE_VMA_DESTROYED;
			xe_svm_notifier_unlock(vm);
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
			xe_svm_notifier_lock(vm);
			vma->gpuva.flags &= ~XE_VMA_DESTROYED;
			xe_svm_notifier_unlock(vm);
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

static void vm_bind_ioctl_ops_unwind(struct xe_vm *vm,
				     struct drm_gpuva_ops **ops,
				     int num_ops_list)
{
	int i;

	for (i = num_ops_list - 1; i >= 0; --i) {
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
	}
}

static int vma_lock_and_validate(struct drm_exec *exec, struct xe_vma *vma,
				 bool res_evict, bool validate)
{
	struct xe_bo *bo = xe_vma_bo(vma);
	struct xe_vm *vm = xe_vma_vm(vma);
	int err = 0;

	if (bo) {
		if (!bo->vm)
			err = drm_exec_lock_obj(exec, &bo->ttm.base);
		if (!err && validate)
			err = xe_bo_validate(bo, vm,
					     !xe_vm_in_preempt_fence_mode(vm) &&
					     res_evict, exec);
	}

	return err;
}

static int check_ufence(struct xe_vma *vma)
{
	if (vma->ufence) {
		struct xe_user_fence * const f = vma->ufence;

		if (!xe_sync_ufence_get_status(f))
			return -EBUSY;

		vma->ufence = NULL;
		xe_sync_ufence_put(f);
	}

	return 0;
}

static int prefetch_ranges(struct xe_vm *vm, struct xe_vma_op *op)
{
	bool devmem_possible = IS_DGFX(vm->xe) && IS_ENABLED(CONFIG_DRM_XE_PAGEMAP);
	struct xe_vma *vma = gpuva_to_vma(op->base.prefetch.va);
	struct xe_tile *tile = op->prefetch_range.tile;
	int err = 0;

	struct xe_svm_range *svm_range;
	struct drm_gpusvm_ctx ctx = {};
	unsigned long i;

	if (!xe_vma_is_cpu_addr_mirror(vma))
		return 0;

	ctx.read_only = xe_vma_read_only(vma);
	ctx.devmem_possible = devmem_possible;
	ctx.check_pages_threshold = devmem_possible ? SZ_64K : 0;
	ctx.device_private_page_owner = xe_svm_devm_owner(vm->xe);

	/* TODO: Threading the migration */
	xa_for_each(&op->prefetch_range.range, i, svm_range) {
		if (!tile)
			xe_svm_range_migrate_to_smem(vm, svm_range);

		if (xe_svm_range_needs_migrate_to_vram(svm_range, vma, !!tile)) {
			err = xe_svm_alloc_vram(tile, svm_range, &ctx);
			if (err) {
				drm_dbg(&vm->xe->drm, "VRAM allocation failed, retry from userspace, asid=%u, gpusvm=%p, errno=%pe\n",
					vm->usm.asid, &vm->svm.gpusvm, ERR_PTR(err));
				return -ENODATA;
			}
			xe_svm_range_debug(svm_range, "PREFETCH - RANGE MIGRATED TO VRAM");
		}

		err = xe_svm_range_get_pages(vm, svm_range, &ctx);
		if (err) {
			drm_dbg(&vm->xe->drm, "Get pages failed, asid=%u, gpusvm=%p, errno=%pe\n",
				vm->usm.asid, &vm->svm.gpusvm, ERR_PTR(err));
			if (err == -EOPNOTSUPP || err == -EFAULT || err == -EPERM)
				err = -ENODATA;
			return err;
		}
		xe_svm_range_debug(svm_range, "PREFETCH - RANGE GET PAGES DONE");
	}

	return err;
}

static int op_lock_and_prep(struct drm_exec *exec, struct xe_vm *vm,
			    struct xe_vma_ops *vops, struct xe_vma_op *op)
{
	int err = 0;
	bool res_evict;

	/*
	 * We only allow evicting a BO within the VM if it is not part of an
	 * array of binds, as an array of binds can evict another BO within the
	 * bind.
	 */
	res_evict = !(vops->flags & XE_VMA_OPS_ARRAY_OF_BINDS);

	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		if (!op->map.invalidate_on_bind)
			err = vma_lock_and_validate(exec, op->map.vma,
						    res_evict,
						    !xe_vm_in_fault_mode(vm) ||
						    op->map.immediate);
		break;
	case DRM_GPUVA_OP_REMAP:
		err = check_ufence(gpuva_to_vma(op->base.remap.unmap->va));
		if (err)
			break;

		err = vma_lock_and_validate(exec,
					    gpuva_to_vma(op->base.remap.unmap->va),
					    res_evict, false);
		if (!err && op->remap.prev)
			err = vma_lock_and_validate(exec, op->remap.prev,
						    res_evict, true);
		if (!err && op->remap.next)
			err = vma_lock_and_validate(exec, op->remap.next,
						    res_evict, true);
		break;
	case DRM_GPUVA_OP_UNMAP:
		err = check_ufence(gpuva_to_vma(op->base.unmap.va));
		if (err)
			break;

		err = vma_lock_and_validate(exec,
					    gpuva_to_vma(op->base.unmap.va),
					    res_evict, false);
		break;
	case DRM_GPUVA_OP_PREFETCH:
	{
		struct xe_vma *vma = gpuva_to_vma(op->base.prefetch.va);
		u32 region;

		if (!xe_vma_is_cpu_addr_mirror(vma)) {
			region = op->prefetch.region;
			xe_assert(vm->xe, region == DRM_XE_CONSULT_MEM_ADVISE_PREF_LOC ||
				  region <= ARRAY_SIZE(region_to_mem_type));
		}

		err = vma_lock_and_validate(exec,
					    gpuva_to_vma(op->base.prefetch.va),
					    res_evict, false);
		if (!err && !xe_vma_has_no_bo(vma))
			err = xe_bo_migrate(xe_vma_bo(vma),
					    region_to_mem_type[region],
					    NULL,
					    exec);
		break;
	}
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}

	return err;
}

static int vm_bind_ioctl_ops_prefetch_ranges(struct xe_vm *vm, struct xe_vma_ops *vops)
{
	struct xe_vma_op *op;
	int err;

	if (!(vops->flags & XE_VMA_OPS_FLAG_HAS_SVM_PREFETCH))
		return 0;

	list_for_each_entry(op, &vops->list, link) {
		if (op->base.op  == DRM_GPUVA_OP_PREFETCH) {
			err = prefetch_ranges(vm, op);
			if (err)
				return err;
		}
	}

	return 0;
}

static int vm_bind_ioctl_ops_lock_and_prep(struct drm_exec *exec,
					   struct xe_vm *vm,
					   struct xe_vma_ops *vops)
{
	struct xe_vma_op *op;
	int err;

	err = drm_exec_lock_obj(exec, xe_vm_obj(vm));
	if (err)
		return err;

	list_for_each_entry(op, &vops->list, link) {
		err = op_lock_and_prep(exec, vm, vops, op);
		if (err)
			return err;
	}

#ifdef TEST_VM_OPS_ERROR
	if (vops->inject_error &&
	    vm->xe->vm_inject_error_position == FORCE_OP_ERROR_LOCK)
		return -ENOSPC;
#endif

	return 0;
}

static void op_trace(struct xe_vma_op *op)
{
	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		trace_xe_vma_bind(op->map.vma);
		break;
	case DRM_GPUVA_OP_REMAP:
		trace_xe_vma_unbind(gpuva_to_vma(op->base.remap.unmap->va));
		if (op->remap.prev)
			trace_xe_vma_bind(op->remap.prev);
		if (op->remap.next)
			trace_xe_vma_bind(op->remap.next);
		break;
	case DRM_GPUVA_OP_UNMAP:
		trace_xe_vma_unbind(gpuva_to_vma(op->base.unmap.va));
		break;
	case DRM_GPUVA_OP_PREFETCH:
		trace_xe_vma_bind(gpuva_to_vma(op->base.prefetch.va));
		break;
	case DRM_GPUVA_OP_DRIVER:
		break;
	default:
		XE_WARN_ON("NOT POSSIBLE");
	}
}

static void trace_xe_vm_ops_execute(struct xe_vma_ops *vops)
{
	struct xe_vma_op *op;

	list_for_each_entry(op, &vops->list, link)
		op_trace(op);
}

static int vm_ops_setup_tile_args(struct xe_vm *vm, struct xe_vma_ops *vops)
{
	struct xe_exec_queue *q = vops->q;
	struct xe_tile *tile;
	int number_tiles = 0;
	u8 id;

	for_each_tile(tile, vm->xe, id) {
		if (vops->pt_update_ops[id].num_ops)
			++number_tiles;

		if (vops->pt_update_ops[id].q)
			continue;

		if (q) {
			vops->pt_update_ops[id].q = q;
			if (vm->pt_root[id] && !list_empty(&q->multi_gt_list))
				q = list_next_entry(q, multi_gt_list);
		} else {
			vops->pt_update_ops[id].q = vm->q[id];
		}
	}

	return number_tiles;
}

static struct dma_fence *ops_execute(struct xe_vm *vm,
				     struct xe_vma_ops *vops)
{
	struct xe_tile *tile;
	struct dma_fence *fence = NULL;
	struct dma_fence **fences = NULL;
	struct dma_fence_array *cf = NULL;
	int number_tiles = 0, current_fence = 0, err;
	u8 id;

	number_tiles = vm_ops_setup_tile_args(vm, vops);
	if (number_tiles == 0)
		return ERR_PTR(-ENODATA);

	if (number_tiles > 1) {
		fences = kmalloc_array(number_tiles, sizeof(*fences),
				       GFP_KERNEL);
		if (!fences) {
			fence = ERR_PTR(-ENOMEM);
			goto err_trace;
		}
	}

	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		err = xe_pt_update_ops_prepare(tile, vops);
		if (err) {
			fence = ERR_PTR(err);
			goto err_out;
		}
	}

	trace_xe_vm_ops_execute(vops);

	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		fence = xe_pt_update_ops_run(tile, vops);
		if (IS_ERR(fence))
			goto err_out;

		if (fences)
			fences[current_fence++] = fence;
	}

	if (fences) {
		cf = dma_fence_array_create(number_tiles, fences,
					    vm->composite_fence_ctx,
					    vm->composite_fence_seqno++,
					    false);
		if (!cf) {
			--vm->composite_fence_seqno;
			fence = ERR_PTR(-ENOMEM);
			goto err_out;
		}
		fence = &cf->base;
	}

	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		xe_pt_update_ops_fini(tile, vops);
	}

	return fence;

err_out:
	for_each_tile(tile, vm->xe, id) {
		if (!vops->pt_update_ops[id].num_ops)
			continue;

		xe_pt_update_ops_abort(tile, vops);
	}
	while (current_fence)
		dma_fence_put(fences[--current_fence]);
	kfree(fences);
	kfree(cf);

err_trace:
	trace_xe_vm_ops_fail(vm);
	return fence;
}

static void vma_add_ufence(struct xe_vma *vma, struct xe_user_fence *ufence)
{
	if (vma->ufence)
		xe_sync_ufence_put(vma->ufence);
	vma->ufence = __xe_sync_ufence_get(ufence);
}

static void op_add_ufence(struct xe_vm *vm, struct xe_vma_op *op,
			  struct xe_user_fence *ufence)
{
	switch (op->base.op) {
	case DRM_GPUVA_OP_MAP:
		vma_add_ufence(op->map.vma, ufence);
		break;
	case DRM_GPUVA_OP_REMAP:
		if (op->remap.prev)
			vma_add_ufence(op->remap.prev, ufence);
		if (op->remap.next)
			vma_add_ufence(op->remap.next, ufence);
		break;
	case DRM_GPUVA_OP_UNMAP:
		break;
	case DRM_GPUVA_OP_PREFETCH:
		vma_add_ufence(gpuva_to_vma(op->base.prefetch.va), ufence);
		break;
	default:
		drm_warn(&vm->xe->drm, "NOT POSSIBLE");
	}
}

static void vm_bind_ioctl_ops_fini(struct xe_vm *vm, struct xe_vma_ops *vops,
				   struct dma_fence *fence)
{
	struct xe_exec_queue *wait_exec_queue = to_wait_exec_queue(vm, vops->q);
	struct xe_user_fence *ufence;
	struct xe_vma_op *op;
	int i;

	ufence = find_ufence_get(vops->syncs, vops->num_syncs);
	list_for_each_entry(op, &vops->list, link) {
		if (ufence)
			op_add_ufence(vm, op, ufence);

		if (op->base.op == DRM_GPUVA_OP_UNMAP)
			xe_vma_destroy(gpuva_to_vma(op->base.unmap.va), fence);
		else if (op->base.op == DRM_GPUVA_OP_REMAP)
			xe_vma_destroy(gpuva_to_vma(op->base.remap.unmap->va),
				       fence);
	}
	if (ufence)
		xe_sync_ufence_put(ufence);
	if (fence) {
		for (i = 0; i < vops->num_syncs; i++)
			xe_sync_entry_signal(vops->syncs + i, fence);
		xe_exec_queue_last_fence_set(wait_exec_queue, vm, fence);
	}
}

static struct dma_fence *vm_bind_ioctl_ops_execute(struct xe_vm *vm,
						   struct xe_vma_ops *vops)
{
	struct xe_validation_ctx ctx;
	struct drm_exec exec;
	struct dma_fence *fence;
	int err = 0;

	lockdep_assert_held_write(&vm->lock);

	xe_validation_guard(&ctx, &vm->xe->val, &exec,
			    ((struct xe_val_flags) {
				    .interruptible = true,
				    .exec_ignore_duplicates = true,
			    }), err) {
		err = vm_bind_ioctl_ops_lock_and_prep(&exec, vm, vops);
		drm_exec_retry_on_contention(&exec);
		xe_validation_retry_on_oom(&ctx, &err);
		if (err)
			return ERR_PTR(err);

		xe_vm_set_validation_exec(vm, &exec);
		fence = ops_execute(vm, vops);
		xe_vm_set_validation_exec(vm, NULL);
		if (IS_ERR(fence)) {
			if (PTR_ERR(fence) == -ENODATA)
				vm_bind_ioctl_ops_fini(vm, vops, NULL);
			return fence;
		}

		vm_bind_ioctl_ops_fini(vm, vops, fence);
	}

	return err ? ERR_PTR(err) : fence;
}
ALLOW_ERROR_INJECTION(vm_bind_ioctl_ops_execute, ERRNO);

#define SUPPORTED_FLAGS_STUB  \
	(DRM_XE_VM_BIND_FLAG_READONLY | \
	 DRM_XE_VM_BIND_FLAG_IMMEDIATE | \
	 DRM_XE_VM_BIND_FLAG_NULL | \
	 DRM_XE_VM_BIND_FLAG_DUMPABLE | \
	 DRM_XE_VM_BIND_FLAG_CHECK_PXP | \
	 DRM_XE_VM_BIND_FLAG_CPU_ADDR_MIRROR | \
	 DRM_XE_VM_BIND_FLAG_MADVISE_AUTORESET)

#ifdef TEST_VM_OPS_ERROR
#define SUPPORTED_FLAGS	(SUPPORTED_FLAGS_STUB | FORCE_OP_ERROR)
#else
#define SUPPORTED_FLAGS	SUPPORTED_FLAGS_STUB
#endif

#define XE_64K_PAGE_MASK 0xffffull
#define ALL_DRM_XE_SYNCS_FLAGS (DRM_XE_SYNCS_FLAG_WAIT_FOR_OP)

static int vm_bind_ioctl_check_args(struct xe_device *xe, struct xe_vm *vm,
				    struct drm_xe_vm_bind *args,
				    struct drm_xe_vm_bind_op **bind_ops)
{
	int err;
	int i;

	if (XE_IOCTL_DBG(xe, args->pad || args->pad2) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->extensions))
		return -EINVAL;

	if (args->num_binds > 1) {
		u64 __user *bind_user =
			u64_to_user_ptr(args->vector_of_binds);

		*bind_ops = kvmalloc_array(args->num_binds,
					   sizeof(struct drm_xe_vm_bind_op),
					   GFP_KERNEL | __GFP_ACCOUNT |
					   __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!*bind_ops)
			return args->num_binds > 1 ? -ENOBUFS : -ENOMEM;

		err = copy_from_user(*bind_ops, bind_user,
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
		u32 prefetch_region = (*bind_ops)[i].prefetch_mem_region_instance;
		bool is_null = flags & DRM_XE_VM_BIND_FLAG_NULL;
		bool is_cpu_addr_mirror = flags &
			DRM_XE_VM_BIND_FLAG_CPU_ADDR_MIRROR;
		u16 pat_index = (*bind_ops)[i].pat_index;
		u16 coh_mode;

		if (XE_IOCTL_DBG(xe, is_cpu_addr_mirror &&
				 (!xe_vm_in_fault_mode(vm) ||
				 !IS_ENABLED(CONFIG_DRM_XE_GPUSVM)))) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, pat_index >= xe->pat.n_entries)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		pat_index = array_index_nospec(pat_index, xe->pat.n_entries);
		(*bind_ops)[i].pat_index = pat_index;
		coh_mode = xe_pat_index_get_coh_mode(xe, pat_index);
		if (XE_IOCTL_DBG(xe, !coh_mode)) { /* hw reserved */
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_WARN_ON(coh_mode > XE_COH_AT_LEAST_1WAY)) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, op > DRM_XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, flags & ~SUPPORTED_FLAGS) ||
		    XE_IOCTL_DBG(xe, obj && (is_null || is_cpu_addr_mirror)) ||
		    XE_IOCTL_DBG(xe, obj_offset && (is_null ||
						    is_cpu_addr_mirror)) ||
		    XE_IOCTL_DBG(xe, op != DRM_XE_VM_BIND_OP_MAP &&
				 (is_null || is_cpu_addr_mirror)) ||
		    XE_IOCTL_DBG(xe, !obj &&
				 op == DRM_XE_VM_BIND_OP_MAP &&
				 !is_null && !is_cpu_addr_mirror) ||
		    XE_IOCTL_DBG(xe, !obj &&
				 op == DRM_XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, addr &&
				 op == DRM_XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, range &&
				 op == DRM_XE_VM_BIND_OP_UNMAP_ALL) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == DRM_XE_VM_BIND_OP_MAP_USERPTR) ||
		    XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE &&
				 op == DRM_XE_VM_BIND_OP_MAP_USERPTR) ||
		    XE_IOCTL_DBG(xe, op == DRM_XE_VM_BIND_OP_MAP_USERPTR &&
				 !IS_ENABLED(CONFIG_DRM_GPUSVM)) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == DRM_XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, prefetch_region &&
				 op != DRM_XE_VM_BIND_OP_PREFETCH) ||
		    XE_IOCTL_DBG(xe, (prefetch_region != DRM_XE_CONSULT_MEM_ADVISE_PREF_LOC &&
				      /* Guard against undefined shift in BIT(prefetch_region) */
				      (prefetch_region >= (sizeof(xe->info.mem_region_mask) * 8) ||
				      !(BIT(prefetch_region) & xe->info.mem_region_mask)))) ||
		    XE_IOCTL_DBG(xe, obj &&
				 op == DRM_XE_VM_BIND_OP_UNMAP) ||
		    XE_IOCTL_DBG(xe, (flags & DRM_XE_VM_BIND_FLAG_MADVISE_AUTORESET) &&
				 (!is_cpu_addr_mirror || op != DRM_XE_VM_BIND_OP_MAP))) {
			err = -EINVAL;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, obj_offset & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, addr & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, range & ~PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, !range &&
				 op != DRM_XE_VM_BIND_OP_UNMAP_ALL)) {
			err = -EINVAL;
			goto free_bind_ops;
		}
	}

	return 0;

free_bind_ops:
	if (args->num_binds > 1)
		kvfree(*bind_ops);
	*bind_ops = NULL;
	return err;
}

static int vm_bind_ioctl_signal_fences(struct xe_vm *vm,
				       struct xe_exec_queue *q,
				       struct xe_sync_entry *syncs,
				       int num_syncs)
{
	struct dma_fence *fence;
	int i, err = 0;

	fence = xe_sync_in_fence_get(syncs, num_syncs,
				     to_wait_exec_queue(vm, q), vm);
	if (IS_ERR(fence))
		return PTR_ERR(fence);

	for (i = 0; i < num_syncs; i++)
		xe_sync_entry_signal(&syncs[i], fence);

	xe_exec_queue_last_fence_set(to_wait_exec_queue(vm, q), vm,
				     fence);
	dma_fence_put(fence);

	return err;
}

static void xe_vma_ops_init(struct xe_vma_ops *vops, struct xe_vm *vm,
			    struct xe_exec_queue *q,
			    struct xe_sync_entry *syncs, u32 num_syncs)
{
	memset(vops, 0, sizeof(*vops));
	INIT_LIST_HEAD(&vops->list);
	vops->vm = vm;
	vops->q = q;
	vops->syncs = syncs;
	vops->num_syncs = num_syncs;
	vops->flags = 0;
}

static int xe_vm_bind_ioctl_validate_bo(struct xe_device *xe, struct xe_bo *bo,
					u64 addr, u64 range, u64 obj_offset,
					u16 pat_index, u32 op, u32 bind_flags)
{
	u16 coh_mode;

	if (XE_IOCTL_DBG(xe, range > xe_bo_size(bo)) ||
	    XE_IOCTL_DBG(xe, obj_offset >
			 xe_bo_size(bo) - range)) {
		return -EINVAL;
	}

	/*
	 * Some platforms require 64k VM_BIND alignment,
	 * specifically those with XE_VRAM_FLAGS_NEED64K.
	 *
	 * Other platforms may have BO's set to 64k physical placement,
	 * but can be mapped at 4k offsets anyway. This check is only
	 * there for the former case.
	 */
	if ((bo->flags & XE_BO_FLAG_INTERNAL_64K) &&
	    (xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K)) {
		if (XE_IOCTL_DBG(xe, obj_offset &
				 XE_64K_PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, addr & XE_64K_PAGE_MASK) ||
		    XE_IOCTL_DBG(xe, range & XE_64K_PAGE_MASK)) {
			return -EINVAL;
		}
	}

	coh_mode = xe_pat_index_get_coh_mode(xe, pat_index);
	if (bo->cpu_caching) {
		if (XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE &&
				 bo->cpu_caching == DRM_XE_GEM_CPU_CACHING_WB)) {
			return -EINVAL;
		}
	} else if (XE_IOCTL_DBG(xe, coh_mode == XE_COH_NONE)) {
		/*
		 * Imported dma-buf from a different device should
		 * require 1way or 2way coherency since we don't know
		 * how it was mapped on the CPU. Just assume is it
		 * potentially cached on CPU side.
		 */
		return -EINVAL;
	}

	/* If a BO is protected it can only be mapped if the key is still valid */
	if ((bind_flags & DRM_XE_VM_BIND_FLAG_CHECK_PXP) && xe_bo_is_protected(bo) &&
	    op != DRM_XE_VM_BIND_OP_UNMAP && op != DRM_XE_VM_BIND_OP_UNMAP_ALL)
		if (XE_IOCTL_DBG(xe, xe_pxp_bo_key_check(xe->pxp, bo) != 0))
			return -ENOEXEC;

	return 0;
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
	u32 num_syncs, num_ufence = 0;
	struct xe_sync_entry *syncs = NULL;
	struct drm_xe_vm_bind_op *bind_ops = NULL;
	struct xe_vma_ops vops;
	struct dma_fence *fence;
	int err;
	int i;

	vm = xe_vm_lookup(xef, args->vm_id);
	if (XE_IOCTL_DBG(xe, !vm))
		return -EINVAL;

	err = vm_bind_ioctl_check_args(xe, vm, args, &bind_ops);
	if (err)
		goto put_vm;

	if (args->exec_queue_id) {
		q = xe_exec_queue_lookup(xef, args->exec_queue_id);
		if (XE_IOCTL_DBG(xe, !q)) {
			err = -ENOENT;
			goto free_bind_ops;
		}

		if (XE_IOCTL_DBG(xe, !(q->flags & EXEC_QUEUE_FLAG_VM))) {
			err = -EINVAL;
			goto put_exec_queue;
		}
	}

	/* Ensure all UNMAPs visible */
	xe_svm_flush(vm);

	err = down_write_killable(&vm->lock);
	if (err)
		goto put_exec_queue;

	if (XE_IOCTL_DBG(xe, xe_vm_is_closed_or_banned(vm))) {
		err = -ENOENT;
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
	}

	if (args->num_binds) {
		bos = kvcalloc(args->num_binds, sizeof(*bos),
			       GFP_KERNEL | __GFP_ACCOUNT |
			       __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!bos) {
			err = -ENOMEM;
			goto release_vm_lock;
		}

		ops = kvcalloc(args->num_binds, sizeof(*ops),
			       GFP_KERNEL | __GFP_ACCOUNT |
			       __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
		if (!ops) {
			err = -ENOMEM;
			goto free_bos;
		}
	}

	for (i = 0; i < args->num_binds; ++i) {
		struct drm_gem_object *gem_obj;
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 obj = bind_ops[i].obj;
		u64 obj_offset = bind_ops[i].obj_offset;
		u16 pat_index = bind_ops[i].pat_index;
		u32 op = bind_ops[i].op;
		u32 bind_flags = bind_ops[i].flags;

		if (!obj)
			continue;

		gem_obj = drm_gem_object_lookup(file, obj);
		if (XE_IOCTL_DBG(xe, !gem_obj)) {
			err = -ENOENT;
			goto put_obj;
		}
		bos[i] = gem_to_xe_bo(gem_obj);

		err = xe_vm_bind_ioctl_validate_bo(xe, bos[i], addr, range,
						   obj_offset, pat_index, op,
						   bind_flags);
		if (err)
			goto put_obj;
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
		struct xe_exec_queue *__q = q ?: vm->q[0];

		err = xe_sync_entry_parse(xe, xef, &syncs[num_syncs],
					  &syncs_user[num_syncs],
					  __q->ufence_syncobj,
					  ++__q->ufence_timeline_value,
					  (xe_vm_in_lr_mode(vm) ?
					   SYNC_PARSE_FLAG_LR_MODE : 0) |
					  (!args->num_binds ?
					   SYNC_PARSE_FLAG_DISALLOW_USER_FENCE : 0));
		if (err)
			goto free_syncs;

		if (xe_sync_is_ufence(&syncs[num_syncs]))
			num_ufence++;
	}

	if (XE_IOCTL_DBG(xe, num_ufence > 1)) {
		err = -EINVAL;
		goto free_syncs;
	}

	if (!args->num_binds) {
		err = -ENODATA;
		goto free_syncs;
	}

	xe_vma_ops_init(&vops, vm, q, syncs, num_syncs);
	if (args->num_binds > 1)
		vops.flags |= XE_VMA_OPS_ARRAY_OF_BINDS;
	for (i = 0; i < args->num_binds; ++i) {
		u64 range = bind_ops[i].range;
		u64 addr = bind_ops[i].addr;
		u32 op = bind_ops[i].op;
		u32 flags = bind_ops[i].flags;
		u64 obj_offset = bind_ops[i].obj_offset;
		u32 prefetch_region = bind_ops[i].prefetch_mem_region_instance;
		u16 pat_index = bind_ops[i].pat_index;

		ops[i] = vm_bind_ioctl_ops_create(vm, &vops, bos[i], obj_offset,
						  addr, range, op, flags,
						  prefetch_region, pat_index);
		if (IS_ERR(ops[i])) {
			err = PTR_ERR(ops[i]);
			ops[i] = NULL;
			goto unwind_ops;
		}

		err = vm_bind_ioctl_ops_parse(vm, ops[i], &vops);
		if (err)
			goto unwind_ops;

#ifdef TEST_VM_OPS_ERROR
		if (flags & FORCE_OP_ERROR) {
			vops.inject_error = true;
			vm->xe->vm_inject_error_position =
				(vm->xe->vm_inject_error_position + 1) %
				FORCE_OP_ERROR_COUNT;
		}
#endif
	}

	/* Nothing to do */
	if (list_empty(&vops.list)) {
		err = -ENODATA;
		goto unwind_ops;
	}

	err = xe_vma_ops_alloc(&vops, args->num_binds > 1);
	if (err)
		goto unwind_ops;

	err = vm_bind_ioctl_ops_prefetch_ranges(vm, &vops);
	if (err)
		goto unwind_ops;

	fence = vm_bind_ioctl_ops_execute(vm, &vops);
	if (IS_ERR(fence))
		err = PTR_ERR(fence);
	else
		dma_fence_put(fence);

unwind_ops:
	if (err && err != -ENODATA)
		vm_bind_ioctl_ops_unwind(vm, ops, args->num_binds);
	xe_vma_ops_fini(&vops);
	for (i = args->num_binds - 1; i >= 0; --i)
		if (ops[i])
			drm_gpuva_ops_free(&vm->gpuvm, ops[i]);
free_syncs:
	if (err == -ENODATA)
		err = vm_bind_ioctl_signal_fences(vm, q, syncs, num_syncs);
	while (num_syncs--)
		xe_sync_entry_cleanup(&syncs[num_syncs]);

	kfree(syncs);
put_obj:
	for (i = 0; i < args->num_binds; ++i)
		xe_bo_put(bos[i]);

	kvfree(ops);
free_bos:
	kvfree(bos);
release_vm_lock:
	up_write(&vm->lock);
put_exec_queue:
	if (q)
		xe_exec_queue_put(q);
free_bind_ops:
	if (args->num_binds > 1)
		kvfree(bind_ops);
put_vm:
	xe_vm_put(vm);
	return err;
}

/**
 * xe_vm_bind_kernel_bo - bind a kernel BO to a VM
 * @vm: VM to bind the BO to
 * @bo: BO to bind
 * @q: exec queue to use for the bind (optional)
 * @addr: address at which to bind the BO
 * @cache_lvl: PAT cache level to use
 *
 * Execute a VM bind map operation on a kernel-owned BO to bind it into a
 * kernel-owned VM.
 *
 * Returns a dma_fence to track the binding completion if the job to do so was
 * successfully submitted, an error pointer otherwise.
 */
struct dma_fence *xe_vm_bind_kernel_bo(struct xe_vm *vm, struct xe_bo *bo,
				       struct xe_exec_queue *q, u64 addr,
				       enum xe_cache_level cache_lvl)
{
	struct xe_vma_ops vops;
	struct drm_gpuva_ops *ops = NULL;
	struct dma_fence *fence;
	int err;

	xe_bo_get(bo);
	xe_vm_get(vm);
	if (q)
		xe_exec_queue_get(q);

	down_write(&vm->lock);

	xe_vma_ops_init(&vops, vm, q, NULL, 0);

	ops = vm_bind_ioctl_ops_create(vm, &vops, bo, 0, addr, xe_bo_size(bo),
				       DRM_XE_VM_BIND_OP_MAP, 0, 0,
				       vm->xe->pat.idx[cache_lvl]);
	if (IS_ERR(ops)) {
		err = PTR_ERR(ops);
		goto release_vm_lock;
	}

	err = vm_bind_ioctl_ops_parse(vm, ops, &vops);
	if (err)
		goto release_vm_lock;

	xe_assert(vm->xe, !list_empty(&vops.list));

	err = xe_vma_ops_alloc(&vops, false);
	if (err)
		goto unwind_ops;

	fence = vm_bind_ioctl_ops_execute(vm, &vops);
	if (IS_ERR(fence))
		err = PTR_ERR(fence);

unwind_ops:
	if (err && err != -ENODATA)
		vm_bind_ioctl_ops_unwind(vm, &ops, 1);

	xe_vma_ops_fini(&vops);
	drm_gpuva_ops_free(&vm->gpuvm, ops);

release_vm_lock:
	up_write(&vm->lock);

	if (q)
		xe_exec_queue_put(q);
	xe_vm_put(vm);
	xe_bo_put(bo);

	if (err)
		fence = ERR_PTR(err);

	return fence;
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
	int ret;

	if (intr)
		ret = dma_resv_lock_interruptible(xe_vm_resv(vm), NULL);
	else
		ret = dma_resv_lock(xe_vm_resv(vm), NULL);

	return ret;
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
 * xe_vm_range_tilemask_tlb_inval - Issue a TLB invalidation on this tilemask for an
 * address range
 * @vm: The VM
 * @start: start address
 * @end: end address
 * @tile_mask: mask for which gt's issue tlb invalidation
 *
 * Issue a range based TLB invalidation for gt's in tilemask
 *
 * Returns 0 for success, negative error code otherwise.
 */
int xe_vm_range_tilemask_tlb_inval(struct xe_vm *vm, u64 start,
				   u64 end, u8 tile_mask)
{
	struct xe_tlb_inval_fence
		fence[XE_MAX_TILES_PER_DEVICE * XE_MAX_GT_PER_TILE];
	struct xe_tile *tile;
	u32 fence_id = 0;
	u8 id;
	int err;

	if (!tile_mask)
		return 0;

	for_each_tile(tile, vm->xe, id) {
		if (!(tile_mask & BIT(id)))
			continue;

		xe_tlb_inval_fence_init(&tile->primary_gt->tlb_inval,
					&fence[fence_id], true);

		err = xe_tlb_inval_range(&tile->primary_gt->tlb_inval,
					 &fence[fence_id], start, end,
					 vm->usm.asid);
		if (err)
			goto wait;
		++fence_id;

		if (!tile->media_gt)
			continue;

		xe_tlb_inval_fence_init(&tile->media_gt->tlb_inval,
					&fence[fence_id], true);

		err = xe_tlb_inval_range(&tile->media_gt->tlb_inval,
					 &fence[fence_id], start, end,
					 vm->usm.asid);
		if (err)
			goto wait;
		++fence_id;
	}

wait:
	for (id = 0; id < fence_id; ++id)
		xe_tlb_inval_fence_wait(&fence[id]);

	return err;
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
	struct xe_vm *vm = xe_vma_vm(vma);
	struct xe_tile *tile;
	u8 tile_mask = 0;
	int ret = 0;
	u8 id;

	xe_assert(xe, !xe_vma_is_null(vma));
	xe_assert(xe, !xe_vma_is_cpu_addr_mirror(vma));
	trace_xe_vma_invalidate(vma);

	vm_dbg(&vm->xe->drm,
	       "INVALIDATE: addr=0x%016llx, range=0x%016llx",
		xe_vma_start(vma), xe_vma_size(vma));

	/*
	 * Check that we don't race with page-table updates, tile_invalidated
	 * update is safe
	 */
	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		if (xe_vma_is_userptr(vma)) {
			lockdep_assert(lockdep_is_held_type(&vm->svm.gpusvm.notifier_lock, 0) ||
				       (lockdep_is_held_type(&vm->svm.gpusvm.notifier_lock, 1) &&
					lockdep_is_held(&xe_vm_resv(vm)->lock.base)));

			WARN_ON_ONCE(!mmu_interval_check_retry
				     (&to_userptr_vma(vma)->userptr.notifier,
				      to_userptr_vma(vma)->userptr.pages.notifier_seq));
			WARN_ON_ONCE(!dma_resv_test_signaled(xe_vm_resv(vm),
							     DMA_RESV_USAGE_BOOKKEEP));

		} else {
			xe_bo_assert_held(xe_vma_bo(vma));
		}
	}

	for_each_tile(tile, xe, id)
		if (xe_pt_zap_ptes(tile, vma))
			tile_mask |= BIT(id);

	xe_device_wmb(xe);

	ret = xe_vm_range_tilemask_tlb_inval(xe_vma_vm(vma), xe_vma_start(vma),
					     xe_vma_end(vma), tile_mask);

	/* WRITE_ONCE pairs with READ_ONCE in xe_vm_has_valid_gpu_mapping() */
	WRITE_ONCE(vma->tile_invalidated, vma->tile_mask);

	return ret;
}

int xe_vm_validate_protected(struct xe_vm *vm)
{
	struct drm_gpuva *gpuva;
	int err = 0;

	if (!vm)
		return -ENODEV;

	mutex_lock(&vm->snap_mutex);

	drm_gpuvm_for_each_va(gpuva, &vm->gpuvm) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);
		struct xe_bo *bo = vma->gpuva.gem.obj ?
			gem_to_xe_bo(vma->gpuva.gem.obj) : NULL;

		if (!bo)
			continue;

		if (xe_bo_is_protected(bo)) {
			err = xe_pxp_bo_key_check(vm->xe->pxp, bo);
			if (err)
				break;
		}
	}

	mutex_unlock(&vm->snap_mutex);
	return err;
}

struct xe_vm_snapshot {
	unsigned long num_snaps;
	struct {
		u64 ofs, bo_ofs;
		unsigned long len;
		struct xe_bo *bo;
		void *data;
		struct mm_struct *mm;
	} snap[];
};

struct xe_vm_snapshot *xe_vm_snapshot_capture(struct xe_vm *vm)
{
	unsigned long num_snaps = 0, i;
	struct xe_vm_snapshot *snap = NULL;
	struct drm_gpuva *gpuva;

	if (!vm)
		return NULL;

	mutex_lock(&vm->snap_mutex);
	drm_gpuvm_for_each_va(gpuva, &vm->gpuvm) {
		if (gpuva->flags & XE_VMA_DUMPABLE)
			num_snaps++;
	}

	if (num_snaps)
		snap = kvzalloc(offsetof(struct xe_vm_snapshot, snap[num_snaps]), GFP_NOWAIT);
	if (!snap) {
		snap = num_snaps ? ERR_PTR(-ENOMEM) : ERR_PTR(-ENODEV);
		goto out_unlock;
	}

	snap->num_snaps = num_snaps;
	i = 0;
	drm_gpuvm_for_each_va(gpuva, &vm->gpuvm) {
		struct xe_vma *vma = gpuva_to_vma(gpuva);
		struct xe_bo *bo = vma->gpuva.gem.obj ?
			gem_to_xe_bo(vma->gpuva.gem.obj) : NULL;

		if (!(gpuva->flags & XE_VMA_DUMPABLE))
			continue;

		snap->snap[i].ofs = xe_vma_start(vma);
		snap->snap[i].len = xe_vma_size(vma);
		if (bo) {
			snap->snap[i].bo = xe_bo_get(bo);
			snap->snap[i].bo_ofs = xe_vma_bo_offset(vma);
		} else if (xe_vma_is_userptr(vma)) {
			struct mm_struct *mm =
				to_userptr_vma(vma)->userptr.notifier.mm;

			if (mmget_not_zero(mm))
				snap->snap[i].mm = mm;
			else
				snap->snap[i].data = ERR_PTR(-EFAULT);

			snap->snap[i].bo_ofs = xe_vma_userptr(vma);
		} else {
			snap->snap[i].data = ERR_PTR(-ENOENT);
		}
		i++;
	}

out_unlock:
	mutex_unlock(&vm->snap_mutex);
	return snap;
}

void xe_vm_snapshot_capture_delayed(struct xe_vm_snapshot *snap)
{
	if (IS_ERR_OR_NULL(snap))
		return;

	for (int i = 0; i < snap->num_snaps; i++) {
		struct xe_bo *bo = snap->snap[i].bo;
		int err;

		if (IS_ERR(snap->snap[i].data))
			continue;

		snap->snap[i].data = kvmalloc(snap->snap[i].len, GFP_USER);
		if (!snap->snap[i].data) {
			snap->snap[i].data = ERR_PTR(-ENOMEM);
			goto cleanup_bo;
		}

		if (bo) {
			err = xe_bo_read(bo, snap->snap[i].bo_ofs,
					 snap->snap[i].data, snap->snap[i].len);
		} else {
			void __user *userptr = (void __user *)(size_t)snap->snap[i].bo_ofs;

			kthread_use_mm(snap->snap[i].mm);
			if (!copy_from_user(snap->snap[i].data, userptr, snap->snap[i].len))
				err = 0;
			else
				err = -EFAULT;
			kthread_unuse_mm(snap->snap[i].mm);

			mmput(snap->snap[i].mm);
			snap->snap[i].mm = NULL;
		}

		if (err) {
			kvfree(snap->snap[i].data);
			snap->snap[i].data = ERR_PTR(err);
		}

cleanup_bo:
		xe_bo_put(bo);
		snap->snap[i].bo = NULL;
	}
}

void xe_vm_snapshot_print(struct xe_vm_snapshot *snap, struct drm_printer *p)
{
	unsigned long i, j;

	if (IS_ERR_OR_NULL(snap)) {
		drm_printf(p, "[0].error: %li\n", PTR_ERR(snap));
		return;
	}

	for (i = 0; i < snap->num_snaps; i++) {
		drm_printf(p, "[%llx].length: 0x%lx\n", snap->snap[i].ofs, snap->snap[i].len);

		if (IS_ERR(snap->snap[i].data)) {
			drm_printf(p, "[%llx].error: %li\n", snap->snap[i].ofs,
				   PTR_ERR(snap->snap[i].data));
			continue;
		}

		drm_printf(p, "[%llx].data: ", snap->snap[i].ofs);

		for (j = 0; j < snap->snap[i].len; j += sizeof(u32)) {
			u32 *val = snap->snap[i].data + j;
			char dumped[ASCII85_BUFSZ];

			drm_puts(p, ascii85_encode(*val, dumped));
		}

		drm_puts(p, "\n");

		if (drm_coredump_printer_is_full(p))
			return;
	}
}

void xe_vm_snapshot_free(struct xe_vm_snapshot *snap)
{
	unsigned long i;

	if (IS_ERR_OR_NULL(snap))
		return;

	for (i = 0; i < snap->num_snaps; i++) {
		if (!IS_ERR(snap->snap[i].data))
			kvfree(snap->snap[i].data);
		xe_bo_put(snap->snap[i].bo);
		if (snap->snap[i].mm)
			mmput(snap->snap[i].mm);
	}
	kvfree(snap);
}

/**
 * xe_vma_need_vram_for_atomic - Check if VMA needs VRAM migration for atomic operations
 * @xe: Pointer to the XE device structure
 * @vma: Pointer to the virtual memory area (VMA) structure
 * @is_atomic: In pagefault path and atomic operation
 *
 * This function determines whether the given VMA needs to be migrated to
 * VRAM in order to do atomic GPU operation.
 *
 * Return:
 *   1        - Migration to VRAM is required
 *   0        - Migration is not required
 *   -EACCES  - Invalid access for atomic memory attr
 *
 */
int xe_vma_need_vram_for_atomic(struct xe_device *xe, struct xe_vma *vma, bool is_atomic)
{
	u32 atomic_access = xe_vma_bo(vma) ? xe_vma_bo(vma)->attr.atomic_access :
					     vma->attr.atomic_access;

	if (!IS_DGFX(xe) || !is_atomic)
		return false;

	/*
	 * NOTE: The checks implemented here are platform-specific. For
	 * instance, on a device supporting CXL atomics, these would ideally
	 * work universally without additional handling.
	 */
	switch (atomic_access) {
	case DRM_XE_ATOMIC_DEVICE:
		return !xe->info.has_device_atomics_on_smem;

	case DRM_XE_ATOMIC_CPU:
		return -EACCES;

	case DRM_XE_ATOMIC_UNDEFINED:
	case DRM_XE_ATOMIC_GLOBAL:
	default:
		return 1;
	}
}

static int xe_vm_alloc_vma(struct xe_vm *vm,
			   struct drm_gpuvm_map_req *map_req,
			   bool is_madvise)
{
	struct xe_vma_ops vops;
	struct drm_gpuva_ops *ops = NULL;
	struct drm_gpuva_op *__op;
	unsigned int vma_flags = 0;
	bool remap_op = false;
	struct xe_vma_mem_attr tmp_attr;
	u16 default_pat;
	int err;

	lockdep_assert_held_write(&vm->lock);

	if (is_madvise)
		ops = drm_gpuvm_madvise_ops_create(&vm->gpuvm, map_req);
	else
		ops = drm_gpuvm_sm_map_ops_create(&vm->gpuvm, map_req);

	if (IS_ERR(ops))
		return PTR_ERR(ops);

	if (list_empty(&ops->list)) {
		err = 0;
		goto free_ops;
	}

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);
		struct xe_vma *vma = NULL;

		if (!is_madvise) {
			if (__op->op == DRM_GPUVA_OP_UNMAP) {
				vma = gpuva_to_vma(op->base.unmap.va);
				XE_WARN_ON(!xe_vma_has_default_mem_attrs(vma));
				default_pat = vma->attr.default_pat_index;
				vma_flags = vma->gpuva.flags;
			}

			if (__op->op == DRM_GPUVA_OP_REMAP) {
				vma = gpuva_to_vma(op->base.remap.unmap->va);
				default_pat = vma->attr.default_pat_index;
				vma_flags = vma->gpuva.flags;
			}

			if (__op->op == DRM_GPUVA_OP_MAP) {
				op->map.vma_flags |= vma_flags & XE_VMA_CREATE_MASK;
				op->map.pat_index = default_pat;
			}
		} else {
			if (__op->op == DRM_GPUVA_OP_REMAP) {
				vma = gpuva_to_vma(op->base.remap.unmap->va);
				xe_assert(vm->xe, !remap_op);
				xe_assert(vm->xe, xe_vma_has_no_bo(vma));
				remap_op = true;
				vma_flags = vma->gpuva.flags;
			}

			if (__op->op == DRM_GPUVA_OP_MAP) {
				xe_assert(vm->xe, remap_op);
				remap_op = false;
				/*
				 * In case of madvise ops DRM_GPUVA_OP_MAP is
				 * always after DRM_GPUVA_OP_REMAP, so ensure
				 * to propagate the flags from the vma we're
				 * unmapping.
				 */
				op->map.vma_flags |= vma_flags & XE_VMA_CREATE_MASK;
			}
		}
		print_op(vm->xe, __op);
	}

	xe_vma_ops_init(&vops, vm, NULL, NULL, 0);

	if (is_madvise)
		vops.flags |= XE_VMA_OPS_FLAG_MADVISE;

	err = vm_bind_ioctl_ops_parse(vm, ops, &vops);
	if (err)
		goto unwind_ops;

	xe_vm_lock(vm, false);

	drm_gpuva_for_each_op(__op, ops) {
		struct xe_vma_op *op = gpuva_op_to_vma_op(__op);
		struct xe_vma *vma;

		if (__op->op == DRM_GPUVA_OP_UNMAP) {
			vma = gpuva_to_vma(op->base.unmap.va);
			/* There should be no unmap for madvise */
			if (is_madvise)
				XE_WARN_ON("UNEXPECTED UNMAP");

			xe_vma_destroy(vma, NULL);
		} else if (__op->op == DRM_GPUVA_OP_REMAP) {
			vma = gpuva_to_vma(op->base.remap.unmap->va);
			/* In case of madvise ops Store attributes for REMAP UNMAPPED
			 * VMA, so they can be assigned to newly MAP created vma.
			 */
			if (is_madvise)
				tmp_attr = vma->attr;

			xe_vma_destroy(gpuva_to_vma(op->base.remap.unmap->va), NULL);
		} else if (__op->op == DRM_GPUVA_OP_MAP) {
			vma = op->map.vma;
			/* In case of madvise call, MAP will always be follwed by REMAP.
			 * Therefore temp_attr will always have sane values, making it safe to
			 * copy them to new vma.
			 */
			if (is_madvise)
				vma->attr = tmp_attr;
		}
	}

	xe_vm_unlock(vm);
	drm_gpuva_ops_free(&vm->gpuvm, ops);
	return 0;

unwind_ops:
	vm_bind_ioctl_ops_unwind(vm, &ops, 1);
free_ops:
	drm_gpuva_ops_free(&vm->gpuvm, ops);
	return err;
}

/**
 * xe_vm_alloc_madvise_vma - Allocate VMA's with madvise ops
 * @vm: Pointer to the xe_vm structure
 * @start: Starting input address
 * @range: Size of the input range
 *
 * This function splits existing vma to create new vma for user provided input range
 *
 * Return: 0 if success
 */
int xe_vm_alloc_madvise_vma(struct xe_vm *vm, uint64_t start, uint64_t range)
{
	struct drm_gpuvm_map_req map_req = {
		.map.va.addr = start,
		.map.va.range = range,
	};

	lockdep_assert_held_write(&vm->lock);

	vm_dbg(&vm->xe->drm, "MADVISE_OPS_CREATE: addr=0x%016llx, size=0x%016llx", start, range);

	return xe_vm_alloc_vma(vm, &map_req, true);
}

/**
 * xe_vm_alloc_cpu_addr_mirror_vma - Allocate CPU addr mirror vma
 * @vm: Pointer to the xe_vm structure
 * @start: Starting input address
 * @range: Size of the input range
 *
 * This function splits/merges existing vma to create new vma for user provided input range
 *
 * Return: 0 if success
 */
int xe_vm_alloc_cpu_addr_mirror_vma(struct xe_vm *vm, uint64_t start, uint64_t range)
{
	struct drm_gpuvm_map_req map_req = {
		.map.va.addr = start,
		.map.va.range = range,
	};

	lockdep_assert_held_write(&vm->lock);

	vm_dbg(&vm->xe->drm, "CPU_ADDR_MIRROR_VMA_OPS_CREATE: addr=0x%016llx, size=0x%016llx",
	       start, range);

	return xe_vm_alloc_vma(vm, &map_req, false);
}
