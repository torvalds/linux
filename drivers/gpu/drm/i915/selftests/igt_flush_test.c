/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include "gt/intel_gt.h"
#include "gt/intel_gt_requests.h"

#include "i915_drv.h"
#include "i915_selftest.h"

#include "igt_flush_test.h"

int igt_flush_test(struct drm_i915_private *i915)
{
	struct intel_gt *gt = to_gt(i915);
	int ret = intel_gt_is_wedged(gt) ? -EIO : 0;

	cond_resched();

	if (intel_gt_wait_for_idle(gt, HZ) == -ETIME) {
		pr_err("%pS timed out, cancelling all further testing.\n",
		       __builtin_return_address(0));

		GEM_TRACE("%pS timed out.\n",
			  __builtin_return_address(0));
		GEM_TRACE_DUMP();

		intel_gt_set_wedged(gt);
		ret = -EIO;
	}

	return ret;
}
