/*
 * Copyright © 2008-2018 Intel Corporation
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

#ifndef I915_REQUEST_H
#define I915_REQUEST_H

#include <linux/dma-fence.h>
#include <linux/lockdep.h>

#include "gem/i915_gem_context_types.h"
#include "gt/intel_context_types.h"
#include "gt/intel_engine_types.h"
#include "gt/intel_timeline_types.h"

#include "i915_gem.h"
#include "i915_scheduler.h"
#include "i915_selftest.h"
#include "i915_sw_fence.h"

#include <uapi/drm/i915_drm.h>

struct drm_file;
struct drm_i915_gem_object;
struct i915_request;

struct i915_capture_list {
	struct i915_capture_list *next;
	struct i915_vma *vma;
};

#define RQ_TRACE(rq, fmt, ...) do {					\
	const struct i915_request *rq__ = (rq);				\
	ENGINE_TRACE(rq__->engine, "fence %llx:%lld, current %d " fmt,	\
		     rq__->fence.context, rq__->fence.seqno,		\
		     hwsp_seqno(rq__), ##__VA_ARGS__);			\
} while (0)

enum {
	/*
	 * I915_FENCE_FLAG_ACTIVE - this request is currently submitted to HW.
	 *
	 * Set by __i915_request_submit() on handing over to HW, and cleared
	 * by __i915_request_unsubmit() if we preempt this request.
	 *
	 * Finally cleared for consistency on retiring the request, when
	 * we know the HW is no longer running this request.
	 *
	 * See i915_request_is_active()
	 */
	I915_FENCE_FLAG_ACTIVE = DMA_FENCE_FLAG_USER_BITS,

	/*
	 * I915_FENCE_FLAG_PQUEUE - this request is ready for execution
	 *
	 * Using the scheduler, when a request is ready for execution it is put
	 * into the priority queue, and removed from that queue when transferred
	 * to the HW runlists. We want to track its membership within the
	 * priority queue so that we can easily check before rescheduling.
	 *
	 * See i915_request_in_priority_queue()
	 */
	I915_FENCE_FLAG_PQUEUE,

	/*
	 * I915_FENCE_FLAG_SIGNAL - this request is currently on signal_list
	 *
	 * Internal bookkeeping used by the breadcrumb code to track when
	 * a request is on the various signal_list.
	 */
	I915_FENCE_FLAG_SIGNAL,

	/*
	 * I915_FENCE_FLAG_HOLD - this request is currently on hold
	 *
	 * This request has been suspended, pending an ongoing investigation.
	 */
	I915_FENCE_FLAG_HOLD,

	/*
	 * I915_FENCE_FLAG_NOPREEMPT - this request should not be preempted
	 *
	 * The execution of some requests should not be interrupted. This is
	 * a sensitive operation as it makes the request super important,
	 * blocking other higher priority work. Abuse of this flag will
	 * lead to quality of service issues.
	 */
	I915_FENCE_FLAG_NOPREEMPT,

	/*
	 * I915_FENCE_FLAG_SENTINEL - this request should be last in the queue
	 *
	 * A high priority sentinel request may be submitted to clear the
	 * submission queue. As it will be the only request in-flight, upon
	 * execution all other active requests will have been preempted and
	 * unsubmitted. This preemptive pulse is used to re-evaluate the
	 * in-flight requests, particularly in cases where an active context
	 * is banned and those active requests need to be cancelled.
	 */
	I915_FENCE_FLAG_SENTINEL,

	/*
	 * I915_FENCE_FLAG_BOOST - upclock the gpu for this request
	 *
	 * Some requests are more important than others! In particular, a
	 * request that the user is waiting on is typically required for
	 * interactive latency, for which we want to minimise by upclocking
	 * the GPU. Here we track such boost requests on a per-request basis.
	 */
	I915_FENCE_FLAG_BOOST,
};

/**
 * Request queue structure.
 *
 * The request queue allows us to note sequence numbers that have been emitted
 * and may be associated with active buffers to be retired.
 *
 * By keeping this list, we can avoid having to do questionable sequence
 * number comparisons on buffer last_read|write_seqno. It also allows an
 * emission time to be associated with the request for tracking how far ahead
 * of the GPU the submission is.
 *
 * When modifying this structure be very aware that we perform a lockless
 * RCU lookup of it that may race against reallocation of the struct
 * from the slab freelist. We intentionally do not zero the structure on
 * allocation so that the lookup can use the dangling pointers (and is
 * cogniscent that those pointers may be wrong). Instead, everything that
 * needs to be initialised must be done so explicitly.
 *
 * The requests are reference counted.
 */
