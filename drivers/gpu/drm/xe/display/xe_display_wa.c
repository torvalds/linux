// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "intel_display_core.h"
#include "intel_display_wa.h"
#include "xe_device.h"
#include "xe_wa.h"

#include <generated/xe_wa_oob.h>

bool intel_display_needs_wa_16023588340(struct intel_display *display)
{
	struct xe_device *xe = to_xe_device(display->drm);
	struct xe_gt *wa_gt = xe_root_mmio_gt(xe);

	return wa_gt && XE_GT_WA(wa_gt, 16023588340);
}
