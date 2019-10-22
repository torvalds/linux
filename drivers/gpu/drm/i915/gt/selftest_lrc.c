/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "gem/i915_gem_pm.h"
#include "gt/intel_reset.h"

#include "i915_selftest.h"
#include "selftests/i915_random.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_live_test.h"
#include "selftests/igt_spinner.h"
#include "selftests/lib_sw_fence.h"

#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"

#define CS_GPR(engine, n) ((engine)->mmio_base + 0x600 + (n) * 4)
#define NUM_GPR_DW (16 * 2) /* each GPR is 2 dwords */

static struct i915_vma *create_scratch(struct intel_gt *gt)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	i915_gem_object_set_cache_coherency(obj, I915_CACHING_CACHED);

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err) {
		i915_gem_object_put(obj);
		return ERR_PTR(err);
	}

	return vma;
}

static int live_sanitycheck(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gem_engines_iter it;
	struct i915_gem_context *ctx;
	struct intel_context *ce;
	struct igt_spinner spin;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_CONTEXTS(gt->i915))
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	ctx = kernel_context(gt->i915);
	if (!ctx)
		goto err_spin;

	for_each_gem_engine(ce, i915_gem_context_lock_engines(ctx), it) {
		struct i915_request *rq;

		rq = igt_spinner_create_request(&spin, ce, MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin, rq)) {
			GEM_TRACE("spinner failed to start\n");
			GEM_TRACE_DUMP();
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto err_ctx;
		}

		igt_spinner_end(&spin);
		if (igt_flush_test(gt->i915)) {
			err = -EIO;
			goto err_ctx;
		}
	}

	err = 0;
err_ctx:
	i915_gem_context_unlock_engines(ctx);
	kernel_context_close(ctx);
err_spin:
	igt_spinner_fini(&spin);
	return err;
}

static int live_unlite_restore(struct intel_gt *gt, int prio)
{
	struct intel_engine_cs *engine;
	struct i915_gem_context *ctx;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = -ENOMEM;

	/*
	 * Check that we can correctly context switch between 2 instances
	 * on the same engine from the same parent context.
	 */

	if (igt_spinner_init(&spin, gt))
		return err;

	ctx = kernel_context(gt->i915);
	if (!ctx)
		goto err_spin;

	err = 0;
	for_each_engine(engine, gt, id) {
		struct intel_context *ce[2] = {};
		struct i915_request *rq[2];
		struct igt_live_test t;
		int n;

		if (prio && !intel_engine_has_preemption(engine))
			continue;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
			err = -EIO;
			break;
		}

		for (n = 0; n < ARRAY_SIZE(ce); n++) {
			struct intel_context *tmp;

			tmp = intel_context_create(ctx, engine);
			if (IS_ERR(tmp)) {
				err = PTR_ERR(tmp);
				goto err_ce;
			}

			err = intel_context_pin(tmp);
			if (err) {
				intel_context_put(tmp);
				goto err_ce;
			}

			/*
			 * Setup the pair of contexts such that if we
			 * lite-restore using the RING_TAIL from ce[1] it
			 * will execute garbage from ce[0]->ring.
			 */
			memset(tmp->ring->vaddr,
			       POISON_INUSE, /* IPEHR: 0x5a5a5a5a [hung!] */
			       tmp->ring->vma->size);

			ce[n] = tmp;
		}
		GEM_BUG_ON(!ce[1]->ring->size);
		intel_ring_reset(ce[1]->ring, ce[1]->ring->size / 2);

		local_irq_disable(); /* appease lockdep */
		__context_pin_acquire(ce[1]);
		__execlists_update_reg_state(ce[1], engine);
		__context_pin_release(ce[1]);
		local_irq_enable();

		rq[0] = igt_spinner_create_request(&spin, ce[0], MI_ARB_CHECK);
		if (IS_ERR(rq[0])) {
			err = PTR_ERR(rq[0]);
			goto err_ce;
		}

		i915_request_get(rq[0]);
		i915_request_add(rq[0]);
		GEM_BUG_ON(rq[0]->postfix > ce[1]->ring->emit);

		if (!igt_wait_for_spinner(&spin, rq[0])) {
			i915_request_put(rq[0]);
			goto err_ce;
		}

		rq[1] = i915_request_create(ce[1]);
		if (IS_ERR(rq[1])) {
			err = PTR_ERR(rq[1]);
			i915_request_put(rq[0]);
			goto err_ce;
		}

		if (!prio) {
			/*
			 * Ensure we do the switch to ce[1] on completion.
			 *
			 * rq[0] is already submitted, so this should reduce
			 * to a no-op (a wait on a request on the same engine
			 * uses the submit fence, not the completion fence),
			 * but it will install a dependency on rq[1] for rq[0]
			 * that will prevent the pair being reordered by
			 * timeslicing.
			 */
			i915_request_await_dma_fence(rq[1], &rq[0]->fence);
		}

		i915_request_get(rq[1]);
		i915_request_add(rq[1]);
		GEM_BUG_ON(rq[1]->postfix <= rq[0]->postfix);
		i915_request_put(rq[0]);

		if (prio) {
			struct i915_sched_attr attr = {
				.priority = prio,
			};

			/* Alternatively preempt the spinner with ce[1] */
			engine->schedule(rq[1], &attr);
		}

		/* And switch back to ce[0] for good measure */
		rq[0] = i915_request_create(ce[0]);
		if (IS_ERR(rq[0])) {
			err = PTR_ERR(rq[0]);
			i915_request_put(rq[1]);
			goto err_ce;
		}

		i915_request_await_dma_fence(rq[0], &rq[1]->fence);
		i915_request_get(rq[0]);
		i915_request_add(rq[0]);
		GEM_BUG_ON(rq[0]->postfix > rq[1]->postfix);
		i915_request_put(rq[1]);
		i915_request_put(rq[0]);

err_ce:
		tasklet_kill(&engine->execlists.tasklet); /* flush submission */
		igt_spinner_end(&spin);
		for (n = 0; n < ARRAY_SIZE(ce); n++) {
			if (IS_ERR_OR_NULL(ce[n]))
				break;

			intel_context_unpin(ce[n]);
			intel_context_put(ce[n]);
		}

		if (igt_live_test_end(&t))
			err = -EIO;
		if (err)
			break;
	}

	kernel_context_close(ctx);
err_spin:
	igt_spinner_fini(&spin);
	return err;
}

static int live_unlite_switch(void *arg)
{
	return live_unlite_restore(arg, 0);
}

static int live_unlite_preempt(void *arg)
{
	return live_unlite_restore(arg, I915_USER_PRIORITY(I915_PRIORITY_MAX));
}

static int
emit_semaphore_chain(struct i915_request *rq, struct i915_vma *vma, int idx)
{
	u32 *cs;

	cs = intel_ring_begin(rq, 10);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_GLOBAL_GTT |
		MI_SEMAPHORE_POLL |
		MI_SEMAPHORE_SAD_NEQ_SDD;
	*cs++ = 0;
	*cs++ = i915_ggtt_offset(vma) + 4 * idx;
	*cs++ = 0;

	if (idx > 0) {
		*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
		*cs++ = i915_ggtt_offset(vma) + 4 * (idx - 1);
		*cs++ = 0;
		*cs++ = 1;
	} else {
		*cs++ = MI_NOOP;
		*cs++ = MI_NOOP;
		*cs++ = MI_NOOP;
		*cs++ = MI_NOOP;
	}

	*cs++ = MI_ARB_ON_OFF | MI_ARB_DISABLE;

	intel_ring_advance(rq, cs);
	return 0;
}

