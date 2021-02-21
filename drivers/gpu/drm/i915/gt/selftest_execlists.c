/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

#include <linux/prime_numbers.h>

#include "gem/i915_gem_pm.h"
#include "gt/intel_engine_heartbeat.h"
#include "gt/intel_reset.h"
#include "gt/selftest_engine_heartbeat.h"

#include "i915_selftest.h"
#include "selftests/i915_random.h"
#include "selftests/igt_flush_test.h"
#include "selftests/igt_live_test.h"
#include "selftests/igt_spinner.h"
#include "selftests/lib_sw_fence.h"

#include "gem/selftests/igt_gem_utils.h"
#include "gem/selftests/mock_context.h"

#define CS_GPR(engine, n) ((engine)->mmio_base + 0x600 + (n) * 4)
#define NUM_GPR 16
#define NUM_GPR_DW (NUM_GPR * 2) /* each GPR is 2 dwords */

static bool is_active(struct i915_request *rq)
{
	if (i915_request_is_active(rq))
		return true;

	if (i915_request_on_hold(rq))
		return true;

	if (i915_request_has_initial_breadcrumb(rq) && i915_request_started(rq))
		return true;

	return false;
}

static int wait_for_submit(struct intel_engine_cs *engine,
			   struct i915_request *rq,
			   unsigned long timeout)
{
	/* Ignore our own attempts to suppress excess tasklets */
	tasklet_hi_schedule(&engine->execlists.tasklet);

	timeout += jiffies;
	do {
		bool done = time_after(jiffies, timeout);

		if (i915_request_completed(rq)) /* that was quick! */
			return 0;

		/* Wait until the HW has acknowleged the submission (or err) */
		intel_engine_flush_submission(engine);
		if (!READ_ONCE(engine->execlists.pending[0]) && is_active(rq))
			return 0;

		if (done)
			return -ETIME;

		cond_resched();
	} while (1);
}

static int wait_for_reset(struct intel_engine_cs *engine,
			  struct i915_request *rq,
			  unsigned long timeout)
{
	timeout += jiffies;

	do {
		cond_resched();
		intel_engine_flush_submission(engine);

		if (READ_ONCE(engine->execlists.pending[0]))
			continue;

		if (i915_request_completed(rq))
			break;

		if (READ_ONCE(rq->fence.error))
			break;
	} while (time_before(jiffies, timeout));

	flush_scheduled_work();

	if (rq->fence.error != -EIO) {
		pr_err("%s: hanging request %llx:%lld not reset\n",
		       engine->name,
		       rq->fence.context,
		       rq->fence.seqno);
		return -EINVAL;
	}

	/* Give the request a jiffie to complete after flushing the worker */
	if (i915_request_wait(rq, 0,
			      max(0l, (long)(timeout - jiffies)) + 1) < 0) {
		pr_err("%s: hanging request %llx:%lld did not complete\n",
		       engine->name,
		       rq->fence.context,
		       rq->fence.seqno);
		return -ETIME;
	}

	return 0;
}

static int live_sanitycheck(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = 0;

	if (!HAS_LOGICAL_RING_CONTEXTS(gt->i915))
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		struct i915_request *rq;

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			break;
		}

		rq = igt_spinner_create_request(&spin, ce, MI_NOOP);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_ctx;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin, rq)) {
			GEM_TRACE("spinner failed to start\n");
			GEM_TRACE_DUMP();
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto out_ctx;
		}

		igt_spinner_end(&spin);
		if (igt_flush_test(gt->i915)) {
			err = -EIO;
			goto out_ctx;
		}

out_ctx:
		intel_context_put(ce);
		if (err)
			break;
	}

	igt_spinner_fini(&spin);
	return err;
}

static int live_unlite_restore(struct intel_gt *gt, int prio)
{
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = -ENOMEM;

	/*
	 * Check that we can correctly context switch between 2 instances
	 * on the same engine from the same parent context.
	 */

	if (igt_spinner_init(&spin, gt))
		return err;

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
		st_engine_heartbeat_disable(engine);

		for (n = 0; n < ARRAY_SIZE(ce); n++) {
			struct intel_context *tmp;

			tmp = intel_context_create(engine);
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
		lrc_update_regs(ce[1], engine, ce[1]->ring->head);

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
		intel_engine_flush_submission(engine);
		igt_spinner_end(&spin);
		for (n = 0; n < ARRAY_SIZE(ce); n++) {
			if (IS_ERR_OR_NULL(ce[n]))
				break;

			intel_context_unpin(ce[n]);
			intel_context_put(ce[n]);
		}

		st_engine_heartbeat_enable(engine);
		if (igt_live_test_end(&t))
			err = -EIO;
		if (err)
			break;
	}

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

static int live_unlite_ring(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct igt_spinner spin;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Setup a preemption event that will cause almost the entire ring
	 * to be unwound, potentially fooling our intel_ring_direction()
	 * into emitting a forward lite-restore instead of the rollback.
	 */

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	for_each_engine(engine, gt, id) {
		struct intel_context *ce[2] = {};
		struct i915_request *rq;
		struct igt_live_test t;
		int n;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (!intel_engine_can_store_dword(engine))
			continue;

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
			err = -EIO;
			break;
		}
		st_engine_heartbeat_disable(engine);

		for (n = 0; n < ARRAY_SIZE(ce); n++) {
			struct intel_context *tmp;

			tmp = intel_context_create(engine);
			if (IS_ERR(tmp)) {
				err = PTR_ERR(tmp);
				goto err_ce;
			}

			err = intel_context_pin(tmp);
			if (err) {
				intel_context_put(tmp);
				goto err_ce;
			}

			memset32(tmp->ring->vaddr,
				 0xdeadbeef, /* trigger a hang if executed */
				 tmp->ring->vma->size / sizeof(u32));

			ce[n] = tmp;
		}

		/* Create max prio spinner, followed by N low prio nops */
		rq = igt_spinner_create_request(&spin, ce[0], MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ce;
		}

		i915_request_get(rq);
		rq->sched.attr.priority = I915_PRIORITY_BARRIER;
		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			intel_gt_set_wedged(gt);
			i915_request_put(rq);
			err = -ETIME;
			goto err_ce;
		}

		/* Fill the ring, until we will cause a wrap */
		n = 0;
		while (intel_ring_direction(ce[0]->ring,
					    rq->wa_tail,
					    ce[0]->ring->tail) <= 0) {
			struct i915_request *tmp;

			tmp = intel_context_create_request(ce[0]);
			if (IS_ERR(tmp)) {
				err = PTR_ERR(tmp);
				i915_request_put(rq);
				goto err_ce;
			}

			i915_request_add(tmp);
			intel_engine_flush_submission(engine);
			n++;
		}
		intel_engine_flush_submission(engine);
		pr_debug("%s: Filled ring with %d nop tails {size:%x, tail:%x, emit:%x, rq.tail:%x}\n",
			 engine->name, n,
			 ce[0]->ring->size,
			 ce[0]->ring->tail,
			 ce[0]->ring->emit,
			 rq->tail);
		GEM_BUG_ON(intel_ring_direction(ce[0]->ring,
						rq->tail,
						ce[0]->ring->tail) <= 0);
		i915_request_put(rq);

		/* Create a second ring to preempt the first ring after rq[0] */
		rq = intel_context_create_request(ce[1]);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ce;
		}

		rq->sched.attr.priority = I915_PRIORITY_BARRIER;
		i915_request_get(rq);
		i915_request_add(rq);

		err = wait_for_submit(engine, rq, HZ / 2);
		i915_request_put(rq);
		if (err) {
			pr_err("%s: preemption request was not submitted\n",
			       engine->name);
			err = -ETIME;
		}

		pr_debug("%s: ring[0]:{ tail:%x, emit:%x }, ring[1]:{ tail:%x, emit:%x }\n",
			 engine->name,
			 ce[0]->ring->tail, ce[0]->ring->emit,
			 ce[1]->ring->tail, ce[1]->ring->emit);

err_ce:
		intel_engine_flush_submission(engine);
		igt_spinner_end(&spin);
		for (n = 0; n < ARRAY_SIZE(ce); n++) {
			if (IS_ERR_OR_NULL(ce[n]))
				break;

			intel_context_unpin(ce[n]);
			intel_context_put(ce[n]);
		}
		st_engine_heartbeat_enable(engine);
		if (igt_live_test_end(&t))
			err = -EIO;
		if (err)
			break;
	}

	igt_spinner_fini(&spin);
	return err;
}

static int live_pin_rewind(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * We have to be careful not to trust intel_ring too much, for example
	 * ring->head is updated upon retire which is out of sync with pinning
	 * the context. Thus we cannot use ring->head to set CTX_RING_HEAD,
	 * or else we risk writing an older, stale value.
	 *
	 * To simulate this, let's apply a bit of deliberate sabotague.
	 */

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		struct i915_request *rq;
		struct intel_ring *ring;
		struct igt_live_test t;

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
			err = -EIO;
			break;
		}

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			break;
		}

		err = intel_context_pin(ce);
		if (err) {
			intel_context_put(ce);
			break;
		}

		/* Keep the context awake while we play games */
		err = i915_active_acquire(&ce->active);
		if (err) {
			intel_context_unpin(ce);
			intel_context_put(ce);
			break;
		}
		ring = ce->ring;

		/* Poison the ring, and offset the next request from HEAD */
		memset32(ring->vaddr, STACK_MAGIC, ring->size / sizeof(u32));
		ring->emit = ring->size / 2;
		ring->tail = ring->emit;
		GEM_BUG_ON(ring->head);

		intel_context_unpin(ce);

		/* Submit a simple nop request */
		GEM_BUG_ON(intel_context_is_pinned(ce));
		rq = intel_context_create_request(ce);
		i915_active_release(&ce->active); /* e.g. async retire */
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			break;
		}
		GEM_BUG_ON(!rq->head);
		i915_request_add(rq);

		/* Expect not to hang! */
		if (igt_live_test_end(&t)) {
			err = -EIO;
			break;
		}
	}

	return err;
}

