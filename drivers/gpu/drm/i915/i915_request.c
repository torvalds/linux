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

#include <linux/prefetch.h>
#include <linux/dma-fence-array.h>
#include <linux/sched.h>
#include <linux/sched/clock.h>
#include <linux/sched/signal.h>

#include "i915_drv.h"

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

	return to_request(fence)->timeline->name;
}

static bool i915_fence_signaled(struct dma_fence *fence)
{
	return i915_request_completed(to_request(fence));
}

static bool i915_fence_enable_signaling(struct dma_fence *fence)
{
	return intel_engine_enable_signaling(to_request(fence), true);
}

static signed long i915_fence_wait(struct dma_fence *fence,
				   bool interruptible,
				   signed long timeout)
{
	return i915_request_wait(to_request(fence), interruptible, timeout);
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

	kmem_cache_free(rq->i915->requests, rq);
}

const struct dma_fence_ops i915_fence_ops = {
	.get_driver_name = i915_fence_get_driver_name,
	.get_timeline_name = i915_fence_get_timeline_name,
	.enable_signaling = i915_fence_enable_signaling,
	.signaled = i915_fence_signaled,
	.wait = i915_fence_wait,
	.release = i915_fence_release,
};

static inline void
i915_request_remove_from_client(struct i915_request *request)
{
	struct drm_i915_file_private *file_priv;

	file_priv = request->file_priv;
	if (!file_priv)
		return;

	spin_lock(&file_priv->mm.lock);
	if (request->file_priv) {
		list_del(&request->client_link);
		request->file_priv = NULL;
	}
	spin_unlock(&file_priv->mm.lock);
}

static int reset_all_global_seqno(struct drm_i915_private *i915, u32 seqno)
{
	struct intel_engine_cs *engine;
	struct i915_timeline *timeline;
	enum intel_engine_id id;
	int ret;

	/* Carefully retire all requests without writing to the rings */
	ret = i915_gem_wait_for_idle(i915,
				     I915_WAIT_INTERRUPTIBLE |
				     I915_WAIT_LOCKED,
				     MAX_SCHEDULE_TIMEOUT);
	if (ret)
		return ret;

	GEM_BUG_ON(i915->gt.active_requests);

	/* If the seqno wraps around, we need to clear the breadcrumb rbtree */
	for_each_engine(engine, i915, id) {
		GEM_TRACE("%s seqno %d (current %d) -> %d\n",
			  engine->name,
			  engine->timeline.seqno,
			  intel_engine_get_seqno(engine),
			  seqno);

		if (seqno == engine->timeline.seqno)
			continue;

		kthread_park(engine->breadcrumbs.signaler);

		if (!i915_seqno_passed(seqno, engine->timeline.seqno)) {
			/* Flush any waiters before we reuse the seqno */
			intel_engine_disarm_breadcrumbs(engine);
			intel_engine_init_hangcheck(engine);
			GEM_BUG_ON(!list_empty(&engine->breadcrumbs.signals));
		}

		/* Check we are idle before we fiddle with hw state! */
		GEM_BUG_ON(!intel_engine_is_idle(engine));
		GEM_BUG_ON(i915_gem_active_isset(&engine->timeline.last_request));

		/* Finally reset hw state */
		intel_engine_init_global_seqno(engine, seqno);
		engine->timeline.seqno = seqno;

		kthread_unpark(engine->breadcrumbs.signaler);
	}

	list_for_each_entry(timeline, &i915->gt.timelines, link)
		memset(timeline->global_sync, 0, sizeof(timeline->global_sync));

	i915->gt.request_serial = seqno;

	return 0;
}

int i915_gem_set_global_seqno(struct drm_device *dev, u32 seqno)
{
	struct drm_i915_private *i915 = to_i915(dev);

	lockdep_assert_held(&i915->drm.struct_mutex);

	if (seqno == 0)
		return -EINVAL;

	/* HWS page needs to be set less than what we will inject to ring */
	return reset_all_global_seqno(i915, seqno - 1);
}

