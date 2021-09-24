/* SPDX-License-Identifier: MIT */
/*
 * Copyright(c) 2020, Intel Corporation. All rights reserved.
 */

#ifndef __INTEL_PXP_TYPES_H__
#define __INTEL_PXP_TYPES_H__

#include <linux/mutex.h>
#include <linux/types.h>

struct intel_context;
struct i915_pxp_component;

struct intel_pxp {
	struct i915_pxp_component *pxp_component;
	bool pxp_component_added;

	struct intel_context *ce;

	/*
	 * After a teardown, the arb session can still be in play on the HW
	 * even if the keys are gone, so we can't rely on the HW state of the
	 * session to know if it's valid and need to track the status in SW.
	 */
	bool arb_is_valid;

	struct mutex tee_mutex; /* protects the tee channel binding */
};

#endif /* __INTEL_PXP_TYPES_H__ */
