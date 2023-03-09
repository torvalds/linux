/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2019 Intel Corporation
 */

#ifndef INTEL_RC6_TYPES_H
#define INTEL_RC6_TYPES_H

#include <linux/spinlock.h>
#include <linux/types.h>

#include "intel_engine_types.h"

struct drm_i915_gem_object;

/* RC6 residency types */
enum intel_rc6_res_type {
	INTEL_RC6_RES_RC6_LOCKED,
	INTEL_RC6_RES_RC6,
	INTEL_RC6_RES_RC6p,
	INTEL_RC6_RES_RC6pp,
	INTEL_RC6_RES_MAX,
	INTEL_RC6_RES_VLV_MEDIA = INTEL_RC6_RES_RC6p,
};

struct intel_rc6 {
	i915_reg_t res_reg[INTEL_RC6_RES_MAX];
	u64 prev_hw_residency[INTEL_RC6_RES_MAX];
	u64 cur_residency[INTEL_RC6_RES_MAX];

	u32 ctl_enable;

	struct drm_i915_gem_object *pctx;

	bool supported : 1;
	bool enabled : 1;
	bool manual : 1;
	bool wakeref : 1;
};

#endif /* INTEL_RC6_TYPES_H */
