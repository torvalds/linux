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

#define f19_2_mhz	19200000
#define f24_mhz		24000000
#define f25_mhz		25000000
#define f38_4_mhz	38400000
#define ts_base_83	83333
#define ts_base_52	52083
#define ts_base_80	80000

static void read_crystal_clock(struct xe_gt *gt, u32 rpm_config_reg, u32 *freq,
			       u32 *timestamp_base)
{
	u32 crystal_clock = REG_FIELD_GET(RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK,
					  rpm_config_reg);

	switch (crystal_clock) {
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_24_MHZ:
		*freq = f24_mhz;
		*timestamp_base = ts_base_83;
		return;
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_19_2_MHZ:
		*freq = f19_2_mhz;
		*timestamp_base = ts_base_52;
		return;
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_38_4_MHZ:
		*freq = f38_4_mhz;
		*timestamp_base = ts_base_52;
		return;
	case RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_25_MHZ:
		*freq = f25_mhz;
		*timestamp_base = ts_base_80;
		return;
	default:
		xe_gt_warn(gt, "Invalid crystal clock frequency: %u", crystal_clock);
		*freq = 0;
		*timestamp_base = 0;
		return;
	}
}

static void check_ctc_mode(struct xe_gt *gt)
{
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
}

int xe_gt_clock_init(struct xe_gt *gt)
{
	u32 freq;
	u32 c0;

	if (!IS_SRIOV_VF(gt_to_xe(gt)))
		check_ctc_mode(gt);

	c0 = xe_mmio_read32(&gt->mmio, RPM_CONFIG0);
	read_crystal_clock(gt, c0, &freq, &gt->info.timestamp_base);

	/*
	 * Now figure out how the command stream's timestamp
	 * register increments from this frequency (it might
	 * increment only every few clock cycle).
	 */
	freq >>= 3 - REG_FIELD_GET(RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK, c0);

	gt->info.reference_clock = freq;
	return 0;
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
	return mul_u64_u32_div(count, MSEC_PER_SEC, gt->info.reference_clock);
}