static int live_hold_reset(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = 0;

	/*
	 * In order to support offline error capture for fast preempt reset,
	 * we need to decouple the guilty request and ensure that it and its
	 * descendents are not executed while the capture is in progress.
	 */

	if (!intel_has_reset_engine(gt))
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		struct i915_request *rq;

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			break;
		}

		st_engine_heartbeat_disable(engine);

		rq = igt_spinner_create_request(&spin, ce, MI_ARB_CHECK);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out;
		}
		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			intel_gt_set_wedged(gt);
			err = -ETIME;
			goto out;
		}

		/* We have our request executing, now remove it and reset */

		local_bh_disable();
		if (test_and_set_bit(I915_RESET_ENGINE + id,
				     &gt->reset.flags)) {
			local_bh_enable();
			intel_gt_set_wedged(gt);
			err = -EBUSY;
			goto out;
		}
		tasklet_disable(&engine->execlists.tasklet);

		engine->execlists.tasklet.func(engine->execlists.tasklet.data);
		GEM_BUG_ON(execlists_active(&engine->execlists) != rq);

		i915_request_get(rq);
		execlists_hold(engine, rq);
		GEM_BUG_ON(!i915_request_on_hold(rq));

		__intel_engine_reset_bh(engine, NULL);
		GEM_BUG_ON(rq->fence.error != -EIO);

		tasklet_enable(&engine->execlists.tasklet);
		clear_and_wake_up_bit(I915_RESET_ENGINE + id,
				      &gt->reset.flags);
		local_bh_enable();

		/* Check that we do not resubmit the held request */
		if (!i915_request_wait(rq, 0, HZ / 5)) {
			pr_err("%s: on hold request completed!\n",
			       engine->name);
			i915_request_put(rq);
			err = -EIO;
			goto out;
		}
		GEM_BUG_ON(!i915_request_on_hold(rq));

		/* But is resubmitted on release */
		execlists_unhold(engine, rq);
		if (i915_request_wait(rq, 0, HZ / 5) < 0) {
			pr_err("%s: held request did not complete!\n",
			       engine->name);
			intel_gt_set_wedged(gt);
			err = -ETIME;
		}
		i915_request_put(rq);

out:
		st_engine_heartbeat_enable(engine);
		intel_context_put(ce);
		if (err)
			break;
	}

	igt_spinner_fini(&spin);
	return err;
}

static const char *error_repr(int err)
{
	return err ? "bad" : "good";
}

static int live_error_interrupt(void *arg)
{
	static const struct error_phase {
		enum { GOOD = 0, BAD = -EIO } error[2];
	} phases[] = {
		{ { BAD,  GOOD } },
		{ { BAD,  BAD  } },
		{ { BAD,  GOOD } },
		{ { GOOD, GOOD } }, /* sentinel */
	};
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * We hook up the CS_MASTER_ERROR_INTERRUPT to have forewarning
	 * of invalid commands in user batches that will cause a GPU hang.
	 * This is a faster mechanism than using hangcheck/heartbeats, but
	 * only detects problems the HW knows about -- it will not warn when
	 * we kill the HW!
	 *
	 * To verify our detection and reset, we throw some invalid commands
	 * at the HW and wait for the interrupt.
	 */

	if (!intel_has_reset_engine(gt))
		return 0;

	for_each_engine(engine, gt, id) {
		const struct error_phase *p;
		int err = 0;

		st_engine_heartbeat_disable(engine);

		for (p = phases; p->error[0] != GOOD; p++) {
			struct i915_request *client[ARRAY_SIZE(phases->error)];
			u32 *cs;
			int i;

			memset(client, 0, sizeof(*client));
			for (i = 0; i < ARRAY_SIZE(client); i++) {
				struct intel_context *ce;
				struct i915_request *rq;

				ce = intel_context_create(engine);
				if (IS_ERR(ce)) {
					err = PTR_ERR(ce);
					goto out;
				}

				rq = intel_context_create_request(ce);
				intel_context_put(ce);
				if (IS_ERR(rq)) {
					err = PTR_ERR(rq);
					goto out;
				}

				if (rq->engine->emit_init_breadcrumb) {
					err = rq->engine->emit_init_breadcrumb(rq);
					if (err) {
						i915_request_add(rq);
						goto out;
					}
				}

				cs = intel_ring_begin(rq, 2);
				if (IS_ERR(cs)) {
					i915_request_add(rq);
					err = PTR_ERR(cs);
					goto out;
				}

				if (p->error[i]) {
					*cs++ = 0xdeadbeef;
					*cs++ = 0xdeadbeef;
				} else {
					*cs++ = MI_NOOP;
					*cs++ = MI_NOOP;
				}

				client[i] = i915_request_get(rq);
				i915_request_add(rq);
			}

			err = wait_for_submit(engine, client[0], HZ / 2);
			if (err) {
				pr_err("%s: first request did not start within time!\n",
				       engine->name);
				err = -ETIME;
				goto out;
			}

			for (i = 0; i < ARRAY_SIZE(client); i++) {
				if (i915_request_wait(client[i], 0, HZ / 5) < 0)
					pr_debug("%s: %s request incomplete!\n",
						 engine->name,
						 error_repr(p->error[i]));

				if (!i915_request_started(client[i])) {
					pr_err("%s: %s request not started!\n",
					       engine->name,
					       error_repr(p->error[i]));
					err = -ETIME;
					goto out;
				}

				/* Kick the tasklet to process the error */
				intel_engine_flush_submission(engine);
				if (client[i]->fence.error != p->error[i]) {
					pr_err("%s: %s request (%s) with wrong error code: %d\n",
					       engine->name,
					       error_repr(p->error[i]),
					       i915_request_completed(client[i]) ? "completed" : "running",
					       client[i]->fence.error);
					err = -EINVAL;
					goto out;
				}
			}

out:
			for (i = 0; i < ARRAY_SIZE(client); i++)
				if (client[i])
					i915_request_put(client[i]);
			if (err) {
				pr_err("%s: failed at phase[%zd] { %d, %d }\n",
				       engine->name, p - phases,
				       p->error[0], p->error[1]);
				break;
			}
		}

		st_engine_heartbeat_enable(engine);
		if (err) {
			intel_gt_set_wedged(gt);
			return err;
		}
	}

	return 0;
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
	struct intel_context *ce;
	struct i915_request *rq;
	int err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return ERR_CAST(ce);

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		goto out_ce;

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

out_ce:
	intel_context_put(ce);
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

	rq = intel_engine_create_kernel_request(engine);
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
		if (!intel_engine_has_preemption(engine))
			continue;

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

	err = release_queue(outer, vma, n, I915_PRIORITY_BARRIER);
	if (err)
		goto out;

	if (i915_request_wait(head, 0,
			      2 * outer->gt->info.num_engines * (count + 2) * (count + 3)) < 0) {
		pr_err("%s: Failed to slice along semaphore chain of length (%d, %d)!\n",
		       outer->name, count, n);
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
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct i915_vma *vma;
	void *vaddr;
	int err = 0;

	/*
	 * If a request takes too long, we would like to give other users
	 * a fair go on the GPU. In particular, users may create batches
	 * that wait upon external input, where that input may even be
	 * supplied by another GPU job. To avoid blocking forever, we
	 * need to preempt the current task and replace it with another
	 * ready task.
	 */
	if (!IS_ACTIVE(CONFIG_DRM_I915_TIMESLICE_DURATION))
		return 0;

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

	err = i915_vma_sync(vma);
	if (err)
		goto err_pin;

	for_each_engine(engine, gt, id) {
		if (!intel_engine_has_preemption(engine))
			continue;

		memset(vaddr, 0, PAGE_SIZE);

		st_engine_heartbeat_disable(engine);
		err = slice_semaphore_queue(engine, vma, 5);
		st_engine_heartbeat_enable(engine);
		if (err)
			goto err_pin;

		if (igt_flush_test(gt->i915)) {
			err = -EIO;
			goto err_pin;
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

static struct i915_request *
create_rewinder(struct intel_context *ce,
		struct i915_request *wait,
		void *slot, int idx)
{
	const u32 offset =
		i915_ggtt_offset(ce->engine->status_page.vma) +
		offset_in_page(slot);
	struct i915_request *rq;
	u32 *cs;
	int err;

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq))
		return rq;

	if (wait) {
		err = i915_request_await_dma_fence(rq, &wait->fence);
		if (err)
			goto err;
	}

	cs = intel_ring_begin(rq, 14);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err;
	}

	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;
	*cs++ = MI_NOOP;

	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_GLOBAL_GTT |
		MI_SEMAPHORE_POLL |
		MI_SEMAPHORE_SAD_GTE_SDD;
	*cs++ = idx;
	*cs++ = offset;
	*cs++ = 0;

	*cs++ = MI_STORE_REGISTER_MEM_GEN8 | MI_USE_GGTT;
	*cs++ = i915_mmio_reg_offset(RING_TIMESTAMP(rq->engine->mmio_base));
	*cs++ = offset + idx * sizeof(u32);
	*cs++ = 0;

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = offset;
	*cs++ = 0;
	*cs++ = idx + 1;

	intel_ring_advance(rq, cs);

	rq->sched.attr.priority = I915_PRIORITY_MASK;
	err = 0;
err:
	i915_request_get(rq);
	i915_request_add(rq);
	if (err) {
		i915_request_put(rq);
		return ERR_PTR(err);
	}

	return rq;
}

static int live_timeslice_rewind(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * The usual presumption on timeslice expiration is that we replace
	 * the active context with another. However, given a chain of
	 * dependencies we may end up with replacing the context with itself,
	 * but only a few of those requests, forcing us to rewind the
	 * RING_TAIL of the original request.
	 */
	if (!IS_ACTIVE(CONFIG_DRM_I915_TIMESLICE_DURATION))
		return 0;

	for_each_engine(engine, gt, id) {
		enum { A1, A2, B1 };
		enum { X = 1, Z, Y };
		struct i915_request *rq[3] = {};
		struct intel_context *ce;
		unsigned long timeslice;
		int i, err = 0;
		u32 *slot;

		if (!intel_engine_has_timeslices(engine))
			continue;

		/*
		 * A:rq1 -- semaphore wait, timestamp X
		 * A:rq2 -- write timestamp Y
		 *
		 * B:rq1 [await A:rq1] -- write timestamp Z
		 *
		 * Force timeslice, release semaphore.
		 *
		 * Expect execution/evaluation order XZY
		 */

		st_engine_heartbeat_disable(engine);
		timeslice = xchg(&engine->props.timeslice_duration_ms, 1);

		slot = memset32(engine->status_page.addr + 1000, 0, 4);

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto err;
		}

		rq[A1] = create_rewinder(ce, NULL, slot, X);
		if (IS_ERR(rq[A1])) {
			intel_context_put(ce);
			goto err;
		}

		rq[A2] = create_rewinder(ce, NULL, slot, Y);
		intel_context_put(ce);
		if (IS_ERR(rq[A2]))
			goto err;

		err = wait_for_submit(engine, rq[A2], HZ / 2);
		if (err) {
			pr_err("%s: failed to submit first context\n",
			       engine->name);
			goto err;
		}

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto err;
		}

		rq[B1] = create_rewinder(ce, rq[A1], slot, Z);
		intel_context_put(ce);
		if (IS_ERR(rq[2]))
			goto err;

		err = wait_for_submit(engine, rq[B1], HZ / 2);
		if (err) {
			pr_err("%s: failed to submit second context\n",
			       engine->name);
			goto err;
		}

		/* ELSP[] = { { A:rq1, A:rq2 }, { B:rq1 } } */
		ENGINE_TRACE(engine, "forcing tasklet for rewind\n");
		while (i915_request_is_active(rq[A2])) { /* semaphore yield! */
			/* Wait for the timeslice to kick in */
			del_timer(&engine->execlists.timer);
			tasklet_hi_schedule(&engine->execlists.tasklet);
			intel_engine_flush_submission(engine);
		}
		/* -> ELSP[] = { { A:rq1 }, { B:rq1 } } */
		GEM_BUG_ON(!i915_request_is_active(rq[A1]));
		GEM_BUG_ON(!i915_request_is_active(rq[B1]));
		GEM_BUG_ON(i915_request_is_active(rq[A2]));

		/* Release the hounds! */
		slot[0] = 1;
		wmb(); /* "pairs" with GPU; paranoid kick of internal CPU$ */

		for (i = 1; i <= 3; i++) {
			unsigned long timeout = jiffies + HZ / 2;

			while (!READ_ONCE(slot[i]) &&
			       time_before(jiffies, timeout))
				;

			if (!time_before(jiffies, timeout)) {
				pr_err("%s: rq[%d] timed out\n",
				       engine->name, i - 1);
				err = -ETIME;
				goto err;
			}

			pr_debug("%s: slot[%d]:%x\n", engine->name, i, slot[i]);
		}

		/* XZY: XZ < XY */
		if (slot[Z] - slot[X] >= slot[Y] - slot[X]) {
			pr_err("%s: timeslicing did not run context B [%u] before A [%u]!\n",
			       engine->name,
			       slot[Z] - slot[X],
			       slot[Y] - slot[X]);
			err = -EINVAL;
		}

err:
		memset32(&slot[0], -1, 4);
		wmb();

		engine->props.timeslice_duration_ms = timeslice;
		st_engine_heartbeat_enable(engine);
		for (i = 0; i < 3; i++)
			i915_request_put(rq[i]);
		if (igt_flush_test(gt->i915))
			err = -EIO;
		if (err)
			return err;
	}

	return 0;
}

