// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/circ_buf.h>

#include <drm/drm_managed.h>
#include <drm/ttm/ttm_execbuf_util.h>

#include "xe_bo.h"
#include "xe_gt.h"
#include "xe_gt_pagefault.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_migrate.h"
#include "xe_pt.h"
#include "xe_trace.h"
#include "xe_vm.h"

struct pagefault {
	u64 page_addr;
	u32 asid;
	u16 pdata;
	u8 vfid;
	u8 access_type;
	u8 fault_type;
	u8 fault_level;
	u8 engine_class;
	u8 engine_instance;
	u8 fault_unsuccessful;
};

enum access_type {
	ACCESS_TYPE_READ = 0,
	ACCESS_TYPE_WRITE = 1,
	ACCESS_TYPE_ATOMIC = 2,
	ACCESS_TYPE_RESERVED = 3,
};

enum fault_type {
	NOT_PRESENT = 0,
	WRITE_ACCESS_VIOLATION = 1,
	ATOMIC_ACCESS_VIOLATION = 2,
};

struct acc {
	u64 va_range_base;
	u32 asid;
	u32 sub_granularity;
	u8 granularity;
	u8 vfid;
	u8 access_type;
	u8 engine_class;
	u8 engine_instance;
};

static struct xe_gt *
guc_to_gt(struct xe_guc *guc)
{
	return container_of(guc, struct xe_gt, uc.guc);
}

static bool access_is_atomic(enum access_type access_type)
{
	return access_type == ACCESS_TYPE_ATOMIC;
}

static bool vma_is_valid(struct xe_gt *gt, struct xe_vma *vma)
{
	return BIT(gt->info.id) & vma->gt_present &&
		!(BIT(gt->info.id) & vma->usm.gt_invalidated);
}

static bool vma_matches(struct xe_vma *vma, struct xe_vma *lookup)
{
	if (lookup->start > vma->end || lookup->end < vma->start)
		return false;

	return true;
}

static bool only_needs_bo_lock(struct xe_bo *bo)
{
	return bo && bo->vm;
}

static struct xe_vma *lookup_vma(struct xe_vm *vm, u64 page_addr)
{
	struct xe_vma *vma = NULL, lookup;

	lookup.start = page_addr;
	lookup.end = lookup.start + SZ_4K - 1;
	if (vm->usm.last_fault_vma) {   /* Fast lookup */
		if (vma_matches(vm->usm.last_fault_vma, &lookup))
			vma = vm->usm.last_fault_vma;
	}
	if (!vma)
		vma = xe_vm_find_overlapping_vma(vm, &lookup);

	return vma;
}

static int handle_pagefault(struct xe_gt *gt, struct pagefault *pf)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_vm *vm;
	struct xe_vma *vma = NULL;
	struct xe_bo *bo;
	LIST_HEAD(objs);
	LIST_HEAD(dups);
	struct ttm_validate_buffer tv_bo, tv_vm;
	struct ww_acquire_ctx ww;
	struct dma_fence *fence;
	bool write_locked;
	int ret = 0;
	bool atomic;

	/* ASID to VM */
	mutex_lock(&xe->usm.lock);
	vm = xa_load(&xe->usm.asid_to_vm, pf->asid);
	if (vm)
		xe_vm_get(vm);
	mutex_unlock(&xe->usm.lock);
	if (!vm || !xe_vm_in_fault_mode(vm))
		return -EINVAL;

