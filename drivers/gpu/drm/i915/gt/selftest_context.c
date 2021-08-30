// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_selftest.h"
#include "intel_engine_heartbeat.h"
#include "intel_engine_pm.h"
#include "intel_gt.h"

#include "gem/selftests/mock_context.h"
#include "selftests/igt_flush_test.h"
#include "selftests/mock_drm.h"

static int request_sync(struct i915_request *rq)
{
	struct intel_timeline *tl = i915_request_timeline(rq);
	long timeout;
	int err = 0;

	intel_timeline_get(tl);
	i915_request_get(rq);

	/* Opencode i915_request_add() so we can keep the timeline locked. */
	__i915_request_commit(rq);
	rq->sched.attr.priority = I915_PRIORITY_BARRIER;
	__i915_request_queue_bh(rq);

	timeout = i915_request_wait(rq, 0, HZ / 10);
	if (timeout < 0)
		err = timeout;
	else
		i915_request_retire_upto(rq);

	lockdep_unpin_lock(&tl->mutex, rq->cookie);
	mutex_unlock(&tl->mutex);

	i915_request_put(rq);
	intel_timeline_put(tl);

	return err;
}

static int context_sync(struct intel_context *ce)
{
	struct intel_timeline *tl = ce->timeline;
	int err = 0;

	mutex_lock(&tl->mutex);
	do {
		struct i915_request *rq;
		long timeout;

		if (list_empty(&tl->requests))
			break;

		rq = list_last_entry(&tl->requests, typeof(*rq), link);
		i915_request_get(rq);

		timeout = i915_request_wait(rq, 0, HZ / 10);
		if (timeout < 0)
			err = timeout;
		else
			i915_request_retire_upto(rq);

		i915_request_put(rq);
	} while (!err);
	mutex_unlock(&tl->mutex);

	/* Wait for all barriers to complete (remote CPU) before we check */
	i915_active_unlock_wait(&ce->active);
	return err;
}

static int __live_context_size(struct intel_engine_cs *engine)
{
	struct intel_context *ce;
	struct i915_request *rq;
	void *vaddr;
	int err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	err = intel_context_pin(ce);
	if (err)
		goto err;

	vaddr = i915_gem_object_pin_map_unlocked(ce->state->obj,
						 i915_coherent_map_type(engine->i915,
									ce->state->obj, false));
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		intel_context_unpin(ce);
		goto err;
	}

	/*
	 * Note that execlists also applies a redzone which it checks on
	 * context unpin when debugging. We are using the same location
	 * and same poison value so that our checks overlap. Despite the
	 * redundancy, we want to keep this little selftest so that we
	 * get coverage of any and all submission backends, and we can
	 * always extend this test to ensure we trick the HW into a
	 * compromising position wrt to the various sections that need
	 * to be written into the context state.
	 *
	 * TLDR; this overlaps with the execlists redzone.
	 */
	vaddr += engine->context_size - I915_GTT_PAGE_SIZE;
	memset(vaddr, POISON_INUSE, I915_GTT_PAGE_SIZE);

	rq = intel_context_create_request(ce);
	intel_context_unpin(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	err = request_sync(rq);
	if (err)
		goto err_unpin;

	/* Force the context switch */
	rq = intel_engine_create_kernel_request(engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}
	err = request_sync(rq);
	if (err)
		goto err_unpin;

	if (memchr_inv(vaddr, POISON_INUSE, I915_GTT_PAGE_SIZE)) {
		pr_err("%s context overwrote trailing red-zone!", engine->name);
		err = -EINVAL;
	}

err_unpin:
	i915_gem_object_unpin_map(ce->state->obj);
err:
	intel_context_put(ce);
	return err;
}

static int live_context_size(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check that our context sizes are correct by seeing if the
	 * HW tries to write past the end of one.
	 */

	for_each_engine(engine, gt, id) {
		struct file *saved;

		if (!engine->context_size)
			continue;

		intel_engine_pm_get(engine);

		/*
		 * Hide the old default state -- we lie about the context size
		 * and get confused when the default state is smaller than
		 * expected. For our do nothing request, inheriting the
		 * active state is sufficient, we are only checking that we
		 * don't use more than we planned.
		 */
		saved = fetch_and_zero(&engine->default_state);

		/* Overlaps with the execlists redzone */
		engine->context_size += I915_GTT_PAGE_SIZE;

		err = __live_context_size(engine);

		engine->context_size -= I915_GTT_PAGE_SIZE;

		engine->default_state = saved;

		intel_engine_pm_put(engine);

		if (err)
			break;
	}

	return err;
}

