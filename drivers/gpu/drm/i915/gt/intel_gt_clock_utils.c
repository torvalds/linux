// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_gt_clock_utils.h"

#define MHZ_12   12000000 /* 12MHz (24MHz/2), 83.333ns */
#define MHZ_12_5 12500000 /* 12.5MHz (25MHz/2), 80ns */
#define MHZ_19_2 19200000 /* 19.2MHz, 52.083ns */

static u32 read_clock_frequency(const struct intel_gt *gt)
{
	if (INTEL_GEN(gt->i915) >= 11) {
		u32 config;

		config = intel_uncore_read(gt->uncore, RPM_CONFIG0);
		config &= GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK;
		config >>= GEN11_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_SHIFT;

		switch (config) {
		case 0: return MHZ_12;
		case 1:
		case 2: return MHZ_19_2;
		default:
		case 3: return MHZ_12_5;
		}
	} else if (INTEL_GEN(gt->i915) >= 9) {
		if (IS_GEN9_LP(gt->i915))
			return MHZ_19_2;
		else
			return MHZ_12;
	} else {
		return MHZ_12_5;
	}
}

void intel_gt_init_clock_frequency(struct intel_gt *gt)
{
	/*
	 * Note that on gen11+, the clock frequency may be reconfigured.
	 * We do not, and we assume nobody else does.
	 */
	gt->clock_frequency = read_clock_frequency(gt);
	GT_TRACE(gt,
		 "Using clock frequency: %dkHz\n",
		 gt->clock_frequency / 1000);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
void intel_gt_check_clock_frequency(const struct intel_gt *gt)
{
	if (gt->clock_frequency != read_clock_frequency(gt)) {
		dev_err(gt->i915->drm.dev,
			"GT clock frequency changed, was %uHz, now %uHz!\n",
			gt->clock_frequency,
			read_clock_frequency(gt));
	}
}
#endif

static u64 div_u64_roundup(u64 nom, u32 den)
{
	return div_u64(nom + den - 1, den);
}

u32 intel_gt_clock_interval_to_ns(const struct intel_gt *gt, u32 count)
{
	return div_u64_roundup(mul_u32_u32(count, 1000 * 1000 * 1000),
			       gt->clock_frequency);
}

u32 intel_gt_pm_interval_to_ns(const struct intel_gt *gt, u32 count)
{
	return intel_gt_clock_interval_to_ns(gt, 16 * count);
}

u32 intel_gt_ns_to_clock_interval(const struct intel_gt *gt, u32 ns)
{
	return div_u64_roundup(mul_u32_u32(gt->clock_frequency, ns),
			       1000 * 1000 * 1000);
}

u32 intel_gt_ns_to_pm_interval(const struct intel_gt *gt, u32 ns)
{
	u32 val;

	/*
	 * Make these a multiple of magic 25 to avoid SNB (eg. Dell XPS
	 * 8300) freezing up around GPU hangs. Looks as if even
	 * scheduling/timer interrupts start misbehaving if the RPS
	 * EI/thresholds are "bad", leading to a very sluggish or even
	 * frozen machine.
	 */
	val = DIV_ROUND_UP(intel_gt_ns_to_clock_interval(gt, ns), 16);
	if (IS_GEN(gt->i915, 6))
		val = roundup(val, 25);

	return val;
}
