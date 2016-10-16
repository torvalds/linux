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

#include "i915_drv.h"

static const char *i915_fence_get_driver_name(struct fence *fence)
{
	return "i915";
}

static const char *i915_fence_get_timeline_name(struct fence *fence)
{
	/* Timelines are bound by eviction to a VM. However, since
	 * we only have a global seqno at the moment, we only have
	 * a single timeline. Note that each timeline will have
	 * multiple execution contexts (fence contexts) as we allow
	 * engines within a single timeline to execute in parallel.
	 */
	return "global";
}

static bool i915_fence_signaled(struct fence *fence)
{
	return i915_gem_request_completed(to_request(fence));
}

static bool i915_fence_enable_signaling(struct fence *fence)
{
	if (i915_fence_signaled(fence))
		return false;

	intel_engine_enable_signaling(to_request(fence));
	return true;
}

static signed long i915_fence_wait(struct fence *fence,
				   bool interruptible,
				   signed long timeout_jiffies)
{
	s64 timeout_ns, *timeout;
	int ret;

	if (timeout_jiffies != MAX_SCHEDULE_TIMEOUT) {
		timeout_ns = jiffies_to_nsecs(timeout_jiffies);
		timeout = &timeout_ns;
	} else {
		timeout = NULL;
	}

	ret = i915_wait_request(to_request(fence),
				interruptible, timeout,
				NO_WAITBOOST);
	if (ret == -ETIME)
		return 0;

	if (ret < 0)
		return ret;

	if (timeout_jiffies != MAX_SCHEDULE_TIMEOUT)
		timeout_jiffies = nsecs_to_jiffies(timeout_ns);

	return timeout_jiffies;
}

static void i915_fence_value_str(struct fence *fence, char *str, int size)
{
	snprintf(str, size, "%u", fence->seqno);
}

static void i915_fence_timeline_value_str(struct fence *fence, char *str,
					  int size)
{
	snprintf(str, size, "%u",
		 intel_engine_get_seqno(to_request(fence)->engine));
}

static void i915_fence_release(struct fence *fence)
{
	struct drm_i915_gem_request *req = to_request(fence);

	kmem_cache_free(req->i915->requests, req);
}

const struct fence_ops i915_fence_ops = {
	.get_driver_name = i915_fence_get_driver_name,
	.get_timeline_name = i915_fence_get_timeline_name,
	.enable_signaling = i915_fence_enable_signaling,
	.signaled = i915_fence_signaled,
	.wait = i915_fence_wait,
	.release = i915_fence_release,
	.fence_value_str = i915_fence_value_str,
	.timeline_value_str = i915_fence_timeline_value_str,
};

int i915_gem_request_add_to_client(struct drm_i915_gem_request *req,
				   struct drm_file *file)
{
	struct drm_i915_private *dev_private;
	struct drm_i915_file_private *file_priv;

	WARN_ON(!req || !file || req->file_priv);

	if (!req || !file)
		return -EINVAL;

	if (req->file_priv)
		return -EINVAL;

	dev_private = req->i915;
	file_priv = file->driver_priv;

	spin_lock(&file_priv->mm.lock);
	req->file_priv = file_priv;
	list_add_tail(&req->client_list, &file_priv->mm.request_list);
	spin_unlock(&file_priv->mm.lock);

	return 0;
}

static inline void
i915_gem_request_remove_from_client(struct drm_i915_gem_request *request)
{
	struct drm_i915_file_private *file_priv = request->file_priv;

	if (!file_priv)
		return;

	spin_lock(&file_priv->mm.lock);
	list_del(&request->client_list);
	request->file_priv = NULL;
	spin_unlock(&file_priv->mm.lock);
}

void i915_gem_retire_noop(struct i915_gem_active *active,
			  struct drm_i915_gem_request *request)
{
	/* Space left intentionally blank */
}