static int __live_active_context(struct intel_engine_cs *engine)
{
	unsigned long saved_heartbeat;
	struct intel_context *ce;
	int pass;
	int err;

	/*
	 * We keep active contexts alive until after a subsequent context
	 * switch as the final write from the context-save will be after
	 * we retire the final request. We track when we unpin the context,
	 * under the presumption that the final pin is from the last request,
	 * and instead of immediately unpinning the context, we add a task
	 * to unpin the context from the next idle-barrier.
	 *
	 * This test makes sure that the context is kept alive until a
	 * subsequent idle-barrier (emitted when the engine wakeref hits 0
	 * with no more outstanding requests).
	 */

	if (intel_engine_pm_is_awake(engine)) {
		pr_err("%s is awake before starting %s!\n",
		       engine->name, __func__);
		return -EINVAL;
	}

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	saved_heartbeat = engine->props.heartbeat_interval_ms;
	engine->props.heartbeat_interval_ms = 0;

	for (pass = 0; pass <= 2; pass++) {
		struct i915_request *rq;

		intel_engine_pm_get(engine);

		rq = intel_context_create_request(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_engine;
		}

		err = request_sync(rq);
		if (err)
			goto out_engine;

		/* Context will be kept active until after an idle-barrier. */
		if (i915_active_is_idle(&ce->active)) {
			pr_err("context is not active; expected idle-barrier (%s pass %d)\n",
			       engine->name, pass);
			err = -EINVAL;
			goto out_engine;
		}

		if (!intel_engine_pm_is_awake(engine)) {
			pr_err("%s is asleep before idle-barrier\n",
			       engine->name);
			err = -EINVAL;
			goto out_engine;
		}

out_engine:
		intel_engine_pm_put(engine);
		if (err)
			goto err;
	}

	/* Now make sure our idle-barriers are flushed */
	err = intel_engine_flush_barriers(engine);
	if (err)
		goto err;

	/* Wait for the barrier and in the process wait for engine to park */
	err = context_sync(engine->kernel_context);
	if (err)
		goto err;

	if (!i915_active_is_idle(&ce->active)) {
		pr_err("context is still active!");
		err = -EINVAL;
	}

	intel_engine_pm_flush(engine);

	if (intel_engine_pm_is_awake(engine)) {
		struct drm_printer p = drm_debug_printer(__func__);

		intel_engine_dump(engine, &p,
				  "%s is still awake:%d after idle-barriers\n",
				  engine->name,
				  atomic_read(&engine->wakeref.count));
		GEM_TRACE_DUMP();

		err = -EINVAL;
		goto err;
	}

err:
	engine->props.heartbeat_interval_ms = saved_heartbeat;
	intel_context_put(ce);
	return err;
}

static int live_active_context(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	for_each_engine(engine, gt, id) {
		err = __live_active_context(engine);
		if (err)
			break;

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	}

	return err;
}

static int __remote_sync(struct intel_context *ce, struct intel_context *remote)
{
	struct i915_request *rq;
	int err;

	err = intel_context_pin(remote);
	if (err)
		return err;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto unpin;
	}

	err = intel_context_prepare_remote_request(remote, rq);
	if (err) {
		i915_request_add(rq);
		goto unpin;
	}

	err = request_sync(rq);

unpin:
	intel_context_unpin(remote);
	return err;
}

static int __live_remote_context(struct intel_engine_cs *engine)
{
	struct intel_context *local, *remote;
	unsigned long saved_heartbeat;
	int pass;
	int err;

	/*
	 * Check that our idle barriers do not interfere with normal
	 * activity tracking. In particular, check that operating
	 * on the context image remotely (intel_context_prepare_remote_request),
	 * which inserts foreign fences into intel_context.active, does not
	 * clobber the idle-barrier.
	 */

	if (intel_engine_pm_is_awake(engine)) {
		pr_err("%s is awake before starting %s!\n",
		       engine->name, __func__);
		return -EINVAL;
	}

	remote = intel_context_create(engine);
	if (IS_ERR(remote))
		return PTR_ERR(remote);

	local = intel_context_create(engine);
	if (IS_ERR(local)) {
		err = PTR_ERR(local);
		goto err_remote;
	}

	saved_heartbeat = engine->props.heartbeat_interval_ms;
	engine->props.heartbeat_interval_ms = 0;
	intel_engine_pm_get(engine);

	for (pass = 0; pass <= 2; pass++) {
		err = __remote_sync(local, remote);
		if (err)
			break;

		err = __remote_sync(engine->kernel_context, remote);
		if (err)
			break;

		if (i915_active_is_idle(&remote->active)) {
			pr_err("remote context is not active; expected idle-barrier (%s pass %d)\n",
			       engine->name, pass);
			err = -EINVAL;
			break;
		}
	}

	intel_engine_pm_put(engine);
	engine->props.heartbeat_interval_ms = saved_heartbeat;

	intel_context_put(local);
err_remote:
	intel_context_put(remote);
	return err;
}

static int live_remote_context(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	for_each_engine(engine, gt, id) {
		err = __live_remote_context(engine);
		if (err)
			break;

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	}

	return err;
}

int intel_context_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_context_size),
		SUBTEST(live_active_context),
		SUBTEST(live_remote_context),
	};
	struct intel_gt *gt = &i915->gt;

	if (intel_gt_is_wedged(gt))
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
