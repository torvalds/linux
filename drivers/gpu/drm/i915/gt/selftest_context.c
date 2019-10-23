/*
 * SPDX-License-Identifier: GPL-2.0
 *
 * Copyright © 2019 Intel Corporation
 */

#include "i915_selftest.h"
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
	__i915_request_queue(rq, NULL);

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
		struct dma_fence *fence;
		long timeout;

		fence = i915_active_fence_get(&tl->last_request);
		if (!fence)
			break;

		timeout = dma_fence_wait_timeout(fence, false, HZ / 10);
		if (timeout < 0)
			err = timeout;
		else
			i915_request_retire_upto(to_request(fence));

		dma_fence_put(fence);
	} while (!err);
	mutex_unlock(&tl->mutex);

	return err;
}

static int __live_context_size(struct intel_engine_cs *engine,
			       struct i915_gem_context *fixme)
{
	struct intel_context *ce;
	struct i915_request *rq;
	void *vaddr;
	int err;

	ce = intel_context_create(fixme, engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	err = intel_context_pin(ce);
	if (err)
		goto err;

	vaddr = i915_gem_object_pin_map(ce->state->obj,
					i915_coherent_map_type(engine->i915));
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
	if (HAS_EXECLISTS(engine->i915))
		vaddr += LRC_HEADER_PAGES * PAGE_SIZE;

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
	rq = i915_request_create(engine->kernel_context);
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
	struct i915_gem_context *fixme;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check that our context sizes are correct by seeing if the
	 * HW tries to write past the end of one.
	 */

	fixme = kernel_context(gt->i915);
	if (IS_ERR(fixme))
		return PTR_ERR(fixme);

	for_each_engine(engine, gt, id) {
		struct {
			struct drm_i915_gem_object *state;
			void *pinned;
		} saved;

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
		saved.state = fetch_and_zero(&engine->default_state);
		saved.pinned = fetch_and_zero(&engine->pinned_default_state);

		/* Overlaps with the execlists redzone */
		engine->context_size += I915_GTT_PAGE_SIZE;

		err = __live_context_size(engine, fixme);

		engine->context_size -= I915_GTT_PAGE_SIZE;

		engine->pinned_default_state = saved.pinned;
		engine->default_state = saved.state;

		intel_engine_pm_put(engine);

		if (err)
			break;
	}

	kernel_context_close(fixme);
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
	if (IS_ERR(ce))
		return PTR_ERR(ce);

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

	fixme = live_context(gt->i915, file);
	if (IS_ERR(fixme)) {
		err = PTR_ERR(fixme);
		goto out_file;
	}

	for_each_engine(engine, gt, id) {
		err = __live_active_context(engine, fixme);
		if (err)
			break;

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	}

out_file:
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
	if (IS_ERR(remote))
		return PTR_ERR(remote);

	local = intel_context_create(fixme, engine);
	if (IS_ERR(local)) {
		err = PTR_ERR(local);
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

	fixme = live_context(gt->i915, file);
	if (IS_ERR(fixme)) {
		err = PTR_ERR(fixme);
		goto out_file;
	}

	for_each_engine(engine, gt, id) {
		err = __live_remote_context(engine, fixme);
		if (err)
			break;

		err = igt_flush_test(gt->i915);
		if (err)
			break;
	}

out_file:
	mock_file_free(gt->i915, file);
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
