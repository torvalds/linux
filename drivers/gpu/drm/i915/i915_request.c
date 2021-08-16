/*
 * Copyright © 2008-2015 Intel Corporation
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

#include <linux/dma-fence-array.h>
#include <linux/dma-fence-chain.h>
#include <linux/irq_work.h>
#include <linux/prefetch.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>

#include "gem/i915_gem_context.h"
#include "gt/intel_breadcrumbs.h"
#include "gt/intel_context.h"
#include "gt/intel_engine.h"
#include "gt/intel_engine_heartbeat.h"
#include "gt/intel_gpu_commands.h"
#include "gt/intel_reset.h"
#include "gt/intel_ring.h"
#include "gt/intel_rps.h"

#include "i915_active.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_pm.h"

struct execute_cb {
	struct irq_work work;
	struct i915_sw_fence *fence;
	struct i915_request *signal;
};

static struct kmem_cache *slab_requests;
static struct kmem_cache *slab_execute_cbs;

static const char *i915_fence_get_driver_name(struct dma_fence *fence)
{
	return dev_name(to_request(fence)->engine->i915->drm.dev);
}

static const char *i915_fence_get_timeline_name(struct dma_fence *fence)
{
	const struct i915_gem_context *ctx;

	/*
	 * The timeline struct (as part of the ppgtt underneath a context)
	 * may be freed when the request is no longer in use by the GPU.
	 * We could extend the life of a context to beyond that of all
	 * fences, possibly keeping the hw resource around indefinitely,
	 * or we just give them a false name. Since
	 * dma_fence_ops.get_timeline_name is a debug feature, the occasional
	 * lie seems justifiable.
	 */
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
		return "signaled";

	ctx = i915_request_gem_context(to_request(fence));
	if (!ctx)
		return "[" DRIVER_NAME "]";

	return ctx->name;
}

static bool i915_fence_signaled(struct dma_fence *fence)
{
	return i915_request_completed(to_request(fence));
}

static bool i915_fence_enable_signaling(struct dma_fence *fence)
{
	return i915_request_enable_breadcrumb(to_request(fence));
}

static signed long i915_fence_wait(struct dma_fence *fence,
				   bool interruptible,
				   signed long timeout)
{
	return i915_request_wait(to_request(fence),
				 interruptible | I915_WAIT_PRIORITY,
				 timeout);
}

struct kmem_cache *i915_request_slab_cache(void)
{
	return slab_requests;
}

static void i915_fence_release(struct dma_fence *fence)
{
	struct i915_request *rq = to_request(fence);

	GEM_BUG_ON(rq->guc_prio != GUC_PRIO_INIT &&
		   rq->guc_prio != GUC_PRIO_FINI);

	/*
	 * The request is put onto a RCU freelist (i.e. the address
	 * is immediately reused), mark the fences as being freed now.
	 * Otherwise the debugobjects for the fences are only marked as
	 * freed when the slab cache itself is freed, and so we would get
	 * caught trying to reuse dead objects.
	 */
	i915_sw_fence_fini(&rq->submit);
	i915_sw_fence_fini(&rq->semaphore);

	/*
	 * Keep one request on each engine for reserved use under mempressure,
	 * do not use with virtual engines as this really is only needed for
	 * kernel contexts.
	 */
	if (!intel_engine_is_virtual(rq->engine) &&
	    !cmpxchg(&rq->engine->request_pool, NULL, rq)) {
		intel_context_put(rq->context);
		return;
	}

	intel_context_put(rq->context);

	kmem_cache_free(slab_requests, rq);
}

const struct dma_fence_ops i915_fence_ops = {
	.get_driver_name = i915_fence_get_driver_name,
	.get_timeline_name = i915_fence_get_timeline_name,
	.enable_signaling = i915_fence_enable_signaling,
	.signaled = i915_fence_signaled,
	.wait = i915_fence_wait,
	.release = i915_fence_release,
};

static void irq_execute_cb(struct irq_work *wrk)
{
	struct execute_cb *cb = container_of(wrk, typeof(*cb), work);

	i915_sw_fence_complete(cb->fence);
	kmem_cache_free(slab_execute_cbs, cb);
}

static __always_inline void
__notify_execute_cb(struct i915_request *rq, bool (*fn)(struct irq_work *wrk))
{
	struct execute_cb *cb, *cn;

	if (llist_empty(&rq->execute_cb))
		return;

	llist_for_each_entry_safe(cb, cn,
				  llist_del_all(&rq->execute_cb),
				  work.node.llist)
		fn(&cb->work);
}

static void __notify_execute_cb_irq(struct i915_request *rq)
{
	__notify_execute_cb(rq, irq_work_queue);
}

static bool irq_work_imm(struct irq_work *wrk)
{
	wrk->func(wrk);
	return false;
}

void i915_request_notify_execute_cb_imm(struct i915_request *rq)
{
	__notify_execute_cb(rq, irq_work_imm);
}

static void free_capture_list(struct i915_request *request)
{
	struct i915_capture_list *capture;

	capture = fetch_and_zero(&request->capture_list);
	while (capture) {
		struct i915_capture_list *next = capture->next;

		kfree(capture);
		capture = next;
	}
}

static void __i915_request_fill(struct i915_request *rq, u8 val)
{
	void *vaddr = rq->ring->vaddr;
	u32 head;

	head = rq->infix;
	if (rq->postfix < head) {
		memset(vaddr + head, val, rq->ring->size - head);
		head = 0;
	}
	memset(vaddr + head, val, rq->postfix - head);
}

/**
 * i915_request_active_engine
 * @rq: request to inspect
 * @active: pointer in which to return the active engine
 *
 * Fills the currently active engine to the @active pointer if the request
 * is active and still not completed.
 *
 * Returns true if request was active or false otherwise.
 */
bool
i915_request_active_engine(struct i915_request *rq,
			   struct intel_engine_cs **active)
{
	struct intel_engine_cs *engine, *locked;
	bool ret = false;

	/*
	 * Serialise with __i915_request_submit() so that it sees
	 * is-banned?, or we know the request is already inflight.
	 *
	 * Note that rq->engine is unstable, and so we double
	 * check that we have acquired the lock on the final engine.
	 */
	locked = READ_ONCE(rq->engine);
	spin_lock_irq(&locked->sched_engine->lock);
	while (unlikely(locked != (engine = READ_ONCE(rq->engine)))) {
		spin_unlock(&locked->sched_engine->lock);
		locked = engine;
		spin_lock(&locked->sched_engine->lock);
	}

	if (i915_request_is_active(rq)) {
		if (!__i915_request_is_complete(rq))
			*active = locked;
		ret = true;
	}

	spin_unlock_irq(&locked->sched_engine->lock);

	return ret;
}

static void __rq_init_watchdog(struct i915_request *rq)
{
	rq->watchdog.timer.function = NULL;
}

static enum hrtimer_restart __rq_watchdog_expired(struct hrtimer *hrtimer)
{
	struct i915_request *rq =
		container_of(hrtimer, struct i915_request, watchdog.timer);
	struct intel_gt *gt = rq->engine->gt;

	if (!i915_request_completed(rq)) {
		if (llist_add(&rq->watchdog.link, &gt->watchdog.list))
			schedule_work(&gt->watchdog.work);
	} else {
		i915_request_put(rq);
	}

	return HRTIMER_NORESTART;
}