struct i915_request {
	struct dma_fence fence;
	spinlock_t lock;

	/** On Which ring this request was generated */
	struct drm_i915_private *i915;

	/**
	 * Context and ring buffer related to this request
	 * Contexts are refcounted, so when this request is associated with a
	 * context, we must increment the context's refcount, to guarantee that
	 * it persists while any request is linked to it. Requests themselves
	 * are also refcounted, so the request will only be freed when the last
	 * reference to it is dismissed, and the code in
	 * i915_request_free() will then decrement the refcount on the
	 * context.
	 */
	struct intel_engine_cs *engine;
	struct intel_context *context;
	struct intel_ring *ring;
	struct intel_timeline __rcu *timeline;
	struct list_head signal_link;

	/*
	 * The rcu epoch of when this request was allocated. Used to judiciously
	 * apply backpressure on future allocations to ensure that under
	 * mempressure there is sufficient RCU ticks for us to reclaim our
	 * RCU protected slabs.
	 */
	unsigned long rcustate;

	/*
	 * We pin the timeline->mutex while constructing the request to
	 * ensure that no caller accidentally drops it during construction.
	 * The timeline->mutex must be held to ensure that only this caller
	 * can use the ring and manipulate the associated timeline during
	 * construction.
	 */
	struct pin_cookie cookie;

	/*
	 * Fences for the various phases in the request's lifetime.
	 *
	 * The submit fence is used to await upon all of the request's
	 * dependencies. When it is signaled, the request is ready to run.
	 * It is used by the driver to then queue the request for execution.
	 */
	struct i915_sw_fence submit;
	union {
		wait_queue_entry_t submitq;
		struct i915_sw_dma_fence_cb dmaq;
		struct i915_request_duration_cb {
			struct dma_fence_cb cb;
			ktime_t emitted;
		} duration;
	};
	struct list_head execute_cb;
	struct i915_sw_fence semaphore;

	/*
	 * A list of everyone we wait upon, and everyone who waits upon us.
	 * Even though we will not be submitted to the hardware before the
	 * submit fence is signaled (it waits for all external events as well
	 * as our own requests), the scheduler still needs to know the
	 * dependency tree for the lifetime of the request (from execbuf
	 * to retirement), i.e. bidirectional dependency information for the
	 * request not tied to individual fences.
	 */
	struct i915_sched_node sched;
	struct i915_dependency dep;
	intel_engine_mask_t execution_mask;

	/*
	 * A convenience pointer to the current breadcrumb value stored in
	 * the HW status page (or our timeline's local equivalent). The full
	 * path would be rq->hw_context->ring->timeline->hwsp_seqno.
	 */
	const u32 *hwsp_seqno;

	/*
	 * If we need to access the timeline's seqno for this request in
	 * another request, we need to keep a read reference to this associated
	 * cacheline, so that we do not free and recycle it before the foreign
	 * observers have completed. Hence, we keep a pointer to the cacheline
	 * inside the timeline's HWSP vma, but it is only valid while this
	 * request has not completed and guarded by the timeline mutex.
	 */
	struct intel_timeline_cacheline __rcu *hwsp_cacheline;

	/** Position in the ring of the start of the request */
	u32 head;

	/** Position in the ring of the start of the user packets */
	u32 infix;

	/**
	 * Position in the ring of the start of the postfix.
	 * This is required to calculate the maximum available ring space
	 * without overwriting the postfix.
	 */
	u32 postfix;

	/** Position in the ring of the end of the whole request */
	u32 tail;

	/** Position in the ring of the end of any workarounds after the tail */
	u32 wa_tail;

	/** Preallocate space in the ring for the emitting the request */
	u32 reserved_space;

	/** Batch buffer related to this request if any (used for
	 * error state dump only).
	 */
	struct i915_vma *batch;
	/**
	 * Additional buffers requested by userspace to be captured upon
	 * a GPU hang. The vma/obj on this list are protected by their
	 * active reference - all objects on this list must also be
	 * on the active_list (of their final request).
	 */
	struct i915_capture_list *capture_list;

	/** Time at which this request was emitted, in jiffies. */
	unsigned long emitted_jiffies;

	/** timeline->request entry for this request */
	struct list_head link;

	struct drm_i915_file_private *file_priv;
	/** file_priv list entry for this request */
	struct list_head client_link;

	I915_SELFTEST_DECLARE(struct {
		struct list_head link;
		unsigned long delay;
	} mock;)
};

#define I915_FENCE_GFP (GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN)

extern const struct dma_fence_ops i915_fence_ops;

