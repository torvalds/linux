// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <linux/jiffies.h>

//#include "gt/intel_engine_user.h"
#include "gt/intel_gt.h"
#include "i915_drv.h"
#include "i915_selftest.h"

#include "selftests/intel_scheduler_helpers.h"

#define REDUCED_TIMESLICE	5
#define REDUCED_PREEMPT		10
#define WAIT_FOR_RESET_TIME_MS	10000

struct intel_engine_cs *intel_selftest_find_any_engine(struct intel_gt *gt)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	for_each_engine(engine, gt, id)
		return engine;

	pr_err("No valid engine found!\n");
	return NULL;
}

int intel_selftest_modify_policy(struct intel_engine_cs *engine,
				 struct intel_selftest_saved_policy *saved,
				 enum selftest_scheduler_modify modify_type)
{
	int err;

	saved->reset = engine->i915->params.reset;
	saved->flags = engine->flags;
	saved->timeslice = engine->props.timeslice_duration_ms;
	saved->preempt_timeout = engine->props.preempt_timeout_ms;

	switch (modify_type) {
	case SELFTEST_SCHEDULER_MODIFY_FAST_RESET:
		/*
		 * Enable force pre-emption on time slice expiration
		 * together with engine reset on pre-emption timeout.
		 * This is required to make the GuC notice and reset
		 * the single hanging context.
		 * Also, reduce the preemption timeout to something
		 * small to speed the test up.
		 */
		engine->i915->params.reset = 2;
		engine->flags |= I915_ENGINE_WANT_FORCED_PREEMPTION;
		engine->props.timeslice_duration_ms = REDUCED_TIMESLICE;
		engine->props.preempt_timeout_ms = REDUCED_PREEMPT;
		break;

	case SELFTEST_SCHEDULER_MODIFY_NO_HANGCHECK:
		engine->props.preempt_timeout_ms = 0;
		break;

	default:
		pr_err("Invalid scheduler policy modification type: %d!\n", modify_type);
		return -EINVAL;
	}

	if (!intel_engine_uses_guc(engine))
		return 0;

	err = intel_guc_global_policies_update(&engine->gt->uc.guc);
	if (err)
		intel_selftest_restore_policy(engine, saved);

	return err;
}

int intel_selftest_restore_policy(struct intel_engine_cs *engine,
				  struct intel_selftest_saved_policy *saved)
{
	/* Restore the original policies */
	engine->i915->params.reset = saved->reset;
	engine->flags = saved->flags;
	engine->props.timeslice_duration_ms = saved->timeslice;
	engine->props.preempt_timeout_ms = saved->preempt_timeout;

	if (!intel_engine_uses_guc(engine))
		return 0;

	return intel_guc_global_policies_update(&engine->gt->uc.guc);
}

int intel_selftest_wait_for_rq(struct i915_request *rq)
{
	long ret;

	ret = i915_request_wait(rq, 0, msecs_to_jiffies(WAIT_FOR_RESET_TIME_MS));
	if (ret < 0)
		return ret;

	return 0;
}
