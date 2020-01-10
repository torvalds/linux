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
#include <linux/irq_work.h>
#include <linux/prefetch.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>

#include "gem/i915_gem_context.h"
#include "gt/intel_context.h"
#include "gt/intel_ring.h"
#include "gt/intel_rps.h"

#include "i915_active.h"
#include "i915_drv.h"
#include "i915_globals.h"
#include "i915_trace.h"
#include "intel_pm.h"

struct execute_cb {
	struct list_head link;
	struct irq_work work;
	struct i915_sw_fence *fence;
	void (*hook)(struct i915_request *rq, struct dma_fence *signal);
	struct i915_request *signal;
};

static struct i915_global_request {
	struct i915_global base;
	struct kmem_cache *slab_requests;
	struct kmem_cache *slab_dependencies;
	struct kmem_cache *slab_execute_cbs;
} global;

static const char *i915_fence_get_driver_name(struct dma_fence *fence)
{
	return "i915";
}

static const char *i915_fence_get_timeline_name(struct dma_fence *fence)
{
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

	return to_request(fence)->gem_context->name ?: "[i915]";
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

static void i915_fence_release(struct dma_fence *fence)
{
	struct i915_request *rq = to_request(fence);

	/*
	 * The request is put onto a RCU freelist (i.e. the address
	 * is immediately reused), mark the fences as being freed now.
	 * Otherwise the debugobjects for the fences are only marked as
	 * freed when the slab cache itself is freed, and so we would get
	 * caught trying to reuse dead objects.
	 */
	i915_sw_fence_fini(&rq->submit);
	i915_sw_fence_fini(&rq->semaphore);

	kmem_cache_free(global.slab_requests, rq);
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
	kmem_cache_free(global.slab_execute_cbs, cb);
}

static void irq_execute_cb_hook(struct irq_work *wrk)
{
	struct execute_cb *cb = container_of(wrk, typeof(*cb), work);

	cb->hook(container_of(cb->fence, struct i915_request, submit),
		 &cb->signal->fence);
	i915_request_put(cb->signal);

	irq_execute_cb(wrk);
}

static void __notify_execute_cb(struct i915_request *rq)
{
	struct execute_cb *cb;

	lockdep_assert_held(&rq->lock);

	if (list_empty(&rq->execute_cb))
		return;

	list_for_each_entry(cb, &rq->execute_cb, link)
		irq_work_queue(&cb->work);

	/*
	 * XXX Rollback on __i915_request_unsubmit()
	 *
	 * In the future, perhaps when we have an active time-slicing scheduler,
	 * it will be interesting to unsubmit parallel execution and remove
	 * busywaits from the GPU until their master is restarted. This is
	 * quite hairy, we have to carefully rollback the fence and do a
	 * preempt-to-idle cycle on the target engine, all the while the
	 * master execute_cb may refire.
	 */
	INIT_LIST_HEAD(&rq->execute_cb);
}

static inline void
remove_from_client(struct i915_request *request)
{
	struct drm_i915_file_private *file_priv;

	if (!READ_ONCE(request->file_priv))
		return;

	rcu_read_lock();
	file_priv = xchg(&request->file_priv, NULL);
	if (file_priv) {
		spin_lock(&file_priv->mm.lock);
		list_del(&request->client_link);
		spin_unlock(&file_priv->mm.lock);
	}
	rcu_read_unlock();
}

static void free_capture_list(struct i915_request *request)
{
	struct i915_capture_list *capture;

	capture = request->capture_list;
	while (capture) {
		struct i915_capture_list *next = capture->next;

		kfree(capture);
		capture = next;
	}
}

static void remove_from_engine(struct i915_request *rq)
{
	struct intel_engine_cs *engine, *locked;

	/*
	 * Virtual engines complicate acquiring the engine timeline lock,
	 * as their rq->engine pointer is not stable until under that
	 * engine lock. The simple ploy we use is to take the lock then
	 * check that the rq still belongs to the newly locked engine.
	 */
	locked = READ_ONCE(rq->engine);
	spin_lock_irq(&locked->active.lock);
	while (unlikely(locked != (engine = READ_ONCE(rq->engine)))) {
		spin_unlock(&locked->active.lock);
		spin_lock(&engine->active.lock);
		locked = engine;
	}
	list_del(&rq->sched.link);
	spin_unlock_irq(&locked->active.lock);
}

bool i915_request_retire(struct i915_request *rq)
{
	if (!i915_request_completed(rq))
		return false;

	GEM_TRACE("%s fence %llx:%lld, current %d\n",
		  rq->engine->name,
		  rq->fence.context, rq->fence.seqno,
		  hwsp_seqno(rq));

	GEM_BUG_ON(!i915_sw_fence_signaled(&rq->submit));
	trace_i915_request_retire(rq);

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
	rq->ring->head = rq->postfix;

	/*
	 * We only loosely track inflight requests across preemption,
	 * and so we may find ourselves attempting to retire a _completed_
	 * request that we have removed from the HW and put back on a run
	 * queue.
	 */
	remove_from_engine(rq);

	spin_lock_irq(&rq->lock);
	i915_request_mark_complete(rq);
	if (!i915_request_signaled(rq))
		dma_fence_signal_locked(&rq->fence);
	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &rq->fence.flags))
		i915_request_cancel_breadcrumb(rq);
	if (i915_request_has_waitboost(rq)) {
		GEM_BUG_ON(!atomic_read(&rq->engine->gt->rps.num_waiters));
		atomic_dec(&rq->engine->gt->rps.num_waiters);
	}
	if (!test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags)) {
		set_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags);
		__notify_execute_cb(rq);
	}
	GEM_BUG_ON(!list_empty(&rq->execute_cb));
	spin_unlock_irq(&rq->lock);

	remove_from_client(rq);
	list_del(&rq->link);

	intel_context_exit(rq->hw_context);
	intel_context_unpin(rq->hw_context);

	free_capture_list(rq);
	i915_sched_node_fini(&rq->sched);
	i915_request_put(rq);

	return true;
}

