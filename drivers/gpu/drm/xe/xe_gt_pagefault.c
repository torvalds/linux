// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_pagefault.h"

#include <linux/bitfield.h>
#include <linux/circ_buf.h>

#include <drm/drm_exec.h>
#include <drm/drm_managed.h>
#include <drm/ttm/ttm_execbuf_util.h>

#include "abi/guc_actions_abi.h"
#include "xe_bo.h"
#include "xe_gt.h"
#include "xe_gt_tlb_invalidation.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_migrate.h"
#include "xe_trace_bo.h"
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
	bool trva_fault;
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

static bool access_is_atomic(enum access_type access_type)
{
	return access_type == ACCESS_TYPE_ATOMIC;
}

static bool vma_is_valid(struct xe_tile *tile, struct xe_vma *vma)
{
	return BIT(tile->id) & vma->tile_present &&
		!(BIT(tile->id) & vma->tile_invalidated);
}

static bool vma_matches(struct xe_vma *vma, u64 page_addr)
{
	if (page_addr > xe_vma_end(vma) - 1 ||
	    page_addr + SZ_4K - 1 < xe_vma_start(vma))
		return false;

	return true;
}

static struct xe_vma *lookup_vma(struct xe_vm *vm, u64 page_addr)
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

static int xe_pf_begin(struct drm_exec *exec, struct xe_vma *vma,
		       bool atomic, unsigned int id)
{
	struct xe_bo *bo = xe_vma_bo(vma);
	struct xe_vm *vm = xe_vma_vm(vma);
	int err;

	err = xe_vm_lock_vma(exec, vma);
	if (err)
		return err;

	if (atomic && IS_DGFX(vm->xe)) {
		if (xe_vma_is_userptr(vma)) {
			err = -EACCES;
			return err;
		}

		/* Migrate to VRAM, move should invalidate the VMA first */
		err = xe_bo_migrate(bo, XE_PL_VRAM0 + id);
		if (err)
			return err;
	} else if (bo) {
		/* Create backing store if needed */
		err = xe_bo_validate(bo, vm, true);
		if (err)
			return err;
	}

	return 0;
}

static int handle_vma_pagefault(struct xe_tile *tile, struct pagefault *pf,
				struct xe_vma *vma)
{
	struct xe_vm *vm = xe_vma_vm(vma);
	struct drm_exec exec;
	struct dma_fence *fence;
	ktime_t end = 0;
	int err;
	bool atomic;

	trace_xe_vma_pagefault(vma);
	atomic = access_is_atomic(pf->access_type);

	/* Check if VMA is valid */
	if (vma_is_valid(tile, vma) && !atomic)
		return 0;

retry_userptr:
	if (xe_vma_is_userptr(vma) &&
	    xe_vma_userptr_check_repin(to_userptr_vma(vma))) {
		struct xe_userptr_vma *uvma = to_userptr_vma(vma);

		err = xe_vma_userptr_pin_pages(uvma);
		if (err)
			return err;
	}

	/* Lock VM and BOs dma-resv */
	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		err = xe_pf_begin(&exec, vma, atomic, tile->id);
		drm_exec_retry_on_contention(&exec);
		if (xe_vm_validate_should_retry(&exec, err, &end))
			err = -EAGAIN;
		if (err)
			goto unlock_dma_resv;

		/* Bind VMA only to the GT that has faulted */
		trace_xe_vma_pf_bind(vma);
		fence = xe_vma_rebind(vm, vma, BIT(tile->id));
		if (IS_ERR(fence)) {
			err = PTR_ERR(fence);
			if (xe_vm_validate_should_retry(&exec, err, &end))
				err = -EAGAIN;
			goto unlock_dma_resv;
		}
	}

	dma_fence_wait(fence, false);
	dma_fence_put(fence);
	vma->tile_invalidated &= ~BIT(tile->id);

unlock_dma_resv:
	drm_exec_fini(&exec);
	if (err == -EAGAIN)
		goto retry_userptr;

	return err;
}

static int handle_pagefault(struct xe_gt *gt, struct pagefault *pf)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_vm *vm;
	struct xe_vma *vma = NULL;
	int err;

	/* SW isn't expected to handle TRTT faults */
	if (pf->trva_fault)
		return -EFAULT;

	/* ASID to VM */
	mutex_lock(&xe->usm.lock);
	vm = xa_load(&xe->usm.asid_to_vm, pf->asid);
	if (vm && xe_vm_in_fault_mode(vm))
		xe_vm_get(vm);
	else
		vm = NULL;
	mutex_unlock(&xe->usm.lock);
	if (!vm)
		return -EINVAL;

	/*
	 * TODO: Change to read lock? Using write lock for simplicity.
	 */
	down_write(&vm->lock);
	vma = lookup_vma(vm, pf->page_addr);
	if (!vma) {
		err = -EINVAL;
		goto unlock_vm;
	}

	err = handle_vma_pagefault(tile, pf, vma);