static void __rq_arm_watchdog(struct i915_request *rq)
{
	struct i915_request_watchdog *wdg = &rq->watchdog;
	struct intel_context *ce = rq->context;

	if (!ce->watchdog.timeout_us)
		return;

	i915_request_get(rq);

	hrtimer_init(&wdg->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	wdg->timer.function = __rq_watchdog_expired;
	hrtimer_start_range_ns(&wdg->timer,
			       ns_to_ktime(ce->watchdog.timeout_us *
					   NSEC_PER_USEC),
			       NSEC_PER_MSEC,
			       HRTIMER_MODE_REL);
}

static void __rq_cancel_watchdog(struct i915_request *rq)
{
	struct i915_request_watchdog *wdg = &rq->watchdog;

	if (wdg->timer.function && hrtimer_try_to_cancel(&wdg->timer) > 0)
		i915_request_put(rq);
}

bool i915_request_retire(struct i915_request *rq)
{
	if (!__i915_request_is_complete(rq))
		return false;

	RQ_TRACE(rq, "\n");

	GEM_BUG_ON(!i915_sw_fence_signaled(&rq->submit));
	trace_i915_request_retire(rq);
	i915_request_mark_complete(rq);

	__rq_cancel_watchdog(rq);

	/*
	 * We know the GPU must have read the request to have
	 * sent us the seqno + interrupt, so use the position
	 * of tail of the request to update the last known position
	 * of the GPU head.
	 *
	 * Note this requires that we are always called in request
	 * completion order.
	 */
	GEM_BUG_ON(!list_is_first(&rq->link,
				  &i915_request_timeline(rq)->requests));
	if (IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM))
		/* Poison before we release our space in the ring */
		__i915_request_fill(rq, POISON_FREE);
	rq->ring->head = rq->postfix;

	if (!i915_request_signaled(rq)) {
		spin_lock_irq(&rq->lock);
		dma_fence_signal_locked(&rq->fence);
		spin_unlock_irq(&rq->lock);
	}

	if (test_and_set_bit(I915_FENCE_FLAG_BOOST, &rq->fence.flags))
		atomic_dec(&rq->engine->gt->rps.num_waiters);

	/*
	 * We only loosely track inflight requests across preemption,
	 * and so we may find ourselves attempting to retire a _completed_
	 * request that we have removed from the HW and put back on a run
	 * queue.
	 *
	 * As we set I915_FENCE_FLAG_ACTIVE on the request, this should be
	 * after removing the breadcrumb and signaling it, so that we do not
	 * inadvertently attach the breadcrumb to a completed request.
	 */
	rq->engine->remove_active_request(rq);
	GEM_BUG_ON(!llist_empty(&rq->execute_cb));

	__list_del_entry(&rq->link); /* poison neither prev/next (RCU walks) */

	intel_context_exit(rq->context);
	intel_context_unpin(rq->context);

	free_capture_list(rq);
	i915_sched_node_fini(&rq->sched);
	i915_request_put(rq);

	return true;
}

void i915_request_retire_upto(struct i915_request *rq)
{
	struct intel_timeline * const tl = i915_request_timeline(rq);
	struct i915_request *tmp;

	RQ_TRACE(rq, "\n");
	GEM_BUG_ON(!__i915_request_is_complete(rq));

	do {
		tmp = list_first_entry(&tl->requests, typeof(*tmp), link);
		GEM_BUG_ON(!i915_request_completed(tmp));
	} while (i915_request_retire(tmp) && tmp != rq);
}

static struct i915_request * const *
__engine_active(struct intel_engine_cs *engine)
{
	return READ_ONCE(engine->execlists.active);
}

static bool __request_in_flight(const struct i915_request *signal)
{
	struct i915_request * const *port, *rq;
	bool inflight = false;

	if (!i915_request_is_ready(signal))
		return false;

	/*
	 * Even if we have unwound the request, it may still be on
	 * the GPU (preempt-to-busy). If that request is inside an
	 * unpreemptible critical section, it will not be removed. Some
	 * GPU functions may even be stuck waiting for the paired request
	 * (__await_execution) to be submitted and cannot be preempted
	 * until the bond is executing.
	 *
	 * As we know that there are always preemption points between
	 * requests, we know that only the currently executing request
	 * may be still active even though we have cleared the flag.
	 * However, we can't rely on our tracking of ELSP[0] to know
	 * which request is currently active and so maybe stuck, as
	 * the tracking maybe an event behind. Instead assume that
	 * if the context is still inflight, then it is still active
	 * even if the active flag has been cleared.
	 *
	 * To further complicate matters, if there a pending promotion, the HW
	 * may either perform a context switch to the second inflight execlists,
	 * or it may switch to the pending set of execlists. In the case of the
	 * latter, it may send the ACK and we process the event copying the
	 * pending[] over top of inflight[], _overwriting_ our *active. Since
	 * this implies the HW is arbitrating and not struck in *active, we do
	 * not worry about complete accuracy, but we do require no read/write
	 * tearing of the pointer [the read of the pointer must be valid, even
	 * as the array is being overwritten, for which we require the writes
	 * to avoid tearing.]
	 *
	 * Note that the read of *execlists->active may race with the promotion
	 * of execlists->pending[] to execlists->inflight[], overwritting
	 * the value at *execlists->active. This is fine. The promotion implies
	 * that we received an ACK from the HW, and so the context is not
	 * stuck -- if we do not see ourselves in *active, the inflight status
	 * is valid. If instead we see ourselves being copied into *active,
	 * we are inflight and may signal the callback.
	 */
	if (!intel_context_inflight(signal->context))
		return false;

	rcu_read_lock();
	for (port = __engine_active(signal->engine);
	     (rq = READ_ONCE(*port)); /* may race with promotion of pending[] */
	     port++) {
		if (rq->context == signal->context) {
			inflight = i915_seqno_passed(rq->fence.seqno,
						     signal->fence.seqno);
			break;
		}
	}
	rcu_read_unlock();

	return inflight;
}

static int
__await_execution(struct i915_request *rq,
		  struct i915_request *signal,
		  gfp_t gfp)
{
	struct execute_cb *cb;

	if (i915_request_is_active(signal))
		return 0;

	cb = kmem_cache_alloc(slab_execute_cbs, gfp);
	if (!cb)
		return -ENOMEM;

	cb->fence = &rq->submit;
	i915_sw_fence_await(cb->fence);
	init_irq_work(&cb->work, irq_execute_cb);

	/*
	 * Register the callback first, then see if the signaler is already
	 * active. This ensures that if we race with the
	 * __notify_execute_cb from i915_request_submit() and we are not
	 * included in that list, we get a second bite of the cherry and
	 * execute it ourselves. After this point, a future
	 * i915_request_submit() will notify us.
	 *
	 * In i915_request_retire() we set the ACTIVE bit on a completed
	 * request (then flush the execute_cb). So by registering the
	 * callback first, then checking the ACTIVE bit, we serialise with
	 * the completed/retired request.
	 */
	if (llist_add(&cb->work.node.llist, &signal->execute_cb)) {
		if (i915_request_is_active(signal) ||
		    __request_in_flight(signal))
			i915_request_notify_execute_cb_imm(signal);
	}

	return 0;
}

static bool fatal_error(int error)
{
	switch (error) {
	case 0: /* not an error! */
	case -EAGAIN: /* innocent victim of a GT reset (__i915_request_reset) */
	case -ETIMEDOUT: /* waiting for Godot (timer_i915_sw_fence_wake) */
		return false;
	default:
		return true;
	}
}

void __i915_request_skip(struct i915_request *rq)
{
	GEM_BUG_ON(!fatal_error(rq->fence.error));

	if (rq->infix == rq->postfix)
		return;

	RQ_TRACE(rq, "error: %d\n", rq->fence.error);

	/*
	 * As this request likely depends on state from the lost
	 * context, clear out all the user operations leaving the
	 * breadcrumb at the end (so we get the fence notifications).
	 */
	__i915_request_fill(rq, 0);
	rq->infix = rq->postfix;
}

bool i915_request_set_error_once(struct i915_request *rq, int error)
{
	int old;

	GEM_BUG_ON(!IS_ERR_VALUE((long)error));

	if (i915_request_signaled(rq))
		return false;

	old = READ_ONCE(rq->fence.error);
	do {
		if (fatal_error(old))
			return false;
	} while (!try_cmpxchg(&rq->fence.error, &old, error));

	return true;
}

struct i915_request *i915_request_mark_eio(struct i915_request *rq)
{
	if (__i915_request_is_complete(rq))
		return NULL;

	GEM_BUG_ON(i915_request_signaled(rq));

	/* As soon as the request is completed, it may be retired */
	rq = i915_request_get(rq);