retry_userptr:
	/*
	 * TODO: Avoid exclusive lock if VM doesn't have userptrs, or
	 * start out read-locked?
	 */
	down_write(&vm->lock);
	write_locked = true;
	vma = lookup_vma(vm, pf->page_addr);
	if (!vma) {
		ret = -EINVAL;
		goto unlock_vm;
	}

	if (!xe_vma_is_userptr(vma) || !xe_vma_userptr_check_repin(vma)) {
		downgrade_write(&vm->lock);
		write_locked = false;
	}

	trace_xe_vma_pagefault(vma);

	atomic = access_is_atomic(pf->access_type);

	/* Check if VMA is valid */
	if (vma_is_valid(gt, vma) && !atomic)
		goto unlock_vm;

	/* TODO: Validate fault */

	if (xe_vma_is_userptr(vma) && write_locked) {
		spin_lock(&vm->userptr.invalidated_lock);
		list_del_init(&vma->userptr.invalidate_link);
		spin_unlock(&vm->userptr.invalidated_lock);

		ret = xe_vma_userptr_pin_pages(vma);
		if (ret)
			goto unlock_vm;

		downgrade_write(&vm->lock);
		write_locked = false;
	}

	/* Lock VM and BOs dma-resv */
	bo = vma->bo;
	if (only_needs_bo_lock(bo)) {
		/* This path ensures the BO's LRU is updated */
		ret = xe_bo_lock(bo, &ww, xe->info.tile_count, false);
	} else {
		tv_vm.num_shared = xe->info.tile_count;
		tv_vm.bo = xe_vm_ttm_bo(vm);
		list_add(&tv_vm.head, &objs);
		if (bo) {
			tv_bo.bo = &bo->ttm;
			tv_bo.num_shared = xe->info.tile_count;
			list_add(&tv_bo.head, &objs);
		}
		ret = ttm_eu_reserve_buffers(&ww, &objs, false, &dups);
	}
	if (ret)
		goto unlock_vm;

	if (atomic) {
		if (xe_vma_is_userptr(vma)) {
			ret = -EACCES;
			goto unlock_dma_resv;
		}

		/* Migrate to VRAM, move should invalidate the VMA first */
		ret = xe_bo_migrate(bo, XE_PL_VRAM0 + gt->info.vram_id);
		if (ret)
			goto unlock_dma_resv;
	} else if (bo) {
		/* Create backing store if needed */
		ret = xe_bo_validate(bo, vm, true);
		if (ret)
			goto unlock_dma_resv;
	}

	/* Bind VMA only to the GT that has faulted */
	trace_xe_vma_pf_bind(vma);
	fence = __xe_pt_bind_vma(gt, vma, xe_gt_migrate_engine(gt), NULL, 0,
				 vma->gt_present & BIT(gt->info.id));
	if (IS_ERR(fence)) {
		ret = PTR_ERR(fence);
		goto unlock_dma_resv;
	}

	/*
	 * XXX: Should we drop the lock before waiting? This only helps if doing
	 * GPU binds which is currently only done if we have to wait for more
	 * than 10ms on a move.
	 */
	dma_fence_wait(fence, false);
	dma_fence_put(fence);

	if (xe_vma_is_userptr(vma))
		ret = xe_vma_userptr_check_repin(vma);
	vma->usm.gt_invalidated &= ~BIT(gt->info.id);

unlock_dma_resv:
	if (only_needs_bo_lock(bo))
		xe_bo_unlock(bo, &ww);
	else
		ttm_eu_backoff_reservation(&ww, &objs);
unlock_vm:
	if (!ret)
		vm->usm.last_fault_vma = vma;
	if (write_locked)
		up_write(&vm->lock);
	else
		up_read(&vm->lock);
	if (ret == -EAGAIN)
		goto retry_userptr;

	if (!ret) {
		/*
		 * FIXME: Doing a full TLB invalidation for now, likely could
		 * defer TLB invalidate + fault response to a callback of fence
		 * too
		 */
		ret = xe_gt_tlb_invalidation(gt, NULL);
		if (ret >= 0)
			ret = 0;
	}
	xe_vm_put(vm);

	return ret;
}

static int send_pagefault_reply(struct xe_guc *guc,
				struct xe_guc_pagefault_reply *reply)
{
	u32 action[] = {
		XE_GUC_ACTION_PAGE_FAULT_RES_DESC,
		reply->dw0,
		reply->dw1,
	};

	return xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action), 0, 0);
}

static void print_pagefault(struct xe_device *xe, struct pagefault *pf)
{
	drm_warn(&xe->drm, "\n\tASID: %d\n"
		 "\tVFID: %d\n"
		 "\tPDATA: 0x%04x\n"
		 "\tFaulted Address: 0x%08x%08x\n"
		 "\tFaultType: %d\n"
		 "\tAccessType: %d\n"
		 "\tFaultLevel: %d\n"
		 "\tEngineClass: %d\n"
		 "\tEngineInstance: %d\n",
		 pf->asid, pf->vfid, pf->pdata, upper_32_bits(pf->page_addr),
		 lower_32_bits(pf->page_addr),
		 pf->fault_type, pf->access_type, pf->fault_level,
		 pf->engine_class, pf->engine_instance);
}

#define PF_MSG_LEN_DW	4