static struct i915_request *
semaphore_queue(struct intel_engine_cs *engine, struct i915_vma *vma, int idx)
{
	struct i915_gem_context *ctx;
	struct i915_request *rq;
	int err;

	ctx = kernel_context(engine->i915);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	rq = igt_request_alloc(ctx, engine);
	if (IS_ERR(rq))
		goto out_ctx;

	err = 0;
	if (rq->engine->emit_init_breadcrumb)
		err = rq->engine->emit_init_breadcrumb(rq);
	if (err == 0)
		err = emit_semaphore_chain(rq, vma, idx);
	if (err == 0)
		i915_request_get(rq);
	i915_request_add(rq);
	if (err)
		rq = ERR_PTR(err);

out_ctx:
	kernel_context_close(ctx);
	return rq;
}

static int
release_queue(struct intel_engine_cs *engine,
	      struct i915_vma *vma,
	      int idx, int prio)
{
	struct i915_sched_attr attr = {
		.priority = prio,
	};
	struct i915_request *rq;
	u32 *cs;

	rq = i915_request_create(engine->kernel_context);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = i915_ggtt_offset(vma) + 4 * (idx - 1);
	*cs++ = 0;
	*cs++ = 1;

	intel_ring_advance(rq, cs);

	i915_request_get(rq);
	i915_request_add(rq);

	local_bh_disable();
	engine->schedule(rq, &attr);
	local_bh_enable(); /* kick tasklet */

	i915_request_put(rq);

	return 0;
}

static int
slice_semaphore_queue(struct intel_engine_cs *outer,
		      struct i915_vma *vma,
		      int count)
{
	struct intel_engine_cs *engine;
	struct i915_request *head;
	enum intel_engine_id id;
	int err, i, n = 0;

	head = semaphore_queue(outer, vma, n++);
	if (IS_ERR(head))
		return PTR_ERR(head);

	for_each_engine(engine, outer->gt, id) {
		for (i = 0; i < count; i++) {
			struct i915_request *rq;

			rq = semaphore_queue(engine, vma, n++);
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				goto out;
			}

			i915_request_put(rq);
		}
	}

	err = release_queue(outer, vma, n, INT_MAX);
	if (err)
		goto out;

	if (i915_request_wait(head, 0,
			      2 * RUNTIME_INFO(outer->i915)->num_engines * (count + 2) * (count + 3)) < 0) {
		pr_err("Failed to slice along semaphore chain of length (%d, %d)!\n",
		       count, n);
		GEM_TRACE_DUMP();
		intel_gt_set_wedged(outer->gt);
		err = -EIO;
	}

out:
	i915_request_put(head);
	return err;
}

static int live_timeslice_preempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	void *vaddr;
	int err = 0;
	int count;

	/*
	 * If a request takes too long, we would like to give other users
	 * a fair go on the GPU. In particular, users may create batches
	 * that wait upon external input, where that input may even be
	 * supplied by another GPU job. To avoid blocking forever, we
	 * need to preempt the current task and replace it with another
	 * ready task.
	 */

	obj = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	vaddr = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto err_map;

	for_each_prime_number_from(count, 1, 16) {
		struct intel_engine_cs *engine;
		enum intel_engine_id id;

		for_each_engine(engine, gt, id) {
			if (!intel_engine_has_preemption(engine))
				continue;

			memset(vaddr, 0, PAGE_SIZE);

			err = slice_semaphore_queue(engine, vma, count);
			if (err)
				goto err_pin;

			if (igt_flush_test(gt->i915)) {
				err = -EIO;
				goto err_pin;
			}
		}
	}

err_pin:
	i915_vma_unpin(vma);
err_map:
	i915_gem_object_unpin_map(obj);
err_obj:
	i915_gem_object_put(obj);
	return err;
}

static struct i915_request *nop_request(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	rq = i915_request_create(engine->kernel_context);
	if (IS_ERR(rq))
		return rq;

	i915_request_get(rq);
	i915_request_add(rq);

	return rq;
}

static void wait_for_submit(struct intel_engine_cs *engine,
			    struct i915_request *rq)
{
	do {
		cond_resched();
		intel_engine_flush_submission(engine);
	} while (!i915_request_is_active(rq));
}

static int live_timeslice_queue(void *arg)
{
	struct intel_gt *gt = arg;
	struct drm_i915_gem_object *obj;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct i915_vma *vma;
	void *vaddr;
	int err = 0;

	/*
	 * Make sure that even if ELSP[0] and ELSP[1] are filled with
	 * timeslicing between them disabled, we *do* enable timeslicing
	 * if the queue demands it. (Normally, we do not submit if
	 * ELSP[1] is already occupied, so must rely on timeslicing to
	 * eject ELSP[0] in favour of the queue.)
	 */

	obj = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return PTR_ERR(obj);

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	vaddr = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(vaddr)) {
		err = PTR_ERR(vaddr);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto err_map;

	for_each_engine(engine, gt, id) {
		struct i915_sched_attr attr = {
			.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX),
		};
		struct i915_request *rq, *nop;

		if (!intel_engine_has_preemption(engine))
			continue;

		memset(vaddr, 0, PAGE_SIZE);

		/* ELSP[0]: semaphore wait */
		rq = semaphore_queue(engine, vma, 0);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_pin;
		}
		engine->schedule(rq, &attr);
		wait_for_submit(engine, rq);

		/* ELSP[1]: nop request */
		nop = nop_request(engine);
		if (IS_ERR(nop)) {
			err = PTR_ERR(nop);
			i915_request_put(rq);
			goto err_pin;
		}
		wait_for_submit(engine, nop);
		i915_request_put(nop);

		GEM_BUG_ON(i915_request_completed(rq));
		GEM_BUG_ON(execlists_active(&engine->execlists) != rq);

		/* Queue: semaphore signal, matching priority as semaphore */
		err = release_queue(engine, vma, 1, effective_prio(rq));
		if (err) {
			i915_request_put(rq);
			goto err_pin;
		}

		intel_engine_flush_submission(engine);
		if (!READ_ONCE(engine->execlists.timer.expires) &&
		    !i915_request_completed(rq)) {
			struct drm_printer p =
				drm_info_printer(gt->i915->drm.dev);

			GEM_TRACE_ERR("%s: Failed to enable timeslicing!\n",
				      engine->name);
			intel_engine_dump(engine, &p,
					  "%s\n", engine->name);
			GEM_TRACE_DUMP();

			memset(vaddr, 0xff, PAGE_SIZE);
			err = -EINVAL;
		}

		/* Timeslice every jiffie, so within 2 we should signal */
		if (i915_request_wait(rq, 0, 3) < 0) {
			struct drm_printer p =
				drm_info_printer(gt->i915->drm.dev);

			pr_err("%s: Failed to timeslice into queue\n",
			       engine->name);
			intel_engine_dump(engine, &p,
					  "%s\n", engine->name);

			memset(vaddr, 0xff, PAGE_SIZE);
			err = -EIO;
		}
		i915_request_put(rq);
		if (err)
			break;
	}

err_pin:
	i915_vma_unpin(vma);
err_map:
	i915_gem_object_unpin_map(obj);
err_obj:
	i915_gem_object_put(obj);
	return err;
}

