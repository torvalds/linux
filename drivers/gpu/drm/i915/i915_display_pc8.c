// SPDX-License-Identifier: MIT
/*
 * Copyright 2025, Intel Corporation.
 */

#include <drm/drm_print.h>
#include <drm/intel/display_parent_interface.h>

#include "i915_display_pc8.h"
#include "i915_drv.h"
#include "intel_uncore.h"

static void i915_display_pc8_block(struct drm_device *drm)
{
	struct intel_uncore *uncore = &to_i915(drm)->uncore;

	/* to prevent PC8 state, just enable force_wake */
	intel_uncore_forcewake_get(uncore, FORCEWAKE_ALL);
}

static void i915_display_pc8_unblock(struct drm_device *drm)
{
	struct intel_uncore *uncore = &to_i915(drm)->uncore;

	intel_uncore_forcewake_put(uncore, FORCEWAKE_ALL);
}

const struct intel_display_pc8_interface i915_display_pc8_interface = {
	.block = i915_display_pc8_block,
	.unblock = i915_display_pc8_unblock,
};
