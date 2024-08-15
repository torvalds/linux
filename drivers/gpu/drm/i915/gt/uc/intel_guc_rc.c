// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/string_helpers.h>

#include "intel_guc_rc.h"
#include "gt/intel_gt.h"
#include "i915_drv.h"

static bool __guc_rc_supported(struct intel_guc *guc)
{
	/* GuC RC is unavailable for pre-Gen12 */
	return guc->submission_supported &&
		GRAPHICS_VER(guc_to_gt(guc)->i915) >= 12;
}

static bool __guc_rc_selected(struct intel_guc *guc)
{
	if (!intel_guc_rc_is_supported(guc))
		return false;

	return guc->submission_selected;
}

void intel_guc_rc_init_early(struct intel_guc *guc)
{
	guc->rc_supported = __guc_rc_supported(guc);
	guc->rc_selected = __guc_rc_selected(guc);
}

static int guc_action_control_gucrc(struct intel_guc *guc, bool enable)
{
	u32 rc_mode = enable ? INTEL_GUCRC_FIRMWARE_CONTROL :
				INTEL_GUCRC_HOST_CONTROL;
	u32 action[] = {
		INTEL_GUC_ACTION_SETUP_PC_GUCRC,
		rc_mode
	};
	int ret;

	ret = intel_guc_send(guc, action, ARRAY_SIZE(action));
	ret = ret > 0 ? -EPROTO : ret;

	return ret;
}

static int __guc_rc_control(struct intel_guc *guc, bool enable)
{
	struct intel_gt *gt = guc_to_gt(guc);
	int ret;

	if (!intel_uc_uses_guc_rc(&gt->uc))
		return -EOPNOTSUPP;

	if (!intel_guc_is_ready(guc))
		return -EINVAL;

	ret = guc_action_control_gucrc(guc, enable);
	if (ret) {
		i915_probe_error(guc_to_gt(guc)->i915, "Failed to %s GuC RC (%pe)\n",
				 str_enable_disable(enable), ERR_PTR(ret));
		return ret;
	}

	drm_info(&gt->i915->drm, "GuC RC: %s\n",
		 str_enabled_disabled(enable));

	return 0;
}

int intel_guc_rc_enable(struct intel_guc *guc)
{
	return __guc_rc_control(guc, true);
}

int intel_guc_rc_disable(struct intel_guc *guc)
{
	return __guc_rc_control(guc, false);
}
