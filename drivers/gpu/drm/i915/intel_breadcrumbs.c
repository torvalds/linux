/*
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

#include <linux/kthread.h>
#include <uapi/linux/sched/types.h>

#include "i915_drv.h"

#define task_asleep(tsk) ((tsk)->state & TASK_NORMAL && !(tsk)->on_rq)

static void irq_enable(struct intel_engine_cs *engine)
{
	if (!engine->irq_enable)
		return;

	/* Caller disables interrupts */
	spin_lock(&engine->i915->irq_lock);
	engine->irq_enable(engine);
	spin_unlock(&engine->i915->irq_lock);
}

static void irq_disable(struct intel_engine_cs *engine)
{
	if (!engine->irq_disable)
		return;

	/* Caller disables interrupts */
	spin_lock(&engine->i915->irq_lock);
	engine->irq_disable(engine);
	spin_unlock(&engine->i915->irq_lock);
}

static void __intel_breadcrumbs_disarm_irq(struct intel_breadcrumbs *b)
{
	lockdep_assert_held(&b->irq_lock);

	GEM_BUG_ON(!b->irq_enabled);
	if (!--b->irq_enabled)
		irq_disable(container_of(b,
					 struct intel_engine_cs,
					 breadcrumbs));

	b->irq_armed = false;
}

void intel_engine_disarm_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	if (!b->irq_armed)
		return;

	spin_lock_irq(&b->irq_lock);
	if (b->irq_armed)
		__intel_breadcrumbs_disarm_irq(b);
	spin_unlock_irq(&b->irq_lock);
}

static inline bool __request_completed(const struct i915_request *rq)
{
	return i915_seqno_passed(__hwsp_seqno(rq), rq->fence.seqno);
}

bool intel_engine_breadcrumbs_irq(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct intel_context *ce, *cn;
	struct list_head *pos, *next;
	LIST_HEAD(signal);

	spin_lock(&b->irq_lock);

	b->irq_fired = true;
	if (b->irq_armed && list_empty(&b->signalers))
		__intel_breadcrumbs_disarm_irq(b);

	list_for_each_entry_safe(ce, cn, &b->signalers, signal_link) {
		GEM_BUG_ON(list_empty(&ce->signals));

		list_for_each_safe(pos, next, &ce->signals) {
			struct i915_request *rq =
				list_entry(pos, typeof(*rq), signal_link);

			if (!__request_completed(rq))
				break;

			GEM_BUG_ON(!test_bit(I915_FENCE_FLAG_SIGNAL,
					     &rq->fence.flags));
			clear_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags);

			/*
			 * We may race with direct invocation of
			 * dma_fence_signal(), e.g. i915_request_retire(),
			 * in which case we can skip processing it ourselves.
			 */
			if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
				     &rq->fence.flags))
				continue;

			/*
			 * Queue for execution after dropping the signaling
			 * spinlock as the callback chain may end up adding
			 * more signalers to the same context or engine.
			 */
			i915_request_get(rq);
			list_add_tail(&rq->signal_link, &signal);
		}

		/*
		 * We process the list deletion in bulk, only using a list_add
		 * (not list_move) above but keeping the status of
		 * rq->signal_link known with the I915_FENCE_FLAG_SIGNAL bit.
		 */
		if (!list_is_first(pos, &ce->signals)) {
			/* Advance the list to the first incomplete request */
			__list_del_many(&ce->signals, pos);
			if (&ce->signals == pos) /* now empty */
				list_del_init(&ce->signal_link);
		}
	}

	spin_unlock(&b->irq_lock);

	list_for_each_safe(pos, next, &signal) {
		struct i915_request *rq =
			list_entry(pos, typeof(*rq), signal_link);

		dma_fence_signal(&rq->fence);
		i915_request_put(rq);
	}

	return !list_empty(&signal);
}

bool intel_engine_signal_breadcrumbs(struct intel_engine_cs *engine)
{
	bool result;

	local_irq_disable();
	result = intel_engine_breadcrumbs_irq(engine);
	local_irq_enable();

	return result;
}

static void signal_irq_work(struct irq_work *work)
{
	struct intel_engine_cs *engine =
		container_of(work, typeof(*engine), breadcrumbs.irq_work);

	intel_engine_breadcrumbs_irq(engine);
}

static unsigned long wait_timeout(void)
{
	return round_jiffies_up(jiffies + DRM_I915_HANGCHECK_JIFFIES);
}