void i915_request_retire_upto(struct i915_request *rq)
{
	struct intel_timeline * const tl = i915_request_timeline(rq);
	struct i915_request *tmp;

	GEM_TRACE("%s fence %llx:%lld, current %d\n",
		  rq->engine->name,
		  rq->fence.context, rq->fence.seqno,
		  hwsp_seqno(rq));

	GEM_BUG_ON(!i915_request_completed(rq));

	do {
		tmp = list_first_entry(&tl->requests, typeof(*tmp), link);
	} while (i915_request_retire(tmp) && tmp != rq);
}

static int
__await_execution(struct i915_request *rq,
		  struct i915_request *signal,
		  void (*hook)(struct i915_request *rq,
			       struct dma_fence *signal),
		  gfp_t gfp)
{
	struct execute_cb *cb;

	if (i915_request_is_active(signal)) {
		if (hook)
			hook(rq, &signal->fence);
		return 0;
	}

	cb = kmem_cache_alloc(global.slab_execute_cbs, gfp);
	if (!cb)
		return -ENOMEM;

	cb->fence = &rq->submit;
	i915_sw_fence_await(cb->fence);
	init_irq_work(&cb->work, irq_execute_cb);

	if (hook) {
		cb->hook = hook;
		cb->signal = i915_request_get(signal);
		cb->work.func = irq_execute_cb_hook;
	}

	spin_lock_irq(&signal->lock);
	if (i915_request_is_active(signal)) {
		if (hook) {
			hook(rq, &signal->fence);
			i915_request_put(signal);
		}
		i915_sw_fence_complete(cb->fence);
		kmem_cache_free(global.slab_execute_cbs, cb);
	} else {
		list_add_tail(&cb->link, &signal->execute_cb);
	}
	spin_unlock_irq(&signal->lock);

	/* Copy across semaphore status as we need the same behaviour */
	rq->sched.flags |= signal->sched.flags;
	return 0;
}