static void i915_gem_request_retire(struct drm_i915_gem_request *request)
{
	struct i915_gem_active *active, *next;

	trace_i915_gem_request_retire(request);
	list_del(&request->link);

	/* We know the GPU must have read the request to have
	 * sent us the seqno + interrupt, so use the position
	 * of tail of the request to update the last known position
	 * of the GPU head.
	 *
	 * Note this requires that we are always called in request
	 * completion order.
	 */
	list_del(&request->ring_link);
	request->ring->last_retired_head = request->postfix;

	/* Walk through the active list, calling retire on each. This allows
	 * objects to track their GPU activity and mark themselves as idle
	 * when their *last* active request is completed (updating state
	 * tracking lists for eviction, active references for GEM, etc).
	 *
	 * As the ->retire() may free the node, we decouple it first and
	 * pass along the auxiliary information (to avoid dereferencing
	 * the node after the callback).
	 */
	list_for_each_entry_safe(active, next, &request->active_list, link) {
		/* In microbenchmarks or focusing upon time inside the kernel,
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

	i915_gem_request_remove_from_client(request);

	if (request->previous_context) {
		if (i915.enable_execlists)
			intel_lr_context_unpin(request->previous_context,
					       request->engine);
	}

	i915_gem_context_put(request->ctx);
	i915_gem_request_put(request);
}

void i915_gem_request_retire_upto(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *engine = req->engine;
	struct drm_i915_gem_request *tmp;

	lockdep_assert_held(&req->i915->drm.struct_mutex);
	GEM_BUG_ON(list_empty(&req->link));

	do {
		tmp = list_first_entry(&engine->request_list,
				       typeof(*tmp), link);

		i915_gem_request_retire(tmp);
	} while (tmp != req);
}

static int i915_gem_check_wedge(struct drm_i915_private *dev_priv)
{
	struct i915_gpu_error *error = &dev_priv->gpu_error;

	if (i915_terminally_wedged(error))
		return -EIO;

	if (i915_reset_in_progress(error)) {
		/* Non-interruptible callers can't handle -EAGAIN, hence return
		 * -EIO unconditionally for these.
		 */
		if (!dev_priv->mm.interruptible)
			return -EIO;

		return -EAGAIN;
	}

	return 0;
}

static int i915_gem_init_seqno(struct drm_i915_private *dev_priv, u32 seqno)
{
	struct intel_engine_cs *engine;
	int ret;

	/* Carefully retire all requests without writing to the rings */
	for_each_engine(engine, dev_priv) {
		ret = intel_engine_idle(engine,
					I915_WAIT_INTERRUPTIBLE |
					I915_WAIT_LOCKED);
		if (ret)
			return ret;
	}
	i915_gem_retire_requests(dev_priv);

	/* If the seqno wraps around, we need to clear the breadcrumb rbtree */
	if (!i915_seqno_passed(seqno, dev_priv->next_seqno)) {
		while (intel_kick_waiters(dev_priv) ||
		       intel_kick_signalers(dev_priv))
			yield();
	}

	/* Finally reset hw state */
	for_each_engine(engine, dev_priv)
		intel_engine_init_seqno(engine, seqno);

	return 0;
}

int i915_gem_set_seqno(struct drm_device *dev, u32 seqno)
{
	struct drm_i915_private *dev_priv = to_i915(dev);
	int ret;

	if (seqno == 0)
		return -EINVAL;

	/* HWS page needs to be set less than what we
	 * will inject to ring
	 */
	ret = i915_gem_init_seqno(dev_priv, seqno - 1);
	if (ret)
		return ret;

	dev_priv->next_seqno = seqno;
	return 0;
}

static int i915_gem_get_seqno(struct drm_i915_private *dev_priv, u32 *seqno)
{
	/* reserve 0 for non-seqno */
	if (unlikely(dev_priv->next_seqno == 0)) {
		int ret;

		ret = i915_gem_init_seqno(dev_priv, 0);
		if (ret)
			return ret;

		dev_priv->next_seqno = 1;
	}

	*seqno = dev_priv->next_seqno++;
	return 0;
}

static int __i915_sw_fence_call
submit_notify(struct i915_sw_fence *fence, enum i915_sw_fence_notify state)
{
	struct drm_i915_gem_request *request =
		container_of(fence, typeof(*request), submit);

	/* Will be called from irq-context when using foreign DMA fences */

	switch (state) {
	case FENCE_COMPLETE:
		request->engine->last_submitted_seqno = request->fence.seqno;
		request->engine->submit_request(request);
		break;

	case FENCE_FREE:
		break;
	}

	return NOTIFY_DONE;
}

