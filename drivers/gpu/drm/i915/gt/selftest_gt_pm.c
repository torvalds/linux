// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include <linux/sort.h>

#include "intel_gt_clock_utils.h"

#include "selftest_llc.h"
#include "selftest_rc6.h"
#include "selftest_rps.h"

static int cmp_u64(const void *A, const void *B)
{
	const u64 *a = A, *b = B;

	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

static int cmp_u32(const void *A, const void *B)
{
	const u32 *a = A, *b = B;

	if (a < b)
		return -1;
	else if (a > b)
		return 1;
	else
		return 0;
}

static void measure_clocks(struct intel_engine_cs *engine,
			   u32 *out_cycles, ktime_t *out_dt)
{
	ktime_t dt[5];
	u32 cycles[5];
	int i;

	for (i = 0; i < 5; i++) {
		preempt_disable();
		cycles[i] = -ENGINE_READ_FW(engine, RING_TIMESTAMP);
		dt[i] = ktime_get();

		udelay(1000);

		dt[i] = ktime_sub(ktime_get(), dt[i]);
		cycles[i] += ENGINE_READ_FW(engine, RING_TIMESTAMP);
		preempt_enable();
	}

	/* Use the median of both cycle/dt; close enough */
	sort(cycles, 5, sizeof(*cycles), cmp_u32, NULL);
	*out_cycles = (cycles[1] + 2 * cycles[2] + cycles[3]) / 4;

	sort(dt, 5, sizeof(*dt), cmp_u64, NULL);
	*out_dt = div_u64(dt[1] + 2 * dt[2] + dt[3], 4);
}

static int live_gt_clocks(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	if (!gt->clock_frequency) { /* unknown */
		pr_info("CS_TIMESTAMP frequency unknown\n");
		return 0;
	}

	if (INTEL_GEN(gt->i915) < 4) /* Any CS_TIMESTAMP? */
		return 0;

	if (IS_GEN(gt->i915, 5))
		/*
		 * XXX CS_TIMESTAMP low dword is dysfunctional?
		 *
		 * Ville's experiments indicate the high dword still works,
		 * but at a correspondingly reduced frequency.
		 */
		return 0;

	if (IS_GEN(gt->i915, 4))
		/*
		 * XXX CS_TIMESTAMP appears gibberish
		 *
		 * Ville's experiments indicate that it mostly appears 'stuck'
		 * in that we see the register report the same cycle count
		 * for a couple of reads.
		 */
		return 0;

	intel_gt_pm_get(gt);
	intel_uncore_forcewake_get(gt->uncore, FORCEWAKE_ALL);

	for_each_engine(engine, gt, id) {
		u32 cycles;
		u32 expected;
		u64 time;
		u64 dt;

		if (INTEL_GEN(engine->i915) < 7 && engine->id != RCS0)
			continue;

		measure_clocks(engine, &cycles, &dt);

		time = intel_gt_clock_interval_to_ns(engine->gt, cycles);
		expected = intel_gt_ns_to_clock_interval(engine->gt, dt);

		pr_info("%s: TIMESTAMP %d cycles [%lldns] in %lldns [%d cycles], using CS clock frequency of %uKHz\n",
			engine->name, cycles, time, dt, expected,
			engine->gt->clock_frequency / 1000);

		if (9 * time < 8 * dt || 8 * time > 9 * dt) {
			pr_err("%s: CS ticks did not match walltime!\n",
			       engine->name);
			err = -EINVAL;
			break;
		}

		if (9 * expected < 8 * cycles || 8 * expected > 9 * cycles) {
			pr_err("%s: walltime did not match CS ticks!\n",
			       engine->name);
			err = -EINVAL;
			break;
		}
	}

	intel_uncore_forcewake_put(gt->uncore, FORCEWAKE_ALL);
	intel_gt_pm_put(gt);

	return err;
}

static int live_gt_resume(void *arg)
{
	struct intel_gt *gt = arg;
	IGT_TIMEOUT(end_time);
	int err;

	/* Do several suspend/resume cycles to check we don't explode! */
	do {
		intel_gt_suspend_prepare(gt);
		intel_gt_suspend_late(gt);

		if (gt->rc6.enabled) {
			pr_err("rc6 still enabled after suspend!\n");
			intel_gt_set_wedged_on_init(gt);
			err = -EINVAL;
			break;
		}

		err = intel_gt_resume(gt);
		if (err)
			break;

		if (gt->rc6.supported && !gt->rc6.enabled) {
			pr_err("rc6 not enabled upon resume!\n");
			intel_gt_set_wedged_on_init(gt);
			err = -EINVAL;
			break;
		}

		err = st_llc_verify(&gt->llc);
		if (err) {
			pr_err("llc state not restored upon resume!\n");
			intel_gt_set_wedged_on_init(gt);
			break;
		}
	} while (!__igt_timeout(end_time, NULL));

	return err;
}

int intel_gt_pm_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_gt_clocks),
		SUBTEST(live_rc6_manual),
		SUBTEST(live_rps_clock_interval),
		SUBTEST(live_rps_control),
		SUBTEST(live_rps_frequency_cs),
		SUBTEST(live_rps_frequency_srm),
		SUBTEST(live_rps_power),
		SUBTEST(live_rps_interrupt),
		SUBTEST(live_rps_dynamic),
		SUBTEST(live_gt_resume),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}

int intel_gt_pm_late_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		/*
		 * These tests may leave the system in an undesirable state.
		 * They are intended to be run last in CI and the system
		 * rebooted afterwards.
		 */
		SUBTEST(live_rc6_ctx_wa),
	};

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