static int get_pagefault(struct pf_queue *pf_queue, struct pagefault *pf)
{
	const struct xe_guc_pagefault_desc *desc;
	int ret = 0;

	spin_lock_irq(&pf_queue->lock);
	if (pf_queue->head != pf_queue->tail) {
		desc = (const struct xe_guc_pagefault_desc *)
			(pf_queue->data + pf_queue->head);

		pf->fault_level = FIELD_GET(PFD_FAULT_LEVEL, desc->dw0);
		pf->engine_class = FIELD_GET(PFD_ENG_CLASS, desc->dw0);
		pf->engine_instance = FIELD_GET(PFD_ENG_INSTANCE, desc->dw0);
		pf->pdata = FIELD_GET(PFD_PDATA_HI, desc->dw1) <<
			PFD_PDATA_HI_SHIFT;
		pf->pdata |= FIELD_GET(PFD_PDATA_LO, desc->dw0);
		pf->asid = FIELD_GET(PFD_ASID, desc->dw1);
		pf->vfid = FIELD_GET(PFD_VFID, desc->dw2);
		pf->access_type = FIELD_GET(PFD_ACCESS_TYPE, desc->dw2);
		pf->fault_type = FIELD_GET(PFD_FAULT_TYPE, desc->dw2);
		pf->page_addr = (u64)(FIELD_GET(PFD_VIRTUAL_ADDR_HI, desc->dw3)) <<
			PFD_VIRTUAL_ADDR_HI_SHIFT;
		pf->page_addr |= FIELD_GET(PFD_VIRTUAL_ADDR_LO, desc->dw2) <<
			PFD_VIRTUAL_ADDR_LO_SHIFT;

		pf_queue->head = (pf_queue->head + PF_MSG_LEN_DW) %
			PF_QUEUE_NUM_DW;
	} else {
		ret = -1;
	}
	spin_unlock_irq(&pf_queue->lock);

	return ret;
}

static bool pf_queue_full(struct pf_queue *pf_queue)
{
	lockdep_assert_held(&pf_queue->lock);

	return CIRC_SPACE(pf_queue->tail, pf_queue->head, PF_QUEUE_NUM_DW) <=
		PF_MSG_LEN_DW;
}

int xe_guc_pagefault_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct pf_queue *pf_queue;
	unsigned long flags;
	u32 asid;
	bool full;

	if (unlikely(len != PF_MSG_LEN_DW))
		return -EPROTO;

	asid = FIELD_GET(PFD_ASID, msg[1]);
	pf_queue = &gt->usm.pf_queue[asid % NUM_PF_QUEUE];

	spin_lock_irqsave(&pf_queue->lock, flags);
	full = pf_queue_full(pf_queue);
	if (!full) {
		memcpy(pf_queue->data + pf_queue->tail, msg, len * sizeof(u32));
		pf_queue->tail = (pf_queue->tail + len) % PF_QUEUE_NUM_DW;
		queue_work(gt->usm.pf_wq, &pf_queue->worker);
	} else {
		XE_WARN_ON("PF Queue full, shouldn't be possible");
	}
	spin_unlock_irqrestore(&pf_queue->lock, flags);

	return full ? -ENOSPC : 0;
}

static void pf_queue_work_func(struct work_struct *w)
{
	struct pf_queue *pf_queue = container_of(w, struct pf_queue, worker);
	struct xe_gt *gt = pf_queue->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_guc_pagefault_reply reply = {};
	struct pagefault pf = {};
	int ret;

	ret = get_pagefault(pf_queue, &pf);
	if (ret)
		return;

	ret = handle_pagefault(gt, &pf);
	if (unlikely(ret)) {
		print_pagefault(xe, &pf);
		pf.fault_unsuccessful = 1;
		drm_warn(&xe->drm, "Fault response: Unsuccessful %d\n", ret);
	}

	reply.dw0 = FIELD_PREP(PFR_VALID, 1) |
		FIELD_PREP(PFR_SUCCESS, pf.fault_unsuccessful) |
		FIELD_PREP(PFR_REPLY, PFR_ACCESS) |
		FIELD_PREP(PFR_DESC_TYPE, FAULT_RESPONSE_DESC) |
		FIELD_PREP(PFR_ASID, pf.asid);

	reply.dw1 = FIELD_PREP(PFR_VFID, pf.vfid) |
		FIELD_PREP(PFR_ENG_INSTANCE, pf.engine_instance) |
		FIELD_PREP(PFR_ENG_CLASS, pf.engine_class) |
		FIELD_PREP(PFR_PDATA, pf.pdata);

	send_pagefault_reply(&gt->uc.guc, &reply);
}

static void acc_queue_work_func(struct work_struct *w);

