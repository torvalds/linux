// SPDX-License-Identifier: MIT
/*
 * Copyright © 2013-2021 Intel Corporation
 *
 * LPT/WPT IOSF sideband.
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_display_core.h"
#include "intel_sbi.h"

/* SBI access */
static int intel_sbi_rw(struct intel_display *display, u16 reg,
			enum intel_sbi_destination destination,
			u32 *val, bool is_read)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_uncore *uncore = &i915->uncore;
	u32 cmd;

	lockdep_assert_held(&display->sbi.lock);

	if (intel_wait_for_register_fw(uncore,
				       SBI_CTL_STAT, SBI_BUSY, 0,
				       100)) {
		drm_err(display->drm, "timeout waiting for SBI to become ready\n");
		return -EBUSY;
	}

	intel_uncore_write_fw(uncore, SBI_ADDR, (u32)reg << 16);
	intel_uncore_write_fw(uncore, SBI_DATA, is_read ? 0 : *val);

	if (destination == SBI_ICLK)
		cmd = SBI_CTL_DEST_ICLK | SBI_CTL_OP_CRRD;
	else
		cmd = SBI_CTL_DEST_MPHY | SBI_CTL_OP_IORD;
	if (!is_read)
		cmd |= BIT(8);
	intel_uncore_write_fw(uncore, SBI_CTL_STAT, cmd | SBI_BUSY);

	if (__intel_wait_for_register_fw(uncore,
					 SBI_CTL_STAT, SBI_BUSY, 0,
					 100, 100, &cmd)) {
		drm_err(display->drm, "timeout waiting for SBI to complete read\n");
		return -ETIMEDOUT;
	}

	if (cmd & SBI_RESPONSE_FAIL) {
		drm_err(display->drm, "error during SBI read of reg %x\n", reg);
		return -ENXIO;
	}

	if (is_read)
		*val = intel_uncore_read_fw(uncore, SBI_DATA);

	return 0;
}

void intel_sbi_lock(struct intel_display *display)
{
	mutex_lock(&display->sbi.lock);
}

void intel_sbi_unlock(struct intel_display *display)
{
	mutex_unlock(&display->sbi.lock);
}

u32 intel_sbi_read(struct intel_display *display, u16 reg,
		   enum intel_sbi_destination destination)
{
	u32 result = 0;

	intel_sbi_rw(display, reg, destination, &result, true);

	return result;
}

void intel_sbi_write(struct intel_display *display, u16 reg, u32 value,
		     enum intel_sbi_destination destination)
{
	intel_sbi_rw(display, reg, destination, &value, false);
}

void intel_sbi_init(struct intel_display *display)
{
	mutex_init(&display->sbi.lock);
}

void intel_sbi_fini(struct intel_display *display)
{
	mutex_destroy(&display->sbi.lock);
}
