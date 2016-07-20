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

	ret = __i915_wait_request(to_request(fence),
				  interruptible, timeout,
				  NULL);
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

	req->pid = get_pid(task_pid(current));

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

	put_pid(request->pid);
	request->pid = NULL;
}

static void i915_gem_request_retire(struct drm_i915_gem_request *request)
{
	trace_i915_gem_request_retire(request);
	list_del_init(&request->list);

	/* We know the GPU must have read the request to have
	 * sent us the seqno + interrupt, so use the position
	 * of tail of the request to update the last known position
	 * of the GPU head.
	 *
	 * Note this requires that we are always called in request
	 * completion order.
	 */
	request->ringbuf->last_retired_head = request->postfix;

	i915_gem_request_remove_from_client(request);

	if (request->previous_context) {
		if (i915.enable_execlists)
			intel_lr_context_unpin(request->previous_context,
					       request->engine);
	}

	i915_gem_context_unreference(request->ctx);
	i915_gem_request_unreference(request);
}

void i915_gem_request_retire_upto(struct drm_i915_gem_request *req)
{
	struct intel_engine_cs *engine = req->engine;
	struct drm_i915_gem_request *tmp;

	lockdep_assert_held(&req->i915->drm.struct_mutex);

	if (list_empty(&req->list))
		return;

	do {
		tmp = list_first_entry(&engine->request_list,
				       typeof(*tmp), list);

		i915_gem_request_retire(tmp);
	} while (tmp != req);

	WARN_ON(i915_verify_lists(engine->dev));
}

