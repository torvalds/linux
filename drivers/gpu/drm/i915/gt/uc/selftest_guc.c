// SPDX-License-Identifier: MIT
/*
 * Copyright �� 2021 Intel Corporation
 */

#include "selftests/igt_spinner.h"
#include "selftests/intel_scheduler_helpers.h"

static int request_add_spin(struct i915_request *rq, struct igt_spinner *spin)
{
	int err = 0;

	i915_request_get(rq);
	i915_request_add(rq);
	if (spin && !igt_wait_for_spinner(spin, rq))
		err = -ETIMEDOUT;

	return err;
}

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

/*
 * intel_guc_steal_guc_ids - Test to exhaust all guc_ids and then steal one
 *
 * This test creates a spinner which is used to block all subsequent submissions
 * until it completes. Next, a loop creates a context and a NOP request each
 * iteration until the guc_ids are exhausted (request creation returns -EAGAIN).
 * The spinner is ended, unblocking all requests created in the loop. At this
 * point all guc_ids are exhausted but are available to steal. Try to create
 * another request which should successfully steal a guc_id. Wait on last
 * request to complete, idle GPU, verify a guc_id was stolen via a counter, and
 * exit the test. Test also artificially reduces the number of guc_ids so the
 * test runs in a timely manner.
 */
static int intel_guc_steal_guc_ids(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_guc *guc = &gt->uc.guc;
	int ret, sv, context_index = 0;
	intel_wakeref_t wakeref;
	struct intel_engine_cs *engine;
	struct intel_context **ce;
	struct igt_spinner spin;
	struct i915_request *spin_rq = NULL, *rq, *last = NULL;
	int number_guc_id_stolen = guc->number_guc_id_stolen;

	ce = kzalloc(sizeof(*ce) * GUC_MAX_LRC_DESCRIPTORS, GFP_KERNEL);
	if (!ce) {
		pr_err("Context array allocation failed\n");
		return -ENOMEM;
	}

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);
	engine = intel_selftest_find_any_engine(gt);
	sv = guc->submission_state.num_guc_ids;
	guc->submission_state.num_guc_ids = 512;

	/* Create spinner to block requests in below loop */
	ce[context_index] = intel_context_create(engine);
	if (IS_ERR(ce[context_index])) {
		ret = PTR_ERR(ce[context_index]);
		ce[context_index] = NULL;
		pr_err("Failed to create context: %d\n", ret);
		goto err_wakeref;
	}
	ret = igt_spinner_init(&spin, engine->gt);
	if (ret) {
		pr_err("Failed to create spinner: %d\n", ret);
		goto err_contexts;
	}
	spin_rq = igt_spinner_create_request(&spin, ce[context_index],
					     MI_ARB_CHECK);
	if (IS_ERR(spin_rq)) {
		ret = PTR_ERR(spin_rq);
		pr_err("Failed to create spinner request: %d\n", ret);
		goto err_contexts;
	}
	ret = request_add_spin(spin_rq, &spin);
	if (ret) {
		pr_err("Failed to add Spinner request: %d\n", ret);
		goto err_spin_rq;
	}

	/* Use all guc_ids */
	while (ret != -EAGAIN) {
		ce[++context_index] = intel_context_create(engine);
		if (IS_ERR(ce[context_index])) {
			ret = PTR_ERR(ce[context_index--]);
			ce[context_index] = NULL;
			pr_err("Failed to create context: %d\n", ret);
			goto err_spin_rq;
		}

		rq = nop_user_request(ce[context_index], spin_rq);
		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			rq = NULL;
			if (ret != -EAGAIN) {
				pr_err("Failed to create request, %d: %d\n",
				       context_index, ret);
				goto err_spin_rq;
			}
		} else {
			if (last)
				i915_request_put(last);
			last = rq;
		}
	}

	/* Release blocked requests */
	igt_spinner_end(&spin);
	ret = intel_selftest_wait_for_rq(spin_rq);
	if (ret) {
		pr_err("Spin request failed to complete: %d\n", ret);
		i915_request_put(last);
		goto err_spin_rq;
	}
	i915_request_put(spin_rq);
	igt_spinner_fini(&spin);
	spin_rq = NULL;

	/* Wait for last request */
	ret = i915_request_wait(last, 0, HZ * 30);
	i915_request_put(last);
	if (ret < 0) {
		pr_err("Last request failed to complete: %d\n", ret);
		goto err_spin_rq;
	}

	/* Try to steal guc_id */
	rq = nop_user_request(ce[context_index], NULL);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		pr_err("Failed to steal guc_id, %d: %d\n", context_index, ret);
		goto err_spin_rq;
	}

	/* Wait for request with stolen guc_id */
	ret = i915_request_wait(rq, 0, HZ);
	i915_request_put(rq);
	if (ret < 0) {
		pr_err("Request with stolen guc_id failed to complete: %d\n",
		       ret);
		goto err_spin_rq;
	}

	/* Wait for idle */
	ret = intel_gt_wait_for_idle(gt, HZ * 30);
	if (ret < 0) {
		pr_err("GT failed to idle: %d\n", ret);
		goto err_spin_rq;
	}

	/* Verify a guc_id was stolen */
	if (guc->number_guc_id_stolen == number_guc_id_stolen) {
		pr_err("No guc_id was stolen");
		ret = -EINVAL;
	} else {
		ret = 0;
	}

err_spin_rq:
	if (spin_rq) {
		igt_spinner_end(&spin);
		intel_selftest_wait_for_rq(spin_rq);
		i915_request_put(spin_rq);
		igt_spinner_fini(&spin);
		intel_gt_wait_for_idle(gt, HZ * 30);
	}
err_contexts:
	for (; context_index >= 0 && ce[context_index]; --context_index)
		intel_context_put(ce[context_index]);
err_wakeref:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	kfree(ce);
	guc->submission_state.num_guc_ids = sv;

	return ret;
}

int intel_guc_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(intel_guc_scrub_ctbs),
		SUBTEST(intel_guc_steal_guc_ids),
	};
	struct intel_gt *gt = to_gt(i915);

	if (intel_gt_is_wedged(gt))
		return 0;

	if (!intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
