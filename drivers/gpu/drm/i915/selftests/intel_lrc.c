/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "../i915_reset.h"

#include "../i915_selftest.h"
#include "igt_flush_test.h"
#include "igt_live_test.h"
#include "igt_spinner.h"
#include "i915_random.h"

#include "mock_context.h"

static int live_sanitycheck(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	enum intel_engine_id id;
	struct igt_spinner spin;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_CONTEXTS(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (igt_spinner_init(&spin, i915))
		goto err_unlock;

	ctx = kernel_context(i915);
	if (!ctx)
		goto err_spin;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		rq = igt_spinner_create_request(&spin, ctx, engine, MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin, rq)) {
			GEM_TRACE("spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx;
		}

		igt_spinner_end(&spin);
		if (igt_flush_test(i915, I915_WAIT_LOCKED)) {
			err = -EIO;
			goto err_ctx;
		}
	}

	err = 0;
err_ctx:
	kernel_context_close(ctx);
err_spin:
	igt_spinner_fini(&spin);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_busywait_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct intel_engine_cs *engine;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;
	u32 *map;

	/*
	 * Verify that even without HAS_LOGICAL_RING_PREEMPTION, we can
	 * preempt the busywaits used to synchronise between rings.
	 */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	ctx_hi = kernel_context(i915);
	if (!ctx_hi)
		goto err_unlock;
	ctx_hi->sched.priority = INT_MAX;

	ctx_lo = kernel_context(i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority = INT_MIN;

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err_ctx_lo;
	}

	map = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto err_obj;
	}

	vma = i915_vma_instance(obj, &i915->ggtt.vm, 0);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_map;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto err_map;

	for_each_engine(engine, i915, id) {
		struct i915_request *lo, *hi;
		struct igt_live_test t;
		u32 *cs;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (igt_live_test_begin(&t, i915, __func__, engine->name)) {
			err = -EIO;
			goto err_vma;
		}

		/*
		 * We create two requests. The low priority request
		 * busywaits on a semaphore (inside the ringbuffer where
		 * is should be preemptible) and the high priority requests
		 * uses a MI_STORE_DWORD_IMM to update the semaphore value
		 * allowing the first request to complete. If preemption
		 * fails, we hang instead.
		 */

		lo = i915_request_alloc(engine, ctx_lo);
		if (IS_ERR(lo)) {
			err = PTR_ERR(lo);
			goto err_vma;
		}

		cs = intel_ring_begin(lo, 8);
		if (IS_ERR(cs)) {
			err = PTR_ERR(cs);
			i915_request_add(lo);
			goto err_vma;
		}

		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = i915_ggtt_offset(vma);
		*cs++ = 0;
		*cs++ = 1;

		/* XXX Do we need a flush + invalidate here? */

		*cs++ = MI_SEMAPHORE_WAIT |
			MI_SEMAPHORE_GLOBAL_GTT |
			MI_SEMAPHORE_POLL |
			MI_SEMAPHORE_SAD_EQ_SDD;
		*cs++ = 0;
		*cs++ = i915_ggtt_offset(vma);
		*cs++ = 0;

		intel_ring_advance(lo, cs);
		i915_request_add(lo);

		if (wait_for(READ_ONCE(*map), 10)) {
			err = -ETIMEDOUT;
			goto err_vma;
		}

		/* Low priority request should be busywaiting now */
		if (i915_request_wait(lo, I915_WAIT_LOCKED, 1) != -ETIME) {
			pr_err("%s: Busywaiting request did not!\n",
			       engine->name);
			err = -EIO;
			goto err_vma;
		}

		hi = i915_request_alloc(engine, ctx_hi);
		if (IS_ERR(hi)) {
			err = PTR_ERR(hi);
			goto err_vma;
		}

		cs = intel_ring_begin(hi, 4);
		if (IS_ERR(cs)) {
			err = PTR_ERR(cs);
			i915_request_add(hi);
			goto err_vma;
		}

		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = i915_ggtt_offset(vma);
		*cs++ = 0;
		*cs++ = 0;

		intel_ring_advance(hi, cs);
		i915_request_add(hi);

		if (i915_request_wait(lo, I915_WAIT_LOCKED, HZ / 5) < 0) {
			struct drm_printer p = drm_info_printer(i915->drm.dev);

			pr_err("%s: Failed to preempt semaphore busywait!\n",
			       engine->name);

			intel_engine_dump(engine, &p, "%s\n", engine->name);
			GEM_TRACE_DUMP();

			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_vma;
		}
		GEM_BUG_ON(READ_ONCE(*map));

		if (igt_live_test_end(&t)) {
			err = -EIO;
			goto err_vma;
		}
	}

	err = 0;