	i915_request_set_error_once(rq, -EIO);
	i915_request_mark_complete(rq);

	return rq;
}

bool __i915_request_submit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	bool result = false;

	RQ_TRACE(request, "\n");

	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&engine->sched_engine->lock);

	/*
	 * With the advent of preempt-to-busy, we frequently encounter
	 * requests that we have unsubmitted from HW, but left running
	 * until the next ack and so have completed in the meantime. On
	 * resubmission of that completed request, we can skip
	 * updating the payload, and execlists can even skip submitting
	 * the request.
	 *
	 * We must remove the request from the caller's priority queue,
	 * and the caller must only call us when the request is in their
	 * priority queue, under the sched_engine->lock. This ensures that the
	 * request has *not* yet been retired and we can safely move
	 * the request into the engine->active.list where it will be
	 * dropped upon retiring. (Otherwise if resubmit a *retired*
	 * request, this would be a horrible use-after-free.)
	 */
	if (__i915_request_is_complete(request)) {
		list_del_init(&request->sched.link);
		goto active;
	}

	if (unlikely(intel_context_is_banned(request->context)))
		i915_request_set_error_once(request, -EIO);

	if (unlikely(fatal_error(request->fence.error)))
		__i915_request_skip(request);

	/*
	 * Are we using semaphores when the gpu is already saturated?
	 *
	 * Using semaphores incurs a cost in having the GPU poll a
	 * memory location, busywaiting for it to change. The continual
	 * memory reads can have a noticeable impact on the rest of the
	 * system with the extra bus traffic, stalling the cpu as it too
	 * tries to access memory across the bus (perf stat -e bus-cycles).
	 *
	 * If we installed a semaphore on this request and we only submit
	 * the request after the signaler completed, that indicates the
	 * system is overloaded and using semaphores at this time only
	 * increases the amount of work we are doing. If so, we disable
	 * further use of semaphores until we are idle again, whence we
	 * optimistically try again.
	 */
	if (request->sched.semaphores &&
	    i915_sw_fence_signaled(&request->semaphore))
		engine->saturated |= request->sched.semaphores;

	engine->emit_fini_breadcrumb(request,
				     request->ring->vaddr + request->postfix);

	trace_i915_request_execute(request);
	if (engine->bump_serial)
		engine->bump_serial(engine);
	else
		engine->serial++;

	result = true;

	GEM_BUG_ON(test_bit(I915_FENCE_FLAG_ACTIVE, &request->fence.flags));
	engine->add_active_request(request);
active:
	clear_bit(I915_FENCE_FLAG_PQUEUE, &request->fence.flags);
	set_bit(I915_FENCE_FLAG_ACTIVE, &request->fence.flags);

	/*
	 * XXX Rollback bonded-execution on __i915_request_unsubmit()?
	 *
	 * In the future, perhaps when we have an active time-slicing scheduler,
	 * it will be interesting to unsubmit parallel execution and remove
	 * busywaits from the GPU until their master is restarted. This is
	 * quite hairy, we have to carefully rollback the fence and do a
	 * preempt-to-idle cycle on the target engine, all the while the
	 * master execute_cb may refire.
	 */
	__notify_execute_cb_irq(request);

	/* We may be recursing from the signal callback of another i915 fence */
	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &request->fence.flags))
		i915_request_enable_breadcrumb(request);

	return result;
}

void i915_request_submit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&engine->sched_engine->lock, flags);

	__i915_request_submit(request);

	spin_unlock_irqrestore(&engine->sched_engine->lock, flags);
}

void __i915_request_unsubmit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;

	/*
	 * Only unwind in reverse order, required so that the per-context list
	 * is kept in seqno/ring order.
	 */
	RQ_TRACE(request, "\n");

	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&engine->sched_engine->lock);

	/*
	 * Before we remove this breadcrumb from the signal list, we have
	 * to ensure that a concurrent dma_fence_enable_signaling() does not
	 * attach itself. We first mark the request as no longer active and
	 * make sure that is visible to other cores, and then remove the
	 * breadcrumb if attached.
	 */
	GEM_BUG_ON(!test_bit(I915_FENCE_FLAG_ACTIVE, &request->fence.flags));
	clear_bit_unlock(I915_FENCE_FLAG_ACTIVE, &request->fence.flags);
	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &request->fence.flags))
		i915_request_cancel_breadcrumb(request);

	/* We've already spun, don't charge on resubmitting. */
	if (request->sched.semaphores && __i915_request_has_started(request))
		request->sched.semaphores = 0;

	/*
	 * We don't need to wake_up any waiters on request->execute, they
	 * will get woken by any other event or us re-adding this request
	 * to the engine timeline (__i915_request_submit()). The waiters
	 * should be quite adapt at finding that the request now has a new
	 * global_seqno to the one they went to sleep on.
	 */
}

void i915_request_unsubmit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&engine->sched_engine->lock, flags);

	__i915_request_unsubmit(request);

	spin_unlock_irqrestore(&engine->sched_engine->lock, flags);
}

void i915_request_cancel(struct i915_request *rq, int error)
{
	if (!i915_request_set_error_once(rq, error))
		return;

	set_bit(I915_FENCE_FLAG_SENTINEL, &rq->fence.flags);

	intel_context_cancel_request(rq->context, rq);
}

static int __i915_sw_fence_call
submit_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	struct i915_request *request =
		container_of(fence, typeof(*request), submit);

	switch (state) {
	case FENCE_COMPLETE:
		trace_i915_request_submit(request);

		if (unlikely(fence->error))
			i915_request_set_error_once(request, fence->error);
		else
			__rq_arm_watchdog(request);

		/*
		 * We need to serialize use of the submit_request() callback
		 * with its hotplugging performed during an emergency
		 * i915_gem_set_wedged().  We use the RCU mechanism to mark the
		 * critical section in order to force i915_gem_set_wedged() to
		 * wait until the submit_request() is completed before
		 * proceeding.
		 */
		rcu_read_lock();
		request->engine->submit_request(request);
		rcu_read_unlock();
		break;

	case FENCE_FREE:
		i915_request_put(request);
		break;
	}

	return NOTIFY_DONE;
}

static int __i915_sw_fence_call
semaphore_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	struct i915_request *rq = container_of(fence, typeof(*rq), semaphore);

	switch (state) {
	case FENCE_COMPLETE:
		break;

	case FENCE_FREE:
		i915_request_put(rq);
		break;
	}

	return NOTIFY_DONE;
}

static void retire_requests(struct intel_timeline *tl)
{
	struct i915_request *rq, *rn;

	list_for_each_entry_safe(rq, rn, &tl->requests, link)
		if (!i915_request_retire(rq))
			break;
}