static int reserve_gt(struct drm_i915_private *i915)
{
	int ret;

	/*
	 * Reservation is fine until we may need to wrap around
	 *
	 * By incrementing the serial for every request, we know that no
	 * individual engine may exceed that serial (as each is reset to 0
	 * on any wrap). This protects even the most pessimistic of migrations
	 * of every request from all engines onto just one.
	 */
	while (unlikely(++i915->gt.request_serial == 0)) {
		ret = reset_all_global_seqno(i915, 0);
		if (ret) {
			i915->gt.request_serial--;
			return ret;
		}
	}

	if (!i915->gt.active_requests++)
		i915_gem_unpark(i915);

	return 0;
}

static void unreserve_gt(struct drm_i915_private *i915)
{
	GEM_BUG_ON(!i915->gt.active_requests);
	if (!--i915->gt.active_requests)
		i915_gem_park(i915);
}

void i915_gem_retire_noop(struct i915_gem_active *active,
			  struct i915_request *request)
{
	/* Space left intentionally blank */
}

static void advance_ring(struct i915_request *request)
{
	struct intel_ring *ring = request->ring;
	unsigned int tail;

	/*
	 * We know the GPU must have read the request to have
	 * sent us the seqno + interrupt, so use the position
	 * of tail of the request to update the last known position
	 * of the GPU head.
	 *
	 * Note this requires that we are always called in request
	 * completion order.
	 */
	GEM_BUG_ON(!list_is_first(&request->ring_link, &ring->request_list));
	if (list_is_last(&request->ring_link, &ring->request_list)) {
		/*
		 * We may race here with execlists resubmitting this request
		 * as we retire it. The resubmission will move the ring->tail
		 * forwards (to request->wa_tail). We either read the
		 * current value that was written to hw, or the value that
		 * is just about to be. Either works, if we miss the last two
		 * noops - they are safe to be replayed on a reset.
		 */
		GEM_TRACE("marking %s as inactive\n", ring->timeline->name);
		tail = READ_ONCE(request->tail);
		list_del(&ring->active_link);
	} else {
		tail = request->postfix;
	}
	list_del_init(&request->ring_link);

	ring->head = tail;
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

static void __retire_engine_request(struct intel_engine_cs *engine,
				    struct i915_request *rq)
{
	GEM_TRACE("%s(%s) fence %llx:%d, global=%d, current %d\n",
		  __func__, engine->name,
		  rq->fence.context, rq->fence.seqno,
		  rq->global_seqno,
		  intel_engine_get_seqno(engine));

	GEM_BUG_ON(!i915_request_completed(rq));

	local_irq_disable();

	spin_lock(&engine->timeline.lock);
	GEM_BUG_ON(!list_is_first(&rq->link, &engine->timeline.requests));
	list_del_init(&rq->link);
	spin_unlock(&engine->timeline.lock);

	spin_lock(&rq->lock);
	if (!test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &rq->fence.flags))
		dma_fence_signal_locked(&rq->fence);
	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &rq->fence.flags))
		intel_engine_cancel_signaling(rq);
	if (rq->waitboost) {
		GEM_BUG_ON(!atomic_read(&rq->i915->gt_pm.rps.num_waiters));
		atomic_dec(&rq->i915->gt_pm.rps.num_waiters);
	}
	spin_unlock(&rq->lock);

	local_irq_enable();

	/*
	 * The backing object for the context is done after switching to the
	 * *next* context. Therefore we cannot retire the previous context until
	 * the next context has already started running. However, since we
	 * cannot take the required locks at i915_request_submit() we
	 * defer the unpinning of the active context to now, retirement of
	 * the subsequent request.
	 */
	if (engine->last_retired_context)
		intel_context_unpin(engine->last_retired_context);
	engine->last_retired_context = rq->hw_context;
}

static void __retire_engine_upto(struct intel_engine_cs *engine,
				 struct i915_request *rq)
{
	struct i915_request *tmp;

	if (list_empty(&rq->link))
		return;