int xe_gt_pagefault_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int i;

	if (!xe->info.supports_usm)
		return 0;

	for (i = 0; i < NUM_PF_QUEUE; ++i) {
		gt->usm.pf_queue[i].gt = gt;
		spin_lock_init(&gt->usm.pf_queue[i].lock);
		INIT_WORK(&gt->usm.pf_queue[i].worker, pf_queue_work_func);
	}
	for (i = 0; i < NUM_ACC_QUEUE; ++i) {
		gt->usm.acc_queue[i].gt = gt;
		spin_lock_init(&gt->usm.acc_queue[i].lock);
		INIT_WORK(&gt->usm.acc_queue[i].worker, acc_queue_work_func);
	}

	gt->usm.pf_wq = alloc_workqueue("xe_gt_page_fault_work_queue",
					WQ_UNBOUND | WQ_HIGHPRI, NUM_PF_QUEUE);
	if (!gt->usm.pf_wq)
		return -ENOMEM;

	gt->usm.acc_wq = alloc_workqueue("xe_gt_access_counter_work_queue",
					 WQ_UNBOUND | WQ_HIGHPRI,
					 NUM_ACC_QUEUE);
	if (!gt->usm.acc_wq)
		return -ENOMEM;

	return 0;
}

void xe_gt_pagefault_reset(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int i;

	if (!xe->info.supports_usm)
		return;

	for (i = 0; i < NUM_PF_QUEUE; ++i) {
		spin_lock_irq(&gt->usm.pf_queue[i].lock);
		gt->usm.pf_queue[i].head = 0;
		gt->usm.pf_queue[i].tail = 0;
		spin_unlock_irq(&gt->usm.pf_queue[i].lock);
	}

	for (i = 0; i < NUM_ACC_QUEUE; ++i) {
		spin_lock(&gt->usm.acc_queue[i].lock);
		gt->usm.acc_queue[i].head = 0;
		gt->usm.acc_queue[i].tail = 0;
		spin_unlock(&gt->usm.acc_queue[i].lock);
	}
}

static int granularity_in_byte(int val)
{
	switch (val) {
	case 0:
		return SZ_128K;
	case 1:
		return SZ_2M;
	case 2:
		return SZ_16M;
	case 3:
		return SZ_64M;
	default:
		return 0;
	}
}

static int sub_granularity_in_byte(int val)
{
	return (granularity_in_byte(val) / 32);
}

static void print_acc(struct xe_device *xe, struct acc *acc)
{
	drm_warn(&xe->drm, "Access counter request:\n"
		 "\tType: %s\n"
		 "\tASID: %d\n"
		 "\tVFID: %d\n"
		 "\tEngine: %d:%d\n"
		 "\tGranularity: 0x%x KB Region/ %d KB sub-granularity\n"
		 "\tSub_Granularity Vector: 0x%08x\n"
		 "\tVA Range base: 0x%016llx\n",
		 acc->access_type ? "AC_NTFY_VAL" : "AC_TRIG_VAL",
		 acc->asid, acc->vfid, acc->engine_class, acc->engine_instance,
		 granularity_in_byte(acc->granularity) / SZ_1K,
		 sub_granularity_in_byte(acc->granularity) / SZ_1K,
		 acc->sub_granularity, acc->va_range_base);
}

static struct xe_vma *get_acc_vma(struct xe_vm *vm, struct acc *acc)
{
	u64 page_va = acc->va_range_base + (ffs(acc->sub_granularity) - 1) *
		sub_granularity_in_byte(acc->granularity);
	struct xe_vma lookup;

	lookup.start = page_va;
	lookup.end = lookup.start + SZ_4K - 1;

	return xe_vm_find_overlapping_vma(vm, &lookup);
}

static int handle_acc(struct xe_gt *gt, struct acc *acc)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_vm *vm;
	struct xe_vma *vma;
	struct xe_bo *bo;
	LIST_HEAD(objs);
	LIST_HEAD(dups);
	struct ttm_validate_buffer tv_bo, tv_vm;
	struct ww_acquire_ctx ww;
	int ret = 0;

	/* We only support ACC_TRIGGER at the moment */
	if (acc->access_type != ACC_TRIGGER)
		return -EINVAL;

	/* ASID to VM */
	mutex_lock(&xe->usm.lock);
	vm = xa_load(&xe->usm.asid_to_vm, acc->asid);
	if (vm)
		xe_vm_get(vm);
	mutex_unlock(&xe->usm.lock);
	if (!vm || !xe_vm_in_fault_mode(vm))
		return -EINVAL;

	down_read(&vm->lock);

	/* Lookup VMA */
	vma = get_acc_vma(vm, acc);
	if (!vma) {
		ret = -EINVAL;
		goto unlock_vm;
	}

	trace_xe_vma_acc(vma);

	/* Userptr can't be migrated, nothing to do */
	if (xe_vma_is_userptr(vma))
		goto unlock_vm;

	/* Lock VM and BOs dma-resv */
	bo = vma->bo;
	if (only_needs_bo_lock(bo)) {
		/* This path ensures the BO's LRU is updated */
		ret = xe_bo_lock(bo, &ww, xe->info.tile_count, false);
	} else {
		tv_vm.num_shared = xe->info.tile_count;
		tv_vm.bo = xe_vm_ttm_bo(vm);
		list_add(&tv_vm.head, &objs);
		tv_bo.bo = &bo->ttm;
		tv_bo.num_shared = xe->info.tile_count;
		list_add(&tv_bo.head, &objs);
		ret = ttm_eu_reserve_buffers(&ww, &objs, false, &dups);
	}
	if (ret)
		goto unlock_vm;

	/* Migrate to VRAM, move should invalidate the VMA first */
	ret = xe_bo_migrate(bo, XE_PL_VRAM0 + gt->info.vram_id);

	if (only_needs_bo_lock(bo))
		xe_bo_unlock(bo, &ww);
	else
		ttm_eu_backoff_reservation(&ww, &objs);