bool __i915_request_submit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	bool result = false;

	GEM_TRACE("%s fence %llx:%lld, current %d\n",
		  engine->name,
		  request->fence.context, request->fence.seqno,
		  hwsp_seqno(request));

	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&engine->active.lock);

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
	 * priority queue, under the active.lock. This ensures that the
	 * request has *not* yet been retired and we can safely move
	 * the request into the engine->active.list where it will be
	 * dropped upon retiring. (Otherwise if resubmit a *retired*
	 * request, this would be a horrible use-after-free.)
	 */
	if (i915_request_completed(request))
		goto xfer;

	if (i915_gem_context_is_banned(request->gem_context))
		i915_request_skip(request, -EIO);

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
	engine->serial++;
	result = true;

xfer:	/* We may be recursing from the signal callback of another i915 fence */
	spin_lock_nested(&request->lock, SINGLE_DEPTH_NESTING);

	if (!test_and_set_bit(I915_FENCE_FLAG_ACTIVE, &request->fence.flags))
		list_move_tail(&request->sched.link, &engine->active.requests);

	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &request->fence.flags) &&
	    !test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &request->fence.flags) &&
	    !i915_request_enable_breadcrumb(request))
		intel_engine_queue_breadcrumbs(engine);

	__notify_execute_cb(request);

	spin_unlock(&request->lock);

	return result;
}

void i915_request_submit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&engine->active.lock, flags);

	__i915_request_submit(request);

	spin_unlock_irqrestore(&engine->active.lock, flags);
}

