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

#ifdef CONFIG_SMP
#define task_asleep(tsk) ((tsk)->state & TASK_NORMAL && !(tsk)->on_cpu)
#else
#define task_asleep(tsk) ((tsk)->state & TASK_NORMAL)
#endif

static unsigned int __intel_breadcrumbs_wakeup(struct intel_breadcrumbs *b)
{
	struct intel_wait *wait;
	unsigned int result = 0;

	lockdep_assert_held(&b->irq_lock);

	wait = b->irq_wait;
	if (wait) {
		/*
		 * N.B. Since task_asleep() and ttwu are not atomic, the
		 * waiter may actually go to sleep after the check, causing
		 * us to suppress a valid wakeup. We prefer to reduce the
		 * number of false positive missed_breadcrumb() warnings
		 * at the expense of a few false negatives, as it it easy
		 * to trigger a false positive under heavy load. Enough
		 * signal should remain from genuine missed_breadcrumb()
		 * for us to detect in CI.
		 */
		bool was_asleep = task_asleep(wait->tsk);

		result = ENGINE_WAKEUP_WAITER;
		if (wake_up_process(wait->tsk) && was_asleep)
			result |= ENGINE_WAKEUP_ASLEEP;
	}

	return result;
}

unsigned int intel_engine_wakeup(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	unsigned long flags;
	unsigned int result;

	spin_lock_irqsave(&b->irq_lock, flags);
	result = __intel_breadcrumbs_wakeup(b);
	spin_unlock_irqrestore(&b->irq_lock, flags);

	return result;
}

static unsigned long wait_timeout(void)
{
	return round_jiffies_up(jiffies + DRM_I915_HANGCHECK_JIFFIES);
}

static noinline void missed_breadcrumb(struct intel_engine_cs *engine)
{
	if (drm_debug & DRM_UT_DRIVER) {
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

	if (b->hangcheck_interrupts != atomic_read(&engine->irq_count)) {
		b->hangcheck_interrupts = atomic_read(&engine->irq_count);
		mod_timer(&b->hangcheck, wait_timeout());
		return;
	}

	/* We keep the hangcheck timer alive until we disarm the irq, even
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
	if (intel_engine_wakeup(engine) & ENGINE_WAKEUP_ASLEEP) {
		missed_breadcrumb(engine);
		mod_timer(&b->fake_irq, jiffies + 1);
	} else {
		mod_timer(&b->hangcheck, wait_timeout());
	}
}

static void intel_breadcrumbs_fake_irq(struct timer_list *t)
{
	struct intel_engine_cs *engine = from_timer(engine, t,
						    breadcrumbs.fake_irq);
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	/* The timer persists in case we cannot enable interrupts,
	 * or if we have previously seen seqno/interrupt incoherency
	 * ("missed interrupt" syndrome, better known as a "missed breadcrumb").
	 * Here the worker will wake up every jiffie in order to kick the
	 * oldest waiter to do the coherent seqno check.
	 */

	spin_lock_irq(&b->irq_lock);
	if (b->irq_armed && !__intel_breadcrumbs_wakeup(b))
		__intel_engine_disarm_breadcrumbs(engine);
	spin_unlock_irq(&b->irq_lock);
	if (!b->irq_armed)
		return;

	mod_timer(&b->fake_irq, jiffies + 1);
}

static void irq_enable(struct intel_engine_cs *engine)
{
	/*
	 * FIXME: Ideally we want this on the API boundary, but for the
	 * sake of testing with mock breadcrumbs (no HW so unable to
	 * enable irqs) we place it deep within the bowels, at the point
	 * of no return.
	 */
	GEM_BUG_ON(!intel_irqs_enabled(engine->i915));

	/* Enabling the IRQ may miss the generation of the interrupt, but
	 * we still need to force the barrier before reading the seqno,
	 * just in case.
	 */
	set_bit(ENGINE_IRQ_BREADCRUMB, &engine->irq_posted);

	/* Caller disables interrupts */
	if (engine->irq_enable) {
		spin_lock(&engine->i915->irq_lock);
		engine->irq_enable(engine);
		spin_unlock(&engine->i915->irq_lock);
	}
}

static void irq_disable(struct intel_engine_cs *engine)
{
	/* Caller disables interrupts */
	if (engine->irq_disable) {
		spin_lock(&engine->i915->irq_lock);
		engine->irq_disable(engine);
		spin_unlock(&engine->i915->irq_lock);
	}
}

void __intel_engine_disarm_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	lockdep_assert_held(&b->irq_lock);
	GEM_BUG_ON(b->irq_wait);
	GEM_BUG_ON(!b->irq_armed);

	GEM_BUG_ON(!b->irq_enabled);
	if (!--b->irq_enabled)
		irq_disable(engine);

	b->irq_armed = false;
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

void intel_engine_disarm_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct intel_wait *wait, *n;

	if (!b->irq_armed)
		return;

	/*
	 * We only disarm the irq when we are idle (all requests completed),
	 * so if the bottom-half remains asleep, it missed the request
	 * completion.
	 */
	if (intel_engine_wakeup(engine) & ENGINE_WAKEUP_ASLEEP)
		missed_breadcrumb(engine);

	spin_lock_irq(&b->rb_lock);

	spin_lock(&b->irq_lock);
	b->irq_wait = NULL;
	if (b->irq_armed)
		__intel_engine_disarm_breadcrumbs(engine);
	spin_unlock(&b->irq_lock);

	rbtree_postorder_for_each_entry_safe(wait, n, &b->waiters, node) {
		GEM_BUG_ON(!i915_seqno_passed(intel_engine_get_seqno(engine),
					      wait->seqno));
		RB_CLEAR_NODE(&wait->node);
		wake_up_process(wait->tsk);
	}
	b->waiters = RB_ROOT;

	spin_unlock_irq(&b->rb_lock);
}