static int live_busywait_preempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct intel_engine_cs *engine;
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	enum intel_engine_id id;
	int err = -ENOMEM;
	u32 *map;

	/*
	 * Verify that even without HAS_LOGICAL_RING_PREEMPTION, we can
	 * preempt the busywaits used to synchronise between rings.
	 */

	ctx_hi = kernel_context(gt->i915);
	if (!ctx_hi)
		return -ENOMEM;
	ctx_hi->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MAX_USER_PRIORITY);

	ctx_lo = kernel_context(gt->i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MIN_USER_PRIORITY);

	obj = i915_gem_object_create_internal(gt->i915, PAGE_SIZE);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err_ctx_lo;
	}

	map = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(map)) {
		err = PTR_ERR(map);
		goto err_obj;
	}

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_map;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL);
	if (err)
		goto err_map;

	for_each_engine(engine, gt, id) {
		struct i915_request *lo, *hi;
		struct igt_live_test t;
		u32 *cs;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
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

		lo = igt_request_alloc(ctx_lo, engine);
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
		if (i915_request_wait(lo, 0, 1) != -ETIME) {
			pr_err("%s: Busywaiting request did not!\n",
			       engine->name);
			err = -EIO;
			goto err_vma;
		}

		hi = igt_request_alloc(ctx_hi, engine);
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

		if (i915_request_wait(lo, 0, HZ / 5) < 0) {
			struct drm_printer p = drm_info_printer(gt->i915->drm.dev);

			pr_err("%s: Failed to preempt semaphore busywait!\n",
			       engine->name);

			intel_engine_dump(engine, &p, "%s\n", engine->name);
			GEM_TRACE_DUMP();

			intel_gt_set_wedged(gt);
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
	return err;
}

static struct i915_request *
spinner_create_request(struct igt_spinner *spin,
		       struct i915_gem_context *ctx,
		       struct intel_engine_cs *engine,
		       u32 arb)
{
	struct intel_context *ce;
	struct i915_request *rq;

	ce = i915_gem_context_get_engine(ctx, engine->legacy_idx);
	if (IS_ERR(ce))
		return ERR_CAST(ce);

	rq = igt_spinner_create_request(spin, ce, arb);
	intel_context_put(ce);
	return rq;
}

static int live_preempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct igt_spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(gt->i915))
		return 0;

	if (!(gt->i915->caps.scheduler & I915_SCHEDULER_CAP_PREEMPTION))
		pr_err("Logical preemption supported, but not exposed\n");

	if (igt_spinner_init(&spin_hi, gt))
		return -ENOMEM;

	if (igt_spinner_init(&spin_lo, gt))
		goto err_spin_hi;

	ctx_hi = kernel_context(gt->i915);
	if (!ctx_hi)
		goto err_spin_lo;
	ctx_hi->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MAX_USER_PRIORITY);

	ctx_lo = kernel_context(gt->i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MIN_USER_PRIORITY);

	for_each_engine(engine, gt, id) {
		struct igt_live_test t;
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = spinner_create_request(&spin_lo, ctx_lo, engine,
					    MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin_lo, rq)) {
			GEM_TRACE("lo spinner failed to start\n");
			GEM_TRACE_DUMP();
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = spinner_create_request(&spin_hi, ctx_hi, engine,
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
			intel_gt_set_wedged(gt);
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
	return err;
}

static int live_late_preempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct igt_spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	struct i915_sched_attr attr = {};
	enum intel_engine_id id;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(gt->i915))
		return 0;

	if (igt_spinner_init(&spin_hi, gt))
		return -ENOMEM;

	if (igt_spinner_init(&spin_lo, gt))
		goto err_spin_hi;

	ctx_hi = kernel_context(gt->i915);
	if (!ctx_hi)
		goto err_spin_lo;

	ctx_lo = kernel_context(gt->i915);
	if (!ctx_lo)
		goto err_ctx_hi;

	/* Make sure ctx_lo stays before ctx_hi until we trigger preemption. */
	ctx_lo->sched.priority = I915_USER_PRIORITY(1);

	for_each_engine(engine, gt, id) {
		struct igt_live_test t;
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = spinner_create_request(&spin_lo, ctx_lo, engine,
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

		rq = spinner_create_request(&spin_hi, ctx_hi, engine,
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
	return err;

err_wedged:
	igt_spinner_end(&spin_hi);
	igt_spinner_end(&spin_lo);
	intel_gt_set_wedged(gt);
	err = -EIO;
	goto err_ctx_lo;
}

struct preempt_client {
	struct igt_spinner spin;
	struct i915_gem_context *ctx;
};

static int preempt_client_init(struct intel_gt *gt, struct preempt_client *c)
{
	c->ctx = kernel_context(gt->i915);
	if (!c->ctx)
		return -ENOMEM;

	if (igt_spinner_init(&c->spin, gt))
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

static int live_nopreempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct preempt_client a, b;
	enum intel_engine_id id;
	int err = -ENOMEM;

	/*
	 * Verify that we can disable preemption for an individual request
	 * that may be being observed and not want to be interrupted.
	 */

	if (!HAS_LOGICAL_RING_PREEMPTION(gt->i915))
		return 0;

	if (preempt_client_init(gt, &a))
		return -ENOMEM;
	if (preempt_client_init(gt, &b))
		goto err_client_a;
	b.ctx->sched.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX);

	for_each_engine(engine, gt, id) {
		struct i915_request *rq_a, *rq_b;

		if (!intel_engine_has_preemption(engine))
			continue;

		engine->execlists.preempt_hang.count = 0;

		rq_a = spinner_create_request(&a.spin,
					      a.ctx, engine,
					      MI_ARB_CHECK);
		if (IS_ERR(rq_a)) {
			err = PTR_ERR(rq_a);
			goto err_client_b;
		}

		/* Low priority client, but unpreemptable! */
		rq_a->flags |= I915_REQUEST_NOPREEMPT;

		i915_request_add(rq_a);
		if (!igt_wait_for_spinner(&a.spin, rq_a)) {
			pr_err("First client failed to start\n");
			goto err_wedged;
		}

		rq_b = spinner_create_request(&b.spin,
					      b.ctx, engine,
					      MI_ARB_CHECK);
		if (IS_ERR(rq_b)) {
			err = PTR_ERR(rq_b);
			goto err_client_b;
		}

		i915_request_add(rq_b);

		/* B is much more important than A! (But A is unpreemptable.) */
		GEM_BUG_ON(rq_prio(rq_b) <= rq_prio(rq_a));

		/* Wait long enough for preemption and timeslicing */
		if (igt_wait_for_spinner(&b.spin, rq_b)) {
			pr_err("Second client started too early!\n");
			goto err_wedged;
		}

		igt_spinner_end(&a.spin);

		if (!igt_wait_for_spinner(&b.spin, rq_b)) {
			pr_err("Second client failed to start\n");
			goto err_wedged;
		}

		igt_spinner_end(&b.spin);

		if (engine->execlists.preempt_hang.count) {
			pr_err("Preemption recorded x%d; should have been suppressed!\n",
			       engine->execlists.preempt_hang.count);
			err = -EINVAL;
			goto err_wedged;
		}

		if (igt_flush_test(gt->i915))
			goto err_wedged;
	}

	err = 0;
err_client_b:
	preempt_client_fini(&b);
err_client_a:
	preempt_client_fini(&a);
	return err;

err_wedged:
	igt_spinner_end(&b.spin);
	igt_spinner_end(&a.spin);
	intel_gt_set_wedged(gt);
	err = -EIO;
	goto err_client_b;
}

static int live_suppress_self_preempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_sched_attr attr = {
		.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX)
	};
	struct preempt_client a, b;
	enum intel_engine_id id;
	int err = -ENOMEM;

	/*
	 * Verify that if a preemption request does not cause a change in
	 * the current execution order, the preempt-to-idle injection is
	 * skipped and that we do not accidentally apply it after the CS
	 * completion event.
	 */

	if (!HAS_LOGICAL_RING_PREEMPTION(gt->i915))
		return 0;

	if (USES_GUC_SUBMISSION(gt->i915))
		return 0; /* presume black blox */

	if (intel_vgpu_active(gt->i915))
		return 0; /* GVT forces single port & request submission */

	if (preempt_client_init(gt, &a))
		return -ENOMEM;
	if (preempt_client_init(gt, &b))
		goto err_client_a;

	for_each_engine(engine, gt, id) {
		struct i915_request *rq_a, *rq_b;
		int depth;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (igt_flush_test(gt->i915))
			goto err_wedged;

		intel_engine_pm_get(engine);
		engine->execlists.preempt_hang.count = 0;

		rq_a = spinner_create_request(&a.spin,
					      a.ctx, engine,
					      MI_NOOP);
		if (IS_ERR(rq_a)) {
			err = PTR_ERR(rq_a);
			intel_engine_pm_put(engine);
			goto err_client_b;
		}

		i915_request_add(rq_a);
		if (!igt_wait_for_spinner(&a.spin, rq_a)) {
			pr_err("First client failed to start\n");
			intel_engine_pm_put(engine);
			goto err_wedged;
		}

		/* Keep postponing the timer to avoid premature slicing */
		mod_timer(&engine->execlists.timer, jiffies + HZ);
		for (depth = 0; depth < 8; depth++) {
			rq_b = spinner_create_request(&b.spin,
						      b.ctx, engine,
						      MI_NOOP);
			if (IS_ERR(rq_b)) {
				err = PTR_ERR(rq_b);
				intel_engine_pm_put(engine);
				goto err_client_b;
			}
			i915_request_add(rq_b);

			GEM_BUG_ON(i915_request_completed(rq_a));
			engine->schedule(rq_a, &attr);
			igt_spinner_end(&a.spin);

			if (!igt_wait_for_spinner(&b.spin, rq_b)) {
				pr_err("Second client failed to start\n");
				intel_engine_pm_put(engine);
				goto err_wedged;
			}

			swap(a, b);
			rq_a = rq_b;
		}
		igt_spinner_end(&a.spin);

		if (engine->execlists.preempt_hang.count) {
			pr_err("Preemption on %s recorded x%d, depth %d; should have been suppressed!\n",
			       engine->name,
			       engine->execlists.preempt_hang.count,
			       depth);
			intel_engine_pm_put(engine);
			err = -EINVAL;
			goto err_client_b;
		}

		intel_engine_pm_put(engine);
		if (igt_flush_test(gt->i915))
			goto err_wedged;
	}

	err = 0;