	do {
		tmp = list_first_entry(&engine->timeline.requests,
				       typeof(*tmp), link);

		GEM_BUG_ON(tmp->engine != engine);
		__retire_engine_request(engine, tmp);
	} while (tmp != rq);
}

static void i915_request_retire(struct i915_request *request)
{
	struct i915_gem_active *active, *next;

	GEM_TRACE("%s fence %llx:%d, global=%d, current %d\n",
		  request->engine->name,
		  request->fence.context, request->fence.seqno,
		  request->global_seqno,
		  intel_engine_get_seqno(request->engine));

	lockdep_assert_held(&request->i915->drm.struct_mutex);
	GEM_BUG_ON(!i915_sw_fence_signaled(&request->submit));
	GEM_BUG_ON(!i915_request_completed(request));

	trace_i915_request_retire(request);

	advance_ring(request);
	free_capture_list(request);

	/*
	 * Walk through the active list, calling retire on each. This allows
	 * objects to track their GPU activity and mark themselves as idle
	 * when their *last* active request is completed (updating state
	 * tracking lists for eviction, active references for GEM, etc).
	 *
	 * As the ->retire() may free the node, we decouple it first and
	 * pass along the auxiliary information (to avoid dereferencing
	 * the node after the callback).
	 */
	list_for_each_entry_safe(active, next, &request->active_list, link) {
		/*
		 * In microbenchmarks or focusing upon time inside the kernel,
		 * we may spend an inordinate amount of time simply handling
		 * the retirement of requests and processing their callbacks.
		 * Of which, this loop itself is particularly hot due to the
		 * cache misses when jumping around the list of i915_gem_active.
		 * So we try to keep this loop as streamlined as possible and
		 * also prefetch the next i915_gem_active to try and hide
		 * the likely cache miss.
		 */
		prefetchw(next);

		INIT_LIST_HEAD(&active->link);
		RCU_INIT_POINTER(active->request, NULL);

		active->retire(active, request);
	}

	i915_request_remove_from_client(request);

	/* Retirement decays the ban score as it is a sign of ctx progress */
	atomic_dec_if_positive(&request->gem_context->ban_score);
	intel_context_unpin(request->hw_context);

	__retire_engine_upto(request->engine, request);

	unreserve_gt(request->i915);

	i915_sched_node_fini(request->i915, &request->sched);
	i915_request_put(request);
}

void i915_request_retire_upto(struct i915_request *rq)
{
	struct intel_ring *ring = rq->ring;
	struct i915_request *tmp;

	GEM_TRACE("%s fence %llx:%d, global=%d, current %d\n",
		  rq->engine->name,
		  rq->fence.context, rq->fence.seqno,
		  rq->global_seqno,
		  intel_engine_get_seqno(rq->engine));

	lockdep_assert_held(&rq->i915->drm.struct_mutex);
	GEM_BUG_ON(!i915_request_completed(rq));

	if (list_empty(&rq->ring_link))
		return;

	do {
		tmp = list_first_entry(&ring->request_list,
				       typeof(*tmp), ring_link);

		i915_request_retire(tmp);
	} while (tmp != rq);
}

static u32 timeline_get_seqno(struct i915_timeline *tl)
{
	return ++tl->seqno;
}

static void move_to_timeline(struct i915_request *request,
			     struct i915_timeline *timeline)
{
	GEM_BUG_ON(request->timeline == &request->engine->timeline);
	lockdep_assert_held(&request->engine->timeline.lock);

	spin_lock(&request->timeline->lock);
	list_move_tail(&request->link, &timeline->requests);
	spin_unlock(&request->timeline->lock);
}