static noinline void missed_breadcrumb(struct intel_engine_cs *engine)
{
	if (GEM_SHOW_DEBUG()) {
		struct drm_printer p = drm_debug_printer(__func__);

		intel_engine_dump(engine, &p,
				  "%s missed breadcrumb at %pS\n",
				  engine->name, __builtin_return_address(0));
	}

	set_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings);
}

static void intel_breadcrumbs_hangcheck(struct timer_list *t)
{
	struct intel_engine_cs *engine =
		from_timer(engine, t, breadcrumbs.hangcheck);
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	if (!b->irq_armed)
		return;

	if (b->irq_fired)
		goto rearm;

	/*
	 * We keep the hangcheck timer alive until we disarm the irq, even
	 * if there are no waiters at present.
	 *
	 * If the waiter was currently running, assume it hasn't had a chance
	 * to process the pending interrupt (e.g, low priority task on a loaded
	 * system) and wait until it sleeps before declaring a missed interrupt.
	 *
	 * If the waiter was asleep (and not even pending a wakeup), then we
	 * must have missed an interrupt as the GPU has stopped advancing
	 * but we still have a waiter. Assuming all batches complete within
	 * DRM_I915_HANGCHECK_JIFFIES [1.5s]!
	 */
	synchronize_hardirq(engine->i915->drm.irq);
	if (intel_engine_signal_breadcrumbs(engine)) {
		missed_breadcrumb(engine);
		mod_timer(&b->fake_irq, jiffies + 1);
	} else {
rearm:
		b->irq_fired = false;
		mod_timer(&b->hangcheck, wait_timeout());
	}
}

static void intel_breadcrumbs_fake_irq(struct timer_list *t)
{
	struct intel_engine_cs *engine =
		from_timer(engine, t, breadcrumbs.fake_irq);
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	/*
	 * The timer persists in case we cannot enable interrupts,
	 * or if we have previously seen seqno/interrupt incoherency
	 * ("missed interrupt" syndrome, better known as a "missed breadcrumb").
	 * Here the worker will wake up every jiffie in order to kick the
	 * oldest waiter to do the coherent seqno check.
	 */

	if (!intel_engine_signal_breadcrumbs(engine) && !b->irq_armed)
		return;

	/* If the user has disabled the fake-irq, restore the hangchecking */
	if (!test_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings)) {
		mod_timer(&b->hangcheck, wait_timeout());
		return;
	}

	mod_timer(&b->fake_irq, jiffies + 1);
}

void intel_engine_pin_breadcrumbs_irq(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	spin_lock_irq(&b->irq_lock);
	if (!b->irq_enabled++)
		irq_enable(engine);
	GEM_BUG_ON(!b->irq_enabled); /* no overflow! */
	spin_unlock_irq(&b->irq_lock);
}

void intel_engine_unpin_breadcrumbs_irq(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	spin_lock_irq(&b->irq_lock);
	GEM_BUG_ON(!b->irq_enabled); /* no underflow! */
	if (!--b->irq_enabled)
		irq_disable(engine);
	spin_unlock_irq(&b->irq_lock);
}

static bool use_fake_irq(const struct intel_breadcrumbs *b)
{
	const struct intel_engine_cs *engine =
		container_of(b, struct intel_engine_cs, breadcrumbs);

	if (!test_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings))
		return false;

	/*
	 * Only start with the heavy weight fake irq timer if we have not
	 * seen any interrupts since enabling it the first time. If the
	 * interrupts are still arriving, it means we made a mistake in our
	 * engine->seqno_barrier(), a timing error that should be transient
	 * and unlikely to reoccur.
	 */
	return !b->irq_fired;
}

static void enable_fake_irq(struct intel_breadcrumbs *b)
{
	/* Ensure we never sleep indefinitely */
	if (!b->irq_enabled || use_fake_irq(b))
		mod_timer(&b->fake_irq, jiffies + 1);
	else
		mod_timer(&b->hangcheck, wait_timeout());
}

static bool __intel_breadcrumbs_arm_irq(struct intel_breadcrumbs *b)
{
	struct intel_engine_cs *engine =
		container_of(b, struct intel_engine_cs, breadcrumbs);
	struct drm_i915_private *i915 = engine->i915;
	bool enabled;

	lockdep_assert_held(&b->irq_lock);
	if (b->irq_armed)
		return false;

	/*
	 * The breadcrumb irq will be disarmed on the interrupt after the
	 * waiters are signaled. This gives us a single interrupt window in
	 * which we can add a new waiter and avoid the cost of re-enabling
	 * the irq.
	 */
	b->irq_armed = true;

	/*
	 * Since we are waiting on a request, the GPU should be busy
	 * and should have its own rpm reference. This is tracked
	 * by i915->gt.awake, we can forgo holding our own wakref
	 * for the interrupt as before i915->gt.awake is released (when
	 * the driver is idle) we disarm the breadcrumbs.
	 */

	/* No interrupts? Kick the waiter every jiffie! */
	enabled = false;
	if (!b->irq_enabled++ &&
	    !test_bit(engine->id, &i915->gpu_error.test_irq_rings)) {
		irq_enable(engine);
		enabled = true;
	}

	enable_fake_irq(b);
	return enabled;
}