err_client_b:
	preempt_client_fini(&b);
err_client_a:
	preempt_client_fini(&a);
	return err;

err_wedged:
	igt_spinner_end(&b.spin);
	igt_spinner_end(&a.spin);
	intel_gt_set_wedged(gt);
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

	rq->engine = engine;

	spin_lock_init(&rq->lock);
	INIT_LIST_HEAD(&rq->fence.cb_list);
	rq->fence.lock = &rq->lock;
	rq->fence.ops = &i915_fence_ops;

	i915_sched_node_init(&rq->sched);

	/* mark this request as permanently incomplete */
	rq->fence.seqno = 1;
	BUILD_BUG_ON(sizeof(rq->fence.seqno) != 8); /* upper 32b == 0 */
	rq->hwsp_seqno = (u32 *)&rq->fence.seqno + 1;
	GEM_BUG_ON(i915_request_completed(rq));

	i915_sw_fence_init(&rq->submit, dummy_notify);
	set_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags);

	spin_lock_init(&rq->lock);
	rq->fence.lock = &rq->lock;
	INIT_LIST_HEAD(&rq->fence.cb_list);

	return rq;
}

static void dummy_request_free(struct i915_request *dummy)
{
	/* We have to fake the CS interrupt to kick the next request */
	i915_sw_fence_commit(&dummy->submit);

	i915_request_mark_complete(dummy);
	dma_fence_signal(&dummy->fence);

	i915_sched_node_fini(&dummy->sched);
	i915_sw_fence_fini(&dummy->submit);

	dma_fence_free(&dummy->fence);
}

static int live_suppress_wait_preempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct preempt_client client[4];
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = -ENOMEM;
	int i;

	/*
	 * Waiters are given a little priority nudge, but not enough
	 * to actually cause any preemption. Double check that we do
	 * not needlessly generate preempt-to-idle cycles.
	 */

	if (!HAS_LOGICAL_RING_PREEMPTION(gt->i915))
		return 0;

	if (preempt_client_init(gt, &client[0])) /* ELSP[0] */
		return -ENOMEM;
	if (preempt_client_init(gt, &client[1])) /* ELSP[1] */
		goto err_client_0;
	if (preempt_client_init(gt, &client[2])) /* head of queue */
		goto err_client_1;
	if (preempt_client_init(gt, &client[3])) /* bystander */
		goto err_client_2;

	for_each_engine(engine, gt, id) {
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
				rq[i] = spinner_create_request(&client[i].spin,
							       client[i].ctx, engine,
							       MI_NOOP);
				if (IS_ERR(rq[i])) {
					err = PTR_ERR(rq[i]);
					goto err_wedged;
				}

				/* Disable NEWCLIENT promotion */
				__i915_active_fence_set(&i915_request_timeline(rq[i])->last_request,
							&dummy->fence);
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
					      I915_WAIT_PRIORITY,
					      1) != -ETIME) {
				pr_err("%s: Waiter depth:%d completed!\n",
				       engine->name, depth);
				goto err_wedged;
			}

			for (i = 0; i < ARRAY_SIZE(client); i++)
				igt_spinner_end(&client[i].spin);

			if (igt_flush_test(gt->i915))
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
	return err;

err_wedged:
	for (i = 0; i < ARRAY_SIZE(client); i++)
		igt_spinner_end(&client[i].spin);
	intel_gt_set_wedged(gt);
	err = -EIO;
	goto err_client_3;
}

static int live_chain_preempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct preempt_client hi, lo;
	enum intel_engine_id id;
	int err = -ENOMEM;

	/*
	 * Build a chain AB...BA between two contexts (A, B) and request
	 * preemption of the last request. It should then complete before
	 * the previously submitted spinner in B.
	 */

	if (!HAS_LOGICAL_RING_PREEMPTION(gt->i915))
		return 0;

	if (preempt_client_init(gt, &hi))
		return -ENOMEM;

	if (preempt_client_init(gt, &lo))
		goto err_client_hi;

	for_each_engine(engine, gt, id) {
		struct i915_sched_attr attr = {
			.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX),
		};
		struct igt_live_test t;
		struct i915_request *rq;
		int ring_size, count, i;

		if (!intel_engine_has_preemption(engine))
			continue;

		rq = spinner_create_request(&lo.spin,
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
		if (i915_request_wait(rq, 0, HZ / 2) < 0) {
			pr_err("Timed out waiting to flush %s\n", engine->name);
			goto err_wedged;
		}

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
			err = -EIO;
			goto err_wedged;
		}

		for_each_prime_number_from(count, 1, ring_size) {
			rq = spinner_create_request(&hi.spin,
						    hi.ctx, engine,
						    MI_ARB_CHECK);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);
			if (!igt_wait_for_spinner(&hi.spin, rq))
				goto err_wedged;

			rq = spinner_create_request(&lo.spin,
						    lo.ctx, engine,
						    MI_ARB_CHECK);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);

			for (i = 0; i < count; i++) {
				rq = igt_request_alloc(lo.ctx, engine);
				if (IS_ERR(rq))
					goto err_wedged;
				i915_request_add(rq);
			}

			rq = igt_request_alloc(hi.ctx, engine);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);
			engine->schedule(rq, &attr);

			igt_spinner_end(&hi.spin);
			if (i915_request_wait(rq, 0, HZ / 5) < 0) {
				struct drm_printer p =
					drm_info_printer(gt->i915->drm.dev);

				pr_err("Failed to preempt over chain of %d\n",
				       count);
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);
				goto err_wedged;
			}
			igt_spinner_end(&lo.spin);

			rq = igt_request_alloc(lo.ctx, engine);
			if (IS_ERR(rq))
				goto err_wedged;
			i915_request_add(rq);
			if (i915_request_wait(rq, 0, HZ / 5) < 0) {
				struct drm_printer p =
					drm_info_printer(gt->i915->drm.dev);

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
	return err;

err_wedged:
	igt_spinner_end(&hi.spin);
	igt_spinner_end(&lo.spin);
	intel_gt_set_wedged(gt);
	err = -EIO;
	goto err_client_lo;
}