static bool use_fake_irq(const struct intel_breadcrumbs *b)
{
	const struct intel_engine_cs *engine =
		container_of(b, struct intel_engine_cs, breadcrumbs);

	if (!test_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings))
		return false;

	/* Only start with the heavy weight fake irq timer if we have not
	 * seen any interrupts since enabling it the first time. If the
	 * interrupts are still arriving, it means we made a mistake in our
	 * engine->seqno_barrier(), a timing error that should be transient
	 * and unlikely to reoccur.
	 */
	return atomic_read(&engine->irq_count) == b->hangcheck_interrupts;
}

static void enable_fake_irq(struct intel_breadcrumbs *b)
{
	/* Ensure we never sleep indefinitely */
	if (!b->irq_enabled || use_fake_irq(b))
		mod_timer(&b->fake_irq, jiffies + 1);
	else
		mod_timer(&b->hangcheck, wait_timeout());
}

static bool __intel_breadcrumbs_enable_irq(struct intel_breadcrumbs *b)
{
	struct intel_engine_cs *engine =
		container_of(b, struct intel_engine_cs, breadcrumbs);
	struct drm_i915_private *i915 = engine->i915;
	bool enabled;

	lockdep_assert_held(&b->irq_lock);
	if (b->irq_armed)
		return false;

	/* The breadcrumb irq will be disarmed on the interrupt after the
	 * waiters are signaled. This gives us a single interrupt window in
	 * which we can add a new waiter and avoid the cost of re-enabling
	 * the irq.
	 */
	b->irq_armed = true;

	if (I915_SELFTEST_ONLY(b->mock)) {
		/* For our mock objects we want to avoid interaction
		 * with the real hardware (which is not set up). So
		 * we simply pretend we have enabled the powerwell
		 * and the irq, and leave it up to the mock
		 * implementation to call intel_engine_wakeup()
		 * itself when it wants to simulate a user interrupt,
		 */
		return true;
	}

	/* Since we are waiting on a request, the GPU should be busy
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

static inline struct intel_wait *to_wait(struct rb_node *node)
{
	return rb_entry(node, struct intel_wait, node);
}

static inline void __intel_breadcrumbs_finish(struct intel_breadcrumbs *b,
					      struct intel_wait *wait)
{
	lockdep_assert_held(&b->rb_lock);
	GEM_BUG_ON(b->irq_wait == wait);

	/*
	 * This request is completed, so remove it from the tree, mark it as
	 * complete, and *then* wake up the associated task. N.B. when the
	 * task wakes up, it will find the empty rb_node, discern that it
	 * has already been removed from the tree and skip the serialisation
	 * of the b->rb_lock and b->irq_lock. This means that the destruction
	 * of the intel_wait is not serialised with the interrupt handler
	 * by the waiter - it must instead be serialised by the caller.
	 */
	rb_erase(&wait->node, &b->waiters);
	RB_CLEAR_NODE(&wait->node);

	if (wait->tsk->state != TASK_RUNNING)
		wake_up_process(wait->tsk); /* implicit smp_wmb() */
}

