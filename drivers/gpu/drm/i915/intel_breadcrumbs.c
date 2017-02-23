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

#include "i915_drv.h"

static unsigned long wait_timeout(void)
{
	return round_jiffies_up(jiffies + DRM_I915_HANGCHECK_JIFFIES);
}

static void intel_breadcrumbs_hangcheck(unsigned long data)
{
	struct intel_engine_cs *engine = (struct intel_engine_cs *)data;
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	if (!b->irq_enabled)
		return;

	if (b->hangcheck_interrupts != atomic_read(&engine->irq_count)) {
		b->hangcheck_interrupts = atomic_read(&engine->irq_count);
		mod_timer(&b->hangcheck, wait_timeout());
		return;
	}

	/* If the waiter was currently running, assume it hasn't had a chance
	 * to process the pending interrupt (e.g, low priority task on a loaded
	 * system) and wait until it sleeps before declaring a missed interrupt.
	 */
	if (!intel_engine_wakeup(engine)) {
		mod_timer(&b->hangcheck, wait_timeout());
		return;
	}

	DRM_DEBUG("Hangcheck timer elapsed... %s idle\n", engine->name);
	set_bit(engine->id, &engine->i915->gpu_error.missed_irq_rings);
	mod_timer(&engine->breadcrumbs.fake_irq, jiffies + 1);

	/* Ensure that even if the GPU hangs, we get woken up.
	 *
	 * However, note that if no one is waiting, we never notice
	 * a gpu hang. Eventually, we will have to wait for a resource
	 * held by the GPU and so trigger a hangcheck. In the most
	 * pathological case, this will be upon memory starvation! To
	 * prevent this, we also queue the hangcheck from the retire
	 * worker.
	 */
	i915_queue_hangcheck(engine->i915);
}

static void intel_breadcrumbs_fake_irq(unsigned long data)
{
	struct intel_engine_cs *engine = (struct intel_engine_cs *)data;

	/*
	 * The timer persists in case we cannot enable interrupts,
	 * or if we have previously seen seqno/interrupt incoherency
	 * ("missed interrupt" syndrome). Here the worker will wake up
	 * every jiffie in order to kick the oldest waiter to do the
	 * coherent seqno check.
	 */
	if (intel_engine_wakeup(engine))
		mod_timer(&engine->breadcrumbs.fake_irq, jiffies + 1);
}

static void irq_enable(struct intel_engine_cs *engine)
{
	/* Enabling the IRQ may miss the generation of the interrupt, but
	 * we still need to force the barrier before reading the seqno,
	 * just in case.
	 */
	set_bit(ENGINE_IRQ_BREADCRUMB, &engine->irq_posted);

	/* Caller disables interrupts */
	spin_lock(&engine->i915->irq_lock);
	engine->irq_enable(engine);
	spin_unlock(&engine->i915->irq_lock);
}

