// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "intel_pm.h" /* intel_gpu_freq() */
#include "selftest_llc.h"
#include "intel_rps.h"

static int gen6_verify_ring_freq(struct intel_llc *llc)
{
	struct drm_i915_private *i915 = llc_to_gt(llc)->i915;
	struct ia_constants consts;
	intel_wakeref_t wakeref;
	unsigned int gpu_freq;
	int err = 0;

	wakeref = intel_runtime_pm_get(llc_to_gt(llc)->uncore->rpm);

	if (!get_ia_constants(llc, &consts))
		goto out_rpm;

	for (gpu_freq = consts.min_gpu_freq;
	     gpu_freq <= consts.max_gpu_freq;
	     gpu_freq++) {
		struct intel_rps *rps = &llc_to_gt(llc)->rps;

		unsigned int ia_freq, ring_freq, found;
		u32 val;

		calc_ia_freq(llc, gpu_freq, &consts, &ia_freq, &ring_freq);

		val = gpu_freq;
		if (snb_pcode_read(llc_to_gt(llc)->uncore, GEN6_PCODE_READ_MIN_FREQ_TABLE,
				   &val, NULL)) {
			pr_err("Failed to read freq table[%d], range [%d, %d]\n",
			       gpu_freq, consts.min_gpu_freq, consts.max_gpu_freq);
			err = -ENXIO;
			break;
		}

		found = (val >> 0) & 0xff;
		if (found != ia_freq) {
			pr_err("Min freq table(%d/[%d, %d]):%dMHz did not match expected CPU freq, found %d, expected %d\n",
			       gpu_freq, consts.min_gpu_freq, consts.max_gpu_freq,
			       intel_gpu_freq(rps, gpu_freq * (GRAPHICS_VER(i915) >= 9 ? GEN9_FREQ_SCALER : 1)),
			       found, ia_freq);
			err = -EINVAL;
			break;
		}

		found = (val >> 8) & 0xff;
		if (found != ring_freq) {
			pr_err("Min freq table(%d/[%d, %d]):%dMHz did not match expected ring freq, found %d, expected %d\n",
			       gpu_freq, consts.min_gpu_freq, consts.max_gpu_freq,
			       intel_gpu_freq(rps, gpu_freq * (GRAPHICS_VER(i915) >= 9 ? GEN9_FREQ_SCALER : 1)),
			       found, ring_freq);
			err = -EINVAL;
			break;
		}
	}

out_rpm:
	intel_runtime_pm_put(llc_to_gt(llc)->uncore->rpm, wakeref);
	return err;
}

int st_llc_verify(struct intel_llc *llc)
{
	return gen6_verify_ring_freq(llc);
}
