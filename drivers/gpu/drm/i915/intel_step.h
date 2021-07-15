/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020,2021 Intel Corporation
 */

#ifndef __INTEL_STEP_H__
#define __INTEL_STEP_H__

#include <linux/types.h>

struct drm_i915_private;

struct intel_step_info {
	u8 gt_step;
	u8 display_step;
};

/*
 * Symbolic steppings that do not match the hardware. These are valid both as gt
 * and display steppings as symbolic names.
 */
enum intel_step {
	STEP_NONE = 0,
	STEP_A0,
	STEP_A2,
	STEP_B0,
	STEP_B1,
	STEP_C0,
	STEP_D0,
	STEP_D1,
	STEP_E0,
	STEP_F0,
	STEP_G0,
	STEP_H0,
	STEP_I0,
	STEP_I1,
	STEP_J0,
	STEP_FUTURE,
	STEP_FOREVER,
};

void intel_step_init(struct drm_i915_private *i915);

#endif /* __INTEL_STEP_H__ */