static void irq_disable(struct intel_engine_cs *engine)
{
	/* Caller disables interrupts */
	spin_lock(&engine->i915->irq_lock);
	engine->irq_disable(engine);
	spin_unlock(&engine->i915->irq_lock);
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

static void __intel_breadcrumbs_enable_irq(struct intel_breadcrumbs *b)
{
	struct intel_engine_cs *engine =
		container_of(b, struct intel_engine_cs, breadcrumbs);
	struct drm_i915_private *i915 = engine->i915;

	assert_spin_locked(&b->lock);
	if (b->rpm_wakelock)
		return;

	if (I915_SELFTEST_ONLY(b->mock)) {
		/* For our mock objects we want to avoid interaction
		 * with the real hardware (which is not set up). So
		 * we simply pretend we have enabled the powerwell
		 * and the irq, and leave it up to the mock
		 * implementation to call intel_engine_wakeup()
		 * itself when it wants to simulate a user interrupt,
		 */
		b->rpm_wakelock = true;
		return;
	}

	/* Since we are waiting on a request, the GPU should be busy
	 * and should have its own rpm reference. For completeness,
	 * record an rpm reference for ourselves to cover the
	 * interrupt we unmask.
	 */
	intel_runtime_pm_get_noresume(i915);
	b->rpm_wakelock = true;

	/* No interrupts? Kick the waiter every jiffie! */
	if (intel_irqs_enabled(i915)) {
		if (!test_bit(engine->id, &i915->gpu_error.test_irq_rings))
			irq_enable(engine);
		b->irq_enabled = true;
	}

	if (!b->irq_enabled || use_fake_irq(b)) {
		mod_timer(&b->fake_irq, jiffies + 1);
		i915_queue_hangcheck(i915);
	} else {
		/* Ensure we never sleep indefinitely */
		mod_timer(&b->hangcheck, wait_timeout());
	}
}

static void __intel_breadcrumbs_disable_irq(struct intel_breadcrumbs *b)
{
	struct intel_engine_cs *engine =
		container_of(b, struct intel_engine_cs, breadcrumbs);

	assert_spin_locked(&b->lock);
	if (!b->rpm_wakelock)
		return;

	if (I915_SELFTEST_ONLY(b->mock)) {
		b->rpm_wakelock = false;
		return;
	}

	if (b->irq_enabled) {
		irq_disable(engine);
		b->irq_enabled = false;
	}

	intel_runtime_pm_put(engine->i915);
	b->rpm_wakelock = false;
}

static inline struct intel_wait *to_wait(struct rb_node *node)
{
	return rb_entry(node, struct intel_wait, node);
}

static inline void __intel_breadcrumbs_finish(struct intel_breadcrumbs *b,
					      struct intel_wait *wait)
{
	assert_spin_locked(&b->lock);

	/* This request is completed, so remove it from the tree, mark it as
	 * complete, and *then* wake up the associated task.
	 */
	rb_erase(&wait->node, &b->waiters);
	RB_CLEAR_NODE(&wait->node);

	wake_up_process(wait->tsk); /* implicit smp_wmb() */
}

static bool __intel_engine_add_wait(struct intel_engine_cs *engine,
				    struct intel_wait *wait)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct rb_node **p, *parent, *completed;
	bool first;
	u32 seqno;

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
	GEM_BUG_ON(!first && !rcu_access_pointer(b->irq_seqno_bh));

	if (completed) {
		struct rb_node *next = rb_next(completed);

		GEM_BUG_ON(!next && !first);
		if (next && next != &wait->node) {
			GEM_BUG_ON(first);
			b->first_wait = to_wait(next);
			rcu_assign_pointer(b->irq_seqno_bh, b->first_wait->tsk);
			/* As there is a delay between reading the current
			 * seqno, processing the completed tasks and selecting
			 * the next waiter, we may have missed the interrupt
			 * and so need for the next bottom-half to wakeup.
			 *
			 * Also as we enable the IRQ, we may miss the
			 * interrupt for that seqno, so we have to wake up
			 * the next bottom-half in order to do a coherent check
			 * in case the seqno passed.
			 */
			__intel_breadcrumbs_enable_irq(b);
			if (test_bit(ENGINE_IRQ_BREADCRUMB,
				     &engine->irq_posted))
				wake_up_process(to_wait(next)->tsk);
		}

		do {
			struct intel_wait *crumb = to_wait(completed);
			completed = rb_prev(completed);
			__intel_breadcrumbs_finish(b, crumb);
		} while (completed);
	}

	if (first) {
		GEM_BUG_ON(rb_first(&b->waiters) != &wait->node);
		b->first_wait = wait;
		rcu_assign_pointer(b->irq_seqno_bh, wait->tsk);
		/* After assigning ourselves as the new bottom-half, we must
		 * perform a cursory check to prevent a missed interrupt.
		 * Either we miss the interrupt whilst programming the hardware,
		 * or if there was a previous waiter (for a later seqno) they
		 * may be woken instead of us (due to the inherent race
		 * in the unlocked read of b->irq_seqno_bh in the irq handler)
		 * and so we miss the wake up.
		 */
		__intel_breadcrumbs_enable_irq(b);
	}
	GEM_BUG_ON(!rcu_access_pointer(b->irq_seqno_bh));
	GEM_BUG_ON(!b->first_wait);
	GEM_BUG_ON(rb_first(&b->waiters) != &b->first_wait->node);

	return first;
}

bool intel_engine_add_wait(struct intel_engine_cs *engine,
			   struct intel_wait *wait)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	bool first;

	spin_lock_irq(&b->lock);
	first = __intel_engine_add_wait(engine, wait);
	spin_unlock_irq(&b->lock);

	return first;
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

	assert_spin_locked(&b->lock);

	if (RB_EMPTY_NODE(&wait->node))
		goto out;

	if (b->first_wait == wait) {
		const int priority = wakeup_priority(b, wait->tsk);
		struct rb_node *next;

		GEM_BUG_ON(rcu_access_pointer(b->irq_seqno_bh) != wait->tsk);

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

		if (next) {
			/* In our haste, we may have completed the first waiter
			 * before we enabled the interrupt. Do so now as we
			 * have a second waiter for a future seqno. Afterwards,
			 * we have to wake up that waiter in case we missed
			 * the interrupt, or if we have to handle an
			 * exception rather than a seqno completion.
			 */
			b->first_wait = to_wait(next);
			rcu_assign_pointer(b->irq_seqno_bh, b->first_wait->tsk);
			if (b->first_wait->seqno != wait->seqno)
				__intel_breadcrumbs_enable_irq(b);
			wake_up_process(b->first_wait->tsk);
		} else {
			b->first_wait = NULL;
			rcu_assign_pointer(b->irq_seqno_bh, NULL);
			__intel_breadcrumbs_disable_irq(b);
		}
	} else {
		GEM_BUG_ON(rb_first(&b->waiters) == &wait->node);
	}

	GEM_BUG_ON(RB_EMPTY_NODE(&wait->node));
	rb_erase(&wait->node, &b->waiters);