void __i915_request_submit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	u32 seqno;

	GEM_TRACE("%s fence %llx:%d -> global=%d, current %d\n",
		  engine->name,
		  request->fence.context, request->fence.seqno,
		  engine->timeline.seqno + 1,
		  intel_engine_get_seqno(engine));

	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&engine->timeline.lock);

	GEM_BUG_ON(request->global_seqno);

	seqno = timeline_get_seqno(&engine->timeline);
	GEM_BUG_ON(!seqno);
	GEM_BUG_ON(intel_engine_signaled(engine, seqno));

	/* We may be recursing from the signal callback of another i915 fence */
	spin_lock_nested(&request->lock, SINGLE_DEPTH_NESTING);
	request->global_seqno = seqno;
	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &request->fence.flags))
		intel_engine_enable_signaling(request, false);
	spin_unlock(&request->lock);

	engine->emit_breadcrumb(request,
				request->ring->vaddr + request->postfix);

	/* Transfer from per-context onto the global per-engine timeline */
	move_to_timeline(request, &engine->timeline);

	trace_i915_request_execute(request);

	wake_up_all(&request->execute);
}

void i915_request_submit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	unsigned long flags;

	/* Will be called from irq-context when using foreign fences. */
	spin_lock_irqsave(&engine->timeline.lock, flags);

	__i915_request_submit(request);

	spin_unlock_irqrestore(&engine->timeline.lock, flags);
}

void __i915_request_unsubmit(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;

	GEM_TRACE("%s fence %llx:%d <- global=%d, current %d\n",
		  engine->name,
		  request->fence.context, request->fence.seqno,
		  request->global_seqno,
		  intel_engine_get_seqno(engine));

	GEM_BUG_ON(!irqs_disabled());
	lockdep_assert_held(&engine->timeline.lock);

	/*
	 * Only unwind in reverse order, required so that the per-context list
	 * is kept in seqno/ring order.
	 */
	GEM_BUG_ON(!request->global_seqno);
	GEM_BUG_ON(request->global_seqno != engine->timeline.seqno);
	GEM_BUG_ON(intel_engine_has_completed(engine, request->global_seqno));
	engine->timeline.seqno--;

	/* We may be recursing from the signal callback of another i915 fence */
	spin_lock_nested(&request->lock, SINGLE_DEPTH_NESTING);
	request->global_seqno = 0;
	if (test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &request->fence.flags))
		intel_engine_cancel_signaling(request);
	spin_unlock(&request->lock);

	/* Transfer back from the global per-engine timeline to per-context */
	move_to_timeline(request, request->timeline);

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
	spin_lock_irqsave(&engine->timeline.lock, flags);

	__i915_request_unsubmit(request);

	spin_unlock_irqrestore(&engine->timeline.lock, flags);
}

