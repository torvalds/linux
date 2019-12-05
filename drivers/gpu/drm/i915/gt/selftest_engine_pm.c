/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright © 2018 Intel Corporation
 */

#include "i915_selftest.h"
#include "selftest_engine.h"
#include "selftests/igt_atomic.h"

static int live_engine_pm(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * Check we can call intel_engine_pm_put from any context. No
	 * failures are reported directly, but if we mess up lockdep should
	 * tell us.
	 */
	if (intel_gt_pm_wait_for_idle(gt)) {
		pr_err("Unable to flush GT pm before test\n");
		return -EBUSY;
	}

	GEM_BUG_ON(intel_gt_pm_is_awake(gt));
	for_each_engine(engine, gt, id) {
		const typeof(*igt_atomic_phases) *p;

		for (p = igt_atomic_phases; p->name; p++) {
			/*
			 * Acquisition is always synchronous, except if we
			 * know that the engine is already awake, in which
			 * case we should use intel_engine_pm_get_if_awake()
			 * to atomically grab the wakeref.
			 *
			 * In practice,
			 *    intel_engine_pm_get();
			 *    intel_engine_pm_put();
			 * occurs in one thread, while simultaneously
			 *    intel_engine_pm_get_if_awake();
			 *    intel_engine_pm_put();
			 * occurs from atomic context in another.
			 */
			GEM_BUG_ON(intel_engine_pm_is_awake(engine));
			intel_engine_pm_get(engine);

			p->critical_section_begin();
			if (!intel_engine_pm_get_if_awake(engine))
				pr_err("intel_engine_pm_get_if_awake(%s) failed under %s\n",
				       engine->name, p->name);
			else
				intel_engine_pm_put(engine);
			intel_engine_pm_put(engine);
			p->critical_section_end();

			/* engine wakeref is sync (instant) */
			if (intel_engine_pm_is_awake(engine)) {
				pr_err("%s is still awake after flushing pm\n",
				       engine->name);
				return -EINVAL;
			}

			/* gt wakeref is async (deferred to workqueue) */
			if (intel_gt_pm_wait_for_idle(gt)) {
				pr_err("GT failed to idle\n");
				return -EINVAL;
			}
		}
	}

	return 0;
}

int live_engine_pm_selftests(struct intel_gt *gt)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_engine_pm),
	};

	return intel_gt_live_subtests(tests, gt);
}
