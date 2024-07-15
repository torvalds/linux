// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_gt_clock.h"

#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_macros.h"
#include "xe_mmio.h"

static u32 read_reference_ts_freq(struct xe_gt *gt)
{
	u32 ts_override = xe_mmio_read32(gt, TIMESTAMP_OVERRIDE);
	u32 base_freq, frac_freq;

	base_freq = REG_FIELD_GET(TIMESTAMP_OVERRIDE_US_COUNTER_DIVIDER_MASK,
				  ts_override) + 1;
	base_freq *= 1000000;

	frac_freq = REG_FIELD_GET(TIMESTAMP_OVERRIDE_US_COUNTER_DENOMINATOR_MASK,
				  ts_override);
	frac_freq = 1000000 / (frac_freq + 1);

	return base_freq + frac_freq;
}

static u32 get_crystal_clock_freq(u32 rpm_config_reg)
{
	const u32 f19_2_mhz = 19200000;
	const u32 f24_mhz = 24000000;
	const u32 f25_mhz = 25000000;
	const u32 f38_4_mhz = 38400000;
	u32 crystal_clock = REG_FIELD_GET(RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK,
					  rpm_config_reg);

	switch (crystal_clock) {
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_24_MHZ:
		return f24_mhz;
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_19_2_MHZ:
		return f19_2_mhz;
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_38_4_MHZ:
		return f38_4_mhz;
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_25_MHZ:
		return f25_mhz;
	default:
		XE_WARN_ON("NOT_POSSIBLE");
		return 0;
	}
}

int xe_gt_clock_init(struct xe_gt *gt)
{
	u32 ctc_reg = xe_mmio_read32(gt, CTC_MODE);
	u32 freq = 0;

	/* Assuming gen11+ so assert this assumption is correct */
	xe_gt_assert(gt, GRAPHICS_VER(gt_to_xe(gt)) >= 11);

	if (ctc_reg & CTC_SOURCE_DIVIDE_LOGIC) {
		freq = read_reference_ts_freq(gt);
	} else {
		u32 c0 = xe_mmio_read32(gt, RPM_CONFIG0);

		freq = get_crystal_clock_freq(c0);

		/*
		 * Now figure out how the command stream's timestamp
		 * register increments from this frequency (it might
		 * increment only every few clock cycle).
		 */
		freq >>= 3 - REG_FIELD_GET(RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK, c0);
	}

	gt->info.reference_clock = freq;
	return 0;
}

u64 xe_gt_clock_cycles_to_ns(const struct xe_gt *gt, u64 count)
{
	return DIV_ROUND_CLOSEST_ULL(count * NSEC_PER_SEC, gt->info.reference_clock);
}
