// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#define NUM_STEPS 5
#define H2G_DELAY 50000
#define delay_for_h2g() usleep_range(H2G_DELAY, H2G_DELAY + 10000)
#define FREQUENCY_REQ_UNIT	DIV_ROUND_CLOSEST(GT_FREQUENCY_MULTIPLIER, \
						  GEN9_FREQ_SCALER)
enum test_type {
	VARY_MIN,
	VARY_MAX,
	MAX_GRANTED
};

static int slpc_set_min_freq(struct intel_guc_slpc *slpc, u32 freq)
{
	int ret;

	ret = intel_guc_slpc_set_min_freq(slpc, freq);
	if (ret)
		pr_err("Could not set min frequency to [%u]\n", freq);
	else /* Delay to ensure h2g completes */
		delay_for_h2g();

	return ret;
}

static int slpc_set_max_freq(struct intel_guc_slpc *slpc, u32 freq)
{
	int ret;

	ret = intel_guc_slpc_set_max_freq(slpc, freq);
	if (ret)
		pr_err("Could not set maximum frequency [%u]\n",
		       freq);
	else /* Delay to ensure h2g completes */
		delay_for_h2g();

	return ret;
}

static int vary_max_freq(struct intel_guc_slpc *slpc, struct intel_rps *rps,
			 u32 *max_act_freq)
{
	u32 step, max_freq, req_freq;
	u32 act_freq;
	int err = 0;

	/* Go from max to min in 5 steps */
	step = (slpc->rp0_freq - slpc->min_freq) / NUM_STEPS;
	*max_act_freq = slpc->min_freq;
	for (max_freq = slpc->rp0_freq; max_freq > slpc->min_freq;
				max_freq -= step) {
		err = slpc_set_max_freq(slpc, max_freq);
		if (err)
			break;

		req_freq = intel_rps_read_punit_req_frequency(rps);

		/* GuC requests freq in multiples of 50/3 MHz */
		if (req_freq > (max_freq + FREQUENCY_REQ_UNIT)) {
			pr_err("SWReq is %d, should be at most %d\n", req_freq,
			       max_freq + FREQUENCY_REQ_UNIT);
			err = -EINVAL;
		}

		act_freq =  intel_rps_read_actual_frequency(rps);
		if (act_freq > *max_act_freq)
			*max_act_freq = act_freq;

		if (err)
			break;
	}

	return err;
}

static int vary_min_freq(struct intel_guc_slpc *slpc, struct intel_rps *rps,
			 u32 *max_act_freq)
{
	u32 step, min_freq, req_freq;
	u32 act_freq;
	int err = 0;

	/* Go from min to max in 5 steps */
	step = (slpc->rp0_freq - slpc->min_freq) / NUM_STEPS;
	*max_act_freq = slpc->min_freq;
	for (min_freq = slpc->min_freq; min_freq < slpc->rp0_freq;
				min_freq += step) {
		err = slpc_set_min_freq(slpc, min_freq);
		if (err)
			break;

		req_freq = intel_rps_read_punit_req_frequency(rps);

		/* GuC requests freq in multiples of 50/3 MHz */
		if (req_freq < (min_freq - FREQUENCY_REQ_UNIT)) {
			pr_err("SWReq is %d, should be at least %d\n", req_freq,
			       min_freq - FREQUENCY_REQ_UNIT);
			err = -EINVAL;
		}

		act_freq =  intel_rps_read_actual_frequency(rps);
		if (act_freq > *max_act_freq)
			*max_act_freq = act_freq;

		if (err)
			break;
	}

	return err;
}

static int max_granted_freq(struct intel_guc_slpc *slpc, struct intel_rps *rps, u32 *max_act_freq)
{
	struct intel_gt *gt = rps_to_gt(rps);
	u32 perf_limit_reasons;
	int err = 0;

	err = slpc_set_min_freq(slpc, slpc->rp0_freq);
	if (err)
		return err;

	*max_act_freq =  intel_rps_read_actual_frequency(rps);
	if (*max_act_freq != slpc->rp0_freq) {
		/* Check if there was some throttling by pcode */
		perf_limit_reasons = intel_uncore_read(gt->uncore, GT0_PERF_LIMIT_REASONS);

		/* If not, this is an error */
		if (!(perf_limit_reasons & GT0_PERF_LIMIT_REASONS_MASK)) {
			pr_err("Pcode did not grant max freq\n");
			err = -EINVAL;
		} else {
			pr_info("Pcode throttled frequency 0x%x\n", perf_limit_reasons);
		}
	}

	return err;
}

