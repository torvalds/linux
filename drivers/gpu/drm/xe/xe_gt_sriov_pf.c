// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "regs/xe_guc_regs.h"
#include "regs/xe_regs.h"

#include "xe_gt.h"
#include "xe_gt_sriov_pf.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_migration.h"
#include "xe_gt_sriov_pf_service.h"
#include "xe_gt_sriov_printk.h"
#include "xe_guc_submit.h"
#include "xe_mmio.h"
#include "xe_pm.h"

static void pf_worker_restart_func(struct work_struct *w);

/*
 * VF's metadata is maintained in the flexible array where:
 *   - entry [0] contains metadata for the PF (only if applicable),
 *   - entries [1..n] contain metadata for VF1..VFn::
 *
 *       <--------------------------- 1 + total_vfs ----------->
 *      +-------+-------+-------+-----------------------+-------+
 *      |   0   |   1   |   2   |                       |   n   |
 *      +-------+-------+-------+-----------------------+-------+
 *      |  PF   |  VF1  |  VF2  |      ...     ...      |  VFn  |
 *      +-------+-------+-------+-----------------------+-------+
 */
static int pf_alloc_metadata(struct xe_gt *gt)
{
	unsigned int num_vfs = xe_gt_sriov_pf_get_totalvfs(gt);

	gt->sriov.pf.vfs = drmm_kcalloc(&gt_to_xe(gt)->drm, 1 + num_vfs,
					sizeof(*gt->sriov.pf.vfs), GFP_KERNEL);
	if (!gt->sriov.pf.vfs)
		return -ENOMEM;

	return 0;
}

static void pf_init_workers(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	INIT_WORK(&gt->sriov.pf.workers.restart, pf_worker_restart_func);
}

static void pf_fini_workers(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	if (disable_work_sync(&gt->sriov.pf.workers.restart)) {
		xe_gt_sriov_dbg_verbose(gt, "pending restart disabled!\n");
		/* release an rpm reference taken on the worker's behalf */
		xe_pm_runtime_put(gt_to_xe(gt));
	}
}

/**
 * xe_gt_sriov_pf_init_early - Prepare SR-IOV PF data structures on PF.
 * @gt: the &xe_gt to initialize
 *
 * Early initialization of the PF data.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_init_early(struct xe_gt *gt)
{
	int err;

	err = pf_alloc_metadata(gt);
	if (err)
		return err;

	err = xe_gt_sriov_pf_service_init(gt);
	if (err)
		return err;

	err = xe_gt_sriov_pf_control_init(gt);
	if (err)
		return err;

	pf_init_workers(gt);

	return 0;
}

static void pf_fini_action(void *arg)
{
	struct xe_gt *gt = arg;

	pf_fini_workers(gt);
}

static int pf_init_late(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	xe_gt_assert(gt, IS_SRIOV_PF(xe));
	return devm_add_action_or_reset(xe->drm.dev, pf_fini_action, gt);
}

/**
 * xe_gt_sriov_pf_init - Prepare SR-IOV PF data structures on PF.
 * @gt: the &xe_gt to initialize
 *
 * Late one-time initialization of the PF data.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_init(struct xe_gt *gt)
{
	int err;

	err = xe_gt_sriov_pf_config_init(gt);
	if (err)
		return err;

	err = xe_gt_sriov_pf_migration_init(gt);
	if (err)
		return err;

	err = pf_init_late(gt);
	if (err)
		return err;

	return 0;
}

static bool pf_needs_enable_ggtt_guest_update(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) == 1200;
}

static void pf_enable_ggtt_guest_update(struct xe_gt *gt)
{
	xe_mmio_write32(&gt->mmio, VIRTUAL_CTRL_REG, GUEST_GTT_UPDATE_EN);
}

/**
 * xe_gt_sriov_pf_init_hw - Initialize SR-IOV hardware support.
 * @gt: the &xe_gt to initialize
 *
 * On some platforms the PF must explicitly enable VF's access to the GGTT.
 */
void xe_gt_sriov_pf_init_hw(struct xe_gt *gt)
{
	if (pf_needs_enable_ggtt_guest_update(gt_to_xe(gt)))
		pf_enable_ggtt_guest_update(gt);

	xe_gt_sriov_pf_service_update(gt);
}

static u32 pf_get_vf_regs_stride(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) > 1200 ? 0x400 : 0x1000;
}

