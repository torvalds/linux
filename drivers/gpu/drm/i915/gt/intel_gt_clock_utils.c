// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_gt_clock_utils.h"

static u32 read_reference_ts_freq(struct intel_uncore *uncore)
{
	u32 ts_override = intel_uncore_read(uncore, GEN9_TIMESTAMP_OVERRIDE);
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

static u32 gen9_get_crystal_clock_freq(struct intel_uncore *uncore,
				       u32 rpm_config_reg)
{
	u32 f19_2_mhz = 19200000;
	u32 f24_mhz = 24000000;
	u32 crystal_clock =
		(rpm_config_reg & GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_MASK) >>
		GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_SHIFT;

	switch (crystal_clock) {
	case GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_19_2_MHZ:
		return f19_2_mhz;
	case GEN9_RPM_CONFIG0_CRYSTAL_CLOCK_FREQ_24_MHZ:
		return f24_mhz;
	default:
		MISSING_CASE(crystal_clock);
		return 0;
	}
}

static u32 gen11_get_crystal_clock_freq(struct intel_uncore *uncore,
					u32 rpm_config_reg)
{
	u32 f19_2_mhz = 19200000;
	u32 f24_mhz = 24000000;
	u32 f25_mhz = 25000000;
	u32 f38_4_mhz = 38400000;
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
		MISSING_CASE(crystal_clock);
		return 0;
	}
}

static u32 read_clock_frequency(struct intel_uncore *uncore)
{
	u32 f12_5_mhz = 12500000;
	u32 f19_2_mhz = 19200000;
	u32 f24_mhz = 24000000;

	if (GRAPHICS_VER(uncore->i915) <= 4) {
		/*
		 * PRMs say:
		 *
		 *     "The value in this register increments once every 16
		 *      hclks." (through the “Clocking Configuration”
		 *      (“CLKCFG”) MCHBAR register)
		 */
		return RUNTIME_INFO(uncore->i915)->rawclk_freq * 1000 / 16;
	} else if (GRAPHICS_VER(uncore->i915) <= 8) {
		/*
		 * PRMs say:
		 *
		 *     "The PCU TSC counts 10ns increments; this timestamp
		 *      reflects bits 38:3 of the TSC (i.e. 80ns granularity,
		 *      rolling over every 1.5 hours).
		 */
		return f12_5_mhz;
	} else if (GRAPHICS_VER(uncore->i915) <= 9) {
		u32 ctc_reg = intel_uncore_read(uncore, CTC_MODE);
		u32 freq = 0;

		if ((ctc_reg & CTC_SOURCE_PARAMETER_MASK) == CTC_SOURCE_DIVIDE_LOGIC) {
			freq = read_reference_ts_freq(uncore);
		} else {
			freq = IS_GEN9_LP(uncore->i915) ? f19_2_mhz : f24_mhz;

			/*
			 * Now figure out how the command stream's timestamp
			 * register increments from this frequency (it might
			 * increment only every few clock cycle).
			 */
			freq >>= 3 - ((ctc_reg & CTC_SHIFT_PARAMETER_MASK) >>
				      CTC_SHIFT_PARAMETER_SHIFT);
		}

		return freq;
	} else if (GRAPHICS_VER(uncore->i915) <= 12) {
		u32 ctc_reg = intel_uncore_read(uncore, CTC_MODE);
		u32 freq = 0;

		/*
		 * First figure out the reference frequency. There are 2 ways
		 * we can compute the frequency, either through the
		 * TIMESTAMP_OVERRIDE register or through RPM_CONFIG. CTC_MODE
		 * tells us which one we should use.
		 */
		if ((ctc_reg & CTC_SOURCE_PARAMETER_MASK) == CTC_SOURCE_DIVIDE_LOGIC) {
			freq = read_reference_ts_freq(uncore);
		} else {
			u32 c0 = intel_uncore_read(uncore, RPM_CONFIG0);

			if (GRAPHICS_VER(uncore->i915) >= 11)
				freq = gen11_get_crystal_clock_freq(uncore, c0);
			else
				freq = gen9_get_crystal_clock_freq(uncore, c0);

			/*
			 * Now figure out how the command stream's timestamp
			 * register increments from this frequency (it might
			 * increment only every few clock cycle).
			 */
			freq >>= 3 - ((c0 & GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_MASK) >>
				      GEN10_RPM_CONFIG0_CTC_SHIFT_PARAMETER_SHIFT);
		}

		return freq;
	}

	MISSING_CASE("Unknown gen, unable to read command streamer timestamp frequency\n");
	return 0;
}

void intel_gt_init_clock_frequency(struct intel_gt *gt)
{
	/*
	 * Note that on gen11+, the clock frequency may be reconfigured.
	 * We do not, and we assume nobody else does.
	 */
	gt->clock_frequency = read_clock_frequency(gt->uncore);
	if (gt->clock_frequency)
		gt->clock_period_ns = intel_gt_clock_interval_to_ns(gt, 1);

	GT_TRACE(gt,
		 "Using clock frequency: %dkHz, period: %dns, wrap: %lldms\n",
		 gt->clock_frequency / 1000,
		 gt->clock_period_ns,
		 div_u64(mul_u32_u32(gt->clock_period_ns, S32_MAX),
			 USEC_PER_SEC));
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
void intel_gt_check_clock_frequency(const struct intel_gt *gt)
{
	if (gt->clock_frequency != read_clock_frequency(gt->uncore)) {
		dev_err(gt->i915->drm.dev,
			"GT clock frequency changed, was %uHz, now %uHz!\n",
			gt->clock_frequency,
			read_clock_frequency(gt->uncore));
	}
}
#endif

static u64 div_u64_roundup(u64 nom, u32 den)
{
	return div_u64(nom + den - 1, den);
}

u64 intel_gt_clock_interval_to_ns(const struct intel_gt *gt, u64 count)
{
	return div_u64_roundup(count * NSEC_PER_SEC, gt->clock_frequency);
}

u64 intel_gt_pm_interval_to_ns(const struct intel_gt *gt, u64 count)
{
	return intel_gt_clock_interval_to_ns(gt, 16 * count);
}

u64 intel_gt_ns_to_clock_interval(const struct intel_gt *gt, u64 ns)
{
	return div_u64_roundup(gt->clock_frequency * ns, NSEC_PER_SEC);
}

u64 intel_gt_ns_to_pm_interval(const struct intel_gt *gt, u64 ns)
{
	u64 val;

	/*
	 * Make these a multiple of magic 25 to avoid SNB (eg. Dell XPS
	 * 8300) freezing up around GPU hangs. Looks as if even
	 * scheduling/timer interrupts start misbehaving if the RPS
	 * EI/thresholds are "bad", leading to a very sluggish or even
	 * frozen machine.
	 */
	val = div_u64_roundup(intel_gt_ns_to_clock_interval(gt, ns), 16);
	if (GRAPHICS_VER(gt->i915) == 6)
		val = div_u64_roundup(val, 25) * 25;

	return val;
}
