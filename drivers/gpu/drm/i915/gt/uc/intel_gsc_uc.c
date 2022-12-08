// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/types.h>

#include "gt/intel_gt.h"
#include "intel_gsc_uc.h"
#include "i915_drv.h"

static bool gsc_engine_supported(struct intel_gt *gt)
{
	intel_engine_mask_t mask;

	/*
	 * We reach here from i915_driver_early_probe for the primary GT before
	 * its engine mask is set, so we use the device info engine mask for it.
	 * For other GTs we expect the GT-specific mask to be set before we
	 * call this function.
	 */
	GEM_BUG_ON(!gt_is_root(gt) && !gt->info.engine_mask);

	if (gt_is_root(gt))
		mask = RUNTIME_INFO(gt->i915)->platform_engine_mask;
	else
		mask = gt->info.engine_mask;

	return __HAS_ENGINE(mask, GSC0);
}

void intel_gsc_uc_init_early(struct intel_gsc_uc *gsc)
{
	intel_uc_fw_init_early(&gsc->fw, INTEL_UC_FW_TYPE_GSC);

	/* we can arrive here from i915_driver_early_probe for primary
	 * GT with it being not fully setup hence check device info's
	 * engine mask
	 */
	if (!gsc_engine_supported(gsc_uc_to_gt(gsc))) {
		intel_uc_fw_change_status(&gsc->fw, INTEL_UC_FIRMWARE_NOT_SUPPORTED);
		return;
	}
}

int intel_gsc_uc_init(struct intel_gsc_uc *gsc)
{
	struct drm_i915_private *i915 = gsc_uc_to_gt(gsc)->i915;
	int err;

	err = intel_uc_fw_init(&gsc->fw);
	if (err)
		goto out;

	intel_uc_fw_change_status(&gsc->fw, INTEL_UC_FIRMWARE_LOADABLE);

	return 0;

out:
	i915_probe_error(i915, "failed with %d\n", err);
	return err;
}

void intel_gsc_uc_fini(struct intel_gsc_uc *gsc)
{
	if (!intel_uc_fw_is_loadable(&gsc->fw))
		return;

	intel_uc_fw_fini(&gsc->fw);
}
