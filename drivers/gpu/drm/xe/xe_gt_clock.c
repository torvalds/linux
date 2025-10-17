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

int xe_gt_clock_init(struct xe_gt *gt)
{
	u32 freq;
	u32 c0;

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
