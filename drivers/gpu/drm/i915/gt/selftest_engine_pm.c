/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright © 2018 Intel Corporation
 */

#include "i915_selftest.h"
#include "selftest_engine.h"
#include "selftest_engine_heartbeat.h"
#include "selftests/igt_atomic.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_spinner.h"

static int live_engine_busy_stats(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = 0;

	/*
	 * Check that if an engine supports busy-stats, they tell the truth.
	 */

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	GEM_BUG_ON(intel_gt_pm_is_awake(gt));
	for_each_engine(engine, gt, id) {
		struct i915_request *rq;
		ktime_t de, dt;
		ktime_t t[2];

		if (!intel_engine_supports_stats(engine))
			continue;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (intel_gt_pm_wait_for_idle(gt)) {
			err = -EBUSY;
			break;
		}

		st_engine_heartbeat_disable(engine);

		ENGINE_TRACE(engine, "measuring idle time\n");
		preempt_disable();
		de = intel_engine_get_busy_time(engine, &t[0]);
		udelay(100);
		de = ktime_sub(intel_engine_get_busy_time(engine, &t[1]), de);
		preempt_enable();
		dt = ktime_sub(t[1], t[0]);
		if (de < 0 || de > 10) {
			pr_err("%s: reported %lldns [%d%%] busyness while sleeping [for %lldns]\n",
			       engine->name,
			       de, (int)div64_u64(100 * de, dt), dt);
			GEM_TRACE_DUMP();
			err = -EINVAL;
			goto end;
		}

		/* 100% busy */
		rq = igt_spinner_create_request(&spin,
						engine->kernel_context,
						MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto end;
		}
		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			intel_gt_set_wedged(engine->gt);
			err = -ETIME;
			goto end;
		}

		ENGINE_TRACE(engine, "measuring busy time\n");
		preempt_disable();
		de = intel_engine_get_busy_time(engine, &t[0]);
		udelay(100);
		de = ktime_sub(intel_engine_get_busy_time(engine, &t[1]), de);
		preempt_enable();
		dt = ktime_sub(t[1], t[0]);
		if (100 * de < 95 * dt || 95 * de > 100 * dt) {
			pr_err("%s: reported %lldns [%d%%] busyness while spinning [for %lldns]\n",
			       engine->name,
			       de, (int)div64_u64(100 * de, dt), dt);
			GEM_TRACE_DUMP();
			err = -EINVAL;
			goto end;
		}

end:
		st_engine_heartbeat_enable(engine);
		igt_spinner_end(&spin);
		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			break;
	}

	igt_spinner_fini(&spin);
	if (igt_flush_test(gt->i915))
		err = -EIO;
	return err;
}

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
				intel_engine_pm_put_async(engine);
			intel_engine_pm_put_async(engine);
			p->critical_section_end();

			intel_engine_pm_flush(engine);

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
		SUBTEST(live_engine_busy_stats),
		SUBTEST(live_engine_pm),
	};

	return intel_gt_live_subtests(tests, gt);
}