unlock_vm:
	up_read(&vm->lock);
	xe_vm_put(vm);

	return ret;
}

#define make_u64(hi__, low__)  ((u64)(hi__) << 32 | (u64)(low__))

static int get_acc(struct acc_queue *acc_queue, struct acc *acc)
{
	const struct xe_guc_acc_desc *desc;
	int ret = 0;

	spin_lock(&acc_queue->lock);
	if (acc_queue->head != acc_queue->tail) {
		desc = (const struct xe_guc_acc_desc *)
			(acc_queue->data + acc_queue->head);

		acc->granularity = FIELD_GET(ACC_GRANULARITY, desc->dw2);
		acc->sub_granularity = FIELD_GET(ACC_SUBG_HI, desc->dw1) << 31 |
			FIELD_GET(ACC_SUBG_LO, desc->dw0);
		acc->engine_class = FIELD_GET(ACC_ENG_CLASS, desc->dw1);
		acc->engine_instance = FIELD_GET(ACC_ENG_INSTANCE, desc->dw1);
		acc->asid =  FIELD_GET(ACC_ASID, desc->dw1);
		acc->vfid =  FIELD_GET(ACC_VFID, desc->dw2);
		acc->access_type = FIELD_GET(ACC_TYPE, desc->dw0);
		acc->va_range_base = make_u64(desc->dw3 & ACC_VIRTUAL_ADDR_RANGE_HI,
					      desc->dw2 & ACC_VIRTUAL_ADDR_RANGE_LO);
	} else {
		ret = -1;
	}
	spin_unlock(&acc_queue->lock);

	return ret;
}

static void acc_queue_work_func(struct work_struct *w)
{
	struct acc_queue *acc_queue = container_of(w, struct acc_queue, worker);
	struct xe_gt *gt = acc_queue->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct acc acc = {};
	int ret;

	ret = get_acc(acc_queue, &acc);
	if (ret)
		return;

	ret = handle_acc(gt, &acc);
	if (unlikely(ret)) {
		print_acc(xe, &acc);
		drm_warn(&xe->drm, "ACC: Unsuccessful %d\n", ret);
	}
}

#define ACC_MSG_LEN_DW	4

static bool acc_queue_full(struct acc_queue *acc_queue)
{
	lockdep_assert_held(&acc_queue->lock);

	return CIRC_SPACE(acc_queue->tail, acc_queue->head, ACC_QUEUE_NUM_DW) <=
		ACC_MSG_LEN_DW;
}

int xe_guc_access_counter_notify_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct acc_queue *acc_queue;
	u32 asid;
	bool full;

	if (unlikely(len != ACC_MSG_LEN_DW))
		return -EPROTO;

	asid = FIELD_GET(ACC_ASID, msg[1]);
	acc_queue = &gt->usm.acc_queue[asid % NUM_ACC_QUEUE];

	spin_lock(&acc_queue->lock);
	full = acc_queue_full(acc_queue);
	if (!full) {
		memcpy(acc_queue->data + acc_queue->tail, msg,
		       len * sizeof(u32));
		acc_queue->tail = (acc_queue->tail + len) % ACC_QUEUE_NUM_DW;
		queue_work(gt->usm.acc_wq, &acc_queue->worker);
	} else {
		drm_warn(&gt_to_xe(gt)->drm, "ACC Queue full, dropping ACC");
	}
	spin_unlock(&acc_queue->lock);

	return full ? -ENOSPC : 0;
}
