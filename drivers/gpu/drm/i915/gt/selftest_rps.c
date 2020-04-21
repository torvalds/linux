// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include "intel_engine_pm.h"
#include "intel_gt_pm.h"
#include "intel_rc6.h"
#include "selftest_rps.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_spinner.h"

static void dummy_rps_work(struct work_struct *wrk)
{
}

static int __rps_up_interrupt(struct intel_rps *rps,
			      struct intel_engine_cs *engine,
			      struct igt_spinner *spin)
{
	struct intel_uncore *uncore = engine->uncore;
	struct i915_request *rq;
	u32 timeout;

	if (!intel_engine_can_store_dword(engine))
		return 0;

	intel_gt_pm_wait_for_idle(engine->gt);
	GEM_BUG_ON(rps->active);

	rps->pm_iir = 0;
	rps->cur_freq = rps->min_freq;

	rq = igt_spinner_create_request(spin, engine->kernel_context, MI_NOOP);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	i915_request_get(rq);
	i915_request_add(rq);

	if (!igt_wait_for_spinner(spin, rq)) {
		pr_err("%s: RPS spinner did not start\n",
		       engine->name);
		i915_request_put(rq);
		intel_gt_set_wedged(engine->gt);
		return -EIO;
	}

	if (!rps->active) {
		pr_err("%s: RPS not enabled on starting spinner\n",
		       engine->name);
		igt_spinner_end(spin);
		i915_request_put(rq);
		return -EINVAL;
	}

	if (!(rps->pm_events & GEN6_PM_RP_UP_THRESHOLD)) {
		pr_err("%s: RPS did not register UP interrupt\n",
		       engine->name);
		i915_request_put(rq);
		return -EINVAL;
	}

	if (rps->last_freq != rps->min_freq) {
		pr_err("%s: RPS did not program min frequency\n",
		       engine->name);
		i915_request_put(rq);
		return -EINVAL;
	}

	timeout = intel_uncore_read(uncore, GEN6_RP_UP_EI);
	timeout = GT_PM_INTERVAL_TO_US(engine->i915, timeout);

	usleep_range(2 * timeout, 3 * timeout);
	GEM_BUG_ON(i915_request_completed(rq));

	igt_spinner_end(spin);
	i915_request_put(rq);

	if (rps->cur_freq != rps->min_freq) {
		pr_err("%s: Frequency unexpectedly changed [up], now %d!\n",
		       engine->name, intel_rps_read_actual_frequency(rps));
		return -EINVAL;
	}

	if (!(rps->pm_iir & GEN6_PM_RP_UP_THRESHOLD)) {
		pr_err("%s: UP interrupt not recorded for spinner, pm_iir:%x, prev_up:%x, up_threshold:%x, up_ei:%x\n",
		       engine->name, rps->pm_iir,
		       intel_uncore_read(uncore, GEN6_RP_PREV_UP),
		       intel_uncore_read(uncore, GEN6_RP_UP_THRESHOLD),
		       intel_uncore_read(uncore, GEN6_RP_UP_EI));
		return -EINVAL;
	}

	intel_gt_pm_wait_for_idle(engine->gt);
	return 0;
}

static int __rps_down_interrupt(struct intel_rps *rps,
				struct intel_engine_cs *engine)
{
	struct intel_uncore *uncore = engine->uncore;
	u32 timeout;

	mutex_lock(&rps->lock);
	GEM_BUG_ON(!rps->active);
	intel_rps_set(rps, rps->max_freq);
	mutex_unlock(&rps->lock);

	if (!(rps->pm_events & GEN6_PM_RP_DOWN_THRESHOLD)) {
		pr_err("%s: RPS did not register DOWN interrupt\n",
		       engine->name);
		return -EINVAL;
	}

	if (rps->last_freq != rps->max_freq) {
		pr_err("%s: RPS did not program max frequency\n",
		       engine->name);
		return -EINVAL;
	}

	timeout = intel_uncore_read(uncore, GEN6_RP_DOWN_EI);
	timeout = GT_PM_INTERVAL_TO_US(engine->i915, timeout);

	/* Flush any previous EI */
	usleep_range(timeout, 2 * timeout);

	/* Reset the interrupt status */
	rps_disable_interrupts(rps);
	GEM_BUG_ON(rps->pm_iir);
	rps_enable_interrupts(rps);

	/* And then wait for the timeout, for real this time */
	usleep_range(2 * timeout, 3 * timeout);

	if (rps->cur_freq != rps->max_freq) {
		pr_err("%s: Frequency unexpectedly changed [down], now %d!\n",
		       engine->name,
		       intel_rps_read_actual_frequency(rps));
		return -EINVAL;
	}

	if (!(rps->pm_iir & (GEN6_PM_RP_DOWN_THRESHOLD | GEN6_PM_RP_DOWN_TIMEOUT))) {
		pr_err("%s: DOWN interrupt not recorded for idle, pm_iir:%x, prev_down:%x, down_threshold:%x, down_ei:%x [prev_up:%x, up_threshold:%x, up_ei:%x]\n",
		       engine->name, rps->pm_iir,
		       intel_uncore_read(uncore, GEN6_RP_PREV_DOWN),
		       intel_uncore_read(uncore, GEN6_RP_DOWN_THRESHOLD),
		       intel_uncore_read(uncore, GEN6_RP_DOWN_EI),
		       intel_uncore_read(uncore, GEN6_RP_PREV_UP),
		       intel_uncore_read(uncore, GEN6_RP_UP_THRESHOLD),
		       intel_uncore_read(uncore, GEN6_RP_UP_EI));
		return -EINVAL;
	}

	return 0;
}

int live_rps_interrupt(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_rps *rps = &gt->rps;
	void (*saved_work)(struct work_struct *wrk);
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	u32 pm_events;
	int err = 0;

	/*
	 * First, let's check whether or not we are receiving interrupts.
	 */

	if (!rps->enabled || rps->max_freq <= rps->min_freq)
		return 0;

	intel_gt_pm_get(gt);
	pm_events = rps->pm_events;
	intel_gt_pm_put(gt);
	if (!pm_events) {
		pr_err("No RPS PM events registered, but RPS is enabled?\n");
		return -ENODEV;
	}

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	intel_gt_pm_wait_for_idle(gt);
	saved_work = rps->work.func;
	rps->work.func = dummy_rps_work;

	for_each_engine(engine, gt, id) {
		/* Keep the engine busy with a spinner; expect an UP! */
		if (pm_events & GEN6_PM_RP_UP_THRESHOLD) {
			err = __rps_up_interrupt(rps, engine, &spin);
			if (err)
				goto out;
		}

		/* Keep the engine awake but idle and check for DOWN */
		if (pm_events & GEN6_PM_RP_DOWN_THRESHOLD) {
			intel_engine_pm_get(engine);
			intel_rc6_disable(&gt->rc6);

			err = __rps_down_interrupt(rps, engine);

			intel_rc6_enable(&gt->rc6);
			intel_engine_pm_put(engine);
			if (err)
				goto out;
		}
	}

out:
	if (igt_flush_test(gt->i915))
		err = -EIO;

	igt_spinner_fini(&spin);

	intel_gt_pm_wait_for_idle(gt);
	rps->work.func = saved_work;

	return err;
}
