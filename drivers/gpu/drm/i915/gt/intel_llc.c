/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#include <linux/cpufreq.h>

#include "i915_drv.h"
#include "intel_gt.h"
#include "intel_llc.h"
#include "intel_sideband.h"

struct ia_constants {
	unsigned int min_gpu_freq;
	unsigned int max_gpu_freq;

	unsigned int min_ring_freq;
	unsigned int max_ia_freq;
};

static struct intel_gt *llc_to_gt(struct intel_llc *llc)
{
	return container_of(llc, struct intel_gt, llc);
}

static unsigned int cpu_max_MHz(void)
{
	struct cpufreq_policy *policy;
	unsigned int max_khz;

	policy = cpufreq_cpu_get(0);
	if (policy) {
		max_khz = policy->cpuinfo.max_freq;
		cpufreq_cpu_put(policy);
	} else {
		/*
		 * Default to measured freq if none found, PCU will ensure we
		 * don't go over
		 */
		max_khz = tsc_khz;
	}

	return max_khz / 1000;
}

static bool get_ia_constants(struct intel_llc *llc,
			     struct ia_constants *consts)
{
	struct drm_i915_private *i915 = llc_to_gt(llc)->i915;
	struct intel_rps *rps = &llc_to_gt(llc)->rps;

	if (rps->max_freq <= rps->min_freq)
		return false;

	consts->max_ia_freq = cpu_max_MHz();

	consts->min_ring_freq =
		intel_uncore_read(llc_to_gt(llc)->uncore, DCLK) & 0xf;
	/* convert DDR frequency from units of 266.6MHz to bandwidth */
	consts->min_ring_freq = mult_frac(consts->min_ring_freq, 8, 3);

	consts->min_gpu_freq = rps->min_freq;
	consts->max_gpu_freq = rps->max_freq;
	if (INTEL_GEN(i915) >= 9) {
		/* Convert GT frequency to 50 HZ units */
		consts->min_gpu_freq /= GEN9_FREQ_SCALER;
		consts->max_gpu_freq /= GEN9_FREQ_SCALER;
	}

	return true;
}

static void calc_ia_freq(struct intel_llc *llc,
			 unsigned int gpu_freq,
			 const struct ia_constants *consts,
			 unsigned int *out_ia_freq,
			 unsigned int *out_ring_freq)
{
	struct drm_i915_private *i915 = llc_to_gt(llc)->i915;
	const int diff = consts->max_gpu_freq - gpu_freq;
	unsigned int ia_freq = 0, ring_freq = 0;

	if (INTEL_GEN(i915) >= 9) {
		/*
		 * ring_freq = 2 * GT. ring_freq is in 100MHz units
		 * No floor required for ring frequency on SKL.
		 */
		ring_freq = gpu_freq;
	} else if (INTEL_GEN(i915) >= 8) {
		/* max(2 * GT, DDR). NB: GT is 50MHz units */
		ring_freq = max(consts->min_ring_freq, gpu_freq);
	} else if (IS_HASWELL(i915)) {
		ring_freq = mult_frac(gpu_freq, 5, 4);
		ring_freq = max(consts->min_ring_freq, ring_freq);
		/* leave ia_freq as the default, chosen by cpufreq */
	} else {
		const int min_freq = 15;
		const int scale = 180;

		/*
		 * On older processors, there is no separate ring
		 * clock domain, so in order to boost the bandwidth
		 * of the ring, we need to upclock the CPU (ia_freq).
		 *
		 * For GPU frequencies less than 750MHz,
		 * just use the lowest ring freq.
		 */
		if (gpu_freq < min_freq)
			ia_freq = 800;
		else
			ia_freq = consts->max_ia_freq - diff * scale / 2;
		ia_freq = DIV_ROUND_CLOSEST(ia_freq, 100);
	}

	*out_ia_freq = ia_freq;
	*out_ring_freq = ring_freq;
}

static void gen6_update_ring_freq(struct intel_llc *llc)
{
	struct drm_i915_private *i915 = llc_to_gt(llc)->i915;
	struct ia_constants consts;
	unsigned int gpu_freq;

	if (!get_ia_constants(llc, &consts))
		return;

	/*
	 * For each potential GPU frequency, load a ring frequency we'd like
	 * to use for memory access.  We do this by specifying the IA frequency
	 * the PCU should use as a reference to determine the ring frequency.
	 */
	for (gpu_freq = consts.max_gpu_freq;
	     gpu_freq >= consts.min_gpu_freq;
	     gpu_freq--) {
		unsigned int ia_freq, ring_freq;

		calc_ia_freq(llc, gpu_freq, &consts, &ia_freq, &ring_freq);
		sandybridge_pcode_write(i915,
					GEN6_PCODE_WRITE_MIN_FREQ_TABLE,
					ia_freq << GEN6_PCODE_FREQ_IA_RATIO_SHIFT |
					ring_freq << GEN6_PCODE_FREQ_RING_RATIO_SHIFT |
					gpu_freq);
	}
}

void intel_llc_enable(struct intel_llc *llc)
{
	if (HAS_LLC(llc_to_gt(llc)->i915))
		gen6_update_ring_freq(llc);
}

void intel_llc_disable(struct intel_llc *llc)
{
	/* Currently there is no HW configuration to be done to disable. */
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftest_llc.c"
#endif