/**
 * i915_gem_request_alloc - allocate a request structure
 *
 * @engine: engine that we wish to issue the request on.
 * @ctx: context that the request will be associated with.
 *       This can be NULL if the request is not directly related to
 *       any specific user context, in which case this function will
 *       choose an appropriate context to use.
 *
 * Returns a pointer to the allocated request if successful,
 * or an error code if not.
 */
struct drm_i915_gem_request *
i915_gem_request_alloc(struct intel_engine_cs *engine,
		       struct i915_gem_context *ctx)
{
	struct drm_i915_private *dev_priv = engine->i915;
	struct drm_i915_gem_request *req;
	u32 seqno;
	int ret;

	/* ABI: Before userspace accesses the GPU (e.g. execbuffer), report
	 * EIO if the GPU is already wedged, or EAGAIN to drop the struct_mutex
	 * and restart.
	 */
	ret = i915_gem_check_wedge(dev_priv);
	if (ret)
		return ERR_PTR(ret);

	/* Move the oldest request to the slab-cache (if not in use!) */
	req = list_first_entry_or_null(&engine->request_list,
				       typeof(*req), link);
	if (req && i915_gem_request_completed(req))
		i915_gem_request_retire(req);

	/* Beware: Dragons be flying overhead.
	 *
	 * We use RCU to look up requests in flight. The lookups may
	 * race with the request being allocated from the slab freelist.
	 * That is the request we are writing to here, may be in the process
	 * of being read by __i915_gem_active_get_rcu(). As such,
	 * we have to be very careful when overwriting the contents. During
	 * the RCU lookup, we change chase the request->engine pointer,
	 * read the request->fence.seqno and increment the reference count.
	 *
	 * The reference count is incremented atomically. If it is zero,
	 * the lookup knows the request is unallocated and complete. Otherwise,
	 * it is either still in use, or has been reallocated and reset
	 * with fence_init(). This increment is safe for release as we check
	 * that the request we have a reference to and matches the active
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
	req = kmem_cache_alloc(dev_priv->requests, GFP_KERNEL);
	if (!req)
		return ERR_PTR(-ENOMEM);

	ret = i915_gem_get_seqno(dev_priv, &seqno);
	if (ret)
		goto err;

	spin_lock_init(&req->lock);
	fence_init(&req->fence,
		   &i915_fence_ops,
		   &req->lock,
		   engine->fence_context,
		   seqno);

	i915_sw_fence_init(&req->submit, submit_notify);

	INIT_LIST_HEAD(&req->active_list);
	req->i915 = dev_priv;
	req->engine = engine;
	req->ctx = i915_gem_context_get(ctx);

	/* No zalloc, must clear what we need by hand */
	req->previous_context = NULL;
	req->file_priv = NULL;
	req->batch = NULL;

	/*
	 * Reserve space in the ring buffer for all the commands required to
	 * eventually emit this request. This is to guarantee that the
	 * i915_add_request() call can't fail. Note that the reserve may need
	 * to be redone if the request is not actually submitted straight
	 * away, e.g. because a GPU scheduler has deferred it.
	 */
	req->reserved_space = MIN_SPACE_FOR_ADD_REQUEST;

	if (i915.enable_execlists)
		ret = intel_logical_ring_alloc_request_extras(req);
	else
		ret = intel_ring_alloc_request_extras(req);
	if (ret)
		goto err_ctx;

	/* Record the position of the start of the request so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the head.
	 */
	req->head = req->ring->tail;

	return req;

err_ctx:
	i915_gem_context_put(ctx);
err:
	kmem_cache_free(dev_priv->requests, req);
	return ERR_PTR(ret);
}

static int
i915_gem_request_await_request(struct drm_i915_gem_request *to,
			       struct drm_i915_gem_request *from)
{
	int idx, ret;

	GEM_BUG_ON(to == from);

	if (to->engine == from->engine)
		return 0;

	idx = intel_engine_sync_index(from->engine, to->engine);
	if (from->fence.seqno <= from->engine->semaphore.sync_seqno[idx])
		return 0;

	trace_i915_gem_ring_sync_to(to, from);
	if (!i915.semaphores) {
		if (!i915_spin_request(from, TASK_INTERRUPTIBLE, 2)) {
			ret = i915_sw_fence_await_dma_fence(&to->submit,
							    &from->fence, 0,
							    GFP_KERNEL);
			if (ret < 0)
				return ret;
		}
	} else {
		ret = to->engine->semaphore.sync_to(to, from);
		if (ret)
			return ret;
	}

