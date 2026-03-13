// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <drm/drm_print.h>

#include "intel_de.h"

int intel_de_wait_us(struct intel_display *display, i915_reg_t reg,
		     u32 mask, u32 value, unsigned int timeout_us,
		     u32 *out_value)
{
	int ret;

	intel_dmc_wl_get(display, reg);

	ret = __intel_wait_for_register(__to_uncore(display), reg, mask,
					value, timeout_us, 0, out_value);

	intel_dmc_wl_put(display, reg);

	return ret;
}

int intel_de_wait_ms(struct intel_display *display, i915_reg_t reg,
		     u32 mask, u32 value, unsigned int timeout_ms,
		     u32 *out_value)
{
	int ret;

	intel_dmc_wl_get(display, reg);

	ret = __intel_wait_for_register(__to_uncore(display), reg, mask,
					value, 2, timeout_ms, out_value);

	intel_dmc_wl_put(display, reg);

	return ret;
}

int intel_de_wait_fw_ms(struct intel_display *display, i915_reg_t reg,
			u32 mask, u32 value, unsigned int timeout_ms,
			u32 *out_value)
{
	return __intel_wait_for_register_fw(__to_uncore(display), reg, mask,
					    value, 2, timeout_ms, out_value);
}

int intel_de_wait_fw_us_atomic(struct intel_display *display, i915_reg_t reg,
			       u32 mask, u32 value, unsigned int timeout_us,
			       u32 *out_value)
{
	return __intel_wait_for_register_fw(__to_uncore(display), reg, mask,
					    value, timeout_us, 0, out_value);
}

int intel_de_wait_for_set_us(struct intel_display *display, i915_reg_t reg,
			     u32 mask, unsigned int timeout_us)
{
	return intel_de_wait_us(display, reg, mask, mask, timeout_us, NULL);
}

int intel_de_wait_for_clear_us(struct intel_display *display, i915_reg_t reg,
			       u32 mask, unsigned int timeout_us)
{
	return intel_de_wait_us(display, reg, mask, 0, timeout_us, NULL);
}

int intel_de_wait_for_set_ms(struct intel_display *display, i915_reg_t reg,
			     u32 mask, unsigned int timeout_ms)
{
	return intel_de_wait_ms(display, reg, mask, mask, timeout_ms, NULL);
}

int intel_de_wait_for_clear_ms(struct intel_display *display, i915_reg_t reg,
			       u32 mask, unsigned int timeout_ms)
{
	return intel_de_wait_ms(display, reg, mask, 0, timeout_ms, NULL);
}

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