static int live_preempt_hang(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct igt_spinner spin_hi, spin_lo;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = -ENOMEM;

	if (!HAS_LOGICAL_RING_PREEMPTION(gt->i915))
		return 0;

	if (!intel_has_reset_engine(gt))
		return 0;

	if (igt_spinner_init(&spin_hi, gt))
		return -ENOMEM;

	if (igt_spinner_init(&spin_lo, gt))
		goto err_spin_hi;

	ctx_hi = kernel_context(gt->i915);
	if (!ctx_hi)
		goto err_spin_lo;
	ctx_hi->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MAX_USER_PRIORITY);

	ctx_lo = kernel_context(gt->i915);
	if (!ctx_lo)
		goto err_ctx_hi;
	ctx_lo->sched.priority =
		I915_USER_PRIORITY(I915_CONTEXT_MIN_USER_PRIORITY);

	for_each_engine(engine, gt, id) {
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		rq = spinner_create_request(&spin_lo, ctx_lo, engine,
					    MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin_lo, rq)) {
			GEM_TRACE("lo spinner failed to start\n");
			GEM_TRACE_DUMP();
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = spinner_create_request(&spin_hi, ctx_hi, engine,
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
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto err_ctx_lo;
		}

		set_bit(I915_RESET_ENGINE + id, &gt->reset.flags);
		intel_engine_reset(engine, NULL);
		clear_bit(I915_RESET_ENGINE + id, &gt->reset.flags);

		engine->execlists.preempt_hang.inject_hang = false;

		if (!igt_wait_for_spinner(&spin_hi, rq)) {
			GEM_TRACE("hi spinner failed to start\n");
			GEM_TRACE_DUMP();
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto err_ctx_lo;
		}

		igt_spinner_end(&spin_hi);
		igt_spinner_end(&spin_lo);
		if (igt_flush_test(gt->i915)) {
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
	struct intel_gt *gt;
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
		struct i915_address_space *vm;

		vm = i915_gem_context_get_vm_rcu(ctx);
		vma = i915_vma_instance(batch, vm, NULL);
		i915_vm_put(vm);
		if (IS_ERR(vma))
			return PTR_ERR(vma);

		err = i915_vma_pin(vma, 0, 0, PIN_USER);
		if (err)
			return err;
	}

	ctx->sched.priority = prio;

	rq = igt_request_alloc(ctx, smoke->engine);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto unpin;
	}

	if (vma) {
		i915_vma_lock(vma);
		err = i915_request_await_object(rq, vma->obj, false);
		if (!err)
			err = i915_vma_move_to_active(vma, rq, 0);
		if (!err)
			err = rq->engine->emit_bb_start(rq,
							vma->node.start,
							PAGE_SIZE, 0);
		i915_vma_unlock(vma);
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

		err = smoke_submit(smoke,
				   ctx, count % I915_PRIORITY_MAX,
				   smoke->batch);
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

	for_each_engine(engine, smoke->gt, id) {
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
	for_each_engine(engine, smoke->gt, id) {
		int status;

		if (IS_ERR_OR_NULL(tsk[id]))
			continue;

		status = kthread_stop(tsk[id]);
		if (status && !err)
			err = status;

		count += arg[id].count;

		put_task_struct(tsk[id]);
	}

	pr_info("Submitted %lu crescendo:%x requests across %d engines and %d contexts\n",
		count, flags,
		RUNTIME_INFO(smoke->gt->i915)->num_engines, smoke->ncontext);
	return 0;
}

static int smoke_random(struct preempt_smoke *smoke, unsigned int flags)
{
	enum intel_engine_id id;
	IGT_TIMEOUT(end_time);
	unsigned long count;

	count = 0;
	do {
		for_each_engine(smoke->engine, smoke->gt, id) {
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
		RUNTIME_INFO(smoke->gt->i915)->num_engines, smoke->ncontext);
	return 0;
}

static int live_preempt_smoke(void *arg)
{
	struct preempt_smoke smoke = {
		.gt = arg,
		.prng = I915_RND_STATE_INITIALIZER(i915_selftest.random_seed),
		.ncontext = 1024,
	};
	const unsigned int phase[] = { 0, BATCH };
	struct igt_live_test t;
	int err = -ENOMEM;
	u32 *cs;
	int n;

	if (!HAS_LOGICAL_RING_PREEMPTION(smoke.gt->i915))
		return 0;

	smoke.contexts = kmalloc_array(smoke.ncontext,
				       sizeof(*smoke.contexts),
				       GFP_KERNEL);
	if (!smoke.contexts)
		return -ENOMEM;

	smoke.batch =
		i915_gem_object_create_internal(smoke.gt->i915, PAGE_SIZE);
	if (IS_ERR(smoke.batch)) {
		err = PTR_ERR(smoke.batch);
		goto err_free;
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

	if (igt_live_test_begin(&t, smoke.gt->i915, __func__, "all")) {
		err = -EIO;
		goto err_batch;
	}

	for (n = 0; n < smoke.ncontext; n++) {
		smoke.contexts[n] = kernel_context(smoke.gt->i915);
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
err_free:
	kfree(smoke.contexts);

	return err;
}

static int nop_virtual_engine(struct intel_gt *gt,
			      struct intel_engine_cs **siblings,
			      unsigned int nsibling,
			      unsigned int nctx,
			      unsigned int flags)
#define CHAIN BIT(0)
{
	IGT_TIMEOUT(end_time);
	struct i915_request *request[16];
	struct i915_gem_context *ctx[16];
	struct intel_context *ve[16];
	unsigned long n, prime, nc;
	struct igt_live_test t;
	ktime_t times[2] = {};
	int err;

	GEM_BUG_ON(!nctx || nctx > ARRAY_SIZE(ctx));

	for (n = 0; n < nctx; n++) {
		ctx[n] = kernel_context(gt->i915);
		if (!ctx[n]) {
			err = -ENOMEM;
			nctx = n;
			goto out;
		}

		ve[n] = intel_execlists_create_virtual(ctx[n],
						       siblings, nsibling);
		if (IS_ERR(ve[n])) {
			kernel_context_close(ctx[n]);
			err = PTR_ERR(ve[n]);
			nctx = n;
			goto out;
		}

		err = intel_context_pin(ve[n]);
		if (err) {
			intel_context_put(ve[n]);
			kernel_context_close(ctx[n]);
			nctx = n;
			goto out;
		}
	}

	err = igt_live_test_begin(&t, gt->i915, __func__, ve[0]->engine->name);
	if (err)
		goto out;

	for_each_prime_number_from(prime, 1, 8192) {
		times[1] = ktime_get_raw();

		if (flags & CHAIN) {
			for (nc = 0; nc < nctx; nc++) {
				for (n = 0; n < prime; n++) {
					request[nc] =
						i915_request_create(ve[nc]);
					if (IS_ERR(request[nc])) {
						err = PTR_ERR(request[nc]);
						goto out;
					}

					i915_request_add(request[nc]);
				}
			}
		} else {
			for (n = 0; n < prime; n++) {
				for (nc = 0; nc < nctx; nc++) {
					request[nc] =
						i915_request_create(ve[nc]);
					if (IS_ERR(request[nc])) {
						err = PTR_ERR(request[nc]);
						goto out;
					}

					i915_request_add(request[nc]);
				}
			}
		}

		for (nc = 0; nc < nctx; nc++) {
			if (i915_request_wait(request[nc], 0, HZ / 10) < 0) {
				pr_err("%s(%s): wait for %llx:%lld timed out\n",
				       __func__, ve[0]->engine->name,
				       request[nc]->fence.context,
				       request[nc]->fence.seqno);

				GEM_TRACE("%s(%s) failed at request %llx:%lld\n",
					  __func__, ve[0]->engine->name,
					  request[nc]->fence.context,
					  request[nc]->fence.seqno);
				GEM_TRACE_DUMP();
				intel_gt_set_wedged(gt);
				break;
			}
		}

		times[1] = ktime_sub(ktime_get_raw(), times[1]);
		if (prime == 1)
			times[0] = times[1];

		if (__igt_timeout(end_time, NULL))
			break;
	}

	err = igt_live_test_end(&t);
	if (err)
		goto out;

	pr_info("Requestx%d latencies on %s: 1 = %lluns, %lu = %lluns\n",
		nctx, ve[0]->engine->name, ktime_to_ns(times[0]),
		prime, div64_u64(ktime_to_ns(times[1]), prime));

out:
	if (igt_flush_test(gt->i915))
		err = -EIO;

	for (nc = 0; nc < nctx; nc++) {
		intel_context_unpin(ve[nc]);
		intel_context_put(ve[nc]);
		kernel_context_close(ctx[nc]);
	}
	return err;
}

static int live_virtual_engine(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned int class, inst;
	int err;

	if (USES_GUC_SUBMISSION(gt->i915))
		return 0;

	for_each_engine(engine, gt, id) {
		err = nop_virtual_engine(gt, &engine, 1, 1, 0);
		if (err) {
			pr_err("Failed to wrap engine %s: err=%d\n",
			       engine->name, err);
			return err;
		}
	}

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		int nsibling, n;

		nsibling = 0;
		for (inst = 0; inst <= MAX_ENGINE_INSTANCE; inst++) {
			if (!gt->engine_class[class][inst])
				continue;

			siblings[nsibling++] = gt->engine_class[class][inst];
		}
		if (nsibling < 2)
			continue;

		for (n = 1; n <= nsibling + 1; n++) {
			err = nop_virtual_engine(gt, siblings, nsibling,
						 n, 0);
			if (err)
				return err;
		}

		err = nop_virtual_engine(gt, siblings, nsibling, n, CHAIN);
		if (err)
			return err;
	}

	return 0;
}

static int mask_virtual_engine(struct intel_gt *gt,
			       struct intel_engine_cs **siblings,
			       unsigned int nsibling)
{
	struct i915_request *request[MAX_ENGINE_INSTANCE + 1];
	struct i915_gem_context *ctx;
	struct intel_context *ve;
	struct igt_live_test t;
	unsigned int n;
	int err;

	/*
	 * Check that by setting the execution mask on a request, we can
	 * restrict it to our desired engine within the virtual engine.
	 */

	ctx = kernel_context(gt->i915);
	if (!ctx)
		return -ENOMEM;

	ve = intel_execlists_create_virtual(ctx, siblings, nsibling);
	if (IS_ERR(ve)) {
		err = PTR_ERR(ve);
		goto out_close;
	}

	err = intel_context_pin(ve);
	if (err)
		goto out_put;

	err = igt_live_test_begin(&t, gt->i915, __func__, ve->engine->name);
	if (err)
		goto out_unpin;

	for (n = 0; n < nsibling; n++) {
		request[n] = i915_request_create(ve);
		if (IS_ERR(request[n])) {
			err = PTR_ERR(request[n]);
			nsibling = n;
			goto out;
		}

		/* Reverse order as it's more likely to be unnatural */
		request[n]->execution_mask = siblings[nsibling - n - 1]->mask;

		i915_request_get(request[n]);
		i915_request_add(request[n]);
	}

	for (n = 0; n < nsibling; n++) {
		if (i915_request_wait(request[n], 0, HZ / 10) < 0) {
			pr_err("%s(%s): wait for %llx:%lld timed out\n",
			       __func__, ve->engine->name,
			       request[n]->fence.context,
			       request[n]->fence.seqno);

			GEM_TRACE("%s(%s) failed at request %llx:%lld\n",
				  __func__, ve->engine->name,
				  request[n]->fence.context,
				  request[n]->fence.seqno);
			GEM_TRACE_DUMP();
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto out;
		}

		if (request[n]->engine != siblings[nsibling - n - 1]) {
			pr_err("Executed on wrong sibling '%s', expected '%s'\n",
			       request[n]->engine->name,
			       siblings[nsibling - n - 1]->name);
			err = -EINVAL;
			goto out;
		}
	}

	err = igt_live_test_end(&t);
out:
	if (igt_flush_test(gt->i915))
		err = -EIO;

	for (n = 0; n < nsibling; n++)
		i915_request_put(request[n]);

out_unpin:
	intel_context_unpin(ve);
out_put:
	intel_context_put(ve);
out_close:
	kernel_context_close(ctx);
	return err;
}

static int live_virtual_mask(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	unsigned int class, inst;
	int err;

	if (USES_GUC_SUBMISSION(gt->i915))
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		unsigned int nsibling;

		nsibling = 0;
		for (inst = 0; inst <= MAX_ENGINE_INSTANCE; inst++) {
			if (!gt->engine_class[class][inst])
				break;

			siblings[nsibling++] = gt->engine_class[class][inst];
		}
		if (nsibling < 2)
			continue;

		err = mask_virtual_engine(gt, siblings, nsibling);
		if (err)
			return err;
	}

	return 0;
}

static int preserved_virtual_engine(struct intel_gt *gt,
				    struct intel_engine_cs **siblings,
				    unsigned int nsibling)
{
	struct i915_request *last = NULL;
	struct i915_gem_context *ctx;
	struct intel_context *ve;
	struct i915_vma *scratch;
	struct igt_live_test t;
	unsigned int n;
	int err = 0;
	u32 *cs;

	ctx = kernel_context(gt->i915);
	if (!ctx)
		return -ENOMEM;

	scratch = create_scratch(siblings[0]->gt);
	if (IS_ERR(scratch)) {
		err = PTR_ERR(scratch);
		goto out_close;
	}

	ve = intel_execlists_create_virtual(ctx, siblings, nsibling);
	if (IS_ERR(ve)) {
		err = PTR_ERR(ve);
		goto out_scratch;
	}

	err = intel_context_pin(ve);
	if (err)
		goto out_put;

	err = igt_live_test_begin(&t, gt->i915, __func__, ve->engine->name);
	if (err)
		goto out_unpin;

	for (n = 0; n < NUM_GPR_DW; n++) {
		struct intel_engine_cs *engine = siblings[n % nsibling];
		struct i915_request *rq;

		rq = i915_request_create(ve);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_end;
		}

		i915_request_put(last);
		last = i915_request_get(rq);

		cs = intel_ring_begin(rq, 8);
		if (IS_ERR(cs)) {
			i915_request_add(rq);
			err = PTR_ERR(cs);
			goto out_end;
		}

		*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
		*cs++ = CS_GPR(engine, n);
		*cs++ = i915_ggtt_offset(scratch) + n * sizeof(u32);
		*cs++ = 0;

		*cs++ = MI_LOAD_REGISTER_IMM(1);
		*cs++ = CS_GPR(engine, (n + 1) % NUM_GPR_DW);
		*cs++ = n + 1;

		*cs++ = MI_NOOP;
		intel_ring_advance(rq, cs);

		/* Restrict this request to run on a particular engine */
		rq->execution_mask = engine->mask;
		i915_request_add(rq);
	}

	if (i915_request_wait(last, 0, HZ / 5) < 0) {
		err = -ETIME;
		goto out_end;
	}

	cs = i915_gem_object_pin_map(scratch->obj, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto out_end;
	}

	for (n = 0; n < NUM_GPR_DW; n++) {
		if (cs[n] != n) {
			pr_err("Incorrect value[%d] found for GPR[%d]\n",
			       cs[n], n);
			err = -EINVAL;
			break;
		}
	}

	i915_gem_object_unpin_map(scratch->obj);

out_end:
	if (igt_live_test_end(&t))
		err = -EIO;
	i915_request_put(last);
out_unpin:
	intel_context_unpin(ve);
out_put:
	intel_context_put(ve);
out_scratch:
	i915_vma_unpin_and_release(&scratch, 0);
out_close:
	kernel_context_close(ctx);
	return err;
}

static int live_virtual_preserved(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	unsigned int class, inst;

	/*
	 * Check that the context image retains non-privileged (user) registers
	 * from one engine to the next. For this we check that the CS_GPR
	 * are preserved.
	 */

	if (USES_GUC_SUBMISSION(gt->i915))
		return 0;

	/* As we use CS_GPR we cannot run before they existed on all engines. */
	if (INTEL_GEN(gt->i915) < 9)
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		int nsibling, err;

		nsibling = 0;
		for (inst = 0; inst <= MAX_ENGINE_INSTANCE; inst++) {
			if (!gt->engine_class[class][inst])
				continue;

			siblings[nsibling++] = gt->engine_class[class][inst];
		}
		if (nsibling < 2)
			continue;

		err = preserved_virtual_engine(gt, siblings, nsibling);
		if (err)
			return err;
	}

	return 0;
}

