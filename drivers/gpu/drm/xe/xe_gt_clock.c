// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <linux/math64.h>

#include "xe_gt_clock.h"

#include "regs/xe_gt_regs.h"
#include "regs/xe_regs.h"
#include "xe_assert.h"
#include "xe_device.h"
#include "xe_gt.h"
#include "xe_gt_printk.h"
#include "xe_macros.h"
#include "xe_mmio.h"

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
	u32 c0 = xe_mmio_read32(&gt->mmio, RPM_CONFIG0);
	u32 freq = 0;

	/*
	 * CTC_MODE[0] = 1 is definitely not supported for Xe2 and later
	 * platforms.  In theory it could be a valid setting for pre-Xe2
	 * platforms, but there's no documentation on how to properly handle
	 * this case.  Reading TIMESTAMP_OVERRIDE, as the driver attempted in
	 * the past has been confirmed as incorrect by the hardware architects.
	 *
	 * For now just warn if we ever encounter hardware in the wild that
	 * has this setting and move on as if it hadn't been set.
	 */
	if (xe_mmio_read32(&gt->mmio, CTC_MODE) & CTC_SOURCE_DIVIDE_LOGIC)
		xe_gt_warn(gt, "CTC_MODE[0] is set; this is unexpected and undocumented\n");

	freq = get_crystal_clock_freq(c0);

	/*
	 * Now figure out how the command stream's timestamp
	 * register increments from this frequency (it might
	 * increment only every few clock cycle).
	 */
	freq >>= 3 - REG_FIELD_GET(RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK, c0);

	gt->info.reference_clock = freq;
	return 0;
}

static u64 div_u64_roundup(u64 n, u32 d)
{
	return div_u64(n + d - 1, d);
}

/**
 * xe_gt_clock_interval_to_ms - Convert sampled GT clock ticks to msec
 *
 * @gt: the &xe_gt
 * @count: count of GT clock ticks
 *
 * Returns: time in msec
 */
u64 xe_gt_clock_interval_to_ms(struct xe_gt *gt, u64 count)
{
	return div_u64_roundup(count * MSEC_PER_SEC, gt->info.reference_clock);
}
