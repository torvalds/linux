// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2024 Intel Corporation.
 */

#include "xe_pxp_submit.h"

#include <uapi/drm/xe_drm.h>

#include "xe_device_types.h"
#include "xe_bo.h"
#include "xe_exec_queue.h"
#include "xe_gsc_submit.h"
#include "xe_gt.h"
#include "xe_pxp_types.h"
#include "xe_vm.h"

/*
 * The VCS is used for kernel-owned GGTT submissions to issue key termination.
 * Terminations are serialized, so we only need a single queue and a single
 * batch.
 */
static int allocate_vcs_execution_resources(struct xe_pxp *pxp)
{
	struct xe_gt *gt = pxp->gt;
	struct xe_device *xe = pxp->xe;
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_hw_engine *hwe;
	struct xe_exec_queue *q;
	struct xe_bo *bo;
	int err;

	hwe = xe_gt_hw_engine(gt, XE_ENGINE_CLASS_VIDEO_DECODE, 0, true);
	if (!hwe)
		return -ENODEV;

	q = xe_exec_queue_create(xe, NULL, BIT(hwe->logical_instance), 1, hwe,
				 EXEC_QUEUE_FLAG_KERNEL | EXEC_QUEUE_FLAG_PERMANENT, 0);
	if (IS_ERR(q))
		return PTR_ERR(q);

	/*
	 * Each termination is 16 DWORDS, so 4K is enough to contain a
	 * termination for each sessions.
	 */
	bo = xe_bo_create_pin_map(xe, tile, 0, SZ_4K, ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM | XE_BO_FLAG_PINNED | XE_BO_FLAG_GGTT);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto out_queue;
	}

	pxp->vcs_exec.q = q;
	pxp->vcs_exec.bo = bo;

	return 0;

out_queue:
	xe_exec_queue_put(q);
	return err;
}

static void destroy_vcs_execution_resources(struct xe_pxp *pxp)
{
	if (pxp->vcs_exec.bo)
		xe_bo_unpin_map_no_vm(pxp->vcs_exec.bo);

	if (pxp->vcs_exec.q)
		xe_exec_queue_put(pxp->vcs_exec.q);
}

#define PXP_BB_SIZE		XE_PAGE_SIZE
static int allocate_gsc_client_resources(struct xe_gt *gt,
					 struct xe_pxp_gsc_client_resources *gsc_res,
					 size_t inout_size)
{
	struct xe_tile *tile = gt_to_tile(gt);
	struct xe_device *xe = tile_to_xe(tile);
	struct xe_hw_engine *hwe;
	struct xe_vm *vm;
	struct xe_bo *bo;
	struct xe_exec_queue *q;
	struct dma_fence *fence;
	long timeout;
	int err = 0;

	hwe = xe_gt_hw_engine(gt, XE_ENGINE_CLASS_OTHER, 0, true);

	/* we shouldn't reach here if the GSC engine is not available */
	xe_assert(xe, hwe);

	/* PXP instructions must be issued from PPGTT */
	vm = xe_vm_create(xe, XE_VM_FLAG_GSC);
	if (IS_ERR(vm))
		return PTR_ERR(vm);

	/* We allocate a single object for the batch and the in/out memory */
	xe_vm_lock(vm, false);
	bo = xe_bo_create_pin_map(xe, tile, vm, PXP_BB_SIZE + inout_size * 2,
				  ttm_bo_type_kernel,
				  XE_BO_FLAG_SYSTEM | XE_BO_FLAG_PINNED | XE_BO_FLAG_NEEDS_UC);
	xe_vm_unlock(vm);
	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto vm_out;
	}

	fence = xe_vm_bind_kernel_bo(vm, bo, NULL, 0, XE_CACHE_WB);
	if (IS_ERR(fence)) {
		err = PTR_ERR(fence);
		goto bo_out;
	}

	timeout = dma_fence_wait_timeout(fence, false, HZ);
	dma_fence_put(fence);
	if (timeout <= 0) {
		err = timeout ?: -ETIME;
		goto bo_out;
	}

	q = xe_exec_queue_create(xe, vm, BIT(hwe->logical_instance), 1, hwe,
				 EXEC_QUEUE_FLAG_KERNEL |
				 EXEC_QUEUE_FLAG_PERMANENT, 0);
	if (IS_ERR(q)) {
		err = PTR_ERR(q);
		goto bo_out;
	}

	gsc_res->vm = vm;
	gsc_res->bo = bo;
	gsc_res->inout_size = inout_size;
	gsc_res->batch = IOSYS_MAP_INIT_OFFSET(&bo->vmap, 0);
	gsc_res->msg_in = IOSYS_MAP_INIT_OFFSET(&bo->vmap, PXP_BB_SIZE);
	gsc_res->msg_out = IOSYS_MAP_INIT_OFFSET(&bo->vmap, PXP_BB_SIZE + inout_size);
	gsc_res->q = q;

	/* initialize host-session-handle (for all Xe-to-gsc-firmware PXP cmds) */
	gsc_res->host_session_handle = xe_gsc_create_host_session_id();

	return 0;

bo_out:
	xe_bo_unpin_map_no_vm(bo);
vm_out:
	xe_vm_close_and_put(vm);

	return err;
}

static void destroy_gsc_client_resources(struct xe_pxp_gsc_client_resources *gsc_res)
{
	if (!gsc_res->q)
		return;

	xe_exec_queue_put(gsc_res->q);
	xe_bo_unpin_map_no_vm(gsc_res->bo);
	xe_vm_close_and_put(gsc_res->vm);
}

/**
 * xe_pxp_allocate_execution_resources - Allocate PXP submission objects
 * @pxp: the xe_pxp structure
 *
 * Allocates exec_queues objects for VCS and GSCCS submission. The GSCCS
 * submissions are done via PPGTT, so this function allocates a VM for it and
 * maps the object into it.
 *
 * Returns 0 if the allocation and mapping is successful, an errno value
 * otherwise.
 */
int xe_pxp_allocate_execution_resources(struct xe_pxp *pxp)
{
	int err;

	err = allocate_vcs_execution_resources(pxp);
	if (err)
		return err;

	/*
	 * PXP commands can require a lot of BO space (see PXP_MAX_PACKET_SIZE),
	 * but we currently only support a subset of commands that are small
	 * (< 20 dwords), so a single page is enough for now.
	 */
	err = allocate_gsc_client_resources(pxp->gt, &pxp->gsc_res, XE_PAGE_SIZE);
	if (err)
		goto destroy_vcs_context;

	return 0;

destroy_vcs_context:
	destroy_vcs_execution_resources(pxp);
	return err;
}

void xe_pxp_destroy_execution_resources(struct xe_pxp *pxp)
{
	destroy_gsc_client_resources(&pxp->gsc_res);
	destroy_vcs_execution_resources(pxp);
}