static struct i915_request *nop_request(struct intel_engine_cs *engine)
{
	struct i915_request *rq;

	rq = intel_engine_create_kernel_request(engine);
	if (IS_ERR(rq))
		return rq;

	i915_request_get(rq);
	i915_request_add(rq);

	return rq;
}

static long slice_timeout(struct intel_engine_cs *engine)
{
	long timeout;

	/* Enough time for a timeslice to kick in, and kick out */
	timeout = 2 * msecs_to_jiffies_timeout(timeslice(engine));

	/* Enough time for the nop request to complete */
	timeout += HZ / 5;

	return timeout + 1;
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
	if (!IS_ACTIVE(CONFIG_DRM_I915_TIMESLICE_DURATION))
		return 0;

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

	err = i915_vma_sync(vma);
	if (err)
		goto err_pin;

	for_each_engine(engine, gt, id) {
		struct i915_sched_attr attr = {
			.priority = I915_USER_PRIORITY(I915_PRIORITY_MAX),
		};
		struct i915_request *rq, *nop;

		if (!intel_engine_has_preemption(engine))
			continue;

		st_engine_heartbeat_disable(engine);
		memset(vaddr, 0, PAGE_SIZE);

		/* ELSP[0]: semaphore wait */
		rq = semaphore_queue(engine, vma, 0);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_heartbeat;
		}
		engine->schedule(rq, &attr);
		err = wait_for_submit(engine, rq, HZ / 2);
		if (err) {
			pr_err("%s: Timed out trying to submit semaphores\n",
			       engine->name);
			goto err_rq;
		}

		/* ELSP[1]: nop request */
		nop = nop_request(engine);
		if (IS_ERR(nop)) {
			err = PTR_ERR(nop);
			goto err_rq;
		}
		err = wait_for_submit(engine, nop, HZ / 2);
		i915_request_put(nop);
		if (err) {
			pr_err("%s: Timed out trying to submit nop\n",
			       engine->name);
			goto err_rq;
		}

		GEM_BUG_ON(i915_request_completed(rq));
		GEM_BUG_ON(execlists_active(&engine->execlists) != rq);

		/* Queue: semaphore signal, matching priority as semaphore */
		err = release_queue(engine, vma, 1, effective_prio(rq));
		if (err)
			goto err_rq;

		/* Wait until we ack the release_queue and start timeslicing */
		do {
			cond_resched();
			intel_engine_flush_submission(engine);
		} while (READ_ONCE(engine->execlists.pending[0]));

		/* Timeslice every jiffy, so within 2 we should signal */
		if (i915_request_wait(rq, 0, slice_timeout(engine)) < 0) {
			struct drm_printer p =
				drm_info_printer(gt->i915->drm.dev);

			pr_err("%s: Failed to timeslice into queue\n",
			       engine->name);
			intel_engine_dump(engine, &p,
					  "%s\n", engine->name);

			memset(vaddr, 0xff, PAGE_SIZE);
			err = -EIO;
		}
err_rq:
		i915_request_put(rq);
err_heartbeat:
		st_engine_heartbeat_enable(engine);
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

static int live_timeslice_nopreempt(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	struct igt_spinner spin;
	int err = 0;

	/*
	 * We should not timeslice into a request that is marked with
	 * I915_REQUEST_NOPREEMPT.
	 */
	if (!IS_ACTIVE(CONFIG_DRM_I915_TIMESLICE_DURATION))
		return 0;

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	for_each_engine(engine, gt, id) {
		struct intel_context *ce;
		struct i915_request *rq;
		unsigned long timeslice;

		if (!intel_engine_has_preemption(engine))
			continue;

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			break;
		}

		st_engine_heartbeat_disable(engine);
		timeslice = xchg(&engine->props.timeslice_duration_ms, 1);

		/* Create an unpreemptible spinner */

		rq = igt_spinner_create_request(&spin, ce, MI_ARB_CHECK);
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_heartbeat;
		}

		i915_request_get(rq);
		i915_request_add(rq);

		if (!igt_wait_for_spinner(&spin, rq)) {
			i915_request_put(rq);
			err = -ETIME;
			goto out_spin;
		}

		set_bit(I915_FENCE_FLAG_NOPREEMPT, &rq->fence.flags);
		i915_request_put(rq);

		/* Followed by a maximum priority barrier (heartbeat) */

		ce = intel_context_create(engine);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto out_spin;
		}

		rq = intel_context_create_request(ce);
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out_spin;
		}

		rq->sched.attr.priority = I915_PRIORITY_BARRIER;
		i915_request_get(rq);
		i915_request_add(rq);

		/*
		 * Wait until the barrier is in ELSP, and we know timeslicing
		 * will have been activated.
		 */
		if (wait_for_submit(engine, rq, HZ / 2)) {
			i915_request_put(rq);
			err = -ETIME;
			goto out_spin;
		}

		/*
		 * Since the ELSP[0] request is unpreemptible, it should not
		 * allow the maximum priority barrier through. Wait long
		 * enough to see if it is timesliced in by mistake.
		 */
		if (i915_request_wait(rq, 0, slice_timeout(engine)) >= 0) {
			pr_err("%s: I915_PRIORITY_BARRIER request completed, bypassing no-preempt request\n",
			       engine->name);
			err = -EINVAL;
		}
		i915_request_put(rq);

out_spin:
		igt_spinner_end(&spin);
out_heartbeat:
		xchg(&engine->props.timeslice_duration_ms, timeslice);
		st_engine_heartbeat_enable(engine);
		if (err)
			break;

		if (igt_flush_test(gt->i915)) {
			err = -EIO;
			break;
		}
	}

	igt_spinner_fini(&spin);
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

	err = i915_vma_sync(vma);
	if (err)
		goto err_vma;

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

		i915_request_get(lo);
		i915_request_add(lo);

		if (wait_for(READ_ONCE(*map), 10)) {
			i915_request_put(lo);
			err = -ETIMEDOUT;
			goto err_vma;
		}

		/* Low priority request should be busywaiting now */
		if (i915_request_wait(lo, 0, 1) != -ETIME) {
			i915_request_put(lo);
			pr_err("%s: Busywaiting request did not!\n",
			       engine->name);
			err = -EIO;
			goto err_vma;
		}

		hi = igt_request_alloc(ctx_hi, engine);
		if (IS_ERR(hi)) {
			err = PTR_ERR(hi);
			i915_request_put(lo);
			goto err_vma;
		}

		cs = intel_ring_begin(hi, 4);
		if (IS_ERR(cs)) {
			err = PTR_ERR(cs);
			i915_request_add(hi);
			i915_request_put(lo);
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

			i915_request_put(lo);
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto err_vma;
		}
		GEM_BUG_ON(READ_ONCE(*map));
		i915_request_put(lo);

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
		__set_bit(I915_FENCE_FLAG_NOPREEMPT, &rq_a->fence.flags);

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