out:
	GEM_BUG_ON(b->first_wait == wait);
	GEM_BUG_ON(rb_first(&b->waiters) !=
		   (b->first_wait ? &b->first_wait->node : NULL));
	GEM_BUG_ON(!rcu_access_pointer(b->irq_seqno_bh) ^ RB_EMPTY_ROOT(&b->waiters));
}

void intel_engine_remove_wait(struct intel_engine_cs *engine,
			      struct intel_wait *wait)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	/* Quick check to see if this waiter was already decoupled from
	 * the tree by the bottom-half to avoid contention on the spinlock
	 * by the herd.
	 */
	if (RB_EMPTY_NODE(&wait->node))
		return;

	spin_lock_irq(&b->lock);
	__intel_engine_remove_wait(engine, wait);
	spin_unlock_irq(&b->lock);
}

static bool signal_complete(struct drm_i915_gem_request *request)
{
	if (!request)
		return false;

	/* If another process served as the bottom-half it may have already
	 * signalled that this wait is already completed.
	 */
	if (intel_wait_complete(&request->signaling.wait))
		return true;

	/* Carefully check if the request is complete, giving time for the
	 * seqno to be visible or if the GPU hung.
	 */
	if (__i915_request_irq_complete(request))
		return true;

	return false;
}

static struct drm_i915_gem_request *to_signaler(struct rb_node *rb)
{
	return rb_entry(rb, struct drm_i915_gem_request, signaling.node);
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
	struct drm_i915_gem_request *request;

	/* Install ourselves with high priority to reduce signalling latency */
	signaler_set_rtpriority();

	do {
		set_current_state(TASK_INTERRUPTIBLE);

		/* We are either woken up by the interrupt bottom-half,
		 * or by a client adding a new signaller. In both cases,
		 * the GPU seqno may have advanced beyond our oldest signal.
		 * If it has, propagate the signal, remove the waiter and
		 * check again with the next oldest signal. Otherwise we
		 * need to wait for a new interrupt from the GPU or for
		 * a new client.
		 */
		rcu_read_lock();
		request = rcu_dereference(b->first_signal);
		if (request)
			request = i915_gem_request_get_rcu(request);
		rcu_read_unlock();
		if (signal_complete(request)) {
			local_bh_disable();
			dma_fence_signal(&request->fence);
			local_bh_enable(); /* kick start the tasklets */

			spin_lock_irq(&b->lock);

			/* Wake up all other completed waiters and select the
			 * next bottom-half for the next user interrupt.
			 */
			__intel_engine_remove_wait(engine,
						   &request->signaling.wait);

			/* Find the next oldest signal. Note that as we have
			 * not been holding the lock, another client may
			 * have installed an even older signal than the one
			 * we just completed - so double check we are still
			 * the oldest before picking the next one.
			 */
			if (request == rcu_access_pointer(b->first_signal)) {
				struct rb_node *rb =
					rb_next(&request->signaling.node);
				rcu_assign_pointer(b->first_signal,
						   rb ? to_signaler(rb) : NULL);
			}
			rb_erase(&request->signaling.node, &b->signals);
			RB_CLEAR_NODE(&request->signaling.node);

			spin_unlock_irq(&b->lock);

			i915_gem_request_put(request);
		} else {
			if (kthread_should_stop()) {
				GEM_BUG_ON(request);
				break;
			}

			schedule();

			if (kthread_should_park())
				kthread_parkme();
		}
		i915_gem_request_put(request);
	} while (1);
	__set_current_state(TASK_RUNNING);

	return 0;
}

