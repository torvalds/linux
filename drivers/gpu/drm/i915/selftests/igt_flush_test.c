/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "gem/i915_gem_context.h"
#include "gt/intel_gt.h"

#include "i915_drv.h"
#include "i915_selftest.h"

#include "igt_flush_test.h"

int igt_flush_test(struct drm_i915_private *i915)
{
	int ret = intel_gt_is_wedged(&i915->gt) ? -EIO : 0;

	cond_resched();

	i915_retire_requests(i915);
	if (i915_gem_wait_for_idle(i915, 0, HZ / 5) == -ETIME) {
		pr_err("%pS timed out, cancelling all further testing.\n",
		       __builtin_return_address(0));

		GEM_TRACE("%pS timed out.\n",
			  __builtin_return_address(0));
		GEM_TRACE_DUMP();

		intel_gt_set_wedged(&i915->gt);
		ret = -EIO;
	}
	i915_retire_requests(i915);

	return ret;
}
