/*
 * Copyright © 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <asm/iosf_mbi.h>

#include "i915_drv.h"
#include "intel_sideband.h"

/* SBI access */
static int intel_sbi_rw(struct drm_i915_private *i915, u16 reg,
			enum intel_sbi_destination destination,
			u32 *val, bool is_read)
{
	struct intel_uncore *uncore = &i915->uncore;
	u32 cmd;

	lockdep_assert_held(&i915->sb_lock);

	if (intel_wait_for_register_fw(uncore,
				       SBI_CTL_STAT, SBI_BUSY, 0,
				       100)) {
		drm_err(&i915->drm,
			"timeout waiting for SBI to become ready\n");
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
		drm_err(&i915->drm,
			"timeout waiting for SBI to complete read\n");
		return -ETIMEDOUT;
	}

	if (cmd & SBI_RESPONSE_FAIL) {
		drm_err(&i915->drm, "error during SBI read of reg %x\n", reg);
		return -ENXIO;
	}

	if (is_read)
		*val = intel_uncore_read_fw(uncore, SBI_DATA);

	return 0;
}

u32 intel_sbi_read(struct drm_i915_private *i915, u16 reg,
		   enum intel_sbi_destination destination)
{
	u32 result = 0;

	intel_sbi_rw(i915, reg, destination, &result, true);

	return result;
}

void intel_sbi_write(struct drm_i915_private *i915, u16 reg, u32 value,
		     enum intel_sbi_destination destination)
{
	intel_sbi_rw(i915, reg, destination, &value, false);
}