void __i915_request_unsubmit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;

	GEM_TRACE("%s fence %llx:%lld, current %d\n",
		  engine->name,
		  request->fence.context, request->fence.seqno,
		  hwsp_seqno(request));

	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&engine->active.lock);

	/*
	 * Only unwind in reverse order, required so that the per-context list
	 * is kept in seqno/ring order.
	 */

	/* We may be recursing from the signal callback of another i915 fence */
	spin_lock_nested(&request->lock, SINGLE_DEPTH_NESTING);

	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &request->fence.flags))
		i915_request_cancel_breadcrumb(request);

	GEM_BUG_ON(!test_bit(I915_FENCE_FLAG_ACTIVE, &request->fence.flags));
	clear_bit(I915_FENCE_FLAG_ACTIVE, &request->fence.flags);

	spin_unlock(&request->lock);

	/* We've already spun, don't charge on resubmitting. */
	if (request->sched.semaphores && i915_request_started(request)) {
		request->sched.attr.priority |= I915_PRIORITY_NOSEMAPHORE;
		request->sched.semaphores = 0;
	}

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
	spin_lock_irqsave(&engine->active.lock, flags);

	__i915_request_unsubmit(request);

	spin_unlock_irqrestore(&engine->active.lock, flags);
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
			i915_request_skip(request, fence->error);

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
	struct i915_request *request =
		container_of(fence, typeof(*request), semaphore);

	switch (state) {
	case FENCE_COMPLETE:
		i915_schedule_bump_priority(request, I915_PRIORITY_NOSEMAPHORE);
		break;

	case FENCE_FREE:
		i915_request_put(request);
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
request_alloc_slow(struct intel_timeline *tl, gfp_t gfp)
{
	struct i915_request *rq;

	if (list_empty(&tl->requests))
		goto out;

	if (!gfpflags_allow_blocking(gfp))
		goto out;

	/* Move our oldest request to the slab-cache (if not in use!) */
	rq = list_first_entry(&tl->requests, typeof(*rq), link);
	i915_request_retire(rq);

	rq = kmem_cache_alloc(global.slab_requests,
			      gfp | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (rq)
		return rq;

	/* Ratelimit ourselves to prevent oom from malicious clients */
	rq = list_last_entry(&tl->requests, typeof(*rq), link);
	cond_synchronize_rcu(rq->rcustate);

	/* Retire our old requests in the hope that we free some */
	retire_requests(tl);

out:
	return kmem_cache_alloc(global.slab_requests, gfp);
}

struct i915_request *
__i915_request_create(struct intel_context *ce, gfp_t gfp)
{
	struct intel_timeline *tl = ce->timeline;
	struct i915_request *rq;
	u32 seqno;
	int ret;

	might_sleep_if(gfpflags_allow_blocking(gfp));

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
	rq = kmem_cache_alloc(global.slab_requests,
			      gfp | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (unlikely(!rq)) {
		rq = request_alloc_slow(tl, gfp);
		if (!rq) {
			ret = -ENOMEM;
			goto err_unreserve;
		}
	}

	ret = intel_timeline_get_seqno(tl, rq, &seqno);
	if (ret)
		goto err_free;

	rq->i915 = ce->engine->i915;
	rq->hw_context = ce;
	rq->gem_context = ce->gem_context;
	rq->engine = ce->engine;
	rq->ring = ce->ring;
	rq->execution_mask = ce->engine->mask;

	rcu_assign_pointer(rq->timeline, tl);
	rq->hwsp_seqno = tl->hwsp_seqno;
	rq->hwsp_cacheline = tl->hwsp_cacheline;

	rq->rcustate = get_state_synchronize_rcu(); /* acts as smp_mb() */

	spin_lock_init(&rq->lock);
	dma_fence_init(&rq->fence, &i915_fence_ops, &rq->lock,
		       tl->fence_context, seqno);

	/* We bump the ref for the fence chain */
	i915_sw_fence_init(&i915_request_get(rq)->submit, submit_notify);
	i915_sw_fence_init(&i915_request_get(rq)->semaphore, semaphore_notify);

	i915_sched_node_init(&rq->sched);

	/* No zalloc, must clear what we need by hand */
	rq->file_priv = NULL;
	rq->batch = NULL;
	rq->capture_list = NULL;
	rq->flags = 0;

	INIT_LIST_HEAD(&rq->execute_cb);

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
	return rq;

err_unwind:
	ce->ring->emit = rq->head;

	/* Make sure we didn't add ourselves to external state before freeing */
	GEM_BUG_ON(!list_empty(&rq->sched.signalers_list));
	GEM_BUG_ON(!list_empty(&rq->sched.waiters_list));

err_free:
	kmem_cache_free(global.slab_requests, rq);
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
	struct intel_timeline *tl;
	struct dma_fence *fence;
	int err;

	GEM_BUG_ON(i915_request_timeline(rq) ==
		   rcu_access_pointer(signal->timeline));

	rcu_read_lock();
	tl = rcu_dereference(signal->timeline);
	if (i915_request_started(signal) || !kref_get_unless_zero(&tl->kref))
		tl = NULL;
	rcu_read_unlock();
	if (!tl) /* already started or maybe even completed */
		return 0;

	fence = ERR_PTR(-EBUSY);
	if (mutex_trylock(&tl->mutex)) {
		fence = NULL;
		if (!i915_request_started(signal) &&
		    !list_is_first(&signal->link, &tl->requests)) {
			signal = list_prev_entry(signal, link);
			fence = dma_fence_get(&signal->fence);
		}
		mutex_unlock(&tl->mutex);
	}
	intel_timeline_put(tl);
	if (IS_ERR_OR_NULL(fence))
		return PTR_ERR_OR_ZERO(fence);

	err = 0;
	if (intel_timeline_sync_is_later(i915_request_timeline(rq), fence))
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
	return rq->sched.semaphores | rq->engine->saturated;
}

static int
__emit_semaphore_wait(struct i915_request *to,
		      struct i915_request *from,
		      u32 seqno)
{
	const int has_token = INTEL_GEN(to->i915) >= 12;
	u32 hwsp_offset;
	int len, err;
	u32 *cs;

	GEM_BUG_ON(INTEL_GEN(to->i915) < 8);

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
	/* Just emit the first semaphore we see as request space is limited. */
	if (already_busywaiting(to) & from->engine->mask)
		goto await_fence;

	if (i915_request_await_start(to, from) < 0)
		goto await_fence;

	/* Only submit our spinner after the signaler is running! */
	if (__await_execution(to, from, NULL, gfp))
		goto await_fence;

	if (__emit_semaphore_wait(to, from, from->fence.seqno))
		goto await_fence;

	to->sched.semaphores |= from->engine->mask;
	to->sched.flags |= I915_SCHED_HAS_SEMAPHORE_CHAIN;
	return 0;

await_fence:
	return i915_sw_fence_await_dma_fence(&to->submit,
					     &from->fence, 0,
					     I915_FENCE_GFP);
}

static int
i915_request_await_request(struct i915_request *to, struct i915_request *from)
{
	int ret;

	GEM_BUG_ON(to == from);
	GEM_BUG_ON(to->timeline == from->timeline);

	if (i915_request_completed(from))
		return 0;

	if (to->engine->schedule) {
		ret = i915_sched_node_add_dependency(&to->sched, &from->sched);
		if (ret < 0)
			return ret;
	}

	if (to->engine == from->engine) {
		ret = i915_sw_fence_await_sw_fence_gfp(&to->submit,
						       &from->submit,
						       I915_FENCE_GFP);
	} else if (intel_engine_has_semaphores(to->engine) &&
		   to->gem_context->sched.priority >= I915_PRIORITY_NORMAL) {
		ret = emit_semaphore_wait(to, from, I915_FENCE_GFP);
	} else {
		ret = i915_sw_fence_await_dma_fence(&to->submit,
						    &from->fence, 0,
						    I915_FENCE_GFP);
	}
	if (ret < 0)
		return ret;

	if (to->sched.flags & I915_SCHED_HAS_SEMAPHORE_CHAIN) {
		ret = i915_sw_fence_await_dma_fence(&to->semaphore,
						    &from->fence, 0,
						    I915_FENCE_GFP);
		if (ret < 0)
			return ret;
	}

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
			ret = i915_sw_fence_await_dma_fence(&rq->submit, fence,
							    fence->context ? I915_FENCE_TIMEOUT : 0,
							    I915_FENCE_GFP);
		if (ret < 0)
			return ret;

		/* Record the latest fence used against each timeline */
		if (fence->context)
			intel_timeline_sync_set(i915_request_timeline(rq),
						fence);
	} while (--nchild);

	return 0;
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
			       struct i915_request *from,
			       void (*hook)(struct i915_request *rq,
					    struct dma_fence *signal))
{
	int err;

	/* Submit both requests at the same time */
	err = __await_execution(to, from, hook, I915_FENCE_GFP);
	if (err)
		return err;

	/* Squash repeated depenendices to the same timelines */
	if (intel_timeline_sync_has_start(i915_request_timeline(to),
					  &from->fence))
		return 0;

	/* Ensure both start together [after all semaphores in signal] */
	if (intel_engine_has_semaphores(to->engine))
		err = __emit_semaphore_wait(to, from, from->fence.seqno - 1);
	else
		err = i915_request_await_start(to, from);
	if (err < 0)
		return err;

	/* Couple the dependency tree for PI on this exposed to->fence */
	if (to->engine->schedule) {
		err = i915_sched_node_add_dependency(&to->sched, &from->sched);
		if (err < 0)
			return err;
	}

	return intel_timeline_sync_set_start(i915_request_timeline(to),
					     &from->fence);
}

int
i915_request_await_execution(struct i915_request *rq,
			     struct dma_fence *fence,
			     void (*hook)(struct i915_request *rq,
					  struct dma_fence *signal))
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

		/*
		 * We don't squash repeated fence dependencies here as we
		 * want to run our callback in all cases.
		 */

		if (dma_fence_is_i915(fence))
			ret = __i915_request_await_execution(rq,
							     to_request(fence),
							     hook);
		else
			ret = i915_sw_fence_await_dma_fence(&rq->submit, fence,
							    I915_FENCE_TIMEOUT,
							    GFP_KERNEL);
		if (ret < 0)
			return ret;
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

		ret = dma_resv_get_fences_rcu(obj->base.resv,
							&excl, &count, &shared);
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
		excl = dma_resv_get_excl_rcu(obj->base.resv);
	}

	if (excl) {
		if (ret == 0)
			ret = i915_request_await_dma_fence(to, excl);

		dma_fence_put(excl);
	}

	return ret;
}

