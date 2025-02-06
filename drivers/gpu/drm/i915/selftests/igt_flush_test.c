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
	struct intel_gt *gt;
	unsigned int i;
	int ret = 0;

	for_each_gt(gt, i915, i) {
		struct intel_engine_cs *engine;
		unsigned long timeout_ms = 0;
		unsigned int id;

		if (intel_gt_is_wedged(gt))
			ret = -EIO;

		for_each_engine(engine, gt, id) {
			if (engine->props.preempt_timeout_ms > timeout_ms)
				timeout_ms = engine->props.preempt_timeout_ms;
		}

		cond_resched();

		/* 2x longest preempt timeout, experimentally determined */
		if (intel_gt_wait_for_idle(gt, HZ * timeout_ms / 500) == -ETIME) {
			pr_err("%pS timed out, cancelling all further testing.\n",
			       __builtin_return_address(0));

			GEM_TRACE("%pS timed out.\n",
				  __builtin_return_address(0));
			GEM_TRACE_DUMP();

			intel_gt_set_wedged(gt);
			ret = -EIO;
		}
	}

	return ret;
}