unlock_vm:
	if (!err)
		vm->usm.last_fault_vma = vma;
	up_write(&vm->lock);
	xe_vm_put(vm);

	return err;
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
	drm_dbg(&xe->drm, "\n\tASID: %d\n"
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

static bool get_pagefault(struct pf_queue *pf_queue, struct pagefault *pf)
{
	const struct xe_guc_pagefault_desc *desc;
	bool ret = false;

	spin_lock_irq(&pf_queue->lock);
	if (pf_queue->tail != pf_queue->head) {
		desc = (const struct xe_guc_pagefault_desc *)
			(pf_queue->data + pf_queue->tail);

		pf->fault_level = FIELD_GET(PFD_FAULT_LEVEL, desc->dw0);
		pf->trva_fault = FIELD_GET(XE2_PFD_TRVA_FAULT, desc->dw0);
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

		pf_queue->tail = (pf_queue->tail + PF_MSG_LEN_DW) %
			pf_queue->num_dw;
		ret = true;
	}
	spin_unlock_irq(&pf_queue->lock);

	return ret;
}

static bool pf_queue_full(struct pf_queue *pf_queue)
{
	lockdep_assert_held(&pf_queue->lock);

	return CIRC_SPACE(pf_queue->head, pf_queue->tail,
			  pf_queue->num_dw) <=
		PF_MSG_LEN_DW;
}

int xe_guc_pagefault_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct xe_device *xe = gt_to_xe(gt);
	struct pf_queue *pf_queue;
	unsigned long flags;
	u32 asid;
	bool full;

	if (unlikely(len != PF_MSG_LEN_DW))
		return -EPROTO;

	asid = FIELD_GET(PFD_ASID, msg[1]);
	pf_queue = gt->usm.pf_queue + (asid % NUM_PF_QUEUE);

	/*
	 * The below logic doesn't work unless PF_QUEUE_NUM_DW % PF_MSG_LEN_DW == 0
	 */
	xe_gt_assert(gt, !(pf_queue->num_dw % PF_MSG_LEN_DW));

	spin_lock_irqsave(&pf_queue->lock, flags);
	full = pf_queue_full(pf_queue);
	if (!full) {
		memcpy(pf_queue->data + pf_queue->head, msg, len * sizeof(u32));
		pf_queue->head = (pf_queue->head + len) %
			pf_queue->num_dw;
		queue_work(gt->usm.pf_wq, &pf_queue->worker);
	} else {
		drm_warn(&xe->drm, "PF Queue full, shouldn't be possible");
	}
	spin_unlock_irqrestore(&pf_queue->lock, flags);

	return full ? -ENOSPC : 0;
}

#define USM_QUEUE_MAX_RUNTIME_MS	20