err_vma:
	i915_vma_unpin(vma);
err_map:
	i915_gem_object_unpin_map(obj);
err_obj:
	i915_gem_object_put(obj);
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_unlock:
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct igt_spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	if (!(i915->caps.scheduler & I915_SCHEDULER_CAP_PREEMPTION))
		pr_err("Logical preemption supported, but not exposed\n");

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (igt_spinner_init(&spin_hi, i915))
		goto err_unlock;

	if (igt_spinner_init(&spin_lo, i915))
		goto err_spin_hi;

	ctx_hi = kernel_context(i915);
	if (!ctx_hi)
		goto err_spin_lo;
	ctx_hi->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MAX_USER_PRIORITY);

	ctx_lo = kernel_context(i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MIN_USER_PRIORITY);

	for_each_engine(engine, i915, id) {
		struct igt_live_test t;
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (igt_live_test_begin(&t, i915, __func__, engine->name)) {
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = igt_spinner_create_request(&spin_lo, ctx_lo, engine,
						MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin_lo, rq)) {
			GEM_TRACE("lo spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = igt_spinner_create_request(&spin_hi, ctx_hi, engine,
						MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			igt_spinner_end(&spin_lo);
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin_hi, rq)) {
			GEM_TRACE("hi spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		igt_spinner_end(&spin_hi);
		igt_spinner_end(&spin_lo);

		if (igt_live_test_end(&t)) {
			err = -EIO;
			goto err_ctx_lo;
		}
	}

	err = 0;
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_spin_lo:
	igt_spinner_fini(&spin_lo);
err_spin_hi:
	igt_spinner_fini(&spin_hi);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int live_late_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct igt_spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	struct i915_sched_attr attr = {};
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (igt_spinner_init(&spin_hi, i915))
		goto err_unlock;

	if (igt_spinner_init(&spin_lo, i915))
		goto err_spin_hi;

	ctx_hi = kernel_context(i915);
	if (!ctx_hi)
		goto err_spin_lo;

	ctx_lo = kernel_context(i915);
	if (!ctx_lo)
		goto err_ctx_hi;

	for_each_engine(engine, i915, id) {
		struct igt_live_test t;
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (igt_live_test_begin(&t, i915, __func__, engine->name)) {
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = igt_spinner_create_request(&spin_lo, ctx_lo, engine,
						MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin_lo, rq)) {
			pr_err("First context failed to start\n");
			goto err_wedged;
		}

		rq = igt_spinner_create_request(&spin_hi, ctx_hi, engine,
						MI_NOOP);
		if (IS_ERR(rq)) {
			igt_spinner_end(&spin_lo);
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (igt_wait_for_spinner(&spin_hi, rq)) {
			pr_err("Second context overtook first?\n");
			goto err_wedged;
		}

		attr.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX);
		engine->schedule(rq, &attr);

		if (!igt_wait_for_spinner(&spin_hi, rq)) {
			pr_err("High priority context failed to preempt the low priority context\n");
			GEM_TRACE_DUMP();
			goto err_wedged;
		}

		igt_spinner_end(&spin_hi);
		igt_spinner_end(&spin_lo);

		if (igt_live_test_end(&t)) {
			err = -EIO;
			goto err_ctx_lo;
		}
	}

	err = 0;
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_spin_lo:
	igt_spinner_fini(&spin_lo);
err_spin_hi:
	igt_spinner_fini(&spin_hi);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;

err_wedged:
	igt_spinner_end(&spin_hi);
	igt_spinner_end(&spin_lo);
	i915_gem_set_wedged(i915);
	err = -EIO;
	goto err_ctx_lo;
}

struct preempt_client {
	struct igt_spinner spin;
	struct i915_gem_context *ctx;
};

static int preempt_client_init(struct drm_i915_private *i915,
			       struct preempt_client *c)
{
	c->ctx = kernel_context(i915);
	if (!c->ctx)
		return -ENOMEM;

	if (igt_spinner_init(&c->spin, i915))
		goto err_ctx;

	return 0;

err_ctx:
	kernel_context_close(c->ctx);
	return -ENOMEM;
}

static void preempt_client_fini(struct preempt_client *c)
{
	igt_spinner_fini(&c->spin);
	kernel_context_close(c->ctx);
}

static int live_suppress_self_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct i915_sched_attr attr = {
		.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX)
	};
	struct preempt_client a, b;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;

	/*
	 * Verify that if a preemption request does not cause a change in
	 * the current execution order, the preempt-to-idle injection is
	 * skipped and that we do not accidentally apply it after the CS
	 * completion event.
	 */

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	if (USES_GUC_SUBMISSION(i915))
		return 0; /* presume black blox */

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (preempt_client_init(i915, &a))
		goto err_unlock;
	if (preempt_client_init(i915, &b))
		goto err_client_a;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq_a, *rq_b;
		int depth;

		if (!intel_engine_has_preemption(engine))
			continue;

		engine->execlists.preempt_hang.count = 0;

		rq_a = igt_spinner_create_request(&a.spin,
						  a.ctx, engine,
						  MI_NOOP);
		if (IS_ERR(rq_a)) {
			err = PTR_ERR(rq_a);
			goto err_client_b;
		}

		i915_request_add(rq_a);
		if (!igt_wait_for_spinner(&a.spin, rq_a)) {
			pr_err("First client failed to start\n");
			goto err_wedged;
		}

		for (depth = 0; depth < 8; depth++) {
			rq_b = igt_spinner_create_request(&b.spin,
							  b.ctx, engine,
							  MI_NOOP);
			if (IS_ERR(rq_b)) {
				err = PTR_ERR(rq_b);
				goto err_client_b;
			}
			i915_request_add(rq_b);

			GEM_BUG_ON(i915_request_completed(rq_a));
			engine->schedule(rq_a, &attr);
			igt_spinner_end(&a.spin);

			if (!igt_wait_for_spinner(&b.spin, rq_b)) {
				pr_err("Second client failed to start\n");
				goto err_wedged;
			}

			swap(a, b);
			rq_a = rq_b;
		}
		igt_spinner_end(&a.spin);

		if (engine->execlists.preempt_hang.count) {
			pr_err("Preemption recorded x%d, depth %d; should have been suppressed!\n",
			       engine->execlists.preempt_hang.count,
			       depth);
			err = -EINVAL;
			goto err_client_b;
		}

		if (igt_flush_test(i915, I915_WAIT_LOCKED))
			goto err_wedged;
	}

	err = 0;