void i915_request_skip(struct i915_request *rq, int error)
{
	void *vaddr = rq->ring->vaddr;
	u32 head;

	GEM_BUG_ON(!IS_ERR_VALUE((long)error));
	dma_fence_set_error(&rq->fence, error);

	if (rq->infix == rq->postfix)
		return;

	/*
	 * As this request likely depends on state from the lost
	 * context, clear out all the user operations leaving the
	 * breadcrumb at the end (so we get the fence notifications).
	 */
	head = rq->infix;
	if (rq->postfix < head) {
		memset(vaddr + head, 0, rq->ring->size - head);
		head = 0;
	}
	memset(vaddr + head, 0, rq->postfix - head);
	rq->infix = rq->postfix;
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
	if (prev && !i915_request_completed(prev)) {
		if (is_power_of_2(prev->engine->mask | rq->engine->mask))
			i915_sw_fence_await_sw_fence(&rq->submit,
						     &prev->submit,
						     &rq->submitq);
		else
			__i915_sw_fence_await_dma_fence(&rq->submit,
							&prev->fence,
							&rq->dmaq);
		if (rq->engine->schedule)
			__i915_sched_node_add_dependency(&rq->sched,
							 &prev->sched,
							 &rq->dep,
							 0);
	}

	list_add_tail(&rq->link, &timeline->requests);

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

	GEM_TRACE("%s fence %llx:%lld\n",
		  engine->name, rq->fence.context, rq->fence.seqno);

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
	i915_sw_fence_commit(&rq->semaphore);
	if (attr && rq->engine->schedule)
		rq->engine->schedule(rq, attr);
	i915_sw_fence_commit(&rq->submit);
}