static inline void __intel_breadcrumbs_next(struct intel_engine_cs *engine,
					    struct rb_node *next)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	spin_lock(&b->irq_lock);
	GEM_BUG_ON(!b->irq_armed);
	GEM_BUG_ON(!b->irq_wait);
	b->irq_wait = to_wait(next);
	spin_unlock(&b->irq_lock);

	/* We always wake up the next waiter that takes over as the bottom-half
	 * as we may delegate not only the irq-seqno barrier to the next waiter
	 * but also the task of waking up concurrent waiters.
	 */
	if (next)
		wake_up_process(to_wait(next)->tsk);
}

static bool __intel_engine_add_wait(struct intel_engine_cs *engine,
				    struct intel_wait *wait)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct rb_node **p, *parent, *completed;
	bool first, armed;
	u32 seqno;

	GEM_BUG_ON(!wait->seqno);

	/* Insert the request into the retirement ordered list
	 * of waiters by walking the rbtree. If we are the oldest
	 * seqno in the tree (the first to be retired), then
	 * set ourselves as the bottom-half.
	 *
	 * As we descend the tree, prune completed branches since we hold the
	 * spinlock we know that the first_waiter must be delayed and can
	 * reduce some of the sequential wake up latency if we take action
	 * ourselves and wake up the completed tasks in parallel. Also, by
	 * removing stale elements in the tree, we may be able to reduce the
	 * ping-pong between the old bottom-half and ourselves as first-waiter.
	 */
	armed = false;
	first = true;
	parent = NULL;
	completed = NULL;
	seqno = intel_engine_get_seqno(engine);

	 /* If the request completed before we managed to grab the spinlock,
	  * return now before adding ourselves to the rbtree. We let the
	  * current bottom-half handle any pending wakeups and instead
	  * try and get out of the way quickly.
	  */
	if (i915_seqno_passed(seqno, wait->seqno)) {
		RB_CLEAR_NODE(&wait->node);
		return first;
	}

	p = &b->waiters.rb_node;
	while (*p) {
		parent = *p;
		if (wait->seqno == to_wait(parent)->seqno) {
			/* We have multiple waiters on the same seqno, select
			 * the highest priority task (that with the smallest
			 * task->prio) to serve as the bottom-half for this
			 * group.
			 */
			if (wait->tsk->prio > to_wait(parent)->tsk->prio) {
				p = &parent->rb_right;
				first = false;
			} else {
				p = &parent->rb_left;
			}
		} else if (i915_seqno_passed(wait->seqno,
					     to_wait(parent)->seqno)) {
			p = &parent->rb_right;
			if (i915_seqno_passed(seqno, to_wait(parent)->seqno))
				completed = parent;
			else
				first = false;
		} else {
			p = &parent->rb_left;
		}
	}
	rb_link_node(&wait->node, parent, p);
	rb_insert_color(&wait->node, &b->waiters);

	if (first) {
		spin_lock(&b->irq_lock);
		b->irq_wait = wait;
		/* After assigning ourselves as the new bottom-half, we must
		 * perform a cursory check to prevent a missed interrupt.
		 * Either we miss the interrupt whilst programming the hardware,
		 * or if there was a previous waiter (for a later seqno) they
		 * may be woken instead of us (due to the inherent race
		 * in the unlocked read of b->irq_seqno_bh in the irq handler)
		 * and so we miss the wake up.
		 */
		armed = __intel_breadcrumbs_enable_irq(b);
		spin_unlock(&b->irq_lock);
	}

	if (completed) {
		/* Advance the bottom-half (b->irq_wait) before we wake up
		 * the waiters who may scribble over their intel_wait
		 * just as the interrupt handler is dereferencing it via
		 * b->irq_wait.
		 */
		if (!first) {
			struct rb_node *next = rb_next(completed);
			GEM_BUG_ON(next == &wait->node);
			__intel_breadcrumbs_next(engine, next);
		}

		do {
			struct intel_wait *crumb = to_wait(completed);
			completed = rb_prev(completed);
			__intel_breadcrumbs_finish(b, crumb);
		} while (completed);
	}

	GEM_BUG_ON(!b->irq_wait);
	GEM_BUG_ON(!b->irq_armed);
	GEM_BUG_ON(rb_first(&b->waiters) != &b->irq_wait->node);

	return armed;
}