err_client_b:
	preempt_client_fini(&b);
err_client_a:
	preempt_client_fini(&a);
err_unlock:
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;

err_wedged:
	igt_spinner_end(&b.spin);
	igt_spinner_end(&a.spin);
	i915_gem_set_wedged(i915);
	err = -EIO;
	goto err_client_b;
}

static int __i915_sw_fence_call
dummy_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	return NOTIFY_DONE;
}

static struct i915_request *dummy_request(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	rq = kzalloc(sizeof(*rq), GFP_KERNEL);
	if (!rq)
		return NULL;

	INIT_LIST_HEAD(&rq->active_list);
	rq->engine = engine;

	i915_sched_node_init(&rq->sched);

	/* mark this request as permanently incomplete */
	rq->fence.seqno = 1;
	BUILD_BUG_ON(sizeof(rq->fence.seqno) != 8); /* upper 32b == 0 */
	rq->hwsp_seqno = (u32 *)&rq->fence.seqno + 1;
	GEM_BUG_ON(i915_request_completed(rq));

	i915_sw_fence_init(&rq->submit, dummy_notify);
	i915_sw_fence_commit(&rq->submit);

	return rq;
}

static void dummy_request_free(struct i915_request *dummy)
{
	i915_request_mark_complete(dummy);
	i915_sched_node_fini(&dummy->sched);
	i915_sw_fence_fini(&dummy->submit);

	dma_fence_free(&dummy->fence);
}