static int bond_virtual_engine(struct intel_gt *gt,
			       unsigned int class,
			       struct intel_engine_cs **siblings,
			       unsigned int nsibling,
			       unsigned int flags)
#define BOND_SCHEDULE BIT(0)
{
	struct intel_engine_cs *master;
	struct i915_gem_context *ctx;
	struct i915_request *rq[16];
	enum intel_engine_id id;
	unsigned long n;
	int err;

	GEM_BUG_ON(nsibling >= ARRAY_SIZE(rq) - 1);

	ctx = kernel_context(gt->i915);
	if (!ctx)
		return -ENOMEM;

	err = 0;
	rq[0] = ERR_PTR(-ENOMEM);
	for_each_engine(master, gt, id) {
		struct i915_sw_fence fence = {};

		if (master->class == class)
			continue;

		memset_p((void *)rq, ERR_PTR(-EINVAL), ARRAY_SIZE(rq));

		rq[0] = igt_request_alloc(ctx, master);
		if (IS_ERR(rq[0])) {
			err = PTR_ERR(rq[0]);
			goto out;
		}
		i915_request_get(rq[0]);

		if (flags & BOND_SCHEDULE) {
			onstack_fence_init(&fence);
			err = i915_sw_fence_await_sw_fence_gfp(&rq[0]->submit,
							       &fence,
							       GFP_KERNEL);
		}
		i915_request_add(rq[0]);
		if (err < 0)
			goto out;

		for (n = 0; n < nsibling; n++) {
			struct intel_context *ve;

			ve = intel_execlists_create_virtual(ctx,
							    siblings,
							    nsibling);
			if (IS_ERR(ve)) {
				err = PTR_ERR(ve);
				onstack_fence_fini(&fence);
				goto out;
			}

			err = intel_virtual_engine_attach_bond(ve->engine,
							       master,
							       siblings[n]);
			if (err) {
				intel_context_put(ve);
				onstack_fence_fini(&fence);
				goto out;
			}

			err = intel_context_pin(ve);
			intel_context_put(ve);
			if (err) {
				onstack_fence_fini(&fence);
				goto out;
			}

			rq[n + 1] = i915_request_create(ve);
			intel_context_unpin(ve);
			if (IS_ERR(rq[n + 1])) {
				err = PTR_ERR(rq[n + 1]);
				onstack_fence_fini(&fence);
				goto out;
			}
			i915_request_get(rq[n + 1]);

			err = i915_request_await_execution(rq[n + 1],
							   &rq[0]->fence,
							   ve->engine->bond_execute);
			i915_request_add(rq[n + 1]);
			if (err < 0) {
				onstack_fence_fini(&fence);
				goto out;
			}
		}
		onstack_fence_fini(&fence);

		if (i915_request_wait(rq[0], 0, HZ / 10) < 0) {
			pr_err("Master request did not execute (on %s)!\n",
			       rq[0]->engine->name);
			err = -EIO;
			goto out;
		}

		for (n = 0; n < nsibling; n++) {
			if (i915_request_wait(rq[n + 1], 0,
					      MAX_SCHEDULE_TIMEOUT) < 0) {
				err = -EIO;
				goto out;
			}

			if (rq[n + 1]->engine != siblings[n]) {
				pr_err("Bonded request did not execute on target engine: expected %s, used %s; master was %s\n",
				       siblings[n]->name,
				       rq[n + 1]->engine->name,
				       rq[0]->engine->name);
				err = -EINVAL;
				goto out;
			}
		}

		for (n = 0; !IS_ERR(rq[n]); n++)
			i915_request_put(rq[n]);
		rq[0] = ERR_PTR(-ENOMEM);
	}

out:
	for (n = 0; !IS_ERR(rq[n]); n++)
		i915_request_put(rq[n]);
	if (igt_flush_test(gt->i915))
		err = -EIO;

	kernel_context_close(ctx);
	return err;
}

