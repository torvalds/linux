// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_clock.h"

#include "xe_device.h"
#include "xe_gt.h"
#include "xe_macros.h"
#include "xe_mmio.h"

#include "gt/intel_gt_regs.h"
#include "i915_reg.h"

static u32 read_reference_ts_freq(struct xe_gt *gt)
{
	u32 ts_override = xe_mmio_read32(gt, GEN9_TIMESTAMP_OVERRIDE.reg);
	u32 base_freq, frac_freq;

	base_freq = ((ts_override & GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_MASK) >>
		     GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_SHIFT) + 1;
	base_freq *= 1000000;

	frac_freq = ((ts_override &
		      GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_MASK) >>
		     GEN9_TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_SHIFT);
	frac_freq = 1000000 / (frac_freq + 1);

	return base_freq + frac_freq;
}

static u32 get_crystal_clock_freq(u32 rpm_config_reg)
{
	const u32 f19_2_mhz = 19200000;
	const u32 f24_mhz = 24000000;
	const u32 f25_mhz = 25000000;
	const u32 f38_4_mhz = 38400000;
	u32 crystal_clock =
		(rpm_config_reg & GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK) >>
		GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_SHIFT;

	switch (crystal_clock) {
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_24_MHZ:
		return f24_mhz;
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_19_2_MHZ:
		return f19_2_mhz;
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_38_4_MHZ:
		return f38_4_mhz;
	case GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_25_MHZ:
		return f25_mhz;
	default:
		XE_BUG_ON("NOT_POSSIBLE");
		return 0;
	}
}

int xe_gt_clock_init(struct xe_gt *gt)
{
	u32 ctc_reg = xe_mmio_read32(gt, CTC_MODE.reg);
	u32 freq = 0;

	/* Assuming gen11+ so assert this assumption is correct */
	XE_BUG_ON(GRAPHICS_VER(gt_to_xe(gt)) < 11);

	if ((ctc_reg & CTC_SOURCE_PARAMETER_MASK) == CTC_SOURCE_DIVIDE_LOGIC) {
		freq = read_reference_ts_freq(gt);
	} else {
		u32 c0 = xe_mmio_read32(gt, RPM_CONFIG0.reg);

		freq = get_crystal_clock_freq(c0);

		/*
		 * Now figure out how the command stream's timestamp
		 * register increments from this frequency (it might
		 * increment only every few clock cycle).
		 */
		freq >>= 3 - ((c0 & GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK) >>
			      GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_SHIFT);
	}

	gt->info.clock_freq = freq;
	return 0;
}
