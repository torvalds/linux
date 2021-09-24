/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TYPES_H__
#define __INTEL_PXP_TYPES_H__

#include <linux/types.h>

struct intel_context;
struct i915_pxp_component;

struct intel_pxp {
	struct i915_pxp_component *pxp_component;
	bool pxp_component_added;

	struct intel_context *ce;
};

#endif /* __INTEL_PXP_TYPES_H__ */