static int __i915_sw_fence_call
submit_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	struct i915_request *request =
		container_of(fence, typeof(*request), submit);

	switch (state) {
	case FENCE_COMPLETE:
		trace_i915_request_submit(request);
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

/**
 * i915_request_alloc - allocate a request structure
 *
 * @engine: engine that we wish to issue the request on.
 * @ctx: context that the request will be associated with.
 *
 * Returns a pointer to the allocated request if successful,
 * or an error code if not.
 */
struct i915_request *
i915_request_alloc(struct intel_engine_cs *engine, struct i915_gem_context *ctx)
{
	struct drm_i915_private *i915 = engine->i915;
	struct i915_request *rq;
	struct intel_context *ce;
	int ret;

	lockdep_assert_held(&i915->drm.struct_mutex);

	/*
	 * Preempt contexts are reserved for exclusive use to inject a
	 * preemption context switch. They are never to be used for any trivial
	 * request!
	 */
	GEM_BUG_ON(ctx == i915->preempt_context);

	/*
	 * ABI: Before userspace accesses the GPU (e.g. execbuffer), report
	 * EIO if the GPU is already wedged.
	 */
	if (i915_terminally_wedged(&i915->gpu_error))
		return ERR_PTR(-EIO);

	/*
	 * Pinning the contexts may generate requests in order to acquire
	 * GGTT space, so do this first before we reserve a seqno for
	 * ourselves.
	 */
	ce = intel_context_pin(ctx, engine);
	if (IS_ERR(ce))
		return ERR_CAST(ce);

	ret = reserve_gt(i915);
	if (ret)
		goto err_unpin;

	ret = intel_ring_wait_for_space(ce->ring, MIN_SPACE_FOR_ADD_REQUEST);
	if (ret)
		goto err_unreserve;

	/* Move our oldest request to the slab-cache (if not in use!) */
	rq = list_first_entry(&ce->ring->request_list, typeof(*rq), ring_link);
	if (!list_is_last(&rq->ring_link, &ce->ring->request_list) &&
	    i915_request_completed(rq))
		i915_request_retire(rq);

	/*
	 * Beware: Dragons be flying overhead.
	 *
	 * We use RCU to look up requests in flight. The lookups may
	 * race with the request being allocated from the slab freelist.
	 * That is the request we are writing to here, may be in the process
	 * of being read by __i915_gem_active_get_rcu(). As such,
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
	rq = kmem_cache_alloc(i915->requests,
			      GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (unlikely(!rq)) {
		i915_retire_requests(i915);

		/* Ratelimit ourselves to prevent oom from malicious clients */
		rq = i915_gem_active_raw(&ce->ring->timeline->last_request,
					 &i915->drm.struct_mutex);
		if (rq)
			cond_synchronize_rcu(rq->rcustate);

		rq = kmem_cache_alloc(i915->requests, GFP_KERNEL);
		if (!rq) {
			ret = -ENOMEM;
			goto err_unreserve;
		}
	}

	rq->rcustate = get_state_synchronize_rcu();

	INIT_LIST_HEAD(&rq->active_list);
	rq->i915 = i915;
	rq->engine = engine;
	rq->gem_context = ctx;
	rq->hw_context = ce;
	rq->ring = ce->ring;
	rq->timeline = ce->ring->timeline;
	GEM_BUG_ON(rq->timeline == &engine->timeline);

	spin_lock_init(&rq->lock);
	dma_fence_init(&rq->fence,
		       &i915_fence_ops,
		       &rq->lock,
		       rq->timeline->fence_context,
		       timeline_get_seqno(rq->timeline));

	/* We bump the ref for the fence chain */
	i915_sw_fence_init(&i915_request_get(rq)->submit, submit_notify);
	init_waitqueue_head(&rq->execute);

	i915_sched_node_init(&rq->sched);

	/* No zalloc, must clear what we need by hand */
	rq->global_seqno = 0;
	rq->signaling.wait.seqno = 0;
	rq->file_priv = NULL;
	rq->batch = NULL;
	rq->capture_list = NULL;
	rq->waitboost = false;

	/*
	 * Reserve space in the ring buffer for all the commands required to
	 * eventually emit this request. This is to guarantee that the
	 * i915_request_add() call can't fail. Note that the reserve may need
	 * to be redone if the request is not actually submitted straight
	 * away, e.g. because a GPU scheduler has deferred it.
	 */
	rq->reserved_space = MIN_SPACE_FOR_ADD_REQUEST;
	GEM_BUG_ON(rq->reserved_space < engine->emit_breadcrumb_sz);

	/*
	 * Record the position of the start of the request so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the head.
	 */
	rq->head = rq->ring->emit;

	ret = engine->request_alloc(rq);
	if (ret)
		goto err_unwind;

	/* Keep a second pin for the dual retirement along engine and ring */
	__intel_context_pin(ce);

	rq->infix = rq->ring->emit; /* end of header; start of user payload */

	/* Check that we didn't interrupt ourselves with a new request */
	GEM_BUG_ON(rq->timeline->seqno != rq->fence.seqno);
	return rq;

err_unwind:
	ce->ring->emit = rq->head;

	/* Make sure we didn't add ourselves to external state before freeing */
	GEM_BUG_ON(!list_empty(&rq->active_list));
	GEM_BUG_ON(!list_empty(&rq->sched.signalers_list));
	GEM_BUG_ON(!list_empty(&rq->sched.waiters_list));

	kmem_cache_free(i915->requests, rq);
err_unreserve:
	unreserve_gt(i915);
err_unpin:
	intel_context_unpin(ce);
	return ERR_PTR(ret);
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
		ret = i915_sched_node_add_dependency(to->i915,
						     &to->sched,
						     &from->sched);
		if (ret < 0)
			return ret;
	}

	if (to->engine == from->engine) {
		ret = i915_sw_fence_await_sw_fence_gfp(&to->submit,
						       &from->submit,
						       I915_FENCE_GFP);
		return ret < 0 ? ret : 0;
	}

	if (to->engine->semaphore.sync_to) {
		u32 seqno;

		GEM_BUG_ON(!from->engine->semaphore.signal);

		seqno = i915_request_global_seqno(from);
		if (!seqno)
			goto await_dma_fence;

		if (seqno <= to->timeline->global_sync[from->engine->id])
			return 0;

		trace_i915_gem_ring_sync_to(to, from);
		ret = to->engine->semaphore.sync_to(to, from);
		if (ret)
			return ret;

		to->timeline->global_sync[from->engine->id] = seqno;
		return 0;
	}

