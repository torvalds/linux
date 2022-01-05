// SPDX-License-Identifier: MIT
/*
 * Copyright �� 2021 Intel Corporation
 */

#include "selftests/intel_scheduler_helpers.h"

static struct i915_request *nop_user_request(struct intel_context *ce,
					     struct i915_request *from)
{
	struct i915_request *rq;
	int ret;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return rq;

	if (from) {
		ret = i915_sw_fence_await_dma_fence(&rq->submit,
						    &from->fence, 0,
						    I915_FENCE_GFP);
		if (ret < 0) {
			i915_request_put(rq);
			return ERR_PTR(ret);
		}
	}

	i915_request_get(rq);
	i915_request_add(rq);

	return rq;
}

static int intel_guc_scrub_ctbs(void *arg)
{
	struct intel_gt *gt = arg;
	int ret = 0;
	int i;
	struct i915_request *last[3] = {NULL, NULL, NULL}, *rq;
	intel_wakeref_t wakeref;
	struct intel_engine_cs *engine;
	struct intel_context *ce;

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);
	engine = intel_selftest_find_any_engine(gt);

	/* Submit requests and inject errors forcing G2H to be dropped */
	for (i = 0; i < 3; ++i) {
		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			ret = PTR_ERR(ce);
			pr_err("Failed to create context, %d: %d\n", i, ret);
			goto err;
		}

		switch (i) {
		case 0:
			ce->drop_schedule_enable = true;
			break;
		case 1:
			ce->drop_schedule_disable = true;
			break;
		case 2:
			ce->drop_deregister = true;
			break;
		}

		rq = nop_user_request(ce, NULL);
		intel_context_put(ce);

		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			pr_err("Failed to create request, %d: %d\n", i, ret);
			goto err;
		}

		last[i] = rq;
	}

	for (i = 0; i < 3; ++i) {
		ret = i915_request_wait(last[i], 0, HZ);
		if (ret < 0) {
			pr_err("Last request failed to complete: %d\n", ret);
			goto err;
		}
		i915_request_put(last[i]);
		last[i] = NULL;
	}

	/* Force all H2G / G2H to be submitted / processed */
	intel_gt_retire_requests(gt);
	msleep(500);

	/* Scrub missing G2H */
	intel_gt_handle_error(engine->gt, -1, 0, "selftest reset");

	/* GT will not idle if G2H are lost */
	ret = intel_gt_wait_for_idle(gt, HZ);
	if (ret < 0) {
		pr_err("GT failed to idle: %d\n", ret);
		goto err;
	}

err:
	for (i = 0; i < 3; ++i)
		if (last[i])
			i915_request_put(last[i]);
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);

	return ret;
}

int intel_guc_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(intel_guc_scrub_ctbs),
	};
	struct intel_gt *gt = &i915->gt;

	if (intel_gt_is_wedged(gt))
		return 0;

	if (!intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