static int live_virtual_bond(void *arg)
{
	static const struct phase {
		const char *name;
		unsigned int flags;
	} phases[] = {
		{ "", 0 },
		{ "schedule", BOND_SCHEDULE },
		{ },
	};
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	unsigned int class, inst;
	int err;

	if (USES_GUC_SUBMISSION(gt->i915))
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		const struct phase *p;
		int nsibling;

		nsibling = 0;
		for (inst = 0; inst <= MAX_ENGINE_INSTANCE; inst++) {
			if (!gt->engine_class[class][inst])
				break;

			GEM_BUG_ON(nsibling == ARRAY_SIZE(siblings));
			siblings[nsibling++] = gt->engine_class[class][inst];
		}
		if (nsibling < 2)
			continue;

		for (p = phases; p->name; p++) {
			err = bond_virtual_engine(gt,
						  class, siblings, nsibling,
						  p->flags);
			if (err) {
				pr_err("%s(%s): failed class=%d, nsibling=%d, err=%d\n",
				       __func__, p->name, class, nsibling, err);
				return err;
			}
		}
	}

	return 0;
}

int intel_execlists_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_sanitycheck),
		SUBTEST(live_unlite_switch),
		SUBTEST(live_unlite_preempt),
		SUBTEST(live_timeslice_preempt),
		SUBTEST(live_timeslice_queue),
		SUBTEST(live_busywait_preempt),
		SUBTEST(live_preempt),
		SUBTEST(live_late_preempt),
		SUBTEST(live_nopreempt),
		SUBTEST(live_suppress_self_preempt),
		SUBTEST(live_suppress_wait_preempt),
		SUBTEST(live_chain_preempt),
		SUBTEST(live_preempt_hang),
		SUBTEST(live_preempt_smoke),
		SUBTEST(live_virtual_engine),
		SUBTEST(live_virtual_mask),
		SUBTEST(live_virtual_preserved),
		SUBTEST(live_virtual_bond),
	};

	if (!HAS_EXECLISTS(i915))
		return 0;

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}

static void hexdump(const void *buf, size_t len)
{
	const size_t rowsize = 8 * sizeof(u32);
	const void *prev = NULL;
	bool skip = false;
	size_t pos;

	for (pos = 0; pos < len; pos += rowsize) {
		char line[128];

		if (prev && !memcmp(prev, buf + pos, rowsize)) {
			if (!skip) {
				pr_info("*\n");
				skip = true;
			}
			continue;
		}

		WARN_ON_ONCE(hex_dump_to_buffer(buf + pos, len - pos,
						rowsize, sizeof(u32),
						line, sizeof(line),
						false) >= sizeof(line));
		pr_info("[%04zx] %s\n", pos, line);

		prev = buf + pos;
		skip = false;
	}
}