await_dma_fence:
	ret = i915_sw_fence_await_dma_fence(&to->submit,
					    &from->fence, 0,
					    I915_FENCE_GFP);
	return ret < 0 ? ret : 0;
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
		if (fence->context != rq->i915->mm.unordered_timeline &&
		    i915_timeline_sync_is_later(rq->timeline, fence))
			continue;

		if (dma_fence_is_i915(fence))
			ret = i915_request_await_request(rq, to_request(fence));
		else
			ret = i915_sw_fence_await_dma_fence(&rq->submit, fence,
							    I915_FENCE_TIMEOUT,
							    I915_FENCE_GFP);
		if (ret < 0)
			return ret;

		/* Record the latest fence used against each timeline */
		if (fence->context != rq->i915->mm.unordered_timeline)
			i915_timeline_sync_set(rq->timeline, fence);
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

		ret = reservation_object_get_fences_rcu(obj->resv,
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
		excl = reservation_object_get_excl_rcu(obj->resv);
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
}

/*
 * NB: This function is not allowed to fail. Doing so would mean the the
 * request is not being tracked for completion but the work itself is
 * going to happen on the hardware. This would be a Bad Thing(tm).
 */
void i915_request_add(struct i915_request *request)
{
	struct intel_engine_cs *engine = request->engine;
	struct i915_timeline *timeline = request->timeline;
	struct intel_ring *ring = request->ring;
	struct i915_request *prev;
	u32 *cs;

	GEM_TRACE("%s fence %llx:%d\n",
		  engine->name, request->fence.context, request->fence.seqno);

	lockdep_assert_held(&request->i915->drm.struct_mutex);
	trace_i915_request_add(request);

	/*
	 * Make sure that no request gazumped us - if it was allocated after
	 * our i915_request_alloc() and called __i915_request_add() before
	 * us, the timeline will hold its seqno which is later than ours.
	 */
	GEM_BUG_ON(timeline->seqno != request->fence.seqno);

	/*
	 * To ensure that this call will not fail, space for its emissions
	 * should already have been reserved in the ring buffer. Let the ring
	 * know that it is time to use that space up.
	 */
	request->reserved_space = 0;
	engine->emit_flush(request, EMIT_FLUSH);

	/*
	 * Record the position of the start of the breadcrumb so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the ring's HEAD.
	 */
	cs = intel_ring_begin(request, engine->emit_breadcrumb_sz);
	GEM_BUG_ON(IS_ERR(cs));
	request->postfix = intel_ring_offset(request, cs);

	/*
	 * Seal the request and mark it as pending execution. Note that
	 * we may inspect this state, without holding any locks, during
	 * hangcheck. Hence we apply the barrier to ensure that we do not
	 * see a more recent value in the hws than we are tracking.
	 */

	prev = i915_gem_active_raw(&timeline->last_request,
				   &request->i915->drm.struct_mutex);
	if (prev && !i915_request_completed(prev)) {
		i915_sw_fence_await_sw_fence(&request->submit, &prev->submit,
					     &request->submitq);
		if (engine->schedule)
			__i915_sched_node_add_dependency(&request->sched,
							 &prev->sched,
							 &request->dep,
							 0);
	}

	spin_lock_irq(&timeline->lock);
	list_add_tail(&request->link, &timeline->requests);
	spin_unlock_irq(&timeline->lock);

	GEM_BUG_ON(timeline->seqno != request->fence.seqno);
	i915_gem_active_set(&timeline->last_request, request);

	list_add_tail(&request->ring_link, &ring->request_list);
	if (list_is_first(&request->ring_link, &ring->request_list)) {
		GEM_TRACE("marking %s as active\n", ring->timeline->name);
		list_add(&ring->active_link, &request->i915->gt.active_rings);
	}
	request->emitted_jiffies = jiffies;

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
	local_bh_disable();
	rcu_read_lock(); /* RCU serialisation for set-wedged protection */
	if (engine->schedule) {
		struct i915_sched_attr attr = request->gem_context->sched;

		/*
		 * Boost priorities to new clients (new request flows).
		 *
		 * Allow interactive/synchronous clients to jump ahead of
		 * the bulk clients. (FQ_CODEL)
		 */
		if (!prev || i915_request_completed(prev))
			attr.priority |= I915_PRIORITY_NEWCLIENT;

		engine->schedule(request, &attr);
	}
	rcu_read_unlock();
	i915_sw_fence_commit(&request->submit);
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
	if (prev && i915_request_completed(prev))
		i915_request_retire_upto(prev);
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

static bool __i915_spin_request(const struct i915_request *rq,
				u32 seqno, int state, unsigned long timeout_us)
{
	struct intel_engine_cs *engine = rq->engine;
	unsigned int irq, cpu;

	GEM_BUG_ON(!seqno);

	/*
	 * Only wait for the request if we know it is likely to complete.
	 *
	 * We don't track the timestamps around requests, nor the average
	 * request length, so we do not have a good indicator that this
	 * request will complete within the timeout. What we do know is the
	 * order in which requests are executed by the engine and so we can
	 * tell if the request has started. If the request hasn't started yet,
	 * it is a fair assumption that it will not complete within our
	 * relatively short timeout.
	 */
	if (!intel_engine_has_started(engine, seqno))
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

	irq = READ_ONCE(engine->breadcrumbs.irq_count);
	timeout_us += local_clock_us(&cpu);
	do {
		if (intel_engine_has_completed(engine, seqno))
			return seqno == i915_request_global_seqno(rq);

		/*
		 * Seqno are meant to be ordered *before* the interrupt. If
		 * we see an interrupt without a corresponding seqno advance,
		 * assume we won't see one in the near future but require
		 * the engine->seqno_barrier() to fixup coherency.
		 */
		if (READ_ONCE(engine->breadcrumbs.irq_count) != irq)
			break;

		if (signal_pending_state(state, current))
			break;

		if (busywait_stop(timeout_us, cpu))
			break;

		cpu_relax();
	} while (!need_resched());

	return false;
}

static bool __i915_wait_request_check_and_reset(struct i915_request *request)
{
	struct i915_gpu_error *error = &request->i915->gpu_error;

	if (likely(!i915_reset_handoff(error)))
		return false;

	__set_current_state(TASK_RUNNING);
	i915_reset(request->i915, error->stalled_mask, error->reason);
	return true;
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
 * If the caller holds the struct_mutex, the caller must pass I915_WAIT_LOCKED
 * in via the flags, and vice versa if the struct_mutex is not held, the caller
 * must not specify that the wait is locked.
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
	wait_queue_head_t *errq = &rq->i915->gpu_error.wait_queue;
	DEFINE_WAIT_FUNC(reset, default_wake_function);
	DEFINE_WAIT_FUNC(exec, default_wake_function);
	struct intel_wait wait;

	might_sleep();
#if IS_ENABLED(CONFIG_LOCKDEP)
	GEM_BUG_ON(debug_locks &&
		   !!lockdep_is_held(&rq->i915->drm.struct_mutex) !=
		   !!(flags & I915_WAIT_LOCKED));
#endif
	GEM_BUG_ON(timeout < 0);

	if (i915_request_completed(rq))
		return timeout;

	if (!timeout)
		return -ETIME;

	trace_i915_request_wait_begin(rq, flags);

	add_wait_queue(&rq->execute, &exec);
	if (flags & I915_WAIT_LOCKED)
		add_wait_queue(errq, &reset);

	intel_wait_init(&wait);
	if (flags & I915_WAIT_PRIORITY)
		i915_schedule_bump_priority(rq, I915_PRIORITY_WAIT);

restart:
	do {
		set_current_state(state);
		if (intel_wait_update_request(&wait, rq))
			break;

		if (flags & I915_WAIT_LOCKED &&
		    __i915_wait_request_check_and_reset(rq))
			continue;

		if (signal_pending_state(state, current)) {
			timeout = -ERESTARTSYS;
			goto complete;
		}

		if (!timeout) {
			timeout = -ETIME;
			goto complete;
		}

		timeout = io_schedule_timeout(timeout);
	} while (1);

	GEM_BUG_ON(!intel_wait_has_seqno(&wait));
	GEM_BUG_ON(!i915_sw_fence_signaled(&rq->submit));

	/* Optimistic short spin before touching IRQs */
	if (__i915_spin_request(rq, wait.seqno, state, 5))
		goto complete;

	set_current_state(state);
	if (intel_engine_add_wait(rq->engine, &wait))
		/*
		 * In order to check that we haven't missed the interrupt
		 * as we enabled it, we need to kick ourselves to do a
		 * coherent check on the seqno before we sleep.
		 */
		goto wakeup;

	if (flags & I915_WAIT_LOCKED)
		__i915_wait_request_check_and_reset(rq);

	for (;;) {
		if (signal_pending_state(state, current)) {
			timeout = -ERESTARTSYS;
			break;
		}

		if (!timeout) {
			timeout = -ETIME;
			break;
		}

		timeout = io_schedule_timeout(timeout);

		if (intel_wait_complete(&wait) &&
		    intel_wait_check_request(&wait, rq))
			break;

		set_current_state(state);

wakeup:
		/*
		 * Carefully check if the request is complete, giving time
		 * for the seqno to be visible following the interrupt.
		 * We also have to check in case we are kicked by the GPU
		 * reset in order to drop the struct_mutex.
		 */
		if (__i915_request_irq_complete(rq))
			break;

		/*
		 * If the GPU is hung, and we hold the lock, reset the GPU
		 * and then check for completion. On a full reset, the engine's
		 * HW seqno will be advanced passed us and we are complete.
		 * If we do a partial reset, we have to wait for the GPU to
		 * resume and update the breadcrumb.
		 *
		 * If we don't hold the mutex, we can just wait for the worker
		 * to come along and update the breadcrumb (either directly
		 * itself, or indirectly by recovering the GPU).
		 */
		if (flags & I915_WAIT_LOCKED &&
		    __i915_wait_request_check_and_reset(rq))
			continue;

		/* Only spin if we know the GPU is processing this request */
		if (__i915_spin_request(rq, wait.seqno, state, 2))
			break;

		if (!intel_wait_check_request(&wait, rq)) {
			intel_engine_remove_wait(rq->engine, &wait);
			goto restart;
		}
	}

	intel_engine_remove_wait(rq->engine, &wait);
complete:
	__set_current_state(TASK_RUNNING);
	if (flags & I915_WAIT_LOCKED)
		remove_wait_queue(errq, &reset);
	remove_wait_queue(&rq->execute, &exec);
	trace_i915_request_wait_end(rq);

	return timeout;
}

static void ring_retire_requests(struct intel_ring *ring)
{
	struct i915_request *request, *next;

	list_for_each_entry_safe(request, next,
				 &ring->request_list, ring_link) {
		if (!i915_request_completed(request))
			break;

		i915_request_retire(request);
	}
}

void i915_retire_requests(struct drm_i915_private *i915)
{
	struct intel_ring *ring, *tmp;

	lockdep_assert_held(&i915->drm.struct_mutex);

	if (!i915->gt.active_requests)
		return;

	list_for_each_entry_safe(ring, tmp, &i915->gt.active_rings, active_link)
		ring_retire_requests(ring);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_request.c"
#include "selftests/i915_request.c"
#endif