void i915_request_add(struct i915_request *rq)
{
	struct i915_sched_attr attr = rq->gem_context->sched;
	struct intel_timeline * const tl = i915_request_timeline(rq);
	struct i915_request *prev;

	lockdep_assert_held(&tl->mutex);
	lockdep_unpin_lock(&tl->mutex, rq->cookie);

	trace_i915_request_add(rq);

	prev = __i915_request_commit(rq);

	/*
	 * Boost actual workloads past semaphores!
	 *
	 * With semaphores we spin on one engine waiting for another,
	 * simply to reduce the latency of starting our work when
	 * the signaler completes. However, if there is any other
	 * work that we could be doing on this engine instead, that
	 * is better utilisation and will reduce the overall duration
	 * of the current work. To avoid PI boosting a semaphore
	 * far in the distance past over useful work, we keep a history
	 * of any semaphore use along our dependency chain.
	 */
	if (!(rq->sched.flags & I915_SCHED_HAS_SEMAPHORE_CHAIN))
		attr.priority |= I915_PRIORITY_NOSEMAPHORE;

	/*
	 * Boost priorities to new clients (new request flows).
	 *
	 * Allow interactive/synchronous clients to jump ahead of
	 * the bulk clients. (FQ_CODEL)
	 */
	if (list_empty(&rq->sched.signalers_list))
		attr.priority |= I915_PRIORITY_WAIT;

	local_bh_disable();
	__i915_request_queue(rq, &attr);
	local_bh_enable(); /* Kick the execlists tasklet if just scheduled */

	/*
	 * In typical scenarios, we do not expect the previous request on
	 * the timeline to be still tracked by timeline->last_request if it
	 * has been completed. If the completed request is still here, that
	 * implies that request retirement is a long way behind submission,
	 * suggesting that we haven't been retiring frequently enough from
	 * the combination of retire-before-alloc, waiters and the background
	 * retirement worker. So if the last request on this timeline was
	 * already completed, do a catch up pass, flushing the retirement queue
	 * up to this client. Since we have now moved the heaviest operations
	 * during retirement onto secondary workers, such as freeing objects
	 * or contexts, retiring a bunch of requests is mostly list management
	 * (and cache misses), and so we should not be overly penalizing this
	 * client by performing excess work, though we may still performing
	 * work on behalf of others -- but instead we should benefit from
	 * improved resource management. (Well, that's the theory at least.)
	 */
	if (prev &&
	    i915_request_completed(prev) &&
	    rcu_access_pointer(prev->timeline) == tl)
		i915_request_retire_upto(prev);

	mutex_unlock(&tl->mutex);
}