bool intel_engine_add_wait(struct intel_engine_cs *engine,
			   struct intel_wait *wait)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	bool armed;

	spin_lock_irq(&b->rb_lock);
	armed = __intel_engine_add_wait(engine, wait);
	spin_unlock_irq(&b->rb_lock);
	if (armed)
		return armed;

	/* Make the caller recheck if its request has already started. */
	return i915_seqno_passed(intel_engine_get_seqno(engine),
				 wait->seqno - 1);
}

static inline bool chain_wakeup(struct rb_node *rb, int priority)
{
	return rb && to_wait(rb)->tsk->prio <= priority;
}

static inline int wakeup_priority(struct intel_breadcrumbs *b,
				  struct task_struct *tsk)
{
	if (tsk == b->signaler)
		return INT_MIN;
	else
		return tsk->prio;
}

static void __intel_engine_remove_wait(struct intel_engine_cs *engine,
				       struct intel_wait *wait)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	lockdep_assert_held(&b->rb_lock);

	if (RB_EMPTY_NODE(&wait->node))
		goto out;

	if (b->irq_wait == wait) {
		const int priority = wakeup_priority(b, wait->tsk);
		struct rb_node *next;

		/* We are the current bottom-half. Find the next candidate,
		 * the first waiter in the queue on the remaining oldest
		 * request. As multiple seqnos may complete in the time it
		 * takes us to wake up and find the next waiter, we have to
		 * wake up that waiter for it to perform its own coherent
		 * completion check.
		 */
		next = rb_next(&wait->node);
		if (chain_wakeup(next, priority)) {
			/* If the next waiter is already complete,
			 * wake it up and continue onto the next waiter. So
			 * if have a small herd, they will wake up in parallel
			 * rather than sequentially, which should reduce
			 * the overall latency in waking all the completed
			 * clients.
			 *
			 * However, waking up a chain adds extra latency to
			 * the first_waiter. This is undesirable if that
			 * waiter is a high priority task.
			 */
			u32 seqno = intel_engine_get_seqno(engine);

			while (i915_seqno_passed(seqno, to_wait(next)->seqno)) {
				struct rb_node *n = rb_next(next);

				__intel_breadcrumbs_finish(b, to_wait(next));
				next = n;
				if (!chain_wakeup(next, priority))
					break;
			}
		}

		__intel_breadcrumbs_next(engine, next);
	} else {
		GEM_BUG_ON(rb_first(&b->waiters) == &wait->node);
	}

	GEM_BUG_ON(RB_EMPTY_NODE(&wait->node));
	rb_erase(&wait->node, &b->waiters);
	RB_CLEAR_NODE(&wait->node);

out:
	GEM_BUG_ON(b->irq_wait == wait);
	GEM_BUG_ON(rb_first(&b->waiters) !=
		   (b->irq_wait ? &b->irq_wait->node : NULL));
}

void intel_engine_remove_wait(struct intel_engine_cs *engine,
			      struct intel_wait *wait)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	/* Quick check to see if this waiter was already decoupled from
	 * the tree by the bottom-half to avoid contention on the spinlock
	 * by the herd.
	 */
	if (RB_EMPTY_NODE(&wait->node)) {
		GEM_BUG_ON(READ_ONCE(b->irq_wait) == wait);
		return;
	}

	spin_lock_irq(&b->rb_lock);
	__intel_engine_remove_wait(engine, wait);
	spin_unlock_irq(&b->rb_lock);
}

