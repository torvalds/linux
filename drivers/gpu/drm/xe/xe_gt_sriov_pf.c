// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023-2024 Intel Corporation
 */

#include <drm/drm_managed.h>

#include "regs/xe_regs.h"

#include "xe_gt_sriov_pf.h"
#include "xe_gt_sriov_pf_config.h"
#include "xe_gt_sriov_pf_control.h"
#include "xe_gt_sriov_pf_helpers.h"
#include "xe_gt_sriov_pf_service.h"
#include "xe_mmio.h"

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

	return 0;
}

static bool pf_needs_enable_ggtt_guest_update(struct xe_device *xe)
{
	return GRAPHICS_VERx100(xe) == 1200;
}

static void pf_enable_ggtt_guest_update(struct xe_gt *gt)
{
	xe_mmio_write32(gt, VIRTUAL_CTRL_REG, GUEST_GTT_UPDATE_EN);
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

/**
 * xe_gt_sriov_pf_restart - Restart SR-IOV support after a GT reset.
 * @gt: the &xe_gt
 *
 * This function can only be called on PF.
 */
void xe_gt_sriov_pf_restart(struct xe_gt *gt)
{
	xe_gt_sriov_pf_config_restart(gt);
	xe_gt_sriov_pf_control_restart(gt);
}
