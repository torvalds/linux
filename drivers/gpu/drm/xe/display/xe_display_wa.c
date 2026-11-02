// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include <drm/intel/display_parent_interface.h>

#include "xe_device.h"
#include "xe_display_wa.h"
#include "xe_wa.h"

#include <generated/xe_wa_oob.h>

static bool intel_display_needs_wa_16023588340(struct drm_device *drm)
{
	struct xe_device *xe = to_xe_device(drm);
	struct xe_gt *wa_gt = xe_root_mmio_gt(xe);

	return wa_gt && XE_GT_WA(wa_gt, 16023588340);
}

const struct intel_display_wa_interface xe_display_wa_interface = {
	.wa_16023588340 = intel_display_needs_wa_16023588340,
};