struct live_preempt_cancel {
	struct intel_engine_cs *engine;
	struct preempt_client a, b;
};

static int __cancel_active0(struct live_preempt_cancel *arg)
{
	struct i915_request *rq;
	struct igt_live_test t;
	int err;

	/* Preempt cancel of ELSP0 */
	GEM_TRACE("%s(%s)\n", __func__, arg->engine->name);
	if (igt_live_test_begin(&t, arg->engine->i915,
				__func__, arg->engine->name))
		return -EIO;

	rq = spinner_create_request(&arg->a.spin,
				    arg->a.ctx, arg->engine,
				    MI_ARB_CHECK);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	clear_bit(CONTEXT_BANNED, &rq->context->flags);
	i915_request_get(rq);
	i915_request_add(rq);
	if (!igt_wait_for_spinner(&arg->a.spin, rq)) {
		err = -EIO;
		goto out;
	}

	intel_context_set_banned(rq->context);
	err = intel_engine_pulse(arg->engine);
	if (err)
		goto out;

	err = wait_for_reset(arg->engine, rq, HZ / 2);
	if (err) {
		pr_err("Cancelled inflight0 request did not reset\n");
		goto out;
	}

out:
	i915_request_put(rq);
	if (igt_live_test_end(&t))
		err = -EIO;
	return err;
}

static int __cancel_active1(struct live_preempt_cancel *arg)
{
	struct i915_request *rq[2] = {};
	struct igt_live_test t;
	int err;

	/* Preempt cancel of ELSP1 */
	GEM_TRACE("%s(%s)\n", __func__, arg->engine->name);
	if (igt_live_test_begin(&t, arg->engine->i915,
				__func__, arg->engine->name))
		return -EIO;

	rq[0] = spinner_create_request(&arg->a.spin,
				       arg->a.ctx, arg->engine,
				       MI_NOOP); /* no preemption */
	if (IS_ERR(rq[0]))
		return PTR_ERR(rq[0]);

	clear_bit(CONTEXT_BANNED, &rq[0]->context->flags);
	i915_request_get(rq[0]);
	i915_request_add(rq[0]);
	if (!igt_wait_for_spinner(&arg->a.spin, rq[0])) {
		err = -EIO;
		goto out;
	}

	rq[1] = spinner_create_request(&arg->b.spin,
				       arg->b.ctx, arg->engine,
				       MI_ARB_CHECK);
	if (IS_ERR(rq[1])) {
		err = PTR_ERR(rq[1]);
		goto out;
	}

	clear_bit(CONTEXT_BANNED, &rq[1]->context->flags);
	i915_request_get(rq[1]);
	err = i915_request_await_dma_fence(rq[1], &rq[0]->fence);
	i915_request_add(rq[1]);
	if (err)
		goto out;

	intel_context_set_banned(rq[1]->context);
	err = intel_engine_pulse(arg->engine);
	if (err)
		goto out;

	igt_spinner_end(&arg->a.spin);
	err = wait_for_reset(arg->engine, rq[1], HZ / 2);
	if (err)
		goto out;

	if (rq[0]->fence.error != 0) {
		pr_err("Normal inflight0 request did not complete\n");
		err = -EINVAL;
		goto out;
	}

	if (rq[1]->fence.error != -EIO) {
		pr_err("Cancelled inflight1 request did not report -EIO\n");
		err = -EINVAL;
		goto out;
	}

out:
	i915_request_put(rq[1]);
	i915_request_put(rq[0]);
	if (igt_live_test_end(&t))
		err = -EIO;
	return err;
}

static int __cancel_queued(struct live_preempt_cancel *arg)
{
	struct i915_request *rq[3] = {};
	struct igt_live_test t;
	int err;

	/* Full ELSP and one in the wings */
	GEM_TRACE("%s(%s)\n", __func__, arg->engine->name);
	if (igt_live_test_begin(&t, arg->engine->i915,
				__func__, arg->engine->name))
		return -EIO;

	rq[0] = spinner_create_request(&arg->a.spin,
				       arg->a.ctx, arg->engine,
				       MI_ARB_CHECK);
	if (IS_ERR(rq[0]))
		return PTR_ERR(rq[0]);

	clear_bit(CONTEXT_BANNED, &rq[0]->context->flags);
	i915_request_get(rq[0]);
	i915_request_add(rq[0]);
	if (!igt_wait_for_spinner(&arg->a.spin, rq[0])) {
		err = -EIO;
		goto out;
	}

	rq[1] = igt_request_alloc(arg->b.ctx, arg->engine);
	if (IS_ERR(rq[1])) {
		err = PTR_ERR(rq[1]);
		goto out;
	}

	clear_bit(CONTEXT_BANNED, &rq[1]->context->flags);
	i915_request_get(rq[1]);
	err = i915_request_await_dma_fence(rq[1], &rq[0]->fence);
	i915_request_add(rq[1]);
	if (err)
		goto out;

	rq[2] = spinner_create_request(&arg->b.spin,
				       arg->a.ctx, arg->engine,
				       MI_ARB_CHECK);
	if (IS_ERR(rq[2])) {
		err = PTR_ERR(rq[2]);
		goto out;
	}

	i915_request_get(rq[2]);
	err = i915_request_await_dma_fence(rq[2], &rq[1]->fence);
	i915_request_add(rq[2]);
	if (err)
		goto out;

	intel_context_set_banned(rq[2]->context);
	err = intel_engine_pulse(arg->engine);
	if (err)
		goto out;

	err = wait_for_reset(arg->engine, rq[2], HZ / 2);
	if (err)
		goto out;

	if (rq[0]->fence.error != -EIO) {
		pr_err("Cancelled inflight0 request did not report -EIO\n");
		err = -EINVAL;
		goto out;
	}

	if (rq[1]->fence.error != 0) {
		pr_err("Normal inflight1 request did not complete\n");
		err = -EINVAL;
		goto out;
	}

	if (rq[2]->fence.error != -EIO) {
		pr_err("Cancelled queued request did not report -EIO\n");
		err = -EINVAL;
		goto out;
	}

out:
	i915_request_put(rq[2]);
	i915_request_put(rq[1]);
	i915_request_put(rq[0]);
	if (igt_live_test_end(&t))
		err = -EIO;
	return err;
}

static int __cancel_hostile(struct live_preempt_cancel *arg)
{
	struct i915_request *rq;
	int err;

	/* Preempt cancel non-preemptible spinner in ELSP0 */
	if (!IS_ACTIVE(CONFIG_DRM_I915_PREEMPT_TIMEOUT))
		return 0;

	if (!intel_has_reset_engine(arg->engine->gt))
		return 0;

	GEM_TRACE("%s(%s)\n", __func__, arg->engine->name);
	rq = spinner_create_request(&arg->a.spin,
				    arg->a.ctx, arg->engine,
				    MI_NOOP); /* preemption disabled */
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	clear_bit(CONTEXT_BANNED, &rq->context->flags);
	i915_request_get(rq);
	i915_request_add(rq);
	if (!igt_wait_for_spinner(&arg->a.spin, rq)) {
		err = -EIO;
		goto out;
	}

	intel_context_set_banned(rq->context);
	err = intel_engine_pulse(arg->engine); /* force reset */
	if (err)
		goto out;

	err = wait_for_reset(arg->engine, rq, HZ / 2);
	if (err) {
		pr_err("Cancelled inflight0 request did not reset\n");
		goto out;
	}

out:
	i915_request_put(rq);
	if (igt_flush_test(arg->engine->i915))
		err = -EIO;
	return err;
}

static void force_reset_timeout(struct intel_engine_cs *engine)
{
	engine->reset_timeout.probability = 999;
	atomic_set(&engine->reset_timeout.times, -1);
}

static void cancel_reset_timeout(struct intel_engine_cs *engine)
{
	memset(&engine->reset_timeout, 0, sizeof(engine->reset_timeout));
}

static int __cancel_fail(struct live_preempt_cancel *arg)
{
	struct intel_engine_cs *engine = arg->engine;
	struct i915_request *rq;
	int err;

	if (!IS_ACTIVE(CONFIG_DRM_I915_PREEMPT_TIMEOUT))
		return 0;

	if (!intel_has_reset_engine(engine->gt))
		return 0;

	GEM_TRACE("%s(%s)\n", __func__, engine->name);
	rq = spinner_create_request(&arg->a.spin,
				    arg->a.ctx, engine,
				    MI_NOOP); /* preemption disabled */
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	clear_bit(CONTEXT_BANNED, &rq->context->flags);
	i915_request_get(rq);
	i915_request_add(rq);
	if (!igt_wait_for_spinner(&arg->a.spin, rq)) {
		err = -EIO;
		goto out;
	}

	intel_context_set_banned(rq->context);

	err = intel_engine_pulse(engine);
	if (err)
		goto out;

	force_reset_timeout(engine);

	/* force preempt reset [failure] */
	while (!engine->execlists.pending[0])
		intel_engine_flush_submission(engine);
	del_timer_sync(&engine->execlists.preempt);
	intel_engine_flush_submission(engine);

	cancel_reset_timeout(engine);

	/* after failure, require heartbeats to reset device */
	intel_engine_set_heartbeat(engine, 1);
	err = wait_for_reset(engine, rq, HZ / 2);
	intel_engine_set_heartbeat(engine,
				   engine->defaults.heartbeat_interval_ms);
	if (err) {
		pr_err("Cancelled inflight0 request did not reset\n");
		goto out;
	}

out:
	i915_request_put(rq);
	if (igt_flush_test(engine->i915))
		err = -EIO;
	return err;
}