void intel_engine_init_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	spin_lock_init(&b->irq_lock);
	INIT_LIST_HEAD(&b->signalers);

	init_irq_work(&b->irq_work, signal_irq_work);

	timer_setup(&b->fake_irq, intel_breadcrumbs_fake_irq, 0);
	timer_setup(&b->hangcheck, intel_breadcrumbs_hangcheck, 0);
}

static void cancel_fake_irq(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	del_timer_sync(&b->fake_irq); /* may queue b->hangcheck */
	del_timer_sync(&b->hangcheck);
	clear_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings);
}

void intel_engine_reset_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	unsigned long flags;

	spin_lock_irqsave(&b->irq_lock, flags);

	/*
	 * Leave the fake_irq timer enabled (if it is running), but clear the
	 * bit so that it turns itself off on its next wake up and goes back
	 * to the long hangcheck interval if still required.
	 */
	clear_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings);

	if (b->irq_enabled)
		irq_enable(engine);
	else
		irq_disable(engine);

	spin_unlock_irqrestore(&b->irq_lock, flags);
}

void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine)
{
	cancel_fake_irq(engine);
}

bool i915_request_enable_breadcrumb(struct i915_request *rq)
{
	struct intel_breadcrumbs *b = &rq->engine->breadcrumbs;

	GEM_BUG_ON(test_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags));

	if (!test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags))
		return true;

	spin_lock(&b->irq_lock);
	if (test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags) &&
	    !__request_completed(rq)) {
		struct intel_context *ce = rq->hw_context;
		struct list_head *pos;

		__intel_breadcrumbs_arm_irq(b);

		/*
		 * We keep the seqno in retirement order, so we can break
		 * inside intel_engine_breadcrumbs_irq as soon as we've passed
		 * the last completed request (or seen a request that hasn't
		 * event started). We could iterate the timeline->requests list,
		 * but keeping a separate signalers_list has the advantage of
		 * hopefully being much smaller than the full list and so
		 * provides faster iteration and detection when there are no
		 * more interrupts required for this context.
		 *
		 * We typically expect to add new signalers in order, so we
		 * start looking for our insertion point from the tail of
		 * the list.
		 */
		list_for_each_prev(pos, &ce->signals) {
			struct i915_request *it =
				list_entry(pos, typeof(*it), signal_link);

			if (i915_seqno_passed(rq->fence.seqno, it->fence.seqno))
				break;
		}
		list_add(&rq->signal_link, pos);
		if (pos == &ce->signals) /* catch transitions from empty list */
			list_move_tail(&ce->signal_link, &b->signalers);

		set_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags);
	}
	spin_unlock(&b->irq_lock);

	return !__request_completed(rq);
}

void i915_request_cancel_breadcrumb(struct i915_request *rq)
{
	struct intel_breadcrumbs *b = &rq->engine->breadcrumbs;

	if (!test_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags))
		return;

	spin_lock(&b->irq_lock);
	if (test_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags)) {
		struct intel_context *ce = rq->hw_context;

		list_del(&rq->signal_link);
		if (list_empty(&ce->signals))
			list_del_init(&ce->signal_link);

		clear_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags);
	}
	spin_unlock(&b->irq_lock);
}

void intel_engine_print_breadcrumbs(struct intel_engine_cs *engine,
				    struct drm_printer *p)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct intel_context *ce;
	struct i915_request *rq;

	if (list_empty(&b->signalers))
		return;

	drm_printf(p, "Signals:\n");

	spin_lock_irq(&b->irq_lock);
	list_for_each_entry(ce, &b->signalers, signal_link) {
		list_for_each_entry(rq, &ce->signals, signal_link) {
			drm_printf(p, "\t[%llx:%llx%s] @ %dms\n",
				   rq->fence.context, rq->fence.seqno,
				   i915_request_completed(rq) ? "!" :
				   i915_request_started(rq) ? "*" :
				   "",
				   jiffies_to_msecs(jiffies - rq->emitted_jiffies));
		}
	}
	spin_unlock_irq(&b->irq_lock);

	if (test_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings))
		drm_printf(p, "Fake irq active\n");
}
