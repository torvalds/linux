// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "xe_device.h"
#include "intel_display_types.h"
#include "intel_tdf.h"

void intel_td_flush(struct intel_display *display)
{
	struct xe_device *xe = to_xe_device(display->drm);

	xe_device_td_flush(xe);
}