static int live_preempt_cancel(void *arg)
{
	struct intel_gt *gt = arg;
	struct live_preempt_cancel data;
	enum intel_engine_id id;
	int err = -ENOMEM;

	/*
	 * To cancel an inflight context, we need to first remove it from the
	 * GPU. That sounds like preemption! Plus a little bit of bookkeeping.
	 */

	if (preempt_client_init(gt, &data.a))
		return -ENOMEM;
	if (preempt_client_init(gt, &data.b))
		goto err_client_a;

	for_each_engine(data.engine, gt, id) {
		if (!intel_engine_has_preemption(data.engine))
			continue;

		err = __cancel_active0(&data);
		if (err)
			goto err_wedged;

		err = __cancel_active1(&data);
		if (err)
			goto err_wedged;

		err = __cancel_queued(&data);
		if (err)
			goto err_wedged;

		err = __cancel_hostile(&data);
		if (err)
			goto err_wedged;

		err = __cancel_fail(&data);
		if (err)
			goto err_wedged;
	}

	err = 0;
err_client_b:
	preempt_client_fini(&data.b);
err_client_a:
	preempt_client_fini(&data.a);
	return err;

err_wedged:
	GEM_TRACE_DUMP();
	igt_spinner_end(&data.b.spin);
	igt_spinner_end(&data.a.spin);
	intel_gt_set_wedged(gt);
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

	if (intel_uc_uses_guc_submission(&gt->uc))
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

		st_engine_heartbeat_disable(engine);
		engine->execlists.preempt_hang.count = 0;

		rq_a = spinner_create_request(&a.spin,
					      a.ctx, engine,
					      MI_NOOP);
		if (IS_ERR(rq_a)) {
			err = PTR_ERR(rq_a);
			st_engine_heartbeat_enable(engine);
			goto err_client_b;
		}

		i915_request_add(rq_a);
		if (!igt_wait_for_spinner(&a.spin, rq_a)) {
			pr_err("First client failed to start\n");
			st_engine_heartbeat_enable(engine);
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
				st_engine_heartbeat_enable(engine);
				goto err_client_b;
			}
			i915_request_add(rq_b);

			GEM_BUG_ON(i915_request_completed(rq_a));
			engine->schedule(rq_a, &attr);
			igt_spinner_end(&a.spin);

			if (!igt_wait_for_spinner(&b.spin, rq_b)) {
				pr_err("Second client failed to start\n");
				st_engine_heartbeat_enable(engine);
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
			st_engine_heartbeat_enable(engine);
			err = -EINVAL;
			goto err_client_b;
		}

		st_engine_heartbeat_enable(engine);
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

		i915_request_get(rq);
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
			i915_request_put(rq);
			goto err_wedged;
		}
		i915_request_put(rq);

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

			i915_request_get(rq);
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
				i915_request_put(rq);
				goto err_wedged;
			}
			igt_spinner_end(&lo.spin);
			i915_request_put(rq);

			rq = igt_request_alloc(lo.ctx, engine);
			if (IS_ERR(rq))
				goto err_wedged;

			i915_request_get(rq);
			i915_request_add(rq);

			if (i915_request_wait(rq, 0, HZ / 5) < 0) {
				struct drm_printer p =
					drm_info_printer(gt->i915->drm.dev);

				pr_err("Failed to flush low priority chain of %d requests\n",
				       count);
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);

				i915_request_put(rq);
				goto err_wedged;
			}
			i915_request_put(rq);
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

static int create_gang(struct intel_engine_cs *engine,
		       struct i915_request **prev)
{
	struct drm_i915_gem_object *obj;
	struct intel_context *ce;
	struct i915_request *rq;
	struct i915_vma *vma;
	u32 *cs;
	int err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return PTR_ERR(ce);

	obj = i915_gem_object_create_internal(engine->i915, 4096);
	if (IS_ERR(obj)) {
		err = PTR_ERR(obj);
		goto err_ce;
	}

	vma = i915_vma_instance(obj, ce->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_obj;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto err_obj;

	cs = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(cs)) {
		err = PTR_ERR(cs);
		goto err_obj;
	}

	/* Semaphore target: spin until zero */
	*cs++ = MI_ARB_ON_OFF | MI_ARB_ENABLE;

	*cs++ = MI_SEMAPHORE_WAIT |
		MI_SEMAPHORE_POLL |
		MI_SEMAPHORE_SAD_EQ_SDD;
	*cs++ = 0;
	*cs++ = lower_32_bits(vma->node.start);
	*cs++ = upper_32_bits(vma->node.start);

	if (*prev) {
		u64 offset = (*prev)->batch->node.start;

		/* Terminate the spinner in the next lower priority batch. */
		*cs++ = MI_STORE_DWORD_IMM_GEN4;
		*cs++ = lower_32_bits(offset);
		*cs++ = upper_32_bits(offset);
		*cs++ = 0;
	}

	*cs++ = MI_BATCH_BUFFER_END;
	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_obj;
	}

	rq->batch = i915_vma_get(vma);
	i915_request_get(rq);

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, false);
	if (!err)
		err = i915_vma_move_to_active(vma, rq, 0);
	if (!err)
		err = rq->engine->emit_bb_start(rq,
						vma->node.start,
						PAGE_SIZE, 0);
	i915_vma_unlock(vma);
	i915_request_add(rq);
	if (err)
		goto err_rq;

	i915_gem_object_put(obj);
	intel_context_put(ce);

	rq->mock.link.next = &(*prev)->mock.link;
	*prev = rq;
	return 0;

err_rq:
	i915_vma_put(rq->batch);
	i915_request_put(rq);
err_obj:
	i915_gem_object_put(obj);
err_ce:
	intel_context_put(ce);
	return err;
}

static int __live_preempt_ring(struct intel_engine_cs *engine,
			       struct igt_spinner *spin,
			       int queue_sz, int ring_sz)
{
	struct intel_context *ce[2] = {};
	struct i915_request *rq;
	struct igt_live_test t;
	int err = 0;
	int n;

	if (igt_live_test_begin(&t, engine->i915, __func__, engine->name))
		return -EIO;

	for (n = 0; n < ARRAY_SIZE(ce); n++) {
		struct intel_context *tmp;

		tmp = intel_context_create(engine);
		if (IS_ERR(tmp)) {
			err = PTR_ERR(tmp);
			goto err_ce;
		}

		tmp->ring = __intel_context_ring_size(ring_sz);

		err = intel_context_pin(tmp);
		if (err) {
			intel_context_put(tmp);
			goto err_ce;
		}

		memset32(tmp->ring->vaddr,
			 0xdeadbeef, /* trigger a hang if executed */
			 tmp->ring->vma->size / sizeof(u32));

		ce[n] = tmp;
	}

	rq = igt_spinner_create_request(spin, ce[0], MI_ARB_CHECK);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_ce;
	}

	i915_request_get(rq);
	rq->sched.attr.priority = I915_PRIORITY_BARRIER;
	i915_request_add(rq);

	if (!igt_wait_for_spinner(spin, rq)) {
		intel_gt_set_wedged(engine->gt);
		i915_request_put(rq);
		err = -ETIME;
		goto err_ce;
	}

	/* Fill the ring, until we will cause a wrap */
	n = 0;
	while (ce[0]->ring->tail - rq->wa_tail <= queue_sz) {
		struct i915_request *tmp;

		tmp = intel_context_create_request(ce[0]);
		if (IS_ERR(tmp)) {
			err = PTR_ERR(tmp);
			i915_request_put(rq);
			goto err_ce;
		}

		i915_request_add(tmp);
		intel_engine_flush_submission(engine);
		n++;
	}
	intel_engine_flush_submission(engine);
	pr_debug("%s: Filled %d with %d nop tails {size:%x, tail:%x, emit:%x, rq.tail:%x}\n",
		 engine->name, queue_sz, n,
		 ce[0]->ring->size,
		 ce[0]->ring->tail,
		 ce[0]->ring->emit,
		 rq->tail);
	i915_request_put(rq);

	/* Create a second request to preempt the first ring */
	rq = intel_context_create_request(ce[1]);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto err_ce;
	}

	rq->sched.attr.priority = I915_PRIORITY_BARRIER;
	i915_request_get(rq);
	i915_request_add(rq);

	err = wait_for_submit(engine, rq, HZ / 2);
	i915_request_put(rq);
	if (err) {
		pr_err("%s: preemption request was not submited\n",
		       engine->name);
		err = -ETIME;
	}

	pr_debug("%s: ring[0]:{ tail:%x, emit:%x }, ring[1]:{ tail:%x, emit:%x }\n",
		 engine->name,
		 ce[0]->ring->tail, ce[0]->ring->emit,
		 ce[1]->ring->tail, ce[1]->ring->emit);

err_ce:
	intel_engine_flush_submission(engine);
	igt_spinner_end(spin);
	for (n = 0; n < ARRAY_SIZE(ce); n++) {
		if (IS_ERR_OR_NULL(ce[n]))
			break;

		intel_context_unpin(ce[n]);
		intel_context_put(ce[n]);
	}
	if (igt_live_test_end(&t))
		err = -EIO;
	return err;
}

static int live_preempt_ring(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct igt_spinner spin;
	enum intel_engine_id id;
	int err = 0;

	/*
	 * Check that we rollback large chunks of a ring in order to do a
	 * preemption event. Similar to live_unlite_ring, but looking at
	 * ring size rather than the impact of intel_ring_direction().
	 */

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	for_each_engine(engine, gt, id) {
		int n;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (!intel_engine_can_store_dword(engine))
			continue;

		st_engine_heartbeat_disable(engine);

		for (n = 0; n <= 3; n++) {
			err = __live_preempt_ring(engine, &spin,
						  n * SZ_4K / 4, SZ_4K);
			if (err)
				break;
		}

		st_engine_heartbeat_enable(engine);
		if (err)
			break;
	}

	igt_spinner_fini(&spin);
	return err;
}