static int run_test(struct intel_gt *gt, int test_type)
{
	struct intel_guc_slpc *slpc = &gt->uc.guc.slpc;
	struct intel_rps *rps = &gt->rps;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	u32 slpc_min_freq, slpc_max_freq;
	int err = 0;

	if (!intel_uc_uses_guc_slpc(&gt->uc))
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	if (intel_guc_slpc_get_max_freq(slpc, &slpc_max_freq)) {
		pr_err("Could not get SLPC max freq\n");
		return -EIO;
	}

	if (intel_guc_slpc_get_min_freq(slpc, &slpc_min_freq)) {
		pr_err("Could not get SLPC min freq\n");
		return -EIO;
	}

	if (slpc->min_freq == slpc->rp0_freq) {
		pr_err("Min/Max are fused to the same value\n");
		return -EINVAL;
	}

	intel_gt_pm_wait_for_idle(gt);
	intel_gt_pm_get(gt);
	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		u32 max_act_freq;

		if (!intel_engine_can_store_dword(engine))
			continue;

		st_engine_heartbeat_disable(engine);

		rq = igt_spinner_create_request(&spin,
						engine->kernel_context,
						MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			st_engine_heartbeat_enable(engine);
			break;
		}

		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			pr_err("%s: Spinner did not start\n",
			       engine->name);
			igt_spinner_end(&spin);
			st_engine_heartbeat_enable(engine);
			intel_gt_set_wedged(engine->gt);
			err = -EIO;
			break;
		}

		switch (test_type) {
		case VARY_MIN:
			err = vary_min_freq(slpc, rps, &max_act_freq);
			break;

		case VARY_MAX:
			err = vary_max_freq(slpc, rps, &max_act_freq);
			break;

		case MAX_GRANTED:
			/* Media engines have a different RP0 */
			if (engine->class == VIDEO_DECODE_CLASS ||
			    engine->class == VIDEO_ENHANCEMENT_CLASS) {
				igt_spinner_end(&spin);
				st_engine_heartbeat_enable(engine);
				err = 0;
				continue;
			}

			err = max_granted_freq(slpc, rps, &max_act_freq);
			break;
		}

		pr_info("Max actual frequency for %s was %d\n",
			engine->name, max_act_freq);

		/* Actual frequency should rise above min */
		if (max_act_freq <= slpc_min_freq) {
			pr_err("Actual freq did not rise above min\n");
			pr_err("Perf Limit Reasons: 0x%x\n",
			       intel_uncore_read(gt->uncore, GT0_PERF_LIMIT_REASONS));
			err = -EINVAL;
		}

		igt_spinner_end(&spin);
		st_engine_heartbeat_enable(engine);

		if (err)
			break;
	}

	/* Restore min/max frequencies */
	slpc_set_max_freq(slpc, slpc_max_freq);
	slpc_set_min_freq(slpc, slpc_min_freq);

	if (igt_flush_test(gt->i915))
		err = -EIO;

	intel_gt_pm_put(gt);
	igt_spinner_fini(&spin);
	intel_gt_pm_wait_for_idle(gt);

	return err;
}

static int live_slpc_vary_min(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_gt *gt = to_gt(i915);

	return run_test(gt, VARY_MIN);
}

static int live_slpc_vary_max(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_gt *gt = to_gt(i915);

	return run_test(gt, VARY_MAX);
}

/* check if pcode can grant RP0 */
static int live_slpc_max_granted(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_gt *gt = to_gt(i915);

	return run_test(gt, MAX_GRANTED);
}

int intel_slpc_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_slpc_vary_max),
		SUBTEST(live_slpc_vary_min),
		SUBTEST(live_slpc_max_granted),
	};

	if (intel_gt_is_wedged(to_gt(i915)))
		return 0;

	return i915_live_subtests(tests, i915);
}
