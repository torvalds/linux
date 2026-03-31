// SPDX-License-Identifier: MIT
/*
 * Copyright © 2026 Intel Corporation
 */

#include <linux/delay.h>

#include <drm/drm_print.h>

#include "intel_de.h"

static int __intel_de_wait_for_register(struct intel_display *display,
					i915_reg_t reg, u32 mask, u32 value,
					unsigned int timeout_us,
					u32 (*read)(struct intel_display *display, i915_reg_t reg),
					u32 *out_val, bool is_atomic)
{
	const ktime_t end = ktime_add_us(ktime_get_raw(), timeout_us);
	int wait_max = 1000;
	int wait = 10;
	u32 reg_value;
	int ret;

	might_sleep_if(!is_atomic);

	if (timeout_us <= 10) {
		is_atomic = true;
		wait = 1;
	}

	for (;;) {
		bool expired = ktime_after(ktime_get_raw(), end);

		/* guarantee the condition is evaluated after timeout expired */
		barrier();

		reg_value = read(display, reg);
		if ((reg_value & mask) == value) {
			ret = 0;
			break;
		}

		if (expired) {
			ret = -ETIMEDOUT;
			break;
		}

		if (is_atomic)
			udelay(wait);
		else
			usleep_range(wait, wait << 1);

		if (wait < wait_max)
			wait <<= 1;
	}

	if (out_val)
		*out_val = reg_value;

	return ret;
}

static int intel_de_wait_for_register(struct intel_display *display,
				      i915_reg_t reg, u32 mask, u32 value,
				      unsigned int fast_timeout_us,
				      unsigned int slow_timeout_us,
				      u32 (*read)(struct intel_display *display, i915_reg_t reg),
				      u32 *out_value, bool is_atomic)
{
	int ret = -EINVAL;

	if (fast_timeout_us)
		ret = __intel_de_wait_for_register(display, reg, mask, value,
						   fast_timeout_us, read,
						   out_value, is_atomic);

	if (ret && slow_timeout_us)
		ret = __intel_de_wait_for_register(display, reg, mask, value,
						   slow_timeout_us, read,
						   out_value, is_atomic);

	return ret;
}

int intel_de_wait_us(struct intel_display *display, i915_reg_t reg,
		     u32 mask, u32 value, unsigned int timeout_us,
		     u32 *out_value)
{
	int ret;

	intel_dmc_wl_get(display, reg);

	ret = intel_de_wait_for_register(display, reg, mask, value,
					 timeout_us, 0,
					 intel_de_read,
					 out_value, false);

	intel_dmc_wl_put(display, reg);

	return ret;
}

int intel_de_wait_ms(struct intel_display *display, i915_reg_t reg,
		     u32 mask, u32 value, unsigned int timeout_ms,
		     u32 *out_value)
{
	int ret;

	intel_dmc_wl_get(display, reg);

	ret = intel_de_wait_for_register(display, reg, mask, value,
					 2, timeout_ms * 1000,
					 intel_de_read,
					 out_value, false);

	intel_dmc_wl_put(display, reg);

	return ret;
}

int intel_de_wait_fw_ms(struct intel_display *display, i915_reg_t reg,
			u32 mask, u32 value, unsigned int timeout_ms,
			u32 *out_value)
{
	return intel_de_wait_for_register(display, reg, mask, value,
					  2, timeout_ms * 1000,
					  intel_de_read_fw,
					  out_value, false);
}

int intel_de_wait_fw_us_atomic(struct intel_display *display, i915_reg_t reg,
			       u32 mask, u32 value, unsigned int timeout_us,
			       u32 *out_value)
{
	return intel_de_wait_for_register(display, reg, mask, value,
					  timeout_us, 0,
					  intel_de_read_fw,
					  out_value, true);
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