static noinline struct i915_request *
request_alloc_slow(struct intel_timeline *tl,
		   struct i915_request **rsvd,
		   gfp_t gfp)
{
	struct i915_request *rq;

	/* If we cannot wait, dip into our reserves */
	if (!gfpflags_allow_blocking(gfp)) {
		rq = xchg(rsvd, NULL);
		if (!rq) /* Use the normal failure path for one final WARN */
			goto out;

		return rq;
	}

	if (list_empty(&tl->requests))
		goto out;

	/* Move our oldest request to the slab-cache (if not in use!) */
	rq = list_first_entry(&tl->requests, typeof(*rq), link);
	i915_request_retire(rq);

	rq = kmem_cache_alloc(slab_requests,
			      gfp | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (rq)
		return rq;

	/* Ratelimit ourselves to prevent oom from malicious clients */
	rq = list_last_entry(&tl->requests, typeof(*rq), link);
	cond_synchronize_rcu(rq->rcustate);

	/* Retire our old requests in the hope that we free some */
	retire_requests(tl);

out:
	return kmem_cache_alloc(slab_requests, gfp);
}

static void __i915_request_ctor(void *arg)
{
	struct i915_request *rq = arg;

	spin_lock_init(&rq->lock);
	i915_sched_node_init(&rq->sched);
	i915_sw_fence_init(&rq->submit, submit_notify);
	i915_sw_fence_init(&rq->semaphore, semaphore_notify);

	dma_fence_init(&rq->fence, &i915_fence_ops, &rq->lock, 0, 0);

	rq->capture_list = NULL;

	init_llist_head(&rq->execute_cb);
}

struct i915_request *
__i915_request_create(struct intel_context *ce, gfp_t gfp)
{
	struct intel_timeline *tl = ce->timeline;
	struct i915_request *rq;
	u32 seqno;
	int ret;

	might_alloc(gfp);

	/* Check that the caller provided an already pinned context */
	__intel_context_pin(ce);

	/*
	 * Beware: Dragons be flying overhead.
	 *
	 * We use RCU to look up requests in flight. The lookups may
	 * race with the request being allocated from the slab freelist.
	 * That is the request we are writing to here, may be in the process
	 * of being read by __i915_active_request_get_rcu(). As such,
	 * we have to be very careful when overwriting the contents. During
	 * the RCU lookup, we change chase the request->engine pointer,
	 * read the request->global_seqno and increment the reference count.
	 *
	 * The reference count is incremented atomically. If it is zero,
	 * the lookup knows the request is unallocated and complete. Otherwise,
	 * it is either still in use, or has been reallocated and reset
	 * with dma_fence_init(). This increment is safe for release as we
	 * check that the request we have a reference to and matches the active
	 * request.
	 *
	 * Before we increment the refcount, we chase the request->engine
	 * pointer. We must not call kmem_cache_zalloc() or else we set
	 * that pointer to NULL and cause a crash during the lookup. If
	 * we see the request is completed (based on the value of the
	 * old engine and seqno), the lookup is complete and reports NULL.
	 * If we decide the request is not completed (new engine or seqno),
	 * then we grab a reference and double check that it is still the
	 * active request - which it won't be and restart the lookup.
	 *
	 * Do not use kmem_cache_zalloc() here!
	 */
	rq = kmem_cache_alloc(slab_requests,
			      gfp | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (unlikely(!rq)) {
		rq = request_alloc_slow(tl, &ce->engine->request_pool, gfp);
		if (!rq) {
			ret = -ENOMEM;
			goto err_unreserve;
		}
	}

	/*
	 * Hold a reference to the intel_context over life of an i915_request.
	 * Without this an i915_request can exist after the context has been
	 * destroyed (e.g. request retired, context closed, but user space holds
	 * a reference to the request from an out fence). In the case of GuC
	 * submission + virtual engine, the engine that the request references
	 * is also destroyed which can trigger bad pointer dref in fence ops
	 * (e.g. i915_fence_get_driver_name). We could likely change these
	 * functions to avoid touching the engine but let's just be safe and
	 * hold the intel_context reference. In execlist mode the request always
	 * eventually points to a physical engine so this isn't an issue.
	 */
	rq->context = intel_context_get(ce);
	rq->engine = ce->engine;
	rq->ring = ce->ring;
	rq->execution_mask = ce->engine->mask;

	kref_init(&rq->fence.refcount);
	rq->fence.flags = 0;
	rq->fence.error = 0;
	INIT_LIST_HEAD(&rq->fence.cb_list);

	ret = intel_timeline_get_seqno(tl, rq, &seqno);
	if (ret)
		goto err_free;

	rq->fence.context = tl->fence_context;
	rq->fence.seqno = seqno;

	RCU_INIT_POINTER(rq->timeline, tl);
	rq->hwsp_seqno = tl->hwsp_seqno;
	GEM_BUG_ON(__i915_request_is_complete(rq));

	rq->rcustate = get_state_synchronize_rcu(); /* acts as smp_mb() */

	rq->guc_prio = GUC_PRIO_INIT;

	/* We bump the ref for the fence chain */
	i915_sw_fence_reinit(&i915_request_get(rq)->submit);
	i915_sw_fence_reinit(&i915_request_get(rq)->semaphore);

	i915_sched_node_reinit(&rq->sched);

	/* No zalloc, everything must be cleared after use */
	rq->batch = NULL;
	__rq_init_watchdog(rq);
	GEM_BUG_ON(rq->capture_list);
	GEM_BUG_ON(!llist_empty(&rq->execute_cb));

	/*
	 * Reserve space in the ring buffer for all the commands required to
	 * eventually emit this request. This is to guarantee that the
	 * i915_request_add() call can't fail. Note that the reserve may need
	 * to be redone if the request is not actually submitted straight
	 * away, e.g. because a GPU scheduler has deferred it.
	 *
	 * Note that due to how we add reserved_space to intel_ring_begin()
	 * we need to double our request to ensure that if we need to wrap
	 * around inside i915_request_add() there is sufficient space at
	 * the beginning of the ring as well.
	 */
	rq->reserved_space =
		2 * rq->engine->emit_fini_breadcrumb_dw * sizeof(u32);

	/*
	 * Record the position of the start of the request so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the head.
	 */
	rq->head = rq->ring->emit;

	ret = rq->engine->request_alloc(rq);
	if (ret)
		goto err_unwind;

	rq->infix = rq->ring->emit; /* end of header; start of user payload */

	intel_context_mark_active(ce);
	list_add_tail_rcu(&rq->link, &tl->requests);

	return rq;

err_unwind:
	ce->ring->emit = rq->head;

	/* Make sure we didn't add ourselves to external state before freeing */
	GEM_BUG_ON(!list_empty(&rq->sched.signalers_list));
	GEM_BUG_ON(!list_empty(&rq->sched.waiters_list));

err_free:
	intel_context_put(ce);
	kmem_cache_free(slab_requests, rq);
err_unreserve:
	intel_context_unpin(ce);
	return ERR_PTR(ret);
}

struct i915_request *
i915_request_create(struct intel_context *ce)
{
	struct i915_request *rq;
	struct intel_timeline *tl;

	tl = intel_context_timeline_lock(ce);
	if (IS_ERR(tl))
		return ERR_CAST(tl);

	/* Move our oldest request to the slab-cache (if not in use!) */
	rq = list_first_entry(&tl->requests, typeof(*rq), link);
	if (!list_is_last(&rq->link, &tl->requests))
		i915_request_retire(rq);

	intel_context_enter(ce);
	rq = __i915_request_create(ce, GFP_KERNEL);
	intel_context_exit(ce); /* active reference transferred to request */
	if (IS_ERR(rq))
		goto err_unlock;

	/* Check that we do not interrupt ourselves with a new request */
	rq->cookie = lockdep_pin_lock(&tl->mutex);

	return rq;

err_unlock:
	intel_context_timeline_unlock(tl);
	return rq;
}

static int
i915_request_await_start(struct i915_request *rq, struct i915_request *signal)
{
	struct dma_fence *fence;
	int err;

	if (i915_request_timeline(rq) == rcu_access_pointer(signal->timeline))
		return 0;

	if (i915_request_started(signal))
		return 0;

	/*
	 * The caller holds a reference on @signal, but we do not serialise
	 * against it being retired and removed from the lists.
	 *
	 * We do not hold a reference to the request before @signal, and
	 * so must be very careful to ensure that it is not _recycled_ as
	 * we follow the link backwards.
	 */
	fence = NULL;
	rcu_read_lock();
	do {
		struct list_head *pos = READ_ONCE(signal->link.prev);
		struct i915_request *prev;

		/* Confirm signal has not been retired, the link is valid */
		if (unlikely(__i915_request_has_started(signal)))
			break;

		/* Is signal the earliest request on its timeline? */
		if (pos == &rcu_dereference(signal->timeline)->requests)
			break;

		/*
		 * Peek at the request before us in the timeline. That
		 * request will only be valid before it is retired, so
		 * after acquiring a reference to it, confirm that it is
		 * still part of the signaler's timeline.
		 */
		prev = list_entry(pos, typeof(*prev), link);
		if (!i915_request_get_rcu(prev))
			break;

		/* After the strong barrier, confirm prev is still attached */
		if (unlikely(READ_ONCE(prev->link.next) != &signal->link)) {
			i915_request_put(prev);
			break;
		}

		fence = &prev->fence;
	} while (0);
	rcu_read_unlock();
	if (!fence)
		return 0;

	err = 0;
	if (!intel_timeline_sync_is_later(i915_request_timeline(rq), fence))
		err = i915_sw_fence_await_dma_fence(&rq->submit,
						    fence, 0,
						    I915_FENCE_GFP);
	dma_fence_put(fence);

	return err;
}

static intel_engine_mask_t
already_busywaiting(struct i915_request *rq)
{
	/*
	 * Polling a semaphore causes bus traffic, delaying other users of
	 * both the GPU and CPU. We want to limit the impact on others,
	 * while taking advantage of early submission to reduce GPU
	 * latency. Therefore we restrict ourselves to not using more
	 * than one semaphore from each source, and not using a semaphore
	 * if we have detected the engine is saturated (i.e. would not be
	 * submitted early and cause bus traffic reading an already passed
	 * semaphore).
	 *
	 * See the are-we-too-late? check in __i915_request_submit().
	 */
	return rq->sched.semaphores | READ_ONCE(rq->engine->saturated);
}

static int
__emit_semaphore_wait(struct i915_request *to,
		      struct i915_request *from,
		      u32 seqno)
{
	const int has_token = GRAPHICS_VER(to->engine->i915) >= 12;
	u32 hwsp_offset;
	int len, err;
	u32 *cs;

	GEM_BUG_ON(GRAPHICS_VER(to->engine->i915) < 8);
	GEM_BUG_ON(i915_request_has_initial_breadcrumb(to));

	/* We need to pin the signaler's HWSP until we are finished reading. */
	err = intel_timeline_read_hwsp(from, to, &hwsp_offset);
	if (err)
		return err;

	len = 4;
	if (has_token)
		len += 2;

	cs = intel_ring_begin(to, len);
	if (IS_ERR(cs))
		return PTR_ERR(cs);

	/*
	 * Using greater-than-or-equal here means we have to worry
	 * about seqno wraparound. To side step that issue, we swap
	 * the timeline HWSP upon wrapping, so that everyone listening
	 * for the old (pre-wrap) values do not see the much smaller
	 * (post-wrap) values than they were expecting (and so wait
	 * forever).
	 */
	*cs++ = (MI_SEMAPHORE_WAIT |
		 MI_SEMAPHORE_GLOBAL_GTT |
		 MI_SEMAPHORE_POLL |
		 MI_SEMAPHORE_SAD_GTE_SDD) +
		has_token;
	*cs++ = seqno;
	*cs++ = hwsp_offset;
	*cs++ = 0;
	if (has_token) {
		*cs++ = 0;
		*cs++ = MI_NOOP;
	}

	intel_ring_advance(to, cs);
	return 0;
}

static int
emit_semaphore_wait(struct i915_request *to,
		    struct i915_request *from,
		    gfp_t gfp)
{
	const intel_engine_mask_t mask = READ_ONCE(from->engine)->mask;
	struct i915_sw_fence *wait = &to->submit;

	if (!intel_context_use_semaphores(to->context))
		goto await_fence;

	if (i915_request_has_initial_breadcrumb(to))
		goto await_fence;

	/*
	 * If this or its dependents are waiting on an external fence
	 * that may fail catastrophically, then we want to avoid using
	 * sempahores as they bypass the fence signaling metadata, and we
	 * lose the fence->error propagation.
	 */
	if (from->sched.flags & I915_SCHED_HAS_EXTERNAL_CHAIN)
		goto await_fence;

	/* Just emit the first semaphore we see as request space is limited. */
	if (already_busywaiting(to) & mask)
		goto await_fence;

	if (i915_request_await_start(to, from) < 0)
		goto await_fence;

	/* Only submit our spinner after the signaler is running! */
	if (__await_execution(to, from, gfp))
		goto await_fence;

	if (__emit_semaphore_wait(to, from, from->fence.seqno))
		goto await_fence;

	to->sched.semaphores |= mask;
	wait = &to->semaphore;

await_fence:
	return i915_sw_fence_await_dma_fence(wait,
					     &from->fence, 0,
					     I915_FENCE_GFP);
}

static bool intel_timeline_sync_has_start(struct intel_timeline *tl,
					  struct dma_fence *fence)
{
	return __intel_timeline_sync_is_later(tl,
					      fence->context,
					      fence->seqno - 1);
}

static int intel_timeline_sync_set_start(struct intel_timeline *tl,
					 const struct dma_fence *fence)
{
	return __intel_timeline_sync_set(tl, fence->context, fence->seqno - 1);
}

static int
__i915_request_await_execution(struct i915_request *to,
			       struct i915_request *from)
{
	int err;

	GEM_BUG_ON(intel_context_is_barrier(from->context));

	/* Submit both requests at the same time */
	err = __await_execution(to, from, I915_FENCE_GFP);
	if (err)
		return err;

	/* Squash repeated depenendices to the same timelines */
	if (intel_timeline_sync_has_start(i915_request_timeline(to),
					  &from->fence))
		return 0;

	/*
	 * Wait until the start of this request.
	 *
	 * The execution cb fires when we submit the request to HW. But in
	 * many cases this may be long before the request itself is ready to
	 * run (consider that we submit 2 requests for the same context, where
	 * the request of interest is behind an indefinite spinner). So we hook
	 * up to both to reduce our queues and keep the execution lag minimised
	 * in the worst case, though we hope that the await_start is elided.
	 */
	err = i915_request_await_start(to, from);
	if (err < 0)
		return err;

	/*
	 * Ensure both start together [after all semaphores in signal]
	 *
	 * Now that we are queued to the HW at roughly the same time (thanks
	 * to the execute cb) and are ready to run at roughly the same time
	 * (thanks to the await start), our signaler may still be indefinitely
	 * delayed by waiting on a semaphore from a remote engine. If our
	 * signaler depends on a semaphore, so indirectly do we, and we do not
	 * want to start our payload until our signaler also starts theirs.
	 * So we wait.
	 *
	 * However, there is also a second condition for which we need to wait
	 * for the precise start of the signaler. Consider that the signaler
	 * was submitted in a chain of requests following another context
	 * (with just an ordinary intra-engine fence dependency between the
	 * two). In this case the signaler is queued to HW, but not for
	 * immediate execution, and so we must wait until it reaches the
	 * active slot.
	 */
	if (intel_engine_has_semaphores(to->engine) &&
	    !i915_request_has_initial_breadcrumb(to)) {
		err = __emit_semaphore_wait(to, from, from->fence.seqno - 1);
		if (err < 0)
			return err;
	}

	/* Couple the dependency tree for PI on this exposed to->fence */
	if (to->engine->sched_engine->schedule) {
		err = i915_sched_node_add_dependency(&to->sched,
						     &from->sched,
						     I915_DEPENDENCY_WEAK);
		if (err < 0)
			return err;
	}

	return intel_timeline_sync_set_start(i915_request_timeline(to),
					     &from->fence);
}

static void mark_external(struct i915_request *rq)
{
	/*
	 * The downside of using semaphores is that we lose metadata passing
	 * along the signaling chain. This is particularly nasty when we
	 * need to pass along a fatal error such as EFAULT or EDEADLK. For
	 * fatal errors we want to scrub the request before it is executed,
	 * which means that we cannot preload the request onto HW and have
	 * it wait upon a semaphore.
	 */
	rq->sched.flags |= I915_SCHED_HAS_EXTERNAL_CHAIN;
}

static int
__i915_request_await_external(struct i915_request *rq, struct dma_fence *fence)
{
	mark_external(rq);
	return i915_sw_fence_await_dma_fence(&rq->submit, fence,
					     i915_fence_context_timeout(rq->engine->i915,
									fence->context),
					     I915_FENCE_GFP);
}

static int
i915_request_await_external(struct i915_request *rq, struct dma_fence *fence)
{
	struct dma_fence *iter;
	int err = 0;

	if (!to_dma_fence_chain(fence))
		return __i915_request_await_external(rq, fence);

	dma_fence_chain_for_each(iter, fence) {
		struct dma_fence_chain *chain = to_dma_fence_chain(iter);

		if (!dma_fence_is_i915(chain->fence)) {
			err = __i915_request_await_external(rq, iter);
			break;
		}

		err = i915_request_await_dma_fence(rq, chain->fence);
		if (err < 0)
			break;
	}

	dma_fence_put(iter);
	return err;
}

int
i915_request_await_execution(struct i915_request *rq,
			     struct dma_fence *fence)
{
	struct dma_fence **child = &fence;
	unsigned int nchild = 1;
	int ret;

	if (dma_fence_is_array(fence)) {
		struct dma_fence_array *array = to_dma_fence_array(fence);

		/* XXX Error for signal-on-any fence arrays */

		child = array->fences;
		nchild = array->num_fences;
		GEM_BUG_ON(!nchild);
	}

	do {
		fence = *child++;
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
			continue;

		if (fence->context == rq->fence.context)
			continue;

		/*
		 * We don't squash repeated fence dependencies here as we
		 * want to run our callback in all cases.
		 */

		if (dma_fence_is_i915(fence))
			ret = __i915_request_await_execution(rq,
							     to_request(fence));
		else
			ret = i915_request_await_external(rq, fence);
		if (ret < 0)
			return ret;
	} while (--nchild);

	return 0;
}

static int
await_request_submit(struct i915_request *to, struct i915_request *from)
{
	/*
	 * If we are waiting on a virtual engine, then it may be
	 * constrained to execute on a single engine *prior* to submission.
	 * When it is submitted, it will be first submitted to the virtual
	 * engine and then passed to the physical engine. We cannot allow
	 * the waiter to be submitted immediately to the physical engine
	 * as it may then bypass the virtual request.
	 */
	if (to->engine == READ_ONCE(from->engine))
		return i915_sw_fence_await_sw_fence_gfp(&to->submit,
							&from->submit,
							I915_FENCE_GFP);
	else
		return __i915_request_await_execution(to, from);
}

static int
i915_request_await_request(struct i915_request *to, struct i915_request *from)
{
	int ret;

	GEM_BUG_ON(to == from);
	GEM_BUG_ON(to->timeline == from->timeline);

	if (i915_request_completed(from)) {
		i915_sw_fence_set_error_once(&to->submit, from->fence.error);
		return 0;
	}

	if (to->engine->sched_engine->schedule) {
		ret = i915_sched_node_add_dependency(&to->sched,
						     &from->sched,
						     I915_DEPENDENCY_EXTERNAL);
		if (ret < 0)
			return ret;
	}

	if (!intel_engine_uses_guc(to->engine) &&
	    is_power_of_2(to->execution_mask | READ_ONCE(from->execution_mask)))
		ret = await_request_submit(to, from);
	else
		ret = emit_semaphore_wait(to, from, I915_FENCE_GFP);
	if (ret < 0)
		return ret;

	return 0;
}

int
i915_request_await_dma_fence(struct i915_request *rq, struct dma_fence *fence)
{
	struct dma_fence **child = &fence;
	unsigned int nchild = 1;
	int ret;

	/*
	 * Note that if the fence-array was created in signal-on-any mode,
	 * we should *not* decompose it into its individual fences. However,
	 * we don't currently store which mode the fence-array is operating
	 * in. Fortunately, the only user of signal-on-any is private to
	 * amdgpu and we should not see any incoming fence-array from
	 * sync-file being in signal-on-any mode.
	 */
	if (dma_fence_is_array(fence)) {
		struct dma_fence_array *array = to_dma_fence_array(fence);

		child = array->fences;
		nchild = array->num_fences;
		GEM_BUG_ON(!nchild);
	}

	do {
		fence = *child++;
		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->flags))
			continue;

		/*
		 * Requests on the same timeline are explicitly ordered, along
		 * with their dependencies, by i915_request_add() which ensures
		 * that requests are submitted in-order through each ring.
		 */
		if (fence->context == rq->fence.context)
			continue;

		/* Squash repeated waits to the same timelines */
		if (fence->context &&
		    intel_timeline_sync_is_later(i915_request_timeline(rq),
						 fence))
			continue;

		if (dma_fence_is_i915(fence))
			ret = i915_request_await_request(rq, to_request(fence));
		else
			ret = i915_request_await_external(rq, fence);
		if (ret < 0)
			return ret;

		/* Record the latest fence used against each timeline */
		if (fence->context)
			intel_timeline_sync_set(i915_request_timeline(rq),
						fence);
	} while (--nchild);

	return 0;
}