	from->engine->semaphore.sync_seqno[idx] = from->fence.seqno;
	return 0;
}

/**
 * i915_gem_request_await_object - set this request to (async) wait upon a bo
 *
 * @to: request we are wishing to use
 * @obj: object which may be in use on another ring.
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
i915_gem_request_await_object(struct drm_i915_gem_request *to,
			      struct drm_i915_gem_object *obj,
			      bool write)
{
	struct i915_gem_active *active;
	unsigned long active_mask;
	int idx;

	if (write) {
		active_mask = i915_gem_object_get_active(obj);
		active = obj->last_read;
	} else {
		active_mask = 1;
		active = &obj->last_write;
	}

	for_each_active(active_mask, idx) {
		struct drm_i915_gem_request *request;
		int ret;

		request = i915_gem_active_peek(&active[idx],
					       &obj->base.dev->struct_mutex);
		if (!request)
			continue;

		ret = i915_gem_request_await_request(to, request);
		if (ret)
			return ret;
	}

	return 0;
}

static void i915_gem_mark_busy(const struct intel_engine_cs *engine)
{
	struct drm_i915_private *dev_priv = engine->i915;

	dev_priv->gt.active_engines |= intel_engine_flag(engine);
	if (dev_priv->gt.awake)
		return;

	intel_runtime_pm_get_noresume(dev_priv);
	dev_priv->gt.awake = true;

	intel_enable_gt_powersave(dev_priv);
	i915_update_gfx_val(dev_priv);
	if (INTEL_GEN(dev_priv) >= 6)
		gen6_rps_busy(dev_priv);

	queue_delayed_work(dev_priv->wq,
			   &dev_priv->gt.retire_work,
			   round_jiffies_up_relative(HZ));
}

/*
 * NB: This function is not allowed to fail. Doing so would mean the the
 * request is not being tracked for completion but the work itself is
 * going to happen on the hardware. This would be a Bad Thing(tm).
 */
void __i915_add_request(struct drm_i915_gem_request *request, bool flush_caches)
{
	struct intel_engine_cs *engine = request->engine;
	struct intel_ring *ring = request->ring;
	struct drm_i915_gem_request *prev;
	u32 request_start;
	u32 reserved_tail;
	int ret;

	trace_i915_gem_request_add(request);

	/*
	 * To ensure that this call will not fail, space for its emissions
	 * should already have been reserved in the ring buffer. Let the ring
	 * know that it is time to use that space up.
	 */
	request_start = ring->tail;
	reserved_tail = request->reserved_space;
	request->reserved_space = 0;

	/*
	 * Emit any outstanding flushes - execbuf can fail to emit the flush
	 * after having emitted the batchbuffer command. Hence we need to fix
	 * things up similar to emitting the lazy request. The difference here
	 * is that the flush _must_ happen before the next request, no matter
	 * what.
	 */
	if (flush_caches) {
		ret = engine->emit_flush(request, EMIT_FLUSH);

		/* Not allowed to fail! */
		WARN(ret, "engine->emit_flush() failed: %d!\n", ret);
	}

	/* Record the position of the start of the breadcrumb so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the ring's HEAD.
	 */
	request->postfix = ring->tail;

	/* Not allowed to fail! */
	ret = engine->emit_request(request);
	WARN(ret, "(%s)->emit_request failed: %d!\n", engine->name, ret);

	/* Sanity check that the reserved size was large enough. */
	ret = ring->tail - request_start;
	if (ret < 0)
		ret += ring->size;
	WARN_ONCE(ret > reserved_tail,
		  "Not enough space reserved (%d bytes) "
		  "for adding the request (%d bytes)\n",
		  reserved_tail, ret);

	/* Seal the request and mark it as pending execution. Note that
	 * we may inspect this state, without holding any locks, during
	 * hangcheck. Hence we apply the barrier to ensure that we do not
	 * see a more recent value in the hws than we are tracking.
	 */

	prev = i915_gem_active_raw(&engine->last_request,
				   &request->i915->drm.struct_mutex);
	if (prev)
		i915_sw_fence_await_sw_fence(&request->submit, &prev->submit,
					     &request->submitq);

	request->emitted_jiffies = jiffies;
	request->previous_seqno = engine->last_pending_seqno;
	engine->last_pending_seqno = request->fence.seqno;
	i915_gem_active_set(&engine->last_request, request);
	list_add_tail(&request->link, &engine->request_list);
	list_add_tail(&request->ring_link, &ring->request_list);

	i915_gem_mark_busy(engine);

	local_bh_disable();
	i915_sw_fence_commit(&request->submit);
	local_bh_enable(); /* Kick the execlists tasklet if just scheduled */
}