static int live_preempt_gang(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;

	/*
	 * Build as long a chain of preempters as we can, with each
	 * request higher priority than the last. Once we are ready, we release
	 * the last batch which then precolates down the chain, each releasing
	 * the next oldest in turn. The intent is to simply push as hard as we
	 * can with the number of preemptions, trying to exceed narrow HW
	 * limits. At a minimum, we insist that we can sort all the user
	 * high priority levels into execution order.
	 */

	for_each_engine(engine, gt, id) {
		struct i915_request *rq = NULL;
		struct igt_live_test t;
		IGT_TIMEOUT(end_time);
		int prio = 0;
		int err = 0;
		u32 *cs;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name))
			return -EIO;

		do {
			struct i915_sched_attr attr = {
				.priority = I915_USER_PRIORITY(prio++),
			};

			err = create_gang(engine, &rq);
			if (err)
				break;

			/* Submit each spinner at increasing priority */
			engine->schedule(rq, &attr);
		} while (prio <= I915_PRIORITY_MAX &&
			 !__igt_timeout(end_time, NULL));
		pr_debug("%s: Preempt chain of %d requests\n",
			 engine->name, prio);

		/*
		 * Such that the last spinner is the highest priority and
		 * should execute first. When that spinner completes,
		 * it will terminate the next lowest spinner until there
		 * are no more spinners and the gang is complete.
		 */
		cs = i915_gem_object_pin_map(rq->batch->obj, I915_MAP_WC);
		if (!IS_ERR(cs)) {
			*cs = 0;
			i915_gem_object_unpin_map(rq->batch->obj);
		} else {
			err = PTR_ERR(cs);
			intel_gt_set_wedged(gt);
		}

		while (rq) { /* wait for each rq from highest to lowest prio */
			struct i915_request *n = list_next_entry(rq, mock.link);

			if (err == 0 && i915_request_wait(rq, 0, HZ / 5) < 0) {
				struct drm_printer p =
					drm_info_printer(engine->i915->drm.dev);

				pr_err("Failed to flush chain of %d requests, at %d\n",
				       prio, rq_prio(rq) >> I915_USER_PRIORITY_SHIFT);
				intel_engine_dump(engine, &p,
						  "%s\n", engine->name);

				err = -ETIME;
			}

			i915_vma_put(rq->batch);
			i915_request_put(rq);
			rq = n;
		}

		if (igt_live_test_end(&t))
			err = -EIO;
		if (err)
			return err;
	}

	return 0;
}

static struct i915_vma *
create_gpr_user(struct intel_engine_cs *engine,
		struct i915_vma *result,
		unsigned int offset)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	u32 *cs;
	int err;
	int i;

	obj = i915_gem_object_create_internal(engine->i915, 4096);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, result->vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err) {
		i915_vma_put(vma);
		return ERR_PTR(err);
	}

	cs = i915_gem_object_pin_map(obj, I915_MAP_WC);
	if (IS_ERR(cs)) {
		i915_vma_put(vma);
		return ERR_CAST(cs);
	}

	/* All GPR are clear for new contexts. We use GPR(0) as a constant */
	*cs++ = MI_LOAD_REGISTER_IMM(1);
	*cs++ = CS_GPR(engine, 0);
	*cs++ = 1;

	for (i = 1; i < NUM_GPR; i++) {
		u64 addr;

		/*
		 * Perform: GPR[i]++
		 *
		 * As we read and write into the context saved GPR[i], if
		 * we restart this batch buffer from an earlier point, we
		 * will repeat the increment and store a value > 1.
		 */
		*cs++ = MI_MATH(4);
		*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCA, MI_MATH_REG(i));
		*cs++ = MI_MATH_LOAD(MI_MATH_REG_SRCB, MI_MATH_REG(0));
		*cs++ = MI_MATH_ADD;
		*cs++ = MI_MATH_STORE(MI_MATH_REG(i), MI_MATH_REG_ACCU);

		addr = result->node.start + offset + i * sizeof(*cs);
		*cs++ = MI_STORE_REGISTER_MEM_GEN8;
		*cs++ = CS_GPR(engine, 2 * i);
		*cs++ = lower_32_bits(addr);
		*cs++ = upper_32_bits(addr);

		*cs++ = MI_SEMAPHORE_WAIT |
			MI_SEMAPHORE_POLL |
			MI_SEMAPHORE_SAD_GTE_SDD;
		*cs++ = i;
		*cs++ = lower_32_bits(result->node.start);
		*cs++ = upper_32_bits(result->node.start);
	}

	*cs++ = MI_BATCH_BUFFER_END;
	i915_gem_object_flush_map(obj);
	i915_gem_object_unpin_map(obj);

	return vma;
}

static struct i915_vma *create_global(struct intel_gt *gt, size_t sz)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;
	int err;

	obj = i915_gem_object_create_internal(gt->i915, sz);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	vma = i915_vma_instance(obj, &gt->ggtt->vm, NULL);
	if (IS_ERR(vma)) {
		i915_gem_object_put(obj);
		return vma;
	}

	err = i915_ggtt_pin(vma, NULL, 0, 0);
	if (err) {
		i915_vma_put(vma);
		return ERR_PTR(err);
	}

	return vma;
}

static struct i915_request *
create_gpr_client(struct intel_engine_cs *engine,
		  struct i915_vma *global,
		  unsigned int offset)
{
	struct i915_vma *batch, *vma;
	struct intel_context *ce;
	struct i915_request *rq;
	int err;

	ce = intel_context_create(engine);
	if (IS_ERR(ce))
		return ERR_CAST(ce);

	vma = i915_vma_instance(global->obj, ce->vm, NULL);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto out_ce;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_USER);
	if (err)
		goto out_ce;

	batch = create_gpr_user(engine, vma, offset);
	if (IS_ERR(batch)) {
		err = PTR_ERR(batch);
		goto out_vma;
	}

	rq = intel_context_create_request(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_batch;
	}

	i915_vma_lock(vma);
	err = i915_request_await_object(rq, vma->obj, false);
	if (!err)
		err = i915_vma_move_to_active(vma, rq, 0);
	i915_vma_unlock(vma);

	i915_vma_lock(batch);
	if (!err)
		err = i915_request_await_object(rq, batch->obj, false);
	if (!err)
		err = i915_vma_move_to_active(batch, rq, 0);
	if (!err)
		err = rq->engine->emit_bb_start(rq,
						batch->node.start,
						PAGE_SIZE, 0);
	i915_vma_unlock(batch);
	i915_vma_unpin(batch);

	if (!err)
		i915_request_get(rq);
	i915_request_add(rq);

out_batch:
	i915_vma_put(batch);
out_vma:
	i915_vma_unpin(vma);
out_ce:
	intel_context_put(ce);
	return err ? ERR_PTR(err) : rq;
}

static int preempt_user(struct intel_engine_cs *engine,
			struct i915_vma *global,
			int id)
{
	struct i915_sched_attr attr = {
		.priority = I915_PRIORITY_MAX
	};
	struct i915_request *rq;
	int err = 0;
	u32 *cs;

	rq = intel_engine_create_kernel_request(engine);
	if (IS_ERR(rq))
		return PTR_ERR(rq);

	cs = intel_ring_begin(rq, 4);
	if (IS_ERR(cs)) {
		i915_request_add(rq);
		return PTR_ERR(cs);
	}

	*cs++ = MI_STORE_DWORD_IMM_GEN4 | MI_USE_GGTT;
	*cs++ = i915_ggtt_offset(global);
	*cs++ = 0;
	*cs++ = id;

	intel_ring_advance(rq, cs);

	i915_request_get(rq);
	i915_request_add(rq);

	engine->schedule(rq, &attr);

	if (i915_request_wait(rq, 0, HZ / 2) < 0)
		err = -ETIME;
	i915_request_put(rq);

	return err;
}

static int live_preempt_user(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *engine;
	struct i915_vma *global;
	enum intel_engine_id id;
	u32 *result;
	int err = 0;

	/*
	 * In our other tests, we look at preemption in carefully
	 * controlled conditions in the ringbuffer. Since most of the
	 * time is spent in user batches, most of our preemptions naturally
	 * occur there. We want to verify that when we preempt inside a batch
	 * we continue on from the current instruction and do not roll back
	 * to the start, or another earlier arbitration point.
	 *
	 * To verify this, we create a batch which is a mixture of
	 * MI_MATH (gpr++) MI_SRM (gpr) and preemption points. Then with
	 * a few preempting contexts thrown into the mix, we look for any
	 * repeated instructions (which show up as incorrect values).
	 */

	global = create_global(gt, 4096);
	if (IS_ERR(global))
		return PTR_ERR(global);

	result = i915_gem_object_pin_map(global->obj, I915_MAP_WC);
	if (IS_ERR(result)) {
		i915_vma_unpin_and_release(&global, 0);
		return PTR_ERR(result);
	}

	for_each_engine(engine, gt, id) {
		struct i915_request *client[3] = {};
		struct igt_live_test t;
		int i;

		if (!intel_engine_has_preemption(engine))
			continue;

		if (IS_GEN(gt->i915, 8) && engine->class != RENDER_CLASS)
			continue; /* we need per-context GPR */

		if (igt_live_test_begin(&t, gt->i915, __func__, engine->name)) {
			err = -EIO;
			break;
		}

		memset(result, 0, 4096);

		for (i = 0; i < ARRAY_SIZE(client); i++) {
			struct i915_request *rq;

			rq = create_gpr_client(engine, global,
					       NUM_GPR * i * sizeof(u32));
			if (IS_ERR(rq)) {
				err = PTR_ERR(rq);
				goto end_test;
			}

			client[i] = rq;
		}

		/* Continuously preempt the set of 3 running contexts */
		for (i = 1; i <= NUM_GPR; i++) {
			err = preempt_user(engine, global, i);
			if (err)
				goto end_test;
		}

		if (READ_ONCE(result[0]) != NUM_GPR) {
			pr_err("%s: Failed to release semaphore\n",
			       engine->name);
			err = -EIO;
			goto end_test;
		}

		for (i = 0; i < ARRAY_SIZE(client); i++) {
			int gpr;

			if (i915_request_wait(client[i], 0, HZ / 2) < 0) {
				err = -ETIME;
				goto end_test;
			}

			for (gpr = 1; gpr < NUM_GPR; gpr++) {
				if (result[NUM_GPR * i + gpr] != 1) {
					pr_err("%s: Invalid result, client %d, gpr %d, result: %d\n",
					       engine->name,
					       i, gpr, result[NUM_GPR * i + gpr]);
					err = -EINVAL;
					goto end_test;
				}
			}
		}

end_test:
		for (i = 0; i < ARRAY_SIZE(client); i++) {
			if (!client[i])
				break;

			i915_request_put(client[i]);
		}

		/* Flush the semaphores on error */
		smp_store_mb(result[0], -1);
		if (igt_live_test_end(&t))
			err = -EIO;
		if (err)
			break;
	}

	i915_vma_unpin_and_release(&global, I915_VMA_RELEASE_MAP);
	return err;
}