/**
 * i915_request_await_object - set this request to (async) wait upon a bo
 * @to: request we are wishing to use
 * @obj: object which may be in use on another ring.
 * @write: whether the wait is on behalf of a writer
 *
 * This code is meant to abstract object synchronization with the GPU.
 * Conceptually we serialise writes between engines inside the GPU.
 * We only allow one engine to write into a buffer at any time, but
 * multiple readers. To ensure each has a coherent view of memory, we must:
 *
 * - If there is an outstanding write request to the object, the new
 *   request must wait for it to complete (either CPU or in hw, requests
 *   on the same ring will be naturally ordered).
 *
 * - If we are a write request (pending_write_domain is set), the new
 *   request must wait for outstanding read requests to complete.
 *
 * Returns 0 if successful, else propagates up the lower layer error.
 */
int
i915_request_await_object(struct i915_request *to,
			  struct drm_i915_gem_object *obj,
			  bool write)
{
	struct dma_fence *excl;
	int ret = 0;

	if (write) {
		struct dma_fence **shared;
		unsigned int count, i;

		ret = dma_resv_get_fences(obj->base.resv, &excl, &count,
					  &shared);
		if (ret)
			return ret;

		for (i = 0; i < count; i++) {
			ret = i915_request_await_dma_fence(to, shared[i]);
			if (ret)
				break;

			dma_fence_put(shared[i]);
		}

		for (; i < count; i++)
			dma_fence_put(shared[i]);
		kfree(shared);
	} else {
		excl = dma_resv_get_excl_unlocked(obj->base.resv);
	}

	if (excl) {
		if (ret == 0)
			ret = i915_request_await_dma_fence(to, excl);

		dma_fence_put(excl);
	}

	return ret;
}