static struct xe_reg xe_reg_vf_to_pf(struct xe_reg vf_reg, unsigned int vfid, u32 stride)
{
	struct xe_reg pf_reg = vf_reg;

	pf_reg.vf = 0;
	pf_reg.addr += stride * vfid;

	return pf_reg;
}

static void pf_clear_vf_scratch_regs(struct xe_gt *gt, unsigned int vfid)
{
	u32 stride = pf_get_vf_regs_stride(gt_to_xe(gt));
	struct xe_reg scratch;
	int n, count;

	if (xe_gt_is_media_type(gt)) {
		count = MED_VF_SW_FLAG_COUNT;
		for (n = 0; n < count; n++) {
			scratch = xe_reg_vf_to_pf(MED_VF_SW_FLAG(n), vfid, stride);
			xe_mmio_write32(&gt->mmio, scratch, 0);
		}
	} else {
		count = VF_SW_FLAG_COUNT;
		for (n = 0; n < count; n++) {
			scratch = xe_reg_vf_to_pf(VF_SW_FLAG(n), vfid, stride);
			xe_mmio_write32(&gt->mmio, scratch, 0);
		}
	}
}

/**
 * xe_gt_sriov_pf_sanitize_hw() - Reset hardware state related to a VF.
 * @gt: the &xe_gt
 * @vfid: the VF identifier
 *
 * This function can only be called on PF.
 */
void xe_gt_sriov_pf_sanitize_hw(struct xe_gt *gt, unsigned int vfid)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	pf_clear_vf_scratch_regs(gt, vfid);
}

static void pf_cancel_restart(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));

	if (cancel_work_sync(&gt->sriov.pf.workers.restart)) {
		xe_gt_sriov_dbg_verbose(gt, "pending restart canceled!\n");
		/* release an rpm reference taken on the worker's behalf */
		xe_pm_runtime_put(gt_to_xe(gt));
	}
}

/**
 * xe_gt_sriov_pf_stop_prepare() - Prepare to stop SR-IOV support.
 * @gt: the &xe_gt
 *
 * This function can only be called on the PF.
 */
void xe_gt_sriov_pf_stop_prepare(struct xe_gt *gt)
{
	pf_cancel_restart(gt);
}

static void pf_restart(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	xe_gt_assert(gt, !xe_pm_runtime_suspended(xe));

	xe_gt_sriov_pf_config_restart(gt);
	xe_gt_sriov_pf_control_restart(gt);

	/* release an rpm reference taken on our behalf */
	xe_pm_runtime_put(xe);

	xe_gt_sriov_dbg(gt, "restart completed\n");
}

static void pf_worker_restart_func(struct work_struct *w)
{
	struct xe_gt *gt = container_of(w, typeof(*gt), sriov.pf.workers.restart);

	pf_restart(gt);
}

static void pf_queue_restart(struct xe_gt *gt)
{
	struct xe_device *xe = gt_to_xe(gt);

	xe_gt_assert(gt, IS_SRIOV_PF(xe));

	/* take an rpm reference on behalf of the worker */
	xe_pm_runtime_get_noresume(xe);

	if (!queue_work(xe->sriov.wq, &gt->sriov.pf.workers.restart)) {
		xe_gt_sriov_dbg(gt, "restart already in queue!\n");
		xe_pm_runtime_put(xe);
	}
}

/**
 * xe_gt_sriov_pf_restart - Restart SR-IOV support after a GT reset.
 * @gt: the &xe_gt
 *
 * This function can only be called on PF.
 */
void xe_gt_sriov_pf_restart(struct xe_gt *gt)
{
	pf_queue_restart(gt);
}

static void pf_flush_restart(struct xe_gt *gt)
{
	xe_gt_assert(gt, IS_SRIOV_PF(gt_to_xe(gt)));
	flush_work(&gt->sriov.pf.workers.restart);
}

/**
 * xe_gt_sriov_pf_wait_ready() - Wait until per-GT PF SR-IOV support is ready.
 * @gt: the &xe_gt
 *
 * This function can only be called on PF.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int xe_gt_sriov_pf_wait_ready(struct xe_gt *gt)
{
	/* don't wait if there is another ongoing reset */
	if (xe_guc_read_stopped(&gt->uc.guc))
		return -EBUSY;

	pf_flush_restart(gt);
	return 0;
}