static int live_suppress_wait_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct preempt_client client[4];
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;
	int i;

	/*
	 * Waiters are given a little priority nudge, but not enough
	 * to actually cause any preemption. Double check that we do
	 * not needlessly generate preempt-to-idle cycles.
	 */

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (preempt_client_init(i915, &client[0])) /* ELSP[0] */
		goto err_unlock;
	if (preempt_client_init(i915, &client[1])) /* ELSP[1] */
		goto err_client_0;
	if (preempt_client_init(i915, &client[2])) /* head of queue */
		goto err_client_1;
	if (preempt_client_init(i915, &client[3])) /* bystander */
		goto err_client_2;

	for_each_engine(engine, i915, id) {
		int depth;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (!engine->emit_init_breadcrumb)
			continue;

		for (depth = 0; depth < ARRAY_SIZE(client); depth++) {
			struct i915_request *rq[ARRAY_SIZE(client)];
			struct i915_request *dummy;

			engine->execlists.preempt_hang.count = 0;

			dummy = dummy_request(engine);
			if (!dummy)
				goto err_client_3;

			for (i = 0; i < ARRAY_SIZE(client); i++) {
				rq[i] = igt_spinner_create_request(&client[i].spin,
								   client[i].ctx, engine,
								   MI_NOOP);
				if (IS_ERR(rq[i])) {
					err = PTR_ERR(rq[i]);
					goto err_wedged;
				}

				/* Disable NEWCLIENT promotion */
				__i915_active_request_set(&rq[i]->timeline->last_request,
							  dummy);
				i915_request_add(rq[i]);
			}

			dummy_request_free(dummy);

			GEM_BUG_ON(i915_request_completed(rq[0]));
			if (!igt_wait_for_spinner(&client[0].spin, rq[0])) {
				pr_err("%s: First client failed to start\n",
				       engine->name);
				goto err_wedged;
			}
			GEM_BUG_ON(!i915_request_started(rq[0]));

			if (i915_request_wait(rq[depth],
					      I915_WAIT_LOCKED |
					      I915_WAIT_PRIORITY,
					      1) != -ETIME) {
				pr_err("%s: Waiter depth:%d completed!\n",
				       engine->name, depth);
				goto err_wedged;
			}

			for (i = 0; i < ARRAY_SIZE(client); i++)
				igt_spinner_end(&client[i].spin);

			if (igt_flush_test(i915, I915_WAIT_LOCKED))
				goto err_wedged;

			if (engine->execlists.preempt_hang.count) {
				pr_err("%s: Preemption recorded x%d, depth %d; should have been suppressed!\n",
				       engine->name,
				       engine->execlists.preempt_hang.count,
				       depth);
				err = -EINVAL;
				goto err_client_3;
			}
		}
	}

	err = 0;
err_client_3:
	preempt_client_fini(&client[3]);
err_client_2:
	preempt_client_fini(&client[2]);
err_client_1:
	preempt_client_fini(&client[1]);
err_client_0:
	preempt_client_fini(&client[0]);
err_unlock:
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;

err_wedged:
	for (i = 0; i < ARRAY_SIZE(client); i++)
		igt_spinner_end(&client[i].spin);
	i915_gem_set_wedged(i915);
	err = -EIO;
	goto err_client_3;
}

