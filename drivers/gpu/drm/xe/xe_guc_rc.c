// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <drm/drm_print.h>

#include "abi/guc_actions_slpc_abi.h"
#include "xe_device.h"
#include "xe_force_wake.h"
#include "xe_gt.h"
#include "xe_gt_idle.h"
#include "xe_gt_printk.h"
#include "xe_guc.h"
#include "xe_guc_ct.h"
#include "xe_guc_pc.h"
#include "xe_guc_rc.h"
#include "xe_pm.h"

/**
 * DOC: GuC RC (Render C-states)
 *
 * GuC handles the GT transition to deeper C-states in conjunction with Pcode.
 * GuC RC can be enabled independently of the frequency component in SLPC,
 * which is also controlled by GuC.
 *
 * This file will contain all H2G related logic for handling Render C-states.
 * There are some calls to xe_gt_idle, where we enable host C6 when GuC RC is
 * skipped. GuC RC is mostly independent of xe_guc_pc with the exception of
 * functions that override the mode for which we have to rely on the SLPC H2G
 * calls.
 */

static int guc_action_setup_gucrc(struct xe_guc *guc, u32 control)
{
	u32 action[] = {
		GUC_ACTION_HOST2GUC_SETUP_PC_GUCRC,
		control,
	};
	int ret;

	ret = xe_guc_ct_send(&guc->ct, action, ARRAY_SIZE(action), 0, 0);
	if (ret && !(xe_device_wedged(guc_to_xe(guc)) && ret == -ECANCELED))
		xe_gt_err(guc_to_gt(guc),
			  "GuC RC setup %s(%u) failed (%pe)\n",
			   control == GUCRC_HOST_CONTROL ? "HOST_CONTROL" :
			   control == GUCRC_FIRMWARE_CONTROL ? "FIRMWARE_CONTROL" :
			   "UNKNOWN", control, ERR_PTR(ret));
	return ret;
}

/**
 * xe_guc_rc_disable() - Disable GuC RC
 * @guc: Xe GuC instance
 *
 * Disables GuC RC by taking control of RC6 back from GuC.
 */
void xe_guc_rc_disable(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);

	if (!xe->info.skip_guc_pc && xe->info.platform != XE_PVC)
		if (guc_action_setup_gucrc(guc, GUCRC_HOST_CONTROL))
			return;

	xe_gt_WARN_ON(gt, xe_gt_idle_disable_c6(gt));
}

static void xe_guc_rc_fini_hw(void *arg)
{
	struct xe_guc *guc = arg;
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);

	if (xe_device_wedged(xe))
		return;

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
	xe_guc_rc_disable(guc);
}

/**
 * xe_guc_rc_init() - Init GuC RC
 * @guc: Xe GuC instance
 *
 * Add callback action for GuC RC
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_guc_rc_init(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);

	xe_gt_assert(gt, xe_device_uc_enabled(xe));

	return devm_add_action_or_reset(xe->drm.dev, xe_guc_rc_fini_hw, guc);
}

/**
 * xe_guc_rc_enable() - Enable GuC RC feature if applicable
 * @guc: Xe GuC instance
 *
 * Enables GuC RC feature.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_guc_rc_enable(struct xe_guc *guc)
{
	struct xe_device *xe = guc_to_xe(guc);
	struct xe_gt *gt = guc_to_gt(guc);

	xe_gt_assert(gt, xe_device_uc_enabled(xe));

	CLASS(xe_force_wake, fw_ref)(gt_to_fw(gt), XE_FW_GT);
	if (!xe_force_wake_ref_has_domain(fw_ref.domains, XE_FW_GT))
		return -ETIMEDOUT;

	if (xe->info.platform == XE_PVC) {
		xe_guc_rc_disable(guc);
		return 0;
	}

	if (xe->info.skip_guc_pc) {
		xe_gt_idle_enable_c6(gt);
		return 0;
	}

	return guc_action_setup_gucrc(guc, GUCRC_FIRMWARE_CONTROL);
}