static int i915_gem_check_wedge(unsigned int reset_counter, bool interruptible)
{
	if (__i915_terminally_wedged(reset_counter))
		return -EIO;

	if (__i915_reset_in_progress(reset_counter)) {
		/* Non-interruptible callers can't handle -EAGAIN, hence return
		 * -EIO unconditionally for these.
		 */
		if (!interruptible)
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
		ret = intel_engine_idle(engine);
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
		intel_ring_init_seqno(engine, seqno);

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

	/* Carefully set the last_seqno value so that wrap
	 * detection still works
	 */
	dev_priv->next_seqno = seqno;
	dev_priv->last_seqno = seqno - 1;
	if (dev_priv->last_seqno == 0)
		dev_priv->last_seqno--;

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

	*seqno = dev_priv->last_seqno = dev_priv->next_seqno++;
	return 0;
}

static inline int
__i915_gem_request_alloc(struct intel_engine_cs *engine,
			 struct i915_gem_context *ctx,
			 struct drm_i915_gem_request **req_out)
{
	struct drm_i915_private *dev_priv = engine->i915;
	unsigned int reset_counter = i915_reset_counter(&dev_priv->gpu_error);
	struct drm_i915_gem_request *req;
	u32 seqno;
	int ret;

	if (!req_out)
		return -EINVAL;

	*req_out = NULL;

	/* ABI: Before userspace accesses the GPU (e.g. execbuffer), report
	 * EIO if the GPU is already wedged, or EAGAIN to drop the struct_mutex
	 * and restart.
	 */
	ret = i915_gem_check_wedge(reset_counter, dev_priv->mm.interruptible);
	if (ret)
		return ret;

	/* Move the oldest request to the slab-cache (if not in use!) */
	if (!list_empty(&engine->request_list)) {
		req = list_first_entry(&engine->request_list,
				       typeof(*req), list);
		if (i915_gem_request_completed(req))
			i915_gem_request_retire(req);
	}

	req = kmem_cache_zalloc(dev_priv->requests, GFP_KERNEL);
	if (!req)
		return -ENOMEM;

	ret = i915_gem_get_seqno(dev_priv, &seqno);
	if (ret)
		goto err;

	spin_lock_init(&req->lock);
	fence_init(&req->fence,
		   &i915_fence_ops,
		   &req->lock,
		   engine->fence_context,
		   seqno);

	req->i915 = dev_priv;
	req->engine = engine;
	req->ctx = ctx;
	i915_gem_context_reference(ctx);

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

	*req_out = req;
	return 0;

err_ctx:
	i915_gem_context_unreference(ctx);
err:
	kmem_cache_free(dev_priv->requests, req);
	return ret;
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
	struct drm_i915_gem_request *req;
	int err;

	if (!ctx)
		ctx = engine->i915->kernel_context;
	err = __i915_gem_request_alloc(engine, ctx, &req);
	return err ? ERR_PTR(err) : req;
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
void __i915_add_request(struct drm_i915_gem_request *request,
			struct drm_i915_gem_object *obj,
			bool flush_caches)
{
	struct intel_engine_cs *engine;
	struct intel_ringbuffer *ringbuf;
	u32 request_start;
	u32 reserved_tail;
	int ret;

	if (WARN_ON(!request))
		return;

	engine = request->engine;
	ringbuf = request->ringbuf;

	/*
	 * To ensure that this call will not fail, space for its emissions
	 * should already have been reserved in the ring buffer. Let the ring
	 * know that it is time to use that space up.
	 */
	request_start = intel_ring_get_tail(ringbuf);
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
		if (i915.enable_execlists)
			ret = logical_ring_flush_all_caches(request);
		else
			ret = intel_ring_flush_all_caches(request);
		/* Not allowed to fail! */
		WARN(ret, "*_ring_flush_all_caches failed: %d!\n", ret);
	}

	trace_i915_gem_request_add(request);

	request->head = request_start;

	/* Whilst this request exists, batch_obj will be on the
	 * active_list, and so will hold the active reference. Only when this
	 * request is retired will the the batch_obj be moved onto the
	 * inactive_list and lose its active reference. Hence we do not need
	 * to explicitly hold another reference here.
	 */
	request->batch_obj = obj;

	/* Seal the request and mark it as pending execution. Note that
	 * we may inspect this state, without holding any locks, during
	 * hangcheck. Hence we apply the barrier to ensure that we do not
	 * see a more recent value in the hws than we are tracking.
	 */
	request->emitted_jiffies = jiffies;
	request->previous_seqno = engine->last_submitted_seqno;
	smp_store_mb(engine->last_submitted_seqno, request->fence.seqno);
	list_add_tail(&request->list, &engine->request_list);

	/* Record the position of the start of the request so that
	 * should we detect the updated seqno part-way through the
	 * GPU processing the request, we never over-estimate the
	 * position of the head.
	 */
	request->postfix = intel_ring_get_tail(ringbuf);

	if (i915.enable_execlists) {
		ret = engine->emit_request(request);
	} else {
		ret = engine->add_request(request);

		request->tail = intel_ring_get_tail(ringbuf);
	}
	/* Not allowed to fail! */
	WARN(ret, "emit|add_request failed: %d!\n", ret);
	/* Sanity check that the reserved size was large enough. */
	ret = intel_ring_get_tail(ringbuf) - request_start;
	if (ret < 0)
		ret += ringbuf->size;
	WARN_ONCE(ret > reserved_tail,
		  "Not enough space reserved (%d bytes) "
		  "for adding the request (%d bytes)\n",
		  reserved_tail, ret);

	i915_gem_mark_busy(engine);
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
 * __i915_wait_request - wait until execution of request has finished
 * @req: duh!
 * @interruptible: do an interruptible wait (normally yes)
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
int __i915_wait_request(struct drm_i915_gem_request *req,
			bool interruptible,
			s64 *timeout,
			struct intel_rps_client *rps)
{
	int state = interruptible ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE;
	DEFINE_WAIT(reset);
	struct intel_wait wait;
	unsigned long timeout_remain;
	int ret = 0;

	might_sleep();

	if (list_empty(&req->list))
		return 0;

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
	if (INTEL_GEN(req->i915) >= 6)
		gen6_rps_boost(req->i915, rps, req->emitted_jiffies);

	/* Optimistic spin for the next ~jiffie before touching IRQs */
	if (i915_spin_request(req, state, 5))
		goto complete;

	set_current_state(state);
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

		/* Only spin if we know the GPU is processing this request */
		if (i915_spin_request(req, state, 2))
			break;
	}
	remove_wait_queue(&req->i915->gpu_error.wait_queue, &reset);

	intel_engine_remove_wait(req->engine, &wait);
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

	if (rps && req->fence.seqno == req->engine->last_submitted_seqno) {
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

/**
 * Waits for a request to be signaled, and cleans up the
 * request and object lists appropriately for that event.
 */
int i915_wait_request(struct drm_i915_gem_request *req)
{
	int ret;

	GEM_BUG_ON(!req);
	lockdep_assert_held(&req->i915->drm.struct_mutex);

	ret = __i915_wait_request(req, req->i915->mm.interruptible, NULL, NULL);
	if (ret)
		return ret;

	/* If the GPU hung, we want to keep the requests to find the guilty. */
	if (!i915_reset_in_progress(&req->i915->gpu_error))
		i915_gem_request_retire_upto(req);

	return 0;
}
