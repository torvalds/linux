/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_selftest.h"
#include "intel_gt.h"

#include "gem/selftests/mock_context.h"
#include "selftests/igt_flush_test.h"
#include "selftests/mock_drm.h"

static int request_sync(struct i915_request *rq)
{
	long timeout;
	int err = 0;

	i915_request_get(rq);

	i915_request_add(rq);
	timeout = i915_request_wait(rq, 0, HZ / 10);
	if (timeout < 0)
		err = timeout;
	else
		i915_request_retire_upto(rq);

	i915_request_put(rq);

	return err;
}

static int context_sync(struct intel_context *ce)
{
	struct intel_timeline *tl = ce->ring->timeline;
	int err = 0;

	do {
		struct i915_request *rq;
		long timeout;

		rcu_read_lock();
		rq = rcu_dereference(tl->last_request.request);
		if (rq)
			rq = i915_request_get_rcu(rq);
		rcu_read_unlock();
		if (!rq)
			break;

		timeout = i915_request_wait(rq, 0, HZ / 10);
		if (timeout < 0)
			err = timeout;
		else
			i915_request_retire_upto(rq);

		i915_request_put(rq);
	} while (!err);

	return err;
}

static int __live_active_context(struct intel_engine_cs *engine,
				 struct i915_gem_context *fixme)
{
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

	ce = intel_context_create(fixme, engine);
	if (!ce)
		return -ENOMEM;

	for (pass = 0; pass <= 2; pass++) {
		struct i915_request *rq;

		rq = intel_context_create_request(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err;
		}

		err = request_sync(rq);
		if (err)
			goto err;

		/* Context will be kept active until after an idle-barrier. */
		if (i915_active_is_idle(&ce->active)) {
			pr_err("context is not active; expected idle-barrier (%s pass %d)\n",
			       engine->name, pass);
			err = -EINVAL;
			goto err;
		}

		if (!intel_engine_pm_is_awake(engine)) {
			pr_err("%s is asleep before idle-barrier\n",
			       engine->name);
			err = -EINVAL;
			goto err;
		}
	}

	/* Now make sure our idle-barriers are flushed */
	err = context_sync(engine->kernel_context);
	if (err)
		goto err;

	if (!i915_active_is_idle(&ce->active)) {
		pr_err("context is still active!");
		err = -EINVAL;
	}

	if (intel_engine_pm_is_awake(engine)) {
		struct drm_printer p = drm_debug_printer(__func__);

		intel_engine_dump(engine, &p,
				  "%s is still awake after idle-barriers\n",
				  engine->name);
		GEM_TRACE_DUMP();

		err = -EINVAL;
		goto err;
	}

err:
	intel_context_put(ce);
	return err;
}

static int live_active_context(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *fixme;
	enum intel_engine_id id;
	struct drm_file *file;
	int err = 0;

	file = mock_file(gt->i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&gt->i915->drm.struct_mutex);

	fixme = live_context(gt->i915, file);
	if (!fixme) {
		err = -ENOMEM;
		goto unlock;
	}

	for_each_engine(engine, gt->i915, id) {
		err = __live_active_context(engine, fixme);
		if (err)
			break;

		err = igt_flush_test(gt->i915, I915_WAIT_LOCKED);
		if (err)
			break;
	}

unlock:
	mutex_unlock(&gt->i915->drm.struct_mutex);
	mock_file_free(gt->i915, file);
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

static int __live_remote_context(struct intel_engine_cs *engine,
				 struct i915_gem_context *fixme)
{
	struct intel_context *local, *remote;
	int pass;
	int err;

	/*
	 * Check that our idle barriers do not interfere with normal
	 * activity tracking. In particular, check that operating
	 * on the context image remotely (intel_context_prepare_remote_request),
	 * which inserts foreign fences into intel_context.active, does not
	 * clobber the idle-barrier.
	 */

	remote = intel_context_create(fixme, engine);
	if (!remote)
		return -ENOMEM;

	local = intel_context_create(fixme, engine);
	if (!local) {
		err = -ENOMEM;
		goto err_remote;
	}

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

	intel_context_put(local);
err_remote:
	intel_context_put(remote);
	return err;
}

static int live_remote_context(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *fixme;
	enum intel_engine_id id;
	struct drm_file *file;
	int err = 0;

	file = mock_file(gt->i915);
	if (IS_ERR(file))
		return PTR_ERR(file);

	mutex_lock(&gt->i915->drm.struct_mutex);

	fixme = live_context(gt->i915, file);
	if (!fixme) {
		err = -ENOMEM;
		goto unlock;
	}

	for_each_engine(engine, gt->i915, id) {
		err = __live_remote_context(engine, fixme);
		if (err)
			break;

		err = igt_flush_test(gt->i915, I915_WAIT_LOCKED);
		if (err)
			break;
	}

unlock:
	mutex_unlock(&gt->i915->drm.struct_mutex);
	mock_file_free(gt->i915, file);
	return err;
}

int intel_context_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_active_context),
		SUBTEST(live_remote_context),
	};
	struct intel_gt *gt = &i915->gt;

	if (intel_gt_is_wedged(gt))
		return 0;

	return intel_gt_live_subtests(tests, gt);
}
