// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "gt/intel_gt_print.h"
#include "selftests/igt_spinner.h"
#include "selftests/igt_reset.h"
#include "selftests/intel_scheduler_helpers.h"
#include "gt/intel_engine_heartbeat.h"
#include "gem/selftests/mock_context.h"

#define BEAT_INTERVAL	100

static struct i915_request *nop_request(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	rq = intel_engine_create_kernel_request(engine);
	if (IS_ERR(rq))
		return rq;

	i915_request_get(rq);
	i915_request_add(rq);

	return rq;
}

static int intel_hang_guc(void *arg)
{
	struct intel_gt *gt = arg;
	int ret = 0;
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	struct igt_spinner spin;
	struct i915_request *rq;
	intel_wakeref_t wakeref;
	struct i915_gpu_error *global = &gt->i915->gpu_error;
	struct intel_engine_cs *engine = intel_selftest_find_any_engine(gt);
	unsigned int reset_count;
	u32 guc_status;
	u32 old_beat;

	if (!engine)
		return 0;

	ctx = kernel_context(gt->i915, NULL);
	if (IS_ERR(ctx)) {
		gt_err(gt, "Failed get kernel context: %pe\n", ctx);
		return PTR_ERR(ctx);
	}

	wakeref = intel_runtime_pm_get(gt->uncore->rpm);

	ce = intel_context_create(engine);
	if (IS_ERR(ce)) {
		ret = PTR_ERR(ce);
		gt_err(gt, "Failed to create spinner request: %pe\n", ce);
		goto err;
	}

	reset_count = i915_reset_count(global);

	old_beat = engine->props.heartbeat_interval_ms;
	ret = intel_engine_set_heartbeat(engine, BEAT_INTERVAL);
	if (ret) {
		gt_err(gt, "Failed to boost heatbeat interval: %pe\n", ERR_PTR(ret));
		goto err;
	}

	ret = igt_spinner_init(&spin, engine->gt);
	if (ret) {
		gt_err(gt, "Failed to create spinner: %pe\n", ERR_PTR(ret));
		goto err;
	}

	rq = igt_spinner_create_request(&spin, ce, MI_NOOP);
	intel_context_put(ce);
	if (IS_ERR(rq)) {
		ret = PTR_ERR(rq);
		gt_err(gt, "Failed to create spinner request: %pe\n", rq);
		goto err_spin;
	}

	ret = request_add_spin(rq, &spin);
	if (ret) {
		i915_request_put(rq);
		gt_err(gt, "Failed to add Spinner request: %pe\n", ERR_PTR(ret));
		goto err_spin;
	}

	ret = intel_reset_guc(gt);
	if (ret) {
		i915_request_put(rq);
		gt_err(gt, "Failed to reset GuC: %pe\n", ERR_PTR(ret));
		goto err_spin;
	}

	guc_status = intel_uncore_read(gt->uncore, GUC_STATUS);
	if (!(guc_status & GS_MIA_IN_RESET)) {
		i915_request_put(rq);
		gt_err(gt, "Failed to reset GuC: status = 0x%08X\n", guc_status);
		ret = -EIO;
		goto err_spin;
	}

	/* Wait for the heartbeat to cause a reset */
	ret = intel_selftest_wait_for_rq(rq);
	i915_request_put(rq);
	if (ret) {
		gt_err(gt, "Request failed to complete: %pe\n", ERR_PTR(ret));
		goto err_spin;
	}

	if (i915_reset_count(global) == reset_count) {
		gt_err(gt, "Failed to record a GPU reset\n");
		ret = -EINVAL;
		goto err_spin;
	}

err_spin:
	igt_spinner_end(&spin);
	igt_spinner_fini(&spin);
	intel_engine_set_heartbeat(engine, old_beat);

	if (ret == 0) {
		rq = nop_request(engine);
		if (IS_ERR(rq)) {
			ret = PTR_ERR(rq);
			goto err;
		}

		ret = intel_selftest_wait_for_rq(rq);
		i915_request_put(rq);
		if (ret) {
			gt_err(gt, "No-op failed to complete: %pe\n", ERR_PTR(ret));
			goto err;
		}
	}

err:
	intel_runtime_pm_put(gt->uncore->rpm, wakeref);
	kernel_context_close(ctx);

	return ret;
}

int intel_guc_hang_check(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(intel_hang_guc),
	};
	struct intel_gt *gt = to_gt(i915);

	if (intel_gt_is_wedged(gt))
		return 0;

	if (!intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