static void signaler_set_rtpriority(void)
{
	 struct sched_param param = { .sched_priority = 1 };

	 sched_setscheduler_nocheck(current, SCHED_FIFO, &param);
}

static int intel_breadcrumbs_signaler(void *arg)
{
	struct intel_engine_cs *engine = arg;
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct i915_request *rq, *n;

	/* Install ourselves with high priority to reduce signalling latency */
	signaler_set_rtpriority();

	do {
		bool do_schedule = true;
		LIST_HEAD(list);
		u32 seqno;

		set_current_state(TASK_INTERRUPTIBLE);
		if (list_empty(&b->signals))
			goto sleep;

		/*
		 * We are either woken up by the interrupt bottom-half,
		 * or by a client adding a new signaller. In both cases,
		 * the GPU seqno may have advanced beyond our oldest signal.
		 * If it has, propagate the signal, remove the waiter and
		 * check again with the next oldest signal. Otherwise we
		 * need to wait for a new interrupt from the GPU or for
		 * a new client.
		 */
		seqno = intel_engine_get_seqno(engine);

		spin_lock_irq(&b->rb_lock);
		list_for_each_entry_safe(rq, n, &b->signals, signaling.link) {
			u32 this = rq->signaling.wait.seqno;

			GEM_BUG_ON(!rq->signaling.wait.seqno);

			if (!i915_seqno_passed(seqno, this))
				break;

			if (likely(this == i915_request_global_seqno(rq))) {
				__intel_engine_remove_wait(engine,
							   &rq->signaling.wait);

				rq->signaling.wait.seqno = 0;
				__list_del_entry(&rq->signaling.link);

				if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT,
					      &rq->fence.flags)) {
					list_add_tail(&rq->signaling.link,
						      &list);
					i915_request_get(rq);
				}
			}
		}
		spin_unlock_irq(&b->rb_lock);

		if (!list_empty(&list)) {
			local_bh_disable();
			list_for_each_entry_safe(rq, n, &list, signaling.link) {
				dma_fence_signal(&rq->fence);
				GEM_BUG_ON(!i915_request_completed(rq));
				i915_request_put(rq);
			}
			local_bh_enable(); /* kick start the tasklets */

			/*
			 * If the engine is saturated we may be continually
			 * processing completed requests. This angers the
			 * NMI watchdog if we never let anything else
			 * have access to the CPU. Let's pretend to be nice
			 * and relinquish the CPU if we burn through the
			 * entire RT timeslice!
			 */
			do_schedule = need_resched();
		}

		if (unlikely(do_schedule)) {
			/* Before we sleep, check for a missed seqno */
			if (current->state & TASK_NORMAL &&
			    !list_empty(&b->signals) &&
			    engine->irq_seqno_barrier &&
			    test_and_clear_bit(ENGINE_IRQ_BREADCRUMB,
					       &engine->irq_posted)) {
				engine->irq_seqno_barrier(engine);
				intel_engine_wakeup(engine);
			}

sleep:
			if (kthread_should_park())
				kthread_parkme();

			if (unlikely(kthread_should_stop()))
				break;

			schedule();
		}
	} while (1);
	__set_current_state(TASK_RUNNING);

	return 0;
}

static void insert_signal(struct intel_breadcrumbs *b,
			  struct i915_request *request,
			  const u32 seqno)
{
	struct i915_request *iter;

	lockdep_assert_held(&b->rb_lock);

	/*
	 * A reasonable assumption is that we are called to add signals
	 * in sequence, as the requests are submitted for execution and
	 * assigned a global_seqno. This will be the case for the majority
	 * of internally generated signals (inter-engine signaling).
	 *
	 * Out of order waiters triggering random signaling enabling will
	 * be more problematic, but hopefully rare enough and the list
	 * small enough that the O(N) insertion sort is not an issue.
	 */

	list_for_each_entry_reverse(iter, &b->signals, signaling.link)
		if (i915_seqno_passed(seqno, iter->signaling.wait.seqno))
			break;

	list_add(&request->signaling.link, &iter->signaling.link);
}