static struct i915_request *
__i915_request_add_to_timeline(struct i915_request *rq)
{
	struct intel_timeline *timeline = i915_request_timeline(rq);
	struct i915_request *prev;

	/*
	 * Dependency tracking and request ordering along the timeline
	 * is special cased so that we can eliminate redundant ordering
	 * operations while building the request (we know that the timeline
	 * itself is ordered, and here we guarantee it).
	 *
	 * As we know we will need to emit tracking along the timeline,
	 * we embed the hooks into our request struct -- at the cost of
	 * having to have specialised no-allocation interfaces (which will
	 * be beneficial elsewhere).
	 *
	 * A second benefit to open-coding i915_request_await_request is
	 * that we can apply a slight variant of the rules specialised
	 * for timelines that jump between engines (such as virtual engines).
	 * If we consider the case of virtual engine, we must emit a dma-fence
	 * to prevent scheduling of the second request until the first is
	 * complete (to maximise our greedy late load balancing) and this
	 * precludes optimising to use semaphores serialisation of a single
	 * timeline across engines.
	 */
	prev = to_request(__i915_active_fence_set(&timeline->last_request,
						  &rq->fence));
	if (prev && !__i915_request_is_complete(prev)) {
		bool uses_guc = intel_engine_uses_guc(rq->engine);

		/*
		 * The requests are supposed to be kept in order. However,
		 * we need to be wary in case the timeline->last_request
		 * is used as a barrier for external modification to this
		 * context.
		 */
		GEM_BUG_ON(prev->context == rq->context &&
			   i915_seqno_passed(prev->fence.seqno,
					     rq->fence.seqno));

		if ((!uses_guc &&
		     is_power_of_2(READ_ONCE(prev->engine)->mask | rq->engine->mask)) ||
		    (uses_guc && prev->context == rq->context))
			i915_sw_fence_await_sw_fence(&rq->submit,
						     &prev->submit,
						     &rq->submitq);
		else
			__i915_sw_fence_await_dma_fence(&rq->submit,
							&prev->fence,
							&rq->dmaq);
		if (rq->engine->sched_engine->schedule)
			__i915_sched_node_add_dependency(&rq->sched,
							 &prev->sched,
							 &rq->dep,
							 0);
	}

	/*
	 * Make sure that no request gazumped us - if it was allocated after
	 * our i915_request_alloc() and called __i915_request_add() before
	 * us, the timeline will hold its seqno which is later than ours.
	 */
	GEM_BUG_ON(timeline->seqno != rq->fence.seqno);

	return prev;
}

