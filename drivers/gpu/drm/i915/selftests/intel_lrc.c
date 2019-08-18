/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "../i915_reset.h"

#include "../i915_selftest.h"
#include "igt_flush_test.h"
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
		struct i915_request *rq;

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
		struct i915_request *rq;

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
		int count, i;

		for_each_prime_number_from(count, 1, 32) { /* must fit ring! */
			struct i915_request *rq;

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
		RUNTIME_INFO(smoke->i915)->num_rings, smoke->ncontext);
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
		RUNTIME_INFO(smoke->i915)->num_rings, smoke->ncontext);
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
	i915_gem_object_unpin_map(smoke.batch);

	err = i915_gem_object_set_to_gtt_domain(smoke.batch, false);
	if (err)
		goto err_batch;

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
	if (igt_flush_test(smoke.i915, I915_WAIT_LOCKED))
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
		SUBTEST(live_preempt),
		SUBTEST(live_late_preempt),
		SUBTEST(live_suppress_self_preempt),
		SUBTEST(live_chain_preempt),
		SUBTEST(live_preempt_hang),
		SUBTEST(live_preempt_smoke),
	};

	if (!HAS_EXECLISTS(i915))
		return 0;

	if (i915_terminally_wedged(&i915->gpu_error))
		return 0;

	return i915_subtests(tests, i915);
}
