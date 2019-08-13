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
#include <trace/events/dma_fence.h>
#include <uapi/linux/sched/types.h>

#include "i915_drv.h"
#include "i915_trace.h"

static void irq_enable(struct intel_engine_cs *engine)
{
	if (!engine->irq_enable)
		return;

	/* Caller disables interrupts */
	spin_lock(&engine->gt->irq_lock);
	engine->irq_enable(engine);
	spin_unlock(&engine->gt->irq_lock);
}

static void irq_disable(struct intel_engine_cs *engine)
{
	if (!engine->irq_disable)
		return;

	/* Caller disables interrupts */
	spin_lock(&engine->gt->irq_lock);
	engine->irq_disable(engine);
	spin_unlock(&engine->gt->irq_lock);
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
	unsigned long flags;

	if (!b->irq_armed)
		return;

	spin_lock_irqsave(&b->irq_lock, flags);
	if (b->irq_armed)
		__intel_breadcrumbs_disarm_irq(b);
	spin_unlock_irqrestore(&b->irq_lock, flags);
}

static inline bool __request_completed(const struct i915_request *rq)
{
	return i915_seqno_passed(__hwsp_seqno(rq), rq->fence.seqno);
}

__maybe_unused static bool
check_signal_order(struct intel_context *ce, struct i915_request *rq)
{
	if (!list_is_last(&rq->signal_link, &ce->signals) &&
	    i915_seqno_passed(rq->fence.seqno,
			      list_next_entry(rq, signal_link)->fence.seqno))
		return false;

	if (!list_is_first(&rq->signal_link, &ce->signals) &&
	    i915_seqno_passed(list_prev_entry(rq, signal_link)->fence.seqno,
			      rq->fence.seqno))
		return false;

	return true;
}

static bool
__dma_fence_signal(struct dma_fence *fence)
{
	return !test_and_set_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags);
}

static void
__dma_fence_signal__timestamp(struct dma_fence *fence, ktime_t timestamp)
{
	fence->timestamp = timestamp;
	set_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags);
	trace_dma_fence_signaled(fence);
}

static void
__dma_fence_signal__notify(struct dma_fence *fence)
{
	struct dma_fence_cb *cur, *tmp;

	lockdep_assert_held(fence->lock);
	lockdep_assert_irqs_disabled();

	list_for_each_entry_safe(cur, tmp, &fence->cb_list, node) {
		INIT_LIST_HEAD(&cur->node);
		cur->func(fence, cur);
	}
	INIT_LIST_HEAD(&fence->cb_list);
}

void intel_engine_breadcrumbs_irq(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	const ktime_t timestamp = ktime_get();
	struct intel_context *ce, *cn;
	struct list_head *pos, *next;
	LIST_HEAD(signal);

	spin_lock(&b->irq_lock);

	if (b->irq_armed && list_empty(&b->signalers))
		__intel_breadcrumbs_disarm_irq(b);

	list_for_each_entry_safe(ce, cn, &b->signalers, signal_link) {
		GEM_BUG_ON(list_empty(&ce->signals));

		list_for_each_safe(pos, next, &ce->signals) {
			struct i915_request *rq =
				list_entry(pos, typeof(*rq), signal_link);

			GEM_BUG_ON(!check_signal_order(ce, rq));

			if (!__request_completed(rq))
				break;

			GEM_BUG_ON(!test_bit(I915_FENCE_FLAG_SIGNAL,
					     &rq->fence.flags));
			clear_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags);

			if (!__dma_fence_signal(&rq->fence))
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

		__dma_fence_signal__timestamp(&rq->fence, timestamp);

		spin_lock(&rq->lock);
		__dma_fence_signal__notify(&rq->fence);
		spin_unlock(&rq->lock);

		i915_request_put(rq);
	}
}

void intel_engine_signal_breadcrumbs(struct intel_engine_cs *engine)
{
	local_irq_disable();
	intel_engine_breadcrumbs_irq(engine);
	local_irq_enable();
}

static void signal_irq_work(struct irq_work *work)
{
	struct intel_engine_cs *engine =
		container_of(work, typeof(*engine), breadcrumbs.irq_work);

	intel_engine_breadcrumbs_irq(engine);
}

static void __intel_breadcrumbs_arm_irq(struct intel_breadcrumbs *b)
{
	struct intel_engine_cs *engine =
		container_of(b, struct intel_engine_cs, breadcrumbs);

	lockdep_assert_held(&b->irq_lock);
	if (b->irq_armed)
		return;

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

	if (!b->irq_enabled++)
		irq_enable(engine);
}

void intel_engine_init_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	spin_lock_init(&b->irq_lock);
	INIT_LIST_HEAD(&b->signalers);

	init_irq_work(&b->irq_work, signal_irq_work);
}

void intel_engine_reset_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	unsigned long flags;

	spin_lock_irqsave(&b->irq_lock, flags);

	if (b->irq_enabled)
		irq_enable(engine);
	else
		irq_disable(engine);

	spin_unlock_irqrestore(&b->irq_lock, flags);
}

void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine)
{
}

bool i915_request_enable_breadcrumb(struct i915_request *rq)
{
	lockdep_assert_held(&rq->lock);
	lockdep_assert_irqs_disabled();

	if (test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags)) {
		struct intel_breadcrumbs *b = &rq->engine->breadcrumbs;
		struct intel_context *ce = rq->hw_context;
		struct list_head *pos;

		spin_lock(&b->irq_lock);
		GEM_BUG_ON(test_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags));

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
		GEM_BUG_ON(!check_signal_order(ce, rq));

		set_bit(I915_FENCE_FLAG_SIGNAL, &rq->fence.flags);
		spin_unlock(&b->irq_lock);
	}

	return !__request_completed(rq);
}

void i915_request_cancel_breadcrumb(struct i915_request *rq)
{
	struct intel_breadcrumbs *b = &rq->engine->breadcrumbs;

	lockdep_assert_held(&rq->lock);
	lockdep_assert_irqs_disabled();

	/*
	 * We must wait for b->irq_lock so that we know the interrupt handler
	 * has released its reference to the intel_context and has completed
	 * the DMA_FENCE_FLAG_SIGNALED_BIT/I915_FENCE_FLAG_SIGNAL dance (if
	 * required).
	 */
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
}