static void pf_queue_work_func(struct work_struct *w)
{
	struct pf_queue *pf_queue = container_of(w, struct pf_queue, worker);
	struct xe_gt *gt = pf_queue->gt;
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_guc_pagefault_reply reply = {};
	struct pagefault pf = {};
	unsigned long threshold;
	int ret;

	threshold = jiffies + msecs_to_jiffies(USM_QUEUE_MAX_RUNTIME_MS);

	while (get_pagefault(pf_queue, &pf)) {
		ret = handle_pagefault(gt, &pf);
		if (unlikely(ret)) {
			print_pagefault(xe, &pf);
			pf.fault_unsuccessful = 1;
			drm_dbg(&xe->drm, "Fault response: Unsuccessful %d\n", ret);
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

		if (time_after(jiffies, threshold) &&
		    pf_queue->tail != pf_queue->head) {
			queue_work(gt->usm.pf_wq, w);
			break;
		}
	}
}

static void acc_queue_work_func(struct work_struct *w);

static void pagefault_fini(void *arg)
{
	struct xe_gt *gt = arg;
	struct xe_device *xe = gt_to_xe(gt);

	if (!xe->info.has_usm)
		return;

	destroy_workqueue(gt->usm.acc_wq);
	destroy_workqueue(gt->usm.pf_wq);
}

static int xe_alloc_pf_queue(struct xe_gt *gt, struct pf_queue *pf_queue)
{
	struct xe_device *xe = gt_to_xe(gt);
	xe_dss_mask_t all_dss;
	int num_dss, num_eus;

	bitmap_or(all_dss, gt->fuse_topo.g_dss_mask, gt->fuse_topo.c_dss_mask,
		  XE_MAX_DSS_FUSE_BITS);

	num_dss = bitmap_weight(all_dss, XE_MAX_DSS_FUSE_BITS);
	num_eus = bitmap_weight(gt->fuse_topo.eu_mask_per_dss,
				XE_MAX_EU_FUSE_BITS) * num_dss;

	/* user can issue separate page faults per EU and per CS */
	pf_queue->num_dw =
		(num_eus + XE_NUM_HW_ENGINES) * PF_MSG_LEN_DW;

	pf_queue->gt = gt;
	pf_queue->data = devm_kcalloc(xe->drm.dev, pf_queue->num_dw,
				      sizeof(u32), GFP_KERNEL);
	if (!pf_queue->data)
		return -ENOMEM;

	spin_lock_init(&pf_queue->lock);
	INIT_WORK(&pf_queue->worker, pf_queue_work_func);

	return 0;
}

int xe_gt_pagefault_init(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int i, ret = 0;

	if (!xe->info.has_usm)
		return 0;

	for (i = 0; i < NUM_PF_QUEUE; ++i) {
		ret = xe_alloc_pf_queue(gt, &gt->usm.pf_queue[i]);
		if (ret)
			return ret;
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
	if (!gt->usm.acc_wq) {
		destroy_workqueue(gt->usm.pf_wq);
		return -ENOMEM;
	}

	return devm_add_action_or_reset(xe->drm.dev, pagefault_fini, gt);
}

void xe_gt_pagefault_reset(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);
	int i;

	if (!xe->info.has_usm)
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

	return xe_vm_find_overlapping_vma(vm, page_va, SZ_4K);
}

static int handle_acc(struct xe_gt *gt, struct acc *acc)
{
	struct xe_device *xe = gt_to_xe(gt);
	struct xe_tile *tile = gt_to_tile(gt);
	struct drm_exec exec;
	struct xe_vm *vm;
	struct xe_vma *vma;
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

	/* Userptr or null can't be migrated, nothing to do */
	if (xe_vma_has_no_bo(vma))
		goto unlock_vm;

	/* Lock VM and BOs dma-resv */
	drm_exec_init(&exec, 0, 0);
	drm_exec_until_all_locked(&exec) {
		ret = xe_pf_begin(&exec, vma, true, tile->id);
		drm_exec_retry_on_contention(&exec);
		if (ret)
			break;
	}

	drm_exec_fini(&exec);
unlock_vm:
	up_read(&vm->lock);
	xe_vm_put(vm);

	return ret;
}

#define make_u64(hi__, low__)  ((u64)(hi__) << 32 | (u64)(low__))

#define ACC_MSG_LEN_DW        4

static bool get_acc(struct acc_queue *acc_queue, struct acc *acc)
{
	const struct xe_guc_acc_desc *desc;
	bool ret = false;

	spin_lock(&acc_queue->lock);
	if (acc_queue->tail != acc_queue->head) {
		desc = (const struct xe_guc_acc_desc *)
			(acc_queue->data + acc_queue->tail);

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

		acc_queue->tail = (acc_queue->tail + ACC_MSG_LEN_DW) %
				  ACC_QUEUE_NUM_DW;
		ret = true;
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
	unsigned long threshold;
	int ret;

	threshold = jiffies + msecs_to_jiffies(USM_QUEUE_MAX_RUNTIME_MS);

	while (get_acc(acc_queue, &acc)) {
		ret = handle_acc(gt, &acc);
		if (unlikely(ret)) {
			print_acc(xe, &acc);
			drm_warn(&xe->drm, "ACC: Unsuccessful %d\n", ret);
		}

		if (time_after(jiffies, threshold) &&
		    acc_queue->tail != acc_queue->head) {
			queue_work(gt->usm.acc_wq, w);
			break;
		}
	}
}

static bool acc_queue_full(struct acc_queue *acc_queue)
{
	lockdep_assert_held(&acc_queue->lock);

	return CIRC_SPACE(acc_queue->head, acc_queue->tail, ACC_QUEUE_NUM_DW) <=
		ACC_MSG_LEN_DW;
}

int xe_guc_access_counter_notify_handler(struct xe_guc *guc, u32 *msg, u32 len)
{
	struct xe_gt *gt = guc_to_gt(guc);
	struct acc_queue *acc_queue;
	u32 asid;
	bool full;

	/*
	 * The below logic doesn't work unless ACC_QUEUE_NUM_DW % ACC_MSG_LEN_DW == 0
	 */
	BUILD_BUG_ON(ACC_QUEUE_NUM_DW % ACC_MSG_LEN_DW);

	if (unlikely(len != ACC_MSG_LEN_DW))
		return -EPROTO;

	asid = FIELD_GET(ACC_ASID, msg[1]);
	acc_queue = &gt->usm.acc_queue[asid % NUM_ACC_QUEUE];

	spin_lock(&acc_queue->lock);
	full = acc_queue_full(acc_queue);
	if (!full) {
		memcpy(acc_queue->data + acc_queue->head, msg,
		       len * sizeof(u32));
		acc_queue->head = (acc_queue->head + len) % ACC_QUEUE_NUM_DW;
		queue_work(gt->usm.acc_wq, &acc_queue->worker);
	} else {
		drm_warn(&gt_to_xe(gt)->drm, "ACC Queue full, dropping ACC");
	}
	spin_unlock(&acc_queue->lock);

	return full ? -ENOSPC : 0;
}