/*
 * NB: This function is not allowed to fail. Doing so would mean the the
 * request is not being tracked for completion but the work itself is
 * going to happen on the hardware. This would be a Bad Thing(tm).
 */
struct i915_request *__i915_request_commit(struct i915_request *rq)
{
	struct intel_engine_cs *engine = rq->engine;
	struct intel_ring *ring = rq->ring;
	u32 *cs;

	RQ_TRACE(rq, "\n");

	/*
	 * To ensure that this call will not fail, space for its emissions
	 * should already have been reserved in the ring buffer. Let the ring
	 * know that it is time to use that space up.
	 */
	GEM_BUG_ON(rq->reserved_space > ring->space);
	rq->reserved_space = 0;
	rq->emitted_jiffies = jiffies;

	/*
	 * Record the position of the start of the breadcrumb so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the ring's HEAD.
	 */
	cs = intel_ring_begin(rq, engine->emit_fini_breadcrumb_dw);
	GEM_BUG_ON(IS_ERR(cs));
	rq->postfix = intel_ring_offset(rq, cs);

	return __i915_request_add_to_timeline(rq);
}

void __i915_request_queue_bh(struct i915_request *rq)
{
	i915_sw_fence_commit(&rq->semaphore);
	i915_sw_fence_commit(&rq->submit);
}

void __i915_request_queue(struct i915_request *rq,
			  const struct i915_sched_attr *attr)
{
	/*
	 * Let the backend know a new request has arrived that may need
	 * to adjust the existing execution schedule due to a high priority
	 * request - i.e. we may want to preempt the current request in order
	 * to run a high priority dependency chain *before* we can execute this
	 * request.
	 *
	 * This is called before the request is ready to run so that we can
	 * decide whether to preempt the entire chain so that it is ready to
	 * run at the earliest possible convenience.
	 */
	if (attr && rq->engine->sched_engine->schedule)
		rq->engine->sched_engine->schedule(rq, attr);

	local_bh_disable();
	__i915_request_queue_bh(rq);
	local_bh_enable(); /* kick tasklets */
}

void i915_request_add(struct i915_request *rq)
{
	struct intel_timeline * const tl = i915_request_timeline(rq);
	struct i915_sched_attr attr = {};
	struct i915_gem_context *ctx;

	lockdep_assert_held(&tl->mutex);
	lockdep_unpin_lock(&tl->mutex, rq->cookie);

	trace_i915_request_add(rq);
	__i915_request_commit(rq);

	/* XXX placeholder for selftests */
	rcu_read_lock();
	ctx = rcu_dereference(rq->context->gem_context);
	if (ctx)
		attr = ctx->sched;
	rcu_read_unlock();

	__i915_request_queue(rq, &attr);

	mutex_unlock(&tl->mutex);
}

static unsigned long local_clock_ns(unsigned int *cpu)
{
	unsigned long t;

	/*
	 * Cheaply and approximately convert from nanoseconds to microseconds.
	 * The result and subsequent calculations are also defined in the same
	 * approximate microseconds units. The principal source of timing
	 * error here is from the simple truncation.
	 *
	 * Note that local_clock() is only defined wrt to the current CPU;
	 * the comparisons are no longer valid if we switch CPUs. Instead of
	 * blocking preemption for the entire busywait, we can detect the CPU
	 * switch and use that as indicator of system load and a reason to
	 * stop busywaiting, see busywait_stop().
	 */
	*cpu = get_cpu();
	t = local_clock();
	put_cpu();

	return t;
}

static bool busywait_stop(unsigned long timeout, unsigned int cpu)
{
	unsigned int this_cpu;

	if (time_after(local_clock_ns(&this_cpu), timeout))
		return true;

	return this_cpu != cpu;
}

static bool __i915_spin_request(struct i915_request * const rq, int state)
{
	unsigned long timeout_ns;
	unsigned int cpu;

	/*
	 * Only wait for the request if we know it is likely to complete.
	 *
	 * We don't track the timestamps around requests, nor the average
	 * request length, so we do not have a good indicator that this
	 * request will complete within the timeout. What we do know is the
	 * order in which requests are executed by the context and so we can
	 * tell if the request has been started. If the request is not even
	 * running yet, it is a fair assumption that it will not complete
	 * within our relatively short timeout.
	 */
	if (!i915_request_is_running(rq))
		return false;

	/*
	 * When waiting for high frequency requests, e.g. during synchronous
	 * rendering split between the CPU and GPU, the finite amount of time
	 * required to set up the irq and wait upon it limits the response
	 * rate. By busywaiting on the request completion for a short while we
	 * can service the high frequency waits as quick as possible. However,
	 * if it is a slow request, we want to sleep as quickly as possible.
	 * The tradeoff between waiting and sleeping is roughly the time it
	 * takes to sleep on a request, on the order of a microsecond.
	 */

	timeout_ns = READ_ONCE(rq->engine->props.max_busywait_duration_ns);
	timeout_ns += local_clock_ns(&cpu);
	do {
		if (dma_fence_is_signaled(&rq->fence))
			return true;

		if (signal_pending_state(state, current))
			break;

		if (busywait_stop(timeout_ns, cpu))
			break;

		cpu_relax();
	} while (!need_resched());

	return false;
}

struct request_wait {
	struct dma_fence_cb cb;
	struct task_struct *tsk;
};

static void request_wait_wake(struct dma_fence *fence, struct dma_fence_cb *cb)
{
	struct request_wait *wait = container_of(cb, typeof(*wait), cb);

	wake_up_process(fetch_and_zero(&wait->tsk));
}

/**
 * i915_request_wait - wait until execution of request has finished
 * @rq: the request to wait upon
 * @flags: how to wait
 * @timeout: how long to wait in jiffies
 *
 * i915_request_wait() waits for the request to be completed, for a
 * maximum of @timeout jiffies (with MAX_SCHEDULE_TIMEOUT implying an
 * unbounded wait).
 *
 * Returns the remaining time (in jiffies) if the request completed, which may
 * be zero or -ETIME if the request is unfinished after the timeout expires.
 * May return -EINTR is called with I915_WAIT_INTERRUPTIBLE and a signal is
 * pending before the request completes.
 */