static unsigned long local_clock_us(unsigned int *cpu)
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
	t = local_clock() >> 10;
	put_cpu();

	return t;
}

static bool busywait_stop(unsigned long timeout, unsigned int cpu)
{
	unsigned int this_cpu;

	if (time_after(local_clock_us(&this_cpu), timeout))
		return true;

	return this_cpu != cpu;
}

static bool __i915_spin_request(const struct i915_request * const rq,
				int state, unsigned long timeout_us)
{
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

	timeout_us += local_clock_us(&cpu);
	do {
		if (i915_request_completed(rq))
			return true;

		if (signal_pending_state(state, current))
			break;

		if (busywait_stop(timeout_us, cpu))
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

	wake_up_process(wait->tsk);
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
	if (IS_ACTIVE(CONFIG_DRM_I915_SPIN_REQUEST) &&
	    __i915_spin_request(rq, state, CONFIG_DRM_I915_SPIN_REQUEST)) {
		dma_fence_signal(&rq->fence);
		goto out;
	}

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
	if (flags & I915_WAIT_PRIORITY) {
		if (!i915_request_started(rq) && INTEL_GEN(rq->i915) >= 6)
			intel_rps_boost(rq);
		i915_schedule_bump_priority(rq, I915_PRIORITY_WAIT);
	}

	wait.tsk = current;
	if (dma_fence_add_callback(&rq->fence, &wait.cb, request_wait_wake))
		goto out;

	for (;;) {
		set_current_state(state);

		if (i915_request_completed(rq)) {
			dma_fence_signal(&rq->fence);
			break;
		}

		if (signal_pending_state(state, current)) {
			timeout = -ERESTARTSYS;
			break;
		}

		if (!timeout) {
			timeout = -ETIME;
			break;
		}

		intel_engine_flush_submission(rq->engine);
		timeout = io_schedule_timeout(timeout);
	}
	__set_current_state(TASK_RUNNING);

	dma_fence_remove_callback(&rq->fence, &wait.cb);

out:
	mutex_release(&rq->engine->gt->reset.mutex.dep_map, _THIS_IP_);
	trace_i915_request_wait_end(rq);
	return timeout;
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_request.c"
#include "selftests/i915_request.c"
#endif

static void i915_global_request_shrink(void)
{
	kmem_cache_shrink(global.slab_dependencies);
	kmem_cache_shrink(global.slab_execute_cbs);
	kmem_cache_shrink(global.slab_requests);
}

static void i915_global_request_exit(void)
{
	kmem_cache_destroy(global.slab_dependencies);
	kmem_cache_destroy(global.slab_execute_cbs);
	kmem_cache_destroy(global.slab_requests);
}

static struct i915_global_request global = { {
	.shrink = i915_global_request_shrink,
	.exit = i915_global_request_exit,
} };

int __init i915_global_request_init(void)
{
	global.slab_requests = KMEM_CACHE(i915_request,
					  SLAB_HWCACHE_ALIGN |
					  SLAB_RECLAIM_ACCOUNT |
					  SLAB_TYPESAFE_BY_RCU);
	if (!global.slab_requests)
		return -ENOMEM;

	global.slab_execute_cbs = KMEM_CACHE(execute_cb,
					     SLAB_HWCACHE_ALIGN |
					     SLAB_RECLAIM_ACCOUNT |
					     SLAB_TYPESAFE_BY_RCU);
	if (!global.slab_execute_cbs)
		goto err_requests;

	global.slab_dependencies = KMEM_CACHE(i915_dependency,
					      SLAB_HWCACHE_ALIGN |
					      SLAB_RECLAIM_ACCOUNT);
	if (!global.slab_dependencies)
		goto err_execute_cbs;

	i915_global_register(&global.base);
	return 0;

err_execute_cbs:
	kmem_cache_destroy(global.slab_execute_cbs);
err_requests:
	kmem_cache_destroy(global.slab_requests);
	return -ENOMEM;
}