static int live_preempt_timeout(void *arg)
{
	struct intel_gt *gt = arg;
	struct i915_gem_context *ctx_hi, *ctx_lo;
	struct igt_spinner spin_lo;
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	int err = -ENOMEM;

	/*
	 * Check that we force preemption to occur by cancelling the previous
	 * context if it refuses to yield the GPU.
	 */
	if (!IS_ACTIVE(CONFIG_DRM_I915_PREEMPT_TIMEOUT))
		return 0;

	if (!intel_has_reset_engine(gt))
		return 0;

	if (igt_spinner_init(&spin_lo, gt))
		return -ENOMEM;

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
		unsigned long saved_timeout;
		struct i915_request *rq;

		if (!intel_engine_has_preemption(engine))
			continue;

		rq = spinner_create_request(&spin_lo, ctx_lo, engine,
					    MI_NOOP); /* preemption disabled */
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		i915_request_add(rq);
		if (!igt_wait_for_spinner(&spin_lo, rq)) {
			intel_gt_set_wedged(gt);
			err = -EIO;
			goto err_ctx_lo;
		}

		rq = igt_request_alloc(ctx_hi, engine);
		if (IS_ERR(rq)) {
			igt_spinner_end(&spin_lo);
			err = PTR_ERR(rq);
			goto err_ctx_lo;
		}

		/* Flush the previous CS ack before changing timeouts */
		while (READ_ONCE(engine->execlists.pending[0]))
			cpu_relax();

		saved_timeout = engine->props.preempt_timeout_ms;
		engine->props.preempt_timeout_ms = 1; /* in ms, -> 1 jiffie */

		i915_request_get(rq);
		i915_request_add(rq);

		intel_engine_flush_submission(engine);
		engine->props.preempt_timeout_ms = saved_timeout;

		if (i915_request_wait(rq, 0, HZ / 10) < 0) {
			intel_gt_set_wedged(gt);
			i915_request_put(rq);
			err = -ETIME;
			goto err_ctx_lo;
		}

		igt_spinner_end(&spin_lo);
		i915_request_put(rq);
	}

	err = 0;
err_ctx_lo:
	kernel_context_close(ctx_lo);
err_ctx_hi:
	kernel_context_close(ctx_hi);
err_spin_lo:
	igt_spinner_fini(&spin_lo);
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
	} while (count < smoke->ncontext && !__igt_timeout(end_time, NULL));

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

	yield(); /* start all threads before we kthread_stop() */

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
		count, flags, smoke->gt->info.num_engines, smoke->ncontext);
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
	} while (count < smoke->ncontext && !__igt_timeout(end_time, NULL));

	pr_info("Submitted %lu random:%x requests across %d engines and %d contexts\n",
		count, flags, smoke->gt->info.num_engines, smoke->ncontext);
	return 0;
}

static int live_preempt_smoke(void *arg)
{
	struct preempt_smoke smoke = {
		.gt = arg,
		.prng = I915_RND_STATE_INITIALIZER(i915_selftest.random_seed),
		.ncontext = 256,
	};
	const unsigned int phase[] = { 0, BATCH };
	struct igt_live_test t;
	int err = -ENOMEM;
	u32 *cs;
	int n;

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
	struct i915_request *request[16] = {};
	struct intel_context *ve[16];
	unsigned long n, prime, nc;
	struct igt_live_test t;
	ktime_t times[2] = {};
	int err;

	GEM_BUG_ON(!nctx || nctx > ARRAY_SIZE(ve));

	for (n = 0; n < nctx; n++) {
		ve[n] = intel_execlists_create_virtual(siblings, nsibling);
		if (IS_ERR(ve[n])) {
			err = PTR_ERR(ve[n]);
			nctx = n;
			goto out;
		}

		err = intel_context_pin(ve[n]);
		if (err) {
			intel_context_put(ve[n]);
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
					struct i915_request *rq;

					rq = i915_request_create(ve[nc]);
					if (IS_ERR(rq)) {
						err = PTR_ERR(rq);
						goto out;
					}

					if (request[nc])
						i915_request_put(request[nc]);
					request[nc] = i915_request_get(rq);
					i915_request_add(rq);
				}
			}
		} else {
			for (n = 0; n < prime; n++) {
				for (nc = 0; nc < nctx; nc++) {
					struct i915_request *rq;

					rq = i915_request_create(ve[nc]);
					if (IS_ERR(rq)) {
						err = PTR_ERR(rq);
						goto out;
					}

					if (request[nc])
						i915_request_put(request[nc]);
					request[nc] = i915_request_get(rq);
					i915_request_add(rq);
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

		for (nc = 0; nc < nctx; nc++) {
			i915_request_put(request[nc]);
			request[nc] = NULL;
		}

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
		i915_request_put(request[nc]);
		intel_context_unpin(ve[nc]);
		intel_context_put(ve[nc]);
	}
	return err;
}

static unsigned int
__select_siblings(struct intel_gt *gt,
		  unsigned int class,
		  struct intel_engine_cs **siblings,
		  bool (*filter)(const struct intel_engine_cs *))
{
	unsigned int n = 0;
	unsigned int inst;

	for (inst = 0; inst <= MAX_ENGINE_INSTANCE; inst++) {
		if (!gt->engine_class[class][inst])
			continue;

		if (filter && !filter(gt->engine_class[class][inst]))
			continue;

		siblings[n++] = gt->engine_class[class][inst];
	}

	return n;
}

static unsigned int
select_siblings(struct intel_gt *gt,
		unsigned int class,
		struct intel_engine_cs **siblings)
{
	return __select_siblings(gt, class, siblings, NULL);
}

static int live_virtual_engine(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	struct intel_engine_cs *engine;
	enum intel_engine_id id;
	unsigned int class;
	int err;

	if (intel_uc_uses_guc_submission(&gt->uc))
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

		nsibling = select_siblings(gt, class, siblings);
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
	struct intel_context *ve;
	struct igt_live_test t;
	unsigned int n;
	int err;

	/*
	 * Check that by setting the execution mask on a request, we can
	 * restrict it to our desired engine within the virtual engine.
	 */

	ve = intel_execlists_create_virtual(siblings, nsibling);
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
	return err;
}

static int live_virtual_mask(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	unsigned int class;
	int err;

	if (intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		unsigned int nsibling;

		nsibling = select_siblings(gt, class, siblings);
		if (nsibling < 2)
			continue;

		err = mask_virtual_engine(gt, siblings, nsibling);
		if (err)
			return err;
	}

	return 0;
}

static int slicein_virtual_engine(struct intel_gt *gt,
				  struct intel_engine_cs **siblings,
				  unsigned int nsibling)
{
	const long timeout = slice_timeout(siblings[0]);
	struct intel_context *ce;
	struct i915_request *rq;
	struct igt_spinner spin;
	unsigned int n;
	int err = 0;

	/*
	 * Virtual requests must take part in timeslicing on the target engines.
	 */

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	for (n = 0; n < nsibling; n++) {
		ce = intel_context_create(siblings[n]);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto out;
		}

		rq = igt_spinner_create_request(&spin, ce, MI_ARB_CHECK);
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out;
		}

		i915_request_add(rq);
	}

	ce = intel_execlists_create_virtual(siblings, nsibling);
	if (IS_ERR(ce)) {
		err = PTR_ERR(ce);
		goto out;
	}

	rq = intel_context_create_request(ce);
	intel_context_put(ce);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out;
	}

	i915_request_get(rq);
	i915_request_add(rq);
	if (i915_request_wait(rq, 0, timeout) < 0) {
		GEM_TRACE_ERR("%s(%s) failed to slice in virtual request\n",
			      __func__, rq->engine->name);
		GEM_TRACE_DUMP();
		intel_gt_set_wedged(gt);
		err = -EIO;
	}
	i915_request_put(rq);

out:
	igt_spinner_end(&spin);
	if (igt_flush_test(gt->i915))
		err = -EIO;
	igt_spinner_fini(&spin);
	return err;
}

static int sliceout_virtual_engine(struct intel_gt *gt,
				   struct intel_engine_cs **siblings,
				   unsigned int nsibling)
{
	const long timeout = slice_timeout(siblings[0]);
	struct intel_context *ce;
	struct i915_request *rq;
	struct igt_spinner spin;
	unsigned int n;
	int err = 0;

	/*
	 * Virtual requests must allow others a fair timeslice.
	 */

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	/* XXX We do not handle oversubscription and fairness with normal rq */
	for (n = 0; n < nsibling; n++) {
		ce = intel_execlists_create_virtual(siblings, nsibling);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto out;
		}

		rq = igt_spinner_create_request(&spin, ce, MI_ARB_CHECK);
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out;
		}

		i915_request_add(rq);
	}

	for (n = 0; !err && n < nsibling; n++) {
		ce = intel_context_create(siblings[n]);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto out;
		}

		rq = intel_context_create_request(ce);
		intel_context_put(ce);
		if (IS_ERR(rq)) {
			err = PTR_ERR(rq);
			goto out;
		}

		i915_request_get(rq);
		i915_request_add(rq);
		if (i915_request_wait(rq, 0, timeout) < 0) {
			GEM_TRACE_ERR("%s(%s) failed to slice out virtual request\n",
				      __func__, siblings[n]->name);
			GEM_TRACE_DUMP();
			intel_gt_set_wedged(gt);
			err = -EIO;
		}
		i915_request_put(rq);
	}

out:
	igt_spinner_end(&spin);
	if (igt_flush_test(gt->i915))
		err = -EIO;
	igt_spinner_fini(&spin);
	return err;
}