void intel_engine_enable_signaling(struct i915_request *request, bool wakeup)
{
	struct intel_engine_cs *engine = request->engine;
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	u32 seqno;

	/*
	 * Note that we may be called from an interrupt handler on another
	 * device (e.g. nouveau signaling a fence completion causing us
	 * to submit a request, and so enable signaling). As such,
	 * we need to make sure that all other users of b->rb_lock protect
	 * against interrupts, i.e. use spin_lock_irqsave.
	 */

	/* locked by dma_fence_enable_sw_signaling() (irqsafe fence->lock) */
	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&request->lock);

	seqno = i915_request_global_seqno(request);
	if (!seqno) /* will be enabled later upon execution */
		return;

	GEM_BUG_ON(request->signaling.wait.seqno);
	request->signaling.wait.tsk = b->signaler;
	request->signaling.wait.request = request;
	request->signaling.wait.seqno = seqno;

	/*
	 * Add ourselves into the list of waiters, but registering our
	 * bottom-half as the signaller thread. As per usual, only the oldest
	 * waiter (not just signaller) is tasked as the bottom-half waking
	 * up all completed waiters after the user interrupt.
	 *
	 * If we are the oldest waiter, enable the irq (after which we
	 * must double check that the seqno did not complete).
	 */
	spin_lock(&b->rb_lock);
	insert_signal(b, request, seqno);
	wakeup &= __intel_engine_add_wait(engine, &request->signaling.wait);
	spin_unlock(&b->rb_lock);

	if (wakeup)
		wake_up_process(b->signaler);
}

void intel_engine_cancel_signaling(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&request->lock);

	if (!READ_ONCE(request->signaling.wait.seqno))
		return;

	spin_lock(&b->rb_lock);
	__intel_engine_remove_wait(engine, &request->signaling.wait);
	if (fetch_and_zero(&request->signaling.wait.seqno))
		__list_del_entry(&request->signaling.link);
	spin_unlock(&b->rb_lock);
}

int intel_engine_init_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct task_struct *tsk;

	spin_lock_init(&b->rb_lock);
	spin_lock_init(&b->irq_lock);

	timer_setup(&b->fake_irq, intel_breadcrumbs_fake_irq, 0);
	timer_setup(&b->hangcheck, intel_breadcrumbs_hangcheck, 0);

	INIT_LIST_HEAD(&b->signals);

	/* Spawn a thread to provide a common bottom-half for all signals.
	 * As this is an asynchronous interface we cannot steal the current
	 * task for handling the bottom-half to the user interrupt, therefore
	 * we create a thread to do the coherent seqno dance after the
	 * interrupt and then signal the waitqueue (via the dma-buf/fence).
	 */
	tsk = kthread_run(intel_breadcrumbs_signaler, engine,
			  "i915/signal:%d", engine->id);
	if (IS_ERR(tsk))
		return PTR_ERR(tsk);

	b->signaler = tsk;

	return 0;
}

static void cancel_fake_irq(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	del_timer_sync(&b->hangcheck);
	del_timer_sync(&b->fake_irq);
	clear_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings);
}

void intel_engine_reset_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	cancel_fake_irq(engine);
	spin_lock_irq(&b->irq_lock);

	if (b->irq_enabled)
		irq_enable(engine);
	else
		irq_disable(engine);

	/* We set the IRQ_BREADCRUMB bit when we enable the irq presuming the
	 * GPU is active and may have already executed the MI_USER_INTERRUPT
	 * before the CPU is ready to receive. However, the engine is currently
	 * idle (we haven't started it yet), there is no possibility for a
	 * missed interrupt as we enabled the irq and so we can clear the
	 * immediate wakeup (until a real interrupt arrives for the waiter).
	 */
	clear_bit(ENGINE_IRQ_BREADCRUMB, &engine->irq_posted);

	if (b->irq_armed)
		enable_fake_irq(b);

	spin_unlock_irq(&b->irq_lock);
}

void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	/* The engines should be idle and all requests accounted for! */
	WARN_ON(READ_ONCE(b->irq_wait));
	WARN_ON(!RB_EMPTY_ROOT(&b->waiters));
	WARN_ON(!list_empty(&b->signals));

	if (!IS_ERR_OR_NULL(b->signaler))
		kthread_stop(b->signaler);

	cancel_fake_irq(engine);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/intel_breadcrumbs.c"
#endif
