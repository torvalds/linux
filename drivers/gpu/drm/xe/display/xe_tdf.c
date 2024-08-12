// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "xe_device.h"
#include "intel_display_types.h"
#include "intel_tdf.h"

void intel_td_flush(struct drm_i915_private *i915)
{
	xe_device_td_flush(i915);
}