static int live_chain_preempt(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct intel_engine_cs *engine;
	struct preempt_client hi, lo;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;

	/*
	 * Build a chain AB...BA between two contexts (A, B) and request
	 * preemption of the last request. It should then complete before
	 * the previously submitted spinner in B.
	 */

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (preempt_client_init(i915, &hi))
		goto err_unlock;

	if (preempt_client_init(i915, &lo))
		goto err_client_hi;

	for_each_engine(engine, i915, id) {
		struct i915_sched_attr attr = {
			.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX),
		};
		struct igt_live_test t;
		struct i915_request *rq;
		int ring_size, count, i;

		if (!intel_engine_has_preemption(engine))
			continue;

		rq = igt_spinner_create_request(&lo.spin,
						lo.ctx, engine,
						MI_ARB_CHECK);
		if (IS_ERR(rq))
			goto err_wedged;
		i915_request_add(rq);

		ring_size = rq->wa_tail - rq->head;
		if (ring_size < 0)
			ring_size += rq->ring->size;
		ring_size = rq->ring->size / ring_size;
		pr_debug("%s(%s): Using maximum of %d requests\n",
			 __func__, engine->name, ring_size);

		igt_spinner_end(&lo.spin);
		if (i915_request_wait(rq, I915_WAIT_LOCKED, HZ / 2) < 0) {
			pr_err("Timed out waiting to flush %s\n", engine->name);
			goto err_wedged;
		}

		if (igt_live_test_begin(&t, i915, __func__, engine->name)) {
			err = -EIO;
			goto err_wedged;
		}

		for_each_prime_number_from(count, 1, ring_size) {
			rq = igt_spinner_create_request(&hi.spin,
							hi.ctx, engine,
							MI_ARB_CHECK);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);
			if (!igt_wait_for_spinner(&hi.spin, rq))
				goto err_wedged;

			rq = igt_spinner_create_request(&lo.spin,
							lo.ctx, engine,
							MI_ARB_CHECK);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);

			for (i = 0; i < count; i++) {
				rq = i915_request_alloc(engine, lo.ctx);
				if (IS_ERR(rq))
					goto err_wedged;
				i915_request_add(rq);
			}

			rq = i915_request_alloc(engine, hi.ctx);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);
			engine->schedule(rq, &attr);

			igt_spinner_end(&hi.spin);
			if (i915_request_wait(rq, I915_WAIT_LOCKED, HZ / 5) < 0) {
				struct drm_printer p =
					drm_info_printer(i915->drm.dev);

				pr_err("Failed to preempt over chain of %d\n",
				       count);
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);
				goto err_wedged;
			}
			igt_spinner_end(&lo.spin);

			rq = i915_request_alloc(engine, lo.ctx);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);
			if (i915_request_wait(rq, I915_WAIT_LOCKED, HZ / 5) < 0) {
				struct drm_printer p =
					drm_info_printer(i915->drm.dev);

				pr_err("Failed to flush low priority chain of %d requests\n",
				       count);
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);
				goto err_wedged;
			}
		}

		if (igt_live_test_end(&t)) {
			err = -EIO;
			goto err_wedged;
		}
	}

	err = 0;
err_client_lo:
	preempt_client_fini(&lo);
err_client_hi:
	preempt_client_fini(&hi);
err_unlock:
	if (igt_flush_test(i915, I915_WAIT_LOCKED))
		err = -EIO;
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;

err_wedged:
	igt_spinner_end(&hi.spin);
	igt_spinner_end(&lo.spin);
	i915_gem_set_wedged(i915);
	err = -EIO;
	goto err_client_lo;
}