static inline bool dma_fence_is_i915(const struct dma_fence *fence)
{
	return fence->ops == &i915_fence_ops;
}

struct i915_request * __must_check
__i915_request_create(struct intel_context *ce, gfp_t gfp);
struct i915_request * __must_check
i915_request_create(struct intel_context *ce);

void i915_request_set_error_once(struct i915_request *rq, int error);
void __i915_request_skip(struct i915_request *rq);

struct i915_request *__i915_request_commit(struct i915_request *request);
void __i915_request_queue(struct i915_request *rq,
			  const struct i915_sched_attr *attr);

bool i915_request_retire(struct i915_request *rq);
void i915_request_retire_upto(struct i915_request *rq);

static inline struct i915_request *
to_request(struct dma_fence *fence)
{
	/* We assume that NULL fence/request are interoperable */
	BUILD_BUG_ON(offsetof(struct i915_request, fence) != 0);
	GEM_BUG_ON(fence && !dma_fence_is_i915(fence));
	return container_of(fence, struct i915_request, fence);
}

static inline struct i915_request *
i915_request_get(struct i915_request *rq)
{
	return to_request(dma_fence_get(&rq->fence));
}

static inline struct i915_request *
i915_request_get_rcu(struct i915_request *rq)
{
	return to_request(dma_fence_get_rcu(&rq->fence));
}

static inline void
i915_request_put(struct i915_request *rq)
{
	dma_fence_put(&rq->fence);
}

int i915_request_await_object(struct i915_request *to,
			      struct drm_i915_gem_object *obj,
			      bool write);
int i915_request_await_dma_fence(struct i915_request *rq,
				 struct dma_fence *fence);
int i915_request_await_execution(struct i915_request *rq,
				 struct dma_fence *fence,
				 void (*hook)(struct i915_request *rq,
					      struct dma_fence *signal));

void i915_request_add(struct i915_request *rq);

bool __i915_request_submit(struct i915_request *request);
void i915_request_submit(struct i915_request *request);

void __i915_request_unsubmit(struct i915_request *request);
void i915_request_unsubmit(struct i915_request *request);

/* Note: part of the intel_breadcrumbs family */
bool i915_request_enable_breadcrumb(struct i915_request *request);
void i915_request_cancel_breadcrumb(struct i915_request *request);

long i915_request_wait(struct i915_request *rq,
		       unsigned int flags,
		       long timeout)
	__attribute__((nonnull(1)));
#define I915_WAIT_INTERRUPTIBLE	BIT(0)
#define I915_WAIT_PRIORITY	BIT(1) /* small priority bump for the request */
#define I915_WAIT_ALL		BIT(2) /* used by i915_gem_object_wait() */

static inline bool i915_request_signaled(const struct i915_request *rq)
{
	/* The request may live longer than its HWSP, so check flags first! */
	return test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &rq->fence.flags);
}

static inline bool i915_request_is_active(const struct i915_request *rq)
{
	return test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags);
}

static inline bool i915_request_in_priority_queue(const struct i915_request *rq)
{
	return test_bit(I915_FENCE_FLAG_PQUEUE, &rq->fence.flags);
}

/**
 * Returns true if seq1 is later than seq2.
 */
static inline bool i915_seqno_passed(u32 seq1, u32 seq2)
{
	return (s32)(seq1 - seq2) >= 0;
}

static inline u32 __hwsp_seqno(const struct i915_request *rq)
{
	return READ_ONCE(*rq->hwsp_seqno);
}

/**
 * hwsp_seqno - the current breadcrumb value in the HW status page
 * @rq: the request, to chase the relevant HW status page
 *
 * The emphasis in naming here is that hwsp_seqno() is not a property of the
 * request, but an indication of the current HW state (associated with this
 * request). Its value will change as the GPU executes more requests.
 *
 * Returns the current breadcrumb value in the associated HW status page (or
 * the local timeline's equivalent) for this request. The request itself
 * has the associated breadcrumb value of rq->fence.seqno, when the HW
 * status page has that breadcrumb or later, this request is complete.
 */
static inline u32 hwsp_seqno(const struct i915_request *rq)
{
	u32 seqno;

	rcu_read_lock(); /* the HWSP may be freed at runtime */
	seqno = __hwsp_seqno(rq);
	rcu_read_unlock();

	return seqno;
}

static inline bool __i915_request_has_started(const struct i915_request *rq)
{
	return i915_seqno_passed(hwsp_seqno(rq), rq->fence.seqno - 1);
}

