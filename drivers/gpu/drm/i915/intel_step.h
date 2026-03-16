/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020,2021 Intel Corporation
 */

#ifndef __INTEL_STEP_H__
#define __INTEL_STEP_H__

#include <linux/types.h>

#include <drm/intel/step.h>

struct drm_i915_private;

struct intel_step_info {
	/*
	 * It is expected to have 4 number steps per letter. Deviation from
	 * the expectation breaks gmd_to_intel_step().
	 */
	u8 graphics_step;	/* Represents the compute tile on Xe_HPC */
	u8 media_step;
};

void intel_step_init(struct drm_i915_private *i915);
const char *intel_step_name(enum intel_step step);

#endif /* __INTEL_STEP_H__ */