static int live_preempt_hang(void *arg)
{
	struct drm_i915_private *i915 = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct igt_spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	intel_wakeref_t wakeref;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(i915))
		return 0;

	if (!intel_has_reset_engine(i915))
		return 0;

	mutex_lock(&i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(i915);

	if (igt_spinner_init(&spin_hi, i915))
		goto err_unlock;

	if (igt_spinner_init(&spin_lo, i915))
		goto err_spin_hi;

	ctx_hi = kernel_context(i915);
	if (!ctx_hi)
		goto err_spin_lo;
	ctx_hi->sched.priority = I915_CONTEXT_MAX_USER_PRIORITY;

	ctx_lo = kernel_context(i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority = I915_CONTEXT_MIN_USER_PRIORITY;

	for_each_engine(engine, i915, id) {
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		rq = igt_spinner_create_request(&spin_lo, ctx_lo, engine,
						MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin_lo, rq)) {
			GEM_TRACE("lo spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = igt_spinner_create_request(&spin_hi, ctx_hi, engine,
						MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			igt_spinner_end(&spin_lo);
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		init_completion(&engine->execlists.preempt_hang.completion);
		engine->execlists.preempt_hang.inject_hang = true;

		i915_request_add(rq);

		if (!wait_for_completion_timeout(&engine->execlists.preempt_hang.completion,
						 HZ / 10)) {
			pr_err("Preemption did not occur within timeout!");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		set_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);
		i915_reset_engine(engine, NULL);
		clear_bit(I915_RESET_ENGINE + id, &i915->gpu_error.flags);

		engine->execlists.preempt_hang.inject_hang = false;

		if (!igt_wait_for_spinner(&spin_hi, rq)) {
			GEM_TRACE("hi spinner failed to start\n");
			GEM_TRACE_DUMP();
			i915_gem_set_wedged(i915);
			err = -EIO;
			goto err_ctx_lo;
		}

		igt_spinner_end(&spin_hi);
		igt_spinner_end(&spin_lo);
		if (igt_flush_test(i915, I915_WAIT_LOCKED)) {
			err = -EIO;
			goto err_ctx_lo;
		}
	}

	err = 0;
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_spin_lo:
	igt_spinner_fini(&spin_lo);
err_spin_hi:
	igt_spinner_fini(&spin_hi);
err_unlock:
	igt_flush_test(i915, I915_WAIT_LOCKED);
	intel_runtime_pm_put(i915, wakeref);
	mutex_unlock(&i915->drm.struct_mutex);
	return err;
}

static int random_range(struct rnd_state *rnd, int min, int max)
{
	return i915_prandom_u32_max_state(max - min, rnd) + min;
}

static int random_priority(struct rnd_state *rnd)
{
	return random_range(rnd, I915_PRIORITY_MIN, I915_PRIORITY_MAX);
}

struct preempt_smoke {
	struct drm_i915_private *i915;
	struct i915_gem_context **contexts;
	struct intel_engine_cs *engine;
	struct drm_i915_gem_object *batch;
	unsigned int ncontext;
	struct rnd_state prng;
	unsigned long count;
};

static struct i915_gem_context *smoke_context(struct preempt_smoke *smoke)
{
	return smoke->contexts[i915_prandom_u32_max_state(smoke->ncontext,
							  &smoke->prng)];
}

static int smoke_submit(struct preempt_smoke *smoke,
			struct i915_gem_context *ctx, int prio,
			struct drm_i915_gem_object *batch)
{
	struct i915_request *rq;
	struct i915_vma *vma = NULL;
	int err = 0;

	if (batch) {
		vma = i915_vma_instance(batch, &ctx->ppgtt->vm, NULL);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		err = i915_vma_pin(vma, 0, 0, PIN_USER);
		if (err)
			return err;
	}

	ctx->sched.priority = prio;

	rq = i915_request_alloc(smoke->engine, ctx);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto unpin;
	}

	if (vma) {
		err = rq->engine->emit_bb_start(rq,
						vma->node.start,
						PAGE_SIZE, 0);
		if (!err)
			err = i915_vma_move_to_active(vma, rq, 0);
	}

	i915_request_add(rq);

unpin:
	if (vma)
		i915_vma_unpin(vma);

	return err;
}

static int smoke_crescendo_thread(void *arg)
{
	struct preempt_smoke *smoke = arg;
	IGT_TIMEOUT(end_time);
	unsigned long count;

	count = 0;
	do {
		struct i915_gem_context *ctx = smoke_context(smoke);
		int err;

		mutex_lock(&smoke->i915->drm.struct_mutex);
		err = smoke_submit(smoke,
				   ctx, count % I915_PRIORITY_MAX,
				   smoke->batch);
		mutex_unlock(&smoke->i915->drm.struct_mutex);
		if (err)
			return err;

		count++;
	} while (!__igt_timeout(end_time, NULL));

	smoke->count = count;
	return 0;
}

static int smoke_crescendo(struct preempt_smoke *smoke, unsigned int flags)
#define BATCH BIT(0)
{
	struct task_struct *tsk[I915_NUM_ENGINES] = {};
	struct preempt_smoke arg[I915_NUM_ENGINES];
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned long count;
	int err = 0;

	mutex_unlock(&smoke->i915->drm.struct_mutex);

	for_each_engine(engine, smoke->i915, id) {
		arg[id] = *smoke;
		arg[id].engine = engine;
		if (!(flags & BATCH))
			arg[id].batch = NULL;
		arg[id].count = 0;

		tsk[id] = kthread_run(smoke_crescendo_thread, &arg,
				      "igt/smoke:%d", id);
		if (IS_ERR(tsk[id])) {
			err = PTR_ERR(tsk[id]);
			break;
		}
		get_task_struct(tsk[id]);
	}

	count = 0;
	for_each_engine(engine, smoke->i915, id) {
		int status;

		if (IS_ERR_OR_NULL(tsk[id]))
			continue;

		status = kthread_stop(tsk[id]);
		if (status && !err)
			err = status;

		count += arg[id].count;

		put_task_struct(tsk[id]);
	}

	mutex_lock(&smoke->i915->drm.struct_mutex);

	pr_info("Submitted %lu crescendo:%x requests across %d engines and %d contexts\n",
		count, flags,
		RUNTIME_INFO(smoke->i915)->num_engines, smoke->ncontext);
	return 0;
}

static int smoke_random(struct preempt_smoke *smoke, unsigned int flags)
{
	enum intel_engine_id id;
	IGT_TIMEOUT(end_time);
	unsigned long count;

	count = 0;
	do {
		for_each_engine(smoke->engine, smoke->i915, id) {
			struct i915_gem_context *ctx = smoke_context(smoke);
			int err;

			err = smoke_submit(smoke,
					   ctx, random_priority(&smoke->prng),
					   flags & BATCH ? smoke->batch : NULL);
			if (err)
				return err;

			count++;
		}
	} while (!__igt_timeout(end_time, NULL));

	pr_info("Submitted %lu random:%x requests across %d engines and %d contexts\n",
		count, flags,
		RUNTIME_INFO(smoke->i915)->num_engines, smoke->ncontext);
	return 0;
}

static int live_preempt_smoke(void *arg)
{
	struct preempt_smoke smoke = {
		.i915 = arg,
		.prng = I915_RND_STATE_INITIALIZER(i915_selftest.random_seed),
		.ncontext = 1024,
	};
	const unsigned int phase[] = { 0, BATCH };
	intel_wakeref_t wakeref;
	struct igt_live_test t;
	int err = -ENOMEM;
	u32 *cs;
	int n;

	if (!HAS_LOGICAL_RING_PREEMPTION(smoke.i915))
		return 0;

	smoke.contexts = kmalloc_array(smoke.ncontext,
				       sizeof(*smoke.contexts),
				       GFP_KERNEL);
	if (!smoke.contexts)
		return -ENOMEM;

	mutex_lock(&smoke.i915->drm.struct_mutex);
	wakeref = intel_runtime_pm_get(smoke.i915);

	smoke.batch = i915_gem_object_create_internal(smoke.i915, PAGE_SIZE);
	if (IS_ERR(smoke.batch)) {
		err = PTR_ERR(smoke.batch);
		goto err_unlock;
	}

	cs = i915_gem_object_pin_map(smoke.batch, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_batch;
	}
	for (n = 0; n < PAGE_SIZE / sizeof(*cs) - 1; n++)
		cs[n] = MI_ARB_CHECK;
	cs[n] = MI_BATCH_BUFFER_END;
	i915_gem_object_flush_map(smoke.batch);
	i915_gem_object_unpin_map(smoke.batch);

	if (igt_live_test_begin(&t, smoke.i915, __func__, "all")) {
		err = -EIO;
		goto err_batch;
	}

	for (n = 0; n < smoke.ncontext; n++) {
		smoke.contexts[n] = kernel_context(smoke.i915);
		if (!smoke.contexts[n])
			goto err_ctx;
	}

	for (n = 0; n < ARRAY_SIZE(phase); n++) {
		err = smoke_crescendo(&smoke, phase[n]);
		if (err)
			goto err_ctx;

		err = smoke_random(&smoke, phase[n]);
		if (err)
			goto err_ctx;
	}

err_ctx:
	if (igt_live_test_end(&t))
		err = -EIO;

	for (n = 0; n < smoke.ncontext; n++) {
		if (!smoke.contexts[n])
			break;
		kernel_context_close(smoke.contexts[n]);
	}

err_batch:
	i915_gem_object_put(smoke.batch);
err_unlock:
	intel_runtime_pm_put(smoke.i915, wakeref);
	mutex_unlock(&smoke.i915->drm.struct_mutex);
	kfree(smoke.contexts);

	return err;
}

int intel_execlists_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_sanitycheck),
		SUBTEST(live_busywait_preempt),
		SUBTEST(live_preempt),
		SUBTEST(live_late_preempt),
		SUBTEST(live_suppress_self_preempt),
		SUBTEST(live_suppress_wait_preempt),
		SUBTEST(live_chain_preempt),
		SUBTEST(live_preempt_hang),
		SUBTEST(live_preempt_smoke),
	};

	if (!HAS_EXECLISTS(i915))
		return 0;

	if (i915_terminally_wedged(i915))
		return 0;

	return i915_subtests(tests, i915);
}