static int live_lrc_layout(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	u32 *mem;
	int err;

	/*
	 * Check the registers offsets we use to create the initial reg state
	 * match the layout saved by HW.
	 */

	mem = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!mem)
		return -ENOMEM;

	err = 0;
	for_each_engine(engine, gt, id) {
		u32 *hw, *lrc;
		int dw;

		if (!engine->default_state)
			continue;

		hw = i915_gem_object_pin_map(engine->default_state,
					     I915_MAP_WB);
		if (IS_ERR(hw)) {
			err = PTR_ERR(hw);
			break;
		}
		hw += LRC_STATE_PN * PAGE_SIZE / sizeof(*hw);

		lrc = memset(mem, 0, PAGE_SIZE);
		execlists_init_reg_state(lrc,
					 engine->kernel_context,
					 engine,
					 engine->kernel_context->ring,
					 true);

		dw = 0;
		do {
			u32 lri = hw[dw];

			if (lri == 0) {
				dw++;
				continue;
			}

			if ((lri & GENMASK(31, 23)) != MI_INSTR(0x22, 0)) {
				pr_err("%s: Expected LRI command at dword %d, found %08x\n",
				       engine->name, dw, lri);
				err = -EINVAL;
				break;
			}

			if (lrc[dw] != lri) {
				pr_err("%s: LRI command mismatch at dword %d, expected %08x found %08x\n",
				       engine->name, dw, lri, lrc[dw]);
				err = -EINVAL;
				break;
			}

			lri &= 0x7f;
			lri++;
			dw++;

			while (lri) {
				if (hw[dw] != lrc[dw]) {
					pr_err("%s: Different registers found at dword %d, expected %x, found %x\n",
					       engine->name, dw, hw[dw], lrc[dw]);
					err = -EINVAL;
					break;
				}

				/*
				 * Skip over the actual register value as we
				 * expect that to differ.
				 */
				dw += 2;
				lri -= 2;
			}
		} while ((lrc[dw] & ~BIT(0)) != MI_BATCH_BUFFER_END);

		if (err) {
			pr_info("%s: HW register image:\n", engine->name);
			hexdump(hw, PAGE_SIZE);

			pr_info("%s: SW register image:\n", engine->name);
			hexdump(lrc, PAGE_SIZE);
		}

		i915_gem_object_unpin_map(engine->default_state);
		if (err)
			break;
	}

	kfree(mem);
	return err;
}

static int __live_lrc_state(struct i915_gem_context *fixme,
			    struct intel_engine_cs *engine,
			    struct i915_vma *scratch)
{
	struct intel_context *ce;
	struct i915_request *rq;
	enum {
		RING_START_IDX = 0,
		RING_TAIL_IDX,
		MAX_IDX
	};
	u32 expected[MAX_IDX];
	u32 *cs;
	int err;
	int n;

	ce = intel_context_create(fixme, engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	err = intel_context_pin(ce);
	if (err)
		goto err_put;

	rq = i915_request_create(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_unpin;
	}

	cs = intel_ring_begin(rq, 4 * MAX_IDX);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		i915_request_add(rq);
		goto err_unpin;
	}

	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = i915_mmio_reg_offset(RING_START(engine->mmio_base));
	*cs++ = i915_ggtt_offset(scratch) + RING_START_IDX * sizeof(u32);
	*cs++ = 0;

	expected[RING_START_IDX] = i915_ggtt_offset(ce->ring->vma);

	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = i915_mmio_reg_offset(RING_TAIL(engine->mmio_base));
	*cs++ = i915_ggtt_offset(scratch) + RING_TAIL_IDX * sizeof(u32);
	*cs++ = 0;

	i915_request_get(rq);
	i915_request_add(rq);

	intel_engine_flush_submission(engine);
	expected[RING_TAIL_IDX] = ce->ring->tail;

	if (i915_request_wait(rq, 0, HZ / 5) < 0) {
		err = -ETIME;
		goto err_rq;
	}

	cs = i915_gem_object_pin_map(scratch->obj, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_rq;
	}

	for (n = 0; n < MAX_IDX; n++) {
		if (cs[n] != expected[n]) {
			pr_err("%s: Stored register[%d] value[0x%x] did not match expected[0x%x]\n",
			       engine->name, n, cs[n], expected[n]);
			err = -EINVAL;
			break;
		}
	}

	i915_gem_object_unpin_map(scratch->obj);

err_rq:
	i915_request_put(rq);
err_unpin:
	intel_context_unpin(ce);
err_put:
	intel_context_put(ce);
	return err;
}

static int live_lrc_state(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *fixme;
	struct i915_vma *scratch;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check the live register state matches what we expect for this
	 * intel_context.
	 */

	fixme = kernel_context(gt->i915);
	if (!fixme)
		return -ENOMEM;

	scratch = create_scratch(gt);
	if (IS_ERR(scratch)) {
		err = PTR_ERR(scratch);
		goto out_close;
	}

	for_each_engine(engine, gt, id) {
		err = __live_lrc_state(fixme, engine, scratch);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	i915_vma_unpin_and_release(&scratch, 0);
out_close:
	kernel_context_close(fixme);
	return err;
}

static int gpr_make_dirty(struct intel_engine_cs *engine)
{
	struct i915_request *rq;
	u32 *cs;
	int n;

	rq = i915_request_create(engine->kernel_context);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 2 * NUM_GPR_DW + 2);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	*cs++ = MI_LOAD_REGISTER_IMM(NUM_GPR_DW);
	for (n = 0; n < NUM_GPR_DW; n++) {
		*cs++ = CS_GPR(engine, n);
		*cs++ = STACK_MAGIC;
	}
	*cs++ = MI_NOOP;

	intel_ring_advance(rq, cs);
	i915_request_add(rq);

	return 0;
}

static int __live_gpr_clear(struct i915_gem_context *fixme,
			    struct intel_engine_cs *engine,
			    struct i915_vma *scratch)
{
	struct intel_context *ce;
	struct i915_request *rq;
	u32 *cs;
	int err;
	int n;

	if (INTEL_GEN(engine->i915) < 9 && engine->class != RENDER_CLASS)
		return 0; /* GPR only on rcs0 for gen8 */

	err = gpr_make_dirty(engine);
	if (err)
		return err;

	ce = intel_context_create(fixme, engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_put;
	}

	cs = intel_ring_begin(rq, 4 * NUM_GPR_DW);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		i915_request_add(rq);
		goto err_put;
	}

	for (n = 0; n < NUM_GPR_DW; n++) {
		*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
		*cs++ = CS_GPR(engine, n);
		*cs++ = i915_ggtt_offset(scratch) + n * sizeof(u32);
		*cs++ = 0;
	}

	i915_request_get(rq);
	i915_request_add(rq);

	if (i915_request_wait(rq, 0, HZ / 5) < 0) {
		err = -ETIME;
		goto err_rq;
	}

	cs = i915_gem_object_pin_map(scratch->obj, I915_MAP_WB);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_rq;
	}

	for (n = 0; n < NUM_GPR_DW; n++) {
		if (cs[n]) {
			pr_err("%s: GPR[%d].%s was not zero, found 0x%08x!\n",
			       engine->name,
			       n / 2, n & 1 ? "udw" : "ldw",
			       cs[n]);
			err = -EINVAL;
			break;
		}
	}

	i915_gem_object_unpin_map(scratch->obj);

err_rq:
	i915_request_put(rq);
err_put:
	intel_context_put(ce);
	return err;
}

static int live_gpr_clear(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_gem_context *fixme;
	struct i915_vma *scratch;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check that GPR registers are cleared in new contexts as we need
	 * to avoid leaking any information from previous contexts.
	 */

	fixme = kernel_context(gt->i915);
	if (!fixme)
		return -ENOMEM;

	scratch = create_scratch(gt);
	if (IS_ERR(scratch)) {
		err = PTR_ERR(scratch);
		goto out_close;
	}

	for_each_engine(engine, gt, id) {
		err = __live_gpr_clear(fixme, engine, scratch);
		if (err)
			break;
	}

	if (igt_flush_test(gt->i915))
		err = -EIO;

	i915_vma_unpin_and_release(&scratch, 0);
out_close:
	kernel_context_close(fixme);
	return err;
}

int intel_lrc_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_lrc_layout),
		SUBTEST(live_lrc_state),
		SUBTEST(live_gpr_clear),
	};

	if (!HAS_LOGICAL_RING_CONTEXTS(i915))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