static int live_virtual_slice(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	unsigned int class;
	int err;

	if (intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		unsigned int nsibling;

		nsibling = __select_siblings(gt, class, siblings,
					     intel_engine_has_timeslices);
		if (nsibling < 2)
			continue;

		err = slicein_virtual_engine(gt, siblings, nsibling);
		if (err)
			return err;

		err = sliceout_virtual_engine(gt, siblings, nsibling);
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
	struct intel_context *ve;
	struct i915_vma *scratch;
	struct igt_live_test t;
	unsigned int n;
	int err = 0;
	u32 *cs;

	scratch = __vm_create_scratch_for_read(&siblings[0]->gt->ggtt->vm,
					       PAGE_SIZE);
	if (IS_ERR(scratch))
		return PTR_ERR(scratch);

	err = i915_vma_sync(scratch);
	if (err)
		goto out_scratch;

	ve = intel_execlists_create_virtual(siblings, nsibling);
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
	return err;
}

static int live_virtual_preserved(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	unsigned int class;

	/*
	 * Check that the context image retains non-privileged (user) registers
	 * from one engine to the next. For this we check that the CS_GPR
	 * are preserved.
	 */

	if (intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	/* As we use CS_GPR we cannot run before they existed on all engines. */
	if (INTEL_GEN(gt->i915) < 9)
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		int nsibling, err;

		nsibling = select_siblings(gt, class, siblings);
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
	struct i915_request *rq[16];
	enum intel_engine_id id;
	struct igt_spinner spin;
	unsigned long n;
	int err;

	/*
	 * A set of bonded requests is intended to be run concurrently
	 * across a number of engines. We use one request per-engine
	 * and a magic fence to schedule each of the bonded requests
	 * at the same time. A consequence of our current scheduler is that
	 * we only move requests to the HW ready queue when the request
	 * becomes ready, that is when all of its prerequisite fences have
	 * been signaled. As one of those fences is the master submit fence,
	 * there is a delay on all secondary fences as the HW may be
	 * currently busy. Equally, as all the requests are independent,
	 * they may have other fences that delay individual request
	 * submission to HW. Ergo, we do not guarantee that all requests are
	 * immediately submitted to HW at the same time, just that if the
	 * rules are abided by, they are ready at the same time as the
	 * first is submitted. Userspace can embed semaphores in its batch
	 * to ensure parallel execution of its phases as it requires.
	 * Though naturally it gets requested that perhaps the scheduler should
	 * take care of parallel execution, even across preemption events on
	 * different HW. (The proper answer is of course "lalalala".)
	 *
	 * With the submit-fence, we have identified three possible phases
	 * of synchronisation depending on the master fence: queued (not
	 * ready), executing, and signaled. The first two are quite simple
	 * and checked below. However, the signaled master fence handling is
	 * contentious. Currently we do not distinguish between a signaled
	 * fence and an expired fence, as once signaled it does not convey
	 * any information about the previous execution. It may even be freed
	 * and hence checking later it may not exist at all. Ergo we currently
	 * do not apply the bonding constraint for an already signaled fence,
	 * as our expectation is that it should not constrain the secondaries
	 * and is outside of the scope of the bonded request API (i.e. all
	 * userspace requests are meant to be running in parallel). As
	 * it imposes no constraint, and is effectively a no-op, we do not
	 * check below as normal execution flows are checked extensively above.
	 *
	 * XXX Is the degenerate handling of signaled submit fences the
	 * expected behaviour for userpace?
	 */

	GEM_BUG_ON(nsibling >= ARRAY_SIZE(rq) - 1);

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	err = 0;
	rq[0] = ERR_PTR(-ENOMEM);
	for_each_engine(master, gt, id) {
		struct i915_sw_fence fence = {};
		struct intel_context *ce;

		if (master->class == class)
			continue;

		ce = intel_context_create(master);
		if (IS_ERR(ce)) {
			err = PTR_ERR(ce);
			goto out;
		}

		memset_p((void *)rq, ERR_PTR(-EINVAL), ARRAY_SIZE(rq));

		rq[0] = igt_spinner_create_request(&spin, ce, MI_NOOP);
		intel_context_put(ce);
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

		if (!(flags & BOND_SCHEDULE) &&
		    !igt_wait_for_spinner(&spin, rq[0])) {
			err = -EIO;
			goto out;
		}

		for (n = 0; n < nsibling; n++) {
			struct intel_context *ve;

			ve = intel_execlists_create_virtual(siblings, nsibling);
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
		intel_engine_flush_submission(master);
		igt_spinner_end(&spin);

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

	igt_spinner_fini(&spin);
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
	unsigned int class;
	int err;

	if (intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		const struct phase *p;
		int nsibling;

		nsibling = select_siblings(gt, class, siblings);
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

static int reset_virtual_engine(struct intel_gt *gt,
				struct intel_engine_cs **siblings,
				unsigned int nsibling)
{
	struct intel_engine_cs *engine;
	struct intel_context *ve;
	struct igt_spinner spin;
	struct i915_request *rq;
	unsigned int n;
	int err = 0;

	/*
	 * In order to support offline error capture for fast preempt reset,
	 * we need to decouple the guilty request and ensure that it and its
	 * descendents are not executed while the capture is in progress.
	 */

	if (igt_spinner_init(&spin, gt))
		return -ENOMEM;

	ve = intel_execlists_create_virtual(siblings, nsibling);
	if (IS_ERR(ve)) {
		err = PTR_ERR(ve);
		goto out_spin;
	}

	for (n = 0; n < nsibling; n++)
		st_engine_heartbeat_disable(siblings[n]);

	rq = igt_spinner_create_request(&spin, ve, MI_ARB_CHECK);
	if (IS_ERR(rq)) {
		err = PTR_ERR(rq);
		goto out_heartbeat;
	}
	i915_request_add(rq);

	if (!igt_wait_for_spinner(&spin, rq)) {
		intel_gt_set_wedged(gt);
		err = -ETIME;
		goto out_heartbeat;
	}

	engine = rq->engine;
	GEM_BUG_ON(engine == ve->engine);

	/* Take ownership of the reset and tasklet */
	local_bh_disable();
	if (test_and_set_bit(I915_RESET_ENGINE + engine->id,
			     &gt->reset.flags)) {
		local_bh_enable();
		intel_gt_set_wedged(gt);
		err = -EBUSY;
		goto out_heartbeat;
	}
	tasklet_disable(&engine->execlists.tasklet);

	engine->execlists.tasklet.func(engine->execlists.tasklet.data);
	GEM_BUG_ON(execlists_active(&engine->execlists) != rq);

	/* Fake a preemption event; failed of course */
	spin_lock_irq(&engine->active.lock);
	__unwind_incomplete_requests(engine);
	spin_unlock_irq(&engine->active.lock);
	GEM_BUG_ON(rq->engine != engine);

	/* Reset the engine while keeping our active request on hold */
	execlists_hold(engine, rq);
	GEM_BUG_ON(!i915_request_on_hold(rq));

	__intel_engine_reset_bh(engine, NULL);
	GEM_BUG_ON(rq->fence.error != -EIO);

	/* Release our grasp on the engine, letting CS flow again */
	tasklet_enable(&engine->execlists.tasklet);
	clear_and_wake_up_bit(I915_RESET_ENGINE + engine->id, &gt->reset.flags);
	local_bh_enable();

	/* Check that we do not resubmit the held request */
	i915_request_get(rq);
	if (!i915_request_wait(rq, 0, HZ / 5)) {
		pr_err("%s: on hold request completed!\n",
		       engine->name);
		intel_gt_set_wedged(gt);
		err = -EIO;
		goto out_rq;
	}
	GEM_BUG_ON(!i915_request_on_hold(rq));

	/* But is resubmitted on release */
	execlists_unhold(engine, rq);
	if (i915_request_wait(rq, 0, HZ / 5) < 0) {
		pr_err("%s: held request did not complete!\n",
		       engine->name);
		intel_gt_set_wedged(gt);
		err = -ETIME;
	}

out_rq:
	i915_request_put(rq);
out_heartbeat:
	for (n = 0; n < nsibling; n++)
		st_engine_heartbeat_enable(siblings[n]);

	intel_context_put(ve);
out_spin:
	igt_spinner_fini(&spin);
	return err;
}

static int live_virtual_reset(void *arg)
{
	struct intel_gt *gt = arg;
	struct intel_engine_cs *siblings[MAX_ENGINE_INSTANCE + 1];
	unsigned int class;

	/*
	 * Check that we handle a reset event within a virtual engine.
	 * Only the physical engine is reset, but we have to check the flow
	 * of the virtual requests around the reset, and make sure it is not
	 * forgotten.
	 */

	if (intel_uc_uses_guc_submission(&gt->uc))
		return 0;

	if (!intel_has_reset_engine(gt))
		return 0;

	for (class = 0; class <= MAX_ENGINE_CLASS; class++) {
		int nsibling, err;

		nsibling = select_siblings(gt, class, siblings);
		if (nsibling < 2)
			continue;

		err = reset_virtual_engine(gt, siblings, nsibling);
		if (err)
			return err;
	}

	return 0;
}

int intel_execlists_live_selftests(struct drm_i915_private *i915)
{
	static const struct i915_subtest tests[] = {
		SUBTEST(live_sanitycheck),
		SUBTEST(live_unlite_switch),
		SUBTEST(live_unlite_preempt),
		SUBTEST(live_unlite_ring),
		SUBTEST(live_pin_rewind),
		SUBTEST(live_hold_reset),
		SUBTEST(live_error_interrupt),
		SUBTEST(live_timeslice_preempt),
		SUBTEST(live_timeslice_rewind),
		SUBTEST(live_timeslice_queue),
		SUBTEST(live_timeslice_nopreempt),
		SUBTEST(live_busywait_preempt),
		SUBTEST(live_preempt),
		SUBTEST(live_late_preempt),
		SUBTEST(live_nopreempt),
		SUBTEST(live_preempt_cancel),
		SUBTEST(live_suppress_self_preempt),
		SUBTEST(live_chain_preempt),
		SUBTEST(live_preempt_ring),
		SUBTEST(live_preempt_gang),
		SUBTEST(live_preempt_timeout),
		SUBTEST(live_preempt_user),
		SUBTEST(live_preempt_smoke),
		SUBTEST(live_virtual_engine),
		SUBTEST(live_virtual_mask),
		SUBTEST(live_virtual_preserved),
		SUBTEST(live_virtual_slice),
		SUBTEST(live_virtual_bond),
		SUBTEST(live_virtual_reset),
	};

	if (!HAS_EXECLISTS(i915))
		return 0;

	if (intel_gt_is_wedged(&i915->gt))
		return 0;

	return intel_gt_live_subtests(tests, &i915->gt);
}