void intel_engine_enable_signaling(struct drm_i915_gem_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct rb_node *parent, **p;
	bool first, wakeup;
	u32 seqno;

	/* Note that we may be called from an interrupt handler on another
	 * device (e.g. nouveau signaling a fence completion causing us
	 * to submit a request, and so enable signaling). As such,
	 * we need to make sure that all other users of b->lock protect
	 * against interrupts, i.e. use spin_lock_irqsave.
	 */

	/* locked by dma_fence_enable_sw_signaling() (irqsafe fence->lock) */
	assert_spin_locked(&request->lock);

	seqno = i915_gem_request_global_seqno(request);
	if (!seqno)
		return;

	request->signaling.wait.tsk = b->signaler;
	request->signaling.wait.seqno = seqno;
	i915_gem_request_get(request);

	spin_lock(&b->lock);

	/* First add ourselves into the list of waiters, but register our
	 * bottom-half as the signaller thread. As per usual, only the oldest
	 * waiter (not just signaller) is tasked as the bottom-half waking
	 * up all completed waiters after the user interrupt.
	 *
	 * If we are the oldest waiter, enable the irq (after which we
	 * must double check that the seqno did not complete).
	 */
	wakeup = __intel_engine_add_wait(engine, &request->signaling.wait);

	/* Now insert ourselves into the retirement ordered list of signals
	 * on this engine. We track the oldest seqno as that will be the
	 * first signal to complete.
	 */
	parent = NULL;
	first = true;
	p = &b->signals.rb_node;
	while (*p) {
		parent = *p;
		if (i915_seqno_passed(seqno,
				      to_signaler(parent)->signaling.wait.seqno)) {
			p = &parent->rb_right;
			first = false;
		} else {
			p = &parent->rb_left;
		}
	}
	rb_link_node(&request->signaling.node, parent, p);
	rb_insert_color(&request->signaling.node, &b->signals);
	if (first)
		rcu_assign_pointer(b->first_signal, request);

	spin_unlock(&b->lock);

	if (wakeup)
		wake_up_process(b->signaler);
}

void intel_engine_cancel_signaling(struct drm_i915_gem_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	assert_spin_locked(&request->lock);
	GEM_BUG_ON(!request->signaling.wait.seqno);

	spin_lock(&b->lock);

	if (!RB_EMPTY_NODE(&request->signaling.node)) {
		if (request == rcu_access_pointer(b->first_signal)) {
			struct rb_node *rb =
				rb_next(&request->signaling.node);
			rcu_assign_pointer(b->first_signal,
					   rb ? to_signaler(rb) : NULL);
		}
		rb_erase(&request->signaling.node, &b->signals);
		RB_CLEAR_NODE(&request->signaling.node);
		i915_gem_request_put(request);
	}

	__intel_engine_remove_wait(engine, &request->signaling.wait);

	spin_unlock(&b->lock);

	request->signaling.wait.seqno = 0;
}

int intel_engine_init_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	struct task_struct *tsk;

	spin_lock_init(&b->lock);
	setup_timer(&b->fake_irq,
		    intel_breadcrumbs_fake_irq,
		    (unsigned long)engine);
	setup_timer(&b->hangcheck,
		    intel_breadcrumbs_hangcheck,
		    (unsigned long)engine);

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
	spin_lock_irq(&b->lock);

	__intel_breadcrumbs_disable_irq(b);
	if (intel_engine_has_waiter(engine)) {
		__intel_breadcrumbs_enable_irq(b);
		if (test_bit(ENGINE_IRQ_BREADCRUMB, &engine->irq_posted))
			wake_up_process(b->first_wait->tsk);
	} else {
		/* sanitize the IMR and unmask any auxiliary interrupts */
		irq_disable(engine);
	}

	spin_unlock_irq(&b->lock);
}

void intel_engine_fini_breadcrumbs(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;

	/* The engines should be idle and all requests accounted for! */
	WARN_ON(READ_ONCE(b->first_wait));
	WARN_ON(!RB_EMPTY_ROOT(&b->waiters));
	WARN_ON(rcu_access_pointer(b->first_signal));
	WARN_ON(!RB_EMPTY_ROOT(&b->signals));

	if (!IS_ERR_OR_NULL(b->signaler))
		kthread_stop(b->signaler);

	cancel_fake_irq(engine);
}

bool intel_breadcrumbs_busy(struct intel_engine_cs *engine)
{
	struct intel_breadcrumbs *b = &engine->breadcrumbs;
	bool busy = false;

	spin_lock_irq(&b->lock);

	if (b->first_wait) {
		wake_up_process(b->first_wait->tsk);
		busy |= intel_engine_flag(engine);
	}

	if (rcu_access_pointer(b->first_signal)) {
		wake_up_process(b->signaler);
		busy |= intel_engine_flag(engine);
	}

	spin_unlock_irq(&b->lock);

	return busy;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/intel_breadcrumbs.c"
#endif