long i915_request_wait(struct i915_request *rq,
		       unsigned int flags,
		       long timeout)
{
	const int state = flags & I915_WAIT_INTERRUPTIBLE ?
		TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
	struct request_wait wait;

	might_sleep();
	GEM_BUG_ON(timeout < 0);

	if (dma_fence_is_signaled(&rq->fence))
		return timeout;

	if (!timeout)
		return -ETIME;

	trace_i915_request_wait_begin(rq, flags);

	/*
	 * We must never wait on the GPU while holding a lock as we
	 * may need to perform a GPU reset. So while we don't need to
	 * serialise wait/reset with an explicit lock, we do want
	 * lockdep to detect potential dependency cycles.
	 */
	mutex_acquire(&rq->engine->gt->reset.mutex.dep_map, 0, 0, _THIS_IP_);

	/*
	 * Optimistic spin before touching IRQs.
	 *
	 * We may use a rather large value here to offset the penalty of
	 * switching away from the active task. Frequently, the client will
	 * wait upon an old swapbuffer to throttle itself to remain within a
	 * frame of the gpu. If the client is running in lockstep with the gpu,
	 * then it should not be waiting long at all, and a sleep now will incur
	 * extra scheduler latency in producing the next frame. To try to
	 * avoid adding the cost of enabling/disabling the interrupt to the
	 * short wait, we first spin to see if the request would have completed
	 * in the time taken to setup the interrupt.
	 *
	 * We need upto 5us to enable the irq, and upto 20us to hide the
	 * scheduler latency of a context switch, ignoring the secondary
	 * impacts from a context switch such as cache eviction.
	 *
	 * The scheme used for low-latency IO is called "hybrid interrupt
	 * polling". The suggestion there is to sleep until just before you
	 * expect to be woken by the device interrupt and then poll for its
	 * completion. That requires having a good predictor for the request
	 * duration, which we currently lack.
	 */
	if (IS_ACTIVE(CONFIG_DRM_I915_MAX_REQUEST_BUSYWAIT) &&
	    __i915_spin_request(rq, state))
		goto out;

	/*
	 * This client is about to stall waiting for the GPU. In many cases
	 * this is undesirable and limits the throughput of the system, as
	 * many clients cannot continue processing user input/output whilst
	 * blocked. RPS autotuning may take tens of milliseconds to respond
	 * to the GPU load and thus incurs additional latency for the client.
	 * We can circumvent that by promoting the GPU frequency to maximum
	 * before we sleep. This makes the GPU throttle up much more quickly
	 * (good for benchmarks and user experience, e.g. window animations),
	 * but at a cost of spending more power processing the workload
	 * (bad for battery).
	 */
	if (flags & I915_WAIT_PRIORITY && !i915_request_started(rq))
		intel_rps_boost(rq);

	wait.tsk = current;
	if (dma_fence_add_callback(&rq->fence, &wait.cb, request_wait_wake))
		goto out;

	/*
	 * Flush the submission tasklet, but only if it may help this request.
	 *
	 * We sometimes experience some latency between the HW interrupts and
	 * tasklet execution (mostly due to ksoftirqd latency, but it can also
	 * be due to lazy CS events), so lets run the tasklet manually if there
	 * is a chance it may submit this request. If the request is not ready
	 * to run, as it is waiting for other fences to be signaled, flushing
	 * the tasklet is busy work without any advantage for this client.
	 *
	 * If the HW is being lazy, this is the last chance before we go to
	 * sleep to catch any pending events. We will check periodically in
	 * the heartbeat to flush the submission tasklets as a last resort
	 * for unhappy HW.
	 */
	if (i915_request_is_ready(rq))
		__intel_engine_flush_submission(rq->engine, false);

	for (;;) {
		set_current_state(state);

		if (dma_fence_is_signaled(&rq->fence))
			break;

		if (signal_pending_state(state, current)) {
			timeout = -ERESTARTSYS;
			break;
		}

		if (!timeout) {
			timeout = -ETIME;
			break;
		}

		timeout = io_schedule_timeout(timeout);
	}
	__set_current_state(TASK_RUNNING);

	if (READ_ONCE(wait.tsk))
		dma_fence_remove_callback(&rq->fence, &wait.cb);
	GEM_BUG_ON(!list_empty(&wait.cb.node));

out:
	mutex_release(&rq->engine->gt->reset.mutex.dep_map, _THIS_IP_);
	trace_i915_request_wait_end(rq);
	return timeout;
}

static int print_sched_attr(const struct i915_sched_attr *attr,
			    char *buf, int x, int len)
{
	if (attr->priority == I915_PRIORITY_INVALID)
		return x;

	x += snprintf(buf + x, len - x,
		      " prio=%d", attr->priority);

	return x;
}

static char queue_status(const struct i915_request *rq)
{
	if (i915_request_is_active(rq))
		return 'E';

	if (i915_request_is_ready(rq))
		return intel_engine_is_virtual(rq->engine) ? 'V' : 'R';

	return 'U';
}

static const char *run_status(const struct i915_request *rq)
{
	if (__i915_request_is_complete(rq))
		return "!";

	if (__i915_request_has_started(rq))
		return "*";

	if (!i915_sw_fence_signaled(&rq->semaphore))
		return "&";

	return "";
}

static const char *fence_status(const struct i915_request *rq)
{
	if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &rq->fence.flags))
		return "+";

	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &rq->fence.flags))
		return "-";

	return "";
}

void i915_request_show(struct drm_printer *m,
		       const struct i915_request *rq,
		       const char *prefix,
		       int indent)
{
	const char *name = rq->fence.ops->get_timeline_name((struct dma_fence *)&rq->fence);
	char buf[80] = "";
	int x = 0;

	/*
	 * The prefix is used to show the queue status, for which we use
	 * the following flags:
	 *
	 *  U [Unready]
	 *    - initial status upon being submitted by the user
	 *
	 *    - the request is not ready for execution as it is waiting
	 *      for external fences
	 *
	 *  R [Ready]
	 *    - all fences the request was waiting on have been signaled,
	 *      and the request is now ready for execution and will be
	 *      in a backend queue
	 *
	 *    - a ready request may still need to wait on semaphores
	 *      [internal fences]
	 *
	 *  V [Ready/virtual]
	 *    - same as ready, but queued over multiple backends
	 *
	 *  E [Executing]
	 *    - the request has been transferred from the backend queue and
	 *      submitted for execution on HW
	 *
	 *    - a completed request may still be regarded as executing, its
	 *      status may not be updated until it is retired and removed
	 *      from the lists
	 */

	x = print_sched_attr(&rq->sched.attr, buf, x, sizeof(buf));

	drm_printf(m, "%s%.*s%c %llx:%lld%s%s %s @ %dms: %s\n",
		   prefix, indent, "                ",
		   queue_status(rq),
		   rq->fence.context, rq->fence.seqno,
		   run_status(rq),
		   fence_status(rq),
		   buf,
		   jiffies_to_msecs(jiffies - rq->emitted_jiffies),
		   name);
}

static bool engine_match_ring(struct intel_engine_cs *engine, struct i915_request *rq)
{
	u32 ring = ENGINE_READ(engine, RING_START);

	return ring == i915_ggtt_offset(rq->ring->vma);
}

static bool match_ring(struct i915_request *rq)
{
	struct intel_engine_cs *engine;
	bool found;
	int i;

	if (!intel_engine_is_virtual(rq->engine))
		return engine_match_ring(rq->engine, rq);

	found = false;
	i = 0;
	while ((engine = intel_engine_get_sibling(rq->engine, i++))) {
		found = engine_match_ring(engine, rq);
		if (found)
			break;
	}

	return found;
}

enum i915_request_state i915_test_request_state(struct i915_request *rq)
{
	if (i915_request_completed(rq))
		return I915_REQUEST_COMPLETE;

	if (!i915_request_started(rq))
		return I915_REQUEST_PENDING;

	if (match_ring(rq))
		return I915_REQUEST_ACTIVE;

	return I915_REQUEST_QUEUED;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_request.c"
#include "selftests/i915_request.c"
#endif

void i915_request_module_exit(void)
{
	kmem_cache_destroy(slab_execute_cbs);
	kmem_cache_destroy(slab_requests);
}

int __init i915_request_module_init(void)
{
	slab_requests =
		kmem_cache_create("i915_request",
				  sizeof(struct i915_request),
				  __alignof__(struct i915_request),
				  SLAB_HWCACHE_ALIGN |
				  SLAB_RECLAIM_ACCOUNT |
				  SLAB_TYPESAFE_BY_RCU,
				  __i915_request_ctor);
	if (!slab_requests)
		return -ENOMEM;

	slab_execute_cbs = KMEM_CACHE(execute_cb,
					     SLAB_HWCACHE_ALIGN |
					     SLAB_RECLAIM_ACCOUNT |
					     SLAB_TYPESAFE_BY_RCU);
	if (!slab_execute_cbs)
		goto err_requests;

	return 0;

err_requests:
	kmem_cache_destroy(slab_requests);
	return -ENOMEM;
}
