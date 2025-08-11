// SPDX-License-Identifier: MIT
/*
 * Copyright © 2013-2021 Intel Corporation
 *
 * LPT/WPT IOSF sideband.
 */

#include <drm/drm_print.h>

#include "intel_de.h"
#include "intel_display_core.h"
#include "intel_sbi.h"
#include "intel_sbi_regs.h"

/* SBI access */
static int intel_sbi_rw(struct intel_display *display, u16 reg,
			enum intel_sbi_destination destination,
			u32 *val, bool is_read)
{
	u32 cmd;

	lockdep_assert_held(&display->sbi.lock);

	if (intel_de_wait_fw(display, SBI_CTL_STAT, SBI_STATUS_MASK, SBI_STATUS_READY, 100, NULL)) {
		drm_err(display->drm, "timeout waiting for SBI to become ready\n");
		return -EBUSY;
	}

	intel_de_write_fw(display, SBI_ADDR, SBI_ADDR_VALUE(reg));
	intel_de_write_fw(display, SBI_DATA, is_read ? 0 : *val);

	if (destination == SBI_ICLK)
		cmd = SBI_CTL_DEST_ICLK | SBI_CTL_OP_CRRD;
	else
		cmd = SBI_CTL_DEST_MPHY | SBI_CTL_OP_IORD;
	if (!is_read)
		cmd |= SBI_CTL_OP_WR;
	intel_de_write_fw(display, SBI_CTL_STAT, cmd | SBI_STATUS_BUSY);

	if (intel_de_wait_fw(display, SBI_CTL_STAT, SBI_STATUS_MASK, SBI_STATUS_READY, 100, &cmd)) {
		drm_err(display->drm, "timeout waiting for SBI to complete read\n");
		return -ETIMEDOUT;
	}

	if (cmd & SBI_RESPONSE_FAIL) {
		drm_err(display->drm, "error during SBI read of reg %x\n", reg);
		return -ENXIO;
	}

	if (is_read)
		*val = intel_de_read_fw(display, SBI_DATA);

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
