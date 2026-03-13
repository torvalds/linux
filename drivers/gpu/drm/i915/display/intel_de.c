// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <drm/drm_print.h>

#include "intel_de.h"

u8 intel_de_read8(struct intel_display *display, i915_reg_t reg)
{
	/* this is only used on VGA registers (possible on pre-g4x) */
	drm_WARN_ON(display->drm, DISPLAY_VER(display) >= 5 || display->platform.g4x);

	return intel_uncore_read8(__to_uncore(display), reg);
}

void intel_de_write8(struct intel_display *display, i915_reg_t reg, u8 val)
{
	drm_WARN_ON(display->drm, DISPLAY_VER(display) >= 5 || display->platform.g4x);

	intel_uncore_write8(__to_uncore(display), reg, val);
}
