/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2020,2021 Intel Corporation
 */

#ifndef __INTEL_STEP_H__
#define __INTEL_STEP_H__

#include <linux/types.h>

struct drm_i915_private;

struct intel_step_info {
	/*
	 * It is expected to have 4 number steps per letter. Deviation from
	 * the expectation breaks gmd_to_intel_step().
	 */
	u8 graphics_step;	/* Represents the compute tile on Xe_HPC */
	u8 media_step;
	u8 basedie_step;
};

#define STEP_ENUM_VAL(name)  STEP_##name,

#define STEP_NAME_LIST(func)		\
	func(A0)			\
	func(A1)			\
	func(A2)			\
	func(A3)			\
	func(B0)			\
	func(B1)			\
	func(B2)			\
	func(B3)			\
	func(C0)			\
	func(C1)			\
	func(C2)			\
	func(C3)			\
	func(D0)			\
	func(D1)			\
	func(D2)			\
	func(D3)			\
	func(E0)			\
	func(E1)			\
	func(E2)			\
	func(E3)			\
	func(F0)			\
	func(F1)			\
	func(F2)			\
	func(F3)			\
	func(G0)			\
	func(G1)			\
	func(G2)			\
	func(G3)			\
	func(H0)			\
	func(H1)			\
	func(H2)			\
	func(H3)			\
	func(I0)			\
	func(I1)			\
	func(I2)			\
	func(I3)			\
	func(J0)			\
	func(J1)			\
	func(J2)			\
	func(J3)

/*
 * Symbolic steppings that do not match the hardware. These are valid both as gt
 * and display steppings as symbolic names.
 */
enum intel_step {
	STEP_NONE = 0,
	STEP_NAME_LIST(STEP_ENUM_VAL)
	STEP_FUTURE,
	STEP_FOREVER,
};

void intel_step_init(struct drm_i915_private *i915);
const char *intel_step_name(enum intel_step step);

#endif /* __INTEL_STEP_H__ */