static void reset_wait_queue(wait_queue_head_t *q, wait_queue_t *wait)
{
	unsigned long flags;

	spin_lock_irqsave(&q->lock, flags);
	if (list_empty(&wait->task_list))
		__add_wait_queue(q, wait);
	spin_unlock_irqrestore(&q->lock, flags);
}

static unsigned long local_clock_us(unsigned int *cpu)
{
	unsigned long t;

	/* Cheaply and approximately convert from nanoseconds to microseconds.
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

bool __i915_spin_request(const struct drm_i915_gem_request *req,
			 int state, unsigned long timeout_us)
{
	unsigned int cpu;

	/* When waiting for high frequency requests, e.g. during synchronous
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
		if (i915_gem_request_completed(req))
			return true;

		if (signal_pending_state(state, current))
			break;

		if (busywait_stop(timeout_us, cpu))
			break;

		cpu_relax_lowlatency();
	} while (!need_resched());

	return false;
}

/**
 * i915_wait_request - wait until execution of request has finished
 * @req: duh!
 * @flags: how to wait
 * @timeout: in - how long to wait (NULL forever); out - how much time remaining
 * @rps: client to charge for RPS boosting
 *
 * Note: It is of utmost importance that the passed in seqno and reset_counter
 * values have been read by the caller in an smp safe manner. Where read-side
 * locks are involved, it is sufficient to read the reset_counter before
 * unlocking the lock that protects the seqno. For lockless tricks, the
 * reset_counter _must_ be read before, and an appropriate smp_rmb must be
 * inserted.
 *
 * Returns 0 if the request was found within the alloted time. Else returns the
 * errno with remaining time filled in timeout argument.
 */
int i915_wait_request(struct drm_i915_gem_request *req,
		      unsigned int flags,
		      s64 *timeout,
		      struct intel_rps_client *rps)
{
	const int state = flags & I915_WAIT_INTERRUPTIBLE ?
		TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
	DEFINE_WAIT(reset);
	struct intel_wait wait;
	unsigned long timeout_remain;
	int ret = 0;

	might_sleep();
#if IS_ENABLED(CONFIG_LOCKDEP)
	GEM_BUG_ON(!!lockdep_is_held(&req->i915->drm.struct_mutex) !=
		   !!(flags & I915_WAIT_LOCKED));
#endif

	if (i915_gem_request_completed(req))
		return 0;

	timeout_remain = MAX_SCHEDULE_TIMEOUT;
	if (timeout) {
		if (WARN_ON(*timeout < 0))
			return -EINVAL;

		if (*timeout == 0)
			return -ETIME;

		/* Record current time in case interrupted, or wedged */
		timeout_remain = nsecs_to_jiffies_timeout(*timeout);
		*timeout += ktime_get_raw_ns();
	}

	trace_i915_gem_request_wait_begin(req);

	/* This client is about to stall waiting for the GPU. In many cases
	 * this is undesirable and limits the throughput of the system, as
	 * many clients cannot continue processing user input/output whilst
	 * blocked. RPS autotuning may take tens of milliseconds to respond
	 * to the GPU load and thus incurs additional latency for the client.
	 * We can circumvent that by promoting the GPU frequency to maximum
	 * before we wait. This makes the GPU throttle up much more quickly
	 * (good for benchmarks and user experience, e.g. window animations),
	 * but at a cost of spending more power processing the workload
	 * (bad for battery). Not all clients even want their results
	 * immediately and for them we should just let the GPU select its own
	 * frequency to maximise efficiency. To prevent a single client from
	 * forcing the clocks too high for the whole system, we only allow
	 * each client to waitboost once in a busy period.
	 */
	if (IS_RPS_CLIENT(rps) && INTEL_GEN(req->i915) >= 6)
		gen6_rps_boost(req->i915, rps, req->emitted_jiffies);

	/* Optimistic short spin before touching IRQs */
	if (i915_spin_request(req, state, 5))
		goto complete;

	set_current_state(state);
	if (flags & I915_WAIT_LOCKED)
		add_wait_queue(&req->i915->gpu_error.wait_queue, &reset);

	intel_wait_init(&wait, req->fence.seqno);
	if (intel_engine_add_wait(req->engine, &wait))
		/* In order to check that we haven't missed the interrupt
		 * as we enabled it, we need to kick ourselves to do a
		 * coherent check on the seqno before we sleep.
		 */
		goto wakeup;

	for (;;) {
		if (signal_pending_state(state, current)) {
			ret = -ERESTARTSYS;
			break;
		}

		timeout_remain = io_schedule_timeout(timeout_remain);
		if (timeout_remain == 0) {
			ret = -ETIME;
			break;
		}

		if (intel_wait_complete(&wait))
			break;

		set_current_state(state);

wakeup:
		/* Carefully check if the request is complete, giving time
		 * for the seqno to be visible following the interrupt.
		 * We also have to check in case we are kicked by the GPU
		 * reset in order to drop the struct_mutex.
		 */
		if (__i915_request_irq_complete(req))
			break;

		/* If the GPU is hung, and we hold the lock, reset the GPU
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
		    i915_reset_in_progress(&req->i915->gpu_error)) {
			__set_current_state(TASK_RUNNING);
			i915_reset(req->i915);
			reset_wait_queue(&req->i915->gpu_error.wait_queue,
					 &reset);
			continue;
		}

		/* Only spin if we know the GPU is processing this request */
		if (i915_spin_request(req, state, 2))
			break;
	}

	intel_engine_remove_wait(req->engine, &wait);
	if (flags & I915_WAIT_LOCKED)
		remove_wait_queue(&req->i915->gpu_error.wait_queue, &reset);
	__set_current_state(TASK_RUNNING);

complete:
	trace_i915_gem_request_wait_end(req);

	if (timeout) {
		*timeout -= ktime_get_raw_ns();
		if (*timeout < 0)
			*timeout = 0;

		/*
		 * Apparently ktime isn't accurate enough and occasionally has a
		 * bit of mismatch in the jiffies<->nsecs<->ktime loop. So patch
		 * things up to make the test happy. We allow up to 1 jiffy.
		 *
		 * This is a regrssion from the timespec->ktime conversion.
		 */
		if (ret == -ETIME && *timeout < jiffies_to_usecs(1)*1000)
			*timeout = 0;
	}

	if (IS_RPS_USER(rps) &&
	    req->fence.seqno == req->engine->last_submitted_seqno) {
		/* The GPU is now idle and this client has stalled.
		 * Since no other client has submitted a request in the
		 * meantime, assume that this client is the only one
		 * supplying work to the GPU but is unable to keep that
		 * work supplied because it is waiting. Since the GPU is
		 * then never kept fully busy, RPS autoclocking will
		 * keep the clocks relatively low, causing further delays.
		 * Compensate by giving the synchronous client credit for
		 * a waitboost next time.
		 */
		spin_lock(&req->i915->rps.client_lock);
		list_del_init(&rps->link);
		spin_unlock(&req->i915->rps.client_lock);
	}

	return ret;
}

static bool engine_retire_requests(struct intel_engine_cs *engine)
{
	struct drm_i915_gem_request *request, *next;

	list_for_each_entry_safe(request, next, &engine->request_list, link) {
		if (!i915_gem_request_completed(request))
			return false;

		i915_gem_request_retire(request);
	}

	return true;
}

void i915_gem_retire_requests(struct drm_i915_private *dev_priv)
{
	struct intel_engine_cs *engine;
	unsigned int tmp;

	lockdep_assert_held(&dev_priv->drm.struct_mutex);

	if (dev_priv->gt.active_engines == 0)
		return;

	GEM_BUG_ON(!dev_priv->gt.awake);

	for_each_engine_masked(engine, dev_priv, dev_priv->gt.active_engines, tmp)
		if (engine_retire_requests(engine))
			dev_priv->gt.active_engines &= ~intel_engine_flag(engine);

	if (dev_priv->gt.active_engines == 0)
		queue_delayed_work(dev_priv->wq,
				   &dev_priv->gt.idle_work,
				   msecs_to_jiffies(100));
}
