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

#include "i915_gem.h"
#include "i915_scheduler.h"
#include "i915_sw_fence.h"

#include <uapi/drm/i915_drm.h>

struct drm_file;
struct drm_i915_gem_object;
struct i915_request;
struct i915_timeline;

struct i915_capture_list {
	struct i915_capture_list *next;
	struct i915_vma *vma;
};

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
	 * I915_FENCE_FLAG_SIGNAL - this request is currently on signal_list
	 *
	 * Internal bookkeeping used by the breadcrumb code to track when
	 * a request is on the various signal_list.
	 */
	I915_FENCE_FLAG_SIGNAL,
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
	struct i915_gem_context *gem_context;
	struct intel_engine_cs *engine;
	struct intel_context *hw_context;
	struct intel_ring *ring;
	struct i915_timeline *timeline;
	struct list_head signal_link;

	/*
	 * The rcu epoch of when this request was allocated. Used to judiciously
	 * apply backpressure on future allocations to ensure that under
	 * mempressure there is sufficient RCU ticks for us to reclaim our
	 * RCU protected slabs.
	 */
	unsigned long rcustate;

	/*
	 * Fences for the various phases in the request's lifetime.
	 *
	 * The submit fence is used to await upon all of the request's
	 * dependencies. When it is signaled, the request is ready to run.
	 * It is used by the driver to then queue the request for execution.
	 */
	struct i915_sw_fence submit;
	wait_queue_entry_t submitq;

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

	/*
	 * A convenience pointer to the current breadcrumb value stored in
	 * the HW status page (or our timeline's local equivalent). The full
	 * path would be rq->hw_context->ring->timeline->hwsp_seqno.
	 */
	const u32 *hwsp_seqno;

	/**
	 * GEM sequence number associated with this request on the
	 * global execution timeline. It is zero when the request is not
	 * on the HW queue (i.e. not on the engine timeline list).
	 * Its value is guarded by the timeline spinlock.
	 */
	u32 global_seqno;

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
	struct list_head active_list;

	/** Time at which this request was emitted, in jiffies. */
	unsigned long emitted_jiffies;

	bool waitboost;

	/** engine->request_list entry for this request */
	struct list_head link;

	/** ring->request_list entry for this request */
	struct list_head ring_link;

	struct drm_i915_file_private *file_priv;
	/** file_priv list entry for this request */
	struct list_head client_link;
};

#define I915_FENCE_GFP (GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN)

extern const struct dma_fence_ops i915_fence_ops;

static inline bool dma_fence_is_i915(const struct dma_fence *fence)
{
	return fence->ops == &i915_fence_ops;
}

struct i915_request * __must_check
i915_request_alloc(struct intel_engine_cs *engine,
		   struct i915_gem_context *ctx);
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

/**
 * i915_request_global_seqno - report the current global seqno
 * @request - the request
 *
 * A request is assigned a global seqno only when it is on the hardware
 * execution queue. The global seqno can be used to maintain a list of
 * requests on the same engine in retirement order, for example for
 * constructing a priority queue for waiting. Prior to its execution, or
 * if it is subsequently removed in the event of preemption, its global
 * seqno is zero. As both insertion and removal from the execution queue
 * may operate in IRQ context, it is not guarded by the usual struct_mutex
 * BKL. Instead those relying on the global seqno must be prepared for its
 * value to change between reads. Only when the request is complete can
 * the global seqno be stable (due to the memory barriers on submitting
 * the commands to the hardware to write the breadcrumb, if the HWS shows
 * that it has passed the global seqno and the global seqno is unchanged
 * after the read, it is indeed complete).
 */
static inline u32
i915_request_global_seqno(const struct i915_request *request)
{
	return READ_ONCE(request->global_seqno);
}

int i915_request_await_object(struct i915_request *to,
			      struct drm_i915_gem_object *obj,
			      bool write);
int i915_request_await_dma_fence(struct i915_request *rq,
				 struct dma_fence *fence);

void i915_request_add(struct i915_request *rq);

void __i915_request_submit(struct i915_request *request);
void i915_request_submit(struct i915_request *request);

void i915_request_skip(struct i915_request *request, int error);

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
#define I915_WAIT_LOCKED	BIT(1) /* struct_mutex held, handle GPU reset */
#define I915_WAIT_PRIORITY	BIT(2) /* small priority bump for the request */
#define I915_WAIT_ALL		BIT(3) /* used by i915_gem_object_wait() */
#define I915_WAIT_FOR_IDLE_BOOST BIT(4)

static inline bool i915_request_signaled(const struct i915_request *rq)
{
	/* The request may live longer than its HWSP, so check flags first! */
	return test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &rq->fence.flags);
}

static inline bool i915_request_is_active(const struct i915_request *rq)
{
	return test_bit(I915_FENCE_FLAG_ACTIVE, &rq->fence.flags);
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
 * Returns true if the request has been submitted to hardware, and the hardware
 * has advanced passed the end of the previous request and so should be either
 * currently processing the request (though it may be preempted and so
 * not necessarily the next request to complete) or have completed the request.
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

void i915_retire_requests(struct drm_i915_private *i915);

#endif /* I915_REQUEST_H */