/**
 * i915_request_started - check if the request has begun being executed
 * @rq: the request
 *
 * If the timeline is not using initial breadcrumbs, a request is
 * considered started if the previous request on its timeline (i.e.
 * context) has been signaled.
 *
 * If the timeline is using semaphores, it will also be emitting an
 * "initial breadcrumb" after the semaphores are complete and just before
 * it began executing the user payload. A request can therefore be active
 * on the HW and not yet started as it is still busywaiting on its
 * dependencies (via HW semaphores).
 *
 * If the request has started, its dependencies will have been signaled
 * (either by fences or by semaphores) and it will have begun processing
 * the user payload.
 *
 * However, even if a request has started, it may have been preempted and
 * so no longer active, or it may have already completed.
 *
 * See also i915_request_is_active().
 *
 * Returns true if the request has begun executing the user payload, or
 * has completed:
 */
static inline bool i915_request_started(const struct i915_request *rq)
{
	if (i915_request_signaled(rq))
		return true;

	/* Remember: started but may have since been preempted! */
	return __i915_request_has_started(rq);
}

/**
 * i915_request_is_running - check if the request may actually be executing
 * @rq: the request
 *
 * Returns true if the request is currently submitted to hardware, has passed
 * its start point (i.e. the context is setup and not busywaiting). Note that
 * it may no longer be running by the time the function returns!
 */
static inline bool i915_request_is_running(const struct i915_request *rq)
{
	if (!i915_request_is_active(rq))
		return false;

	return __i915_request_has_started(rq);
}

/**
 * i915_request_is_ready - check if the request is ready for execution
 * @rq: the request
 *
 * Upon construction, the request is instructed to wait upon various
 * signals before it is ready to be executed by the HW. That is, we do
 * not want to start execution and read data before it is written. In practice,
 * this is controlled with a mixture of interrupts and semaphores. Once
 * the submit fence is completed, the backend scheduler will place the
 * request into its queue and from there submit it for execution. So we
 * can detect when a request is eligible for execution (and is under control
 * of the scheduler) by querying where it is in any of the scheduler's lists.
 *
 * Returns true if the request is ready for execution (it may be inflight),
 * false otherwise.
 */
static inline bool i915_request_is_ready(const struct i915_request *rq)
{
	return !list_empty(&rq->sched.link);
}

static inline bool i915_request_completed(const struct i915_request *rq)
{
	if (i915_request_signaled(rq))
		return true;

	return i915_seqno_passed(hwsp_seqno(rq), rq->fence.seqno);
}

static inline void i915_request_mark_complete(struct i915_request *rq)
{
	rq->hwsp_seqno = (u32 *)&rq->fence.seqno; /* decouple from HWSP */
}

static inline bool i915_request_has_waitboost(const struct i915_request *rq)
{
	return test_bit(I915_FENCE_FLAG_BOOST, &rq->fence.flags);
}

static inline bool i915_request_has_nopreempt(const struct i915_request *rq)
{
	/* Preemption should only be disabled very rarely */
	return unlikely(test_bit(I915_FENCE_FLAG_NOPREEMPT, &rq->fence.flags));
}

static inline bool i915_request_has_sentinel(const struct i915_request *rq)
{
	return unlikely(test_bit(I915_FENCE_FLAG_SENTINEL, &rq->fence.flags));
}

static inline bool i915_request_on_hold(const struct i915_request *rq)
{
	return unlikely(test_bit(I915_FENCE_FLAG_HOLD, &rq->fence.flags));
}

static inline void i915_request_set_hold(struct i915_request *rq)
{
	set_bit(I915_FENCE_FLAG_HOLD, &rq->fence.flags);
}

static inline void i915_request_clear_hold(struct i915_request *rq)
{
	clear_bit(I915_FENCE_FLAG_HOLD, &rq->fence.flags);
}

static inline struct intel_timeline *
i915_request_timeline(struct i915_request *rq)
{
	/* Valid only while the request is being constructed (or retired). */
	return rcu_dereference_protected(rq->timeline,
					 lockdep_is_held(&rcu_access_pointer(rq->timeline)->mutex));
}

static inline struct i915_gem_context *
i915_request_gem_context(struct i915_request *rq)
{
	/* Valid only while the request is being constructed (or retired). */
	return rcu_dereference_protected(rq->context->gem_context, true);
}

static inline struct intel_timeline *
i915_request_active_timeline(struct i915_request *rq)
{
	/*
	 * When in use during submission, we are protected by a guarantee that
	 * the context/timeline is pinned and must remain pinned until after
	 * this submission.
	 */
	return rcu_dereference_protected(rq->timeline,
					 lockdep_is_held(&rq->engine->active.lock));
}

#endif /* I915_REQUEST_H */
