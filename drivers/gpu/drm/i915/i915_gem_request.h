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

#ifndef I915_GEM_REQUEST_H
#define I915_GEM_REQUEST_H

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
 * The requests are reference counted, so upon creation they should have an
 * initial reference taken using kref_init
 */
struct drm_i915_gem_request {
	struct kref ref;

	/** On Which ring this request was generated */
	struct drm_i915_private *i915;

	/**
	 * Context and ring buffer related to this request
	 * Contexts are refcounted, so when this request is associated with a
	 * context, we must increment the context's refcount, to guarantee that
	 * it persists while any request is linked to it. Requests themselves
	 * are also refcounted, so the request will only be freed when the last
	 * reference to it is dismissed, and the code in
	 * i915_gem_request_free() will then decrement the refcount on the
	 * context.
	 */
	struct i915_gem_context *ctx;
	struct intel_engine_cs *engine;
	struct intel_ringbuffer *ringbuf;
	struct intel_signal_node signaling;

	/** GEM sequence number associated with the previous request,
	 * when the HWS breadcrumb is equal to this the GPU is processing
	 * this request.
	 */
	u32 previous_seqno;

	/** GEM sequence number associated with this request,
	 * when the HWS breadcrumb is equal or greater than this the GPU
	 * has finished processing this request.
	 */
	u32 seqno;

	/** Position in the ringbuffer of the start of the request */
	u32 head;

	/**
	 * Position in the ringbuffer of the start of the postfix.
	 * This is required to calculate the maximum available ringbuffer
	 * space without overwriting the postfix.
	 */
	u32 postfix;

	/** Position in the ringbuffer of the end of the whole request */
	u32 tail;

	/** Preallocate space in the ringbuffer for the emitting the request */
	u32 reserved_space;

	/**
	 * Context related to the previous request.
	 * As the contexts are accessed by the hardware until the switch is
	 * completed to a new context, the hardware may still be writing
	 * to the context object after the breadcrumb is visible. We must
	 * not unpin/unbind/prune that object whilst still active and so
	 * we keep the previous context pinned until the following (this)
	 * request is retired.
	 */
	struct i915_gem_context *previous_context;

	/** Batch buffer related to this request if any (used for
	 * error state dump only).
	 */
	struct drm_i915_gem_object *batch_obj;

	/** Time at which this request was emitted, in jiffies. */
	unsigned long emitted_jiffies;

	/** global list entry for this request */
	struct list_head list;

	struct drm_i915_file_private *file_priv;
	/** file_priv list entry for this request */
	struct list_head client_list;

	/** process identifier submitting this request */
	struct pid *pid;

	/**
	 * The ELSP only accepts two elements at a time, so we queue
	 * context/tail pairs on a given queue (ring->execlist_queue) until the
	 * hardware is available. The queue serves a double purpose: we also use
	 * it to keep track of the up to 2 contexts currently in the hardware
	 * (usually one in execution and the other queued up by the GPU): We
	 * only remove elements from the head of the queue when the hardware
	 * informs us that an element has been completed.
	 *
	 * All accesses to the queue are mediated by a spinlock
	 * (ring->execlist_lock).
	 */

	/** Execlist link in the submission queue.*/
	struct list_head execlist_link;

	/** Execlists no. of times this request has been sent to the ELSP */
	int elsp_submitted;

	/** Execlists context hardware id. */
	unsigned int ctx_hw_id;
};

struct drm_i915_gem_request * __must_check
i915_gem_request_alloc(struct intel_engine_cs *engine,
		       struct i915_gem_context *ctx);
void i915_gem_request_free(struct kref *req_ref);
int i915_gem_request_add_to_client(struct drm_i915_gem_request *req,
				   struct drm_file *file);
void i915_gem_request_retire_upto(struct drm_i915_gem_request *req);

static inline u32
i915_gem_request_get_seqno(struct drm_i915_gem_request *req)
{
	return req ? req->seqno : 0;
}

static inline struct intel_engine_cs *
i915_gem_request_get_engine(struct drm_i915_gem_request *req)
{
	return req ? req->engine : NULL;
}

static inline struct drm_i915_gem_request *
i915_gem_request_reference(struct drm_i915_gem_request *req)
{
	if (req)
		kref_get(&req->ref);
	return req;
}

static inline void
i915_gem_request_unreference(struct drm_i915_gem_request *req)
{
	kref_put(&req->ref, i915_gem_request_free);
}

static inline void i915_gem_request_assign(struct drm_i915_gem_request **pdst,
					   struct drm_i915_gem_request *src)
{
	if (src)
		i915_gem_request_reference(src);

	if (*pdst)
		i915_gem_request_unreference(*pdst);

	*pdst = src;
}

void __i915_add_request(struct drm_i915_gem_request *req,
			struct drm_i915_gem_object *batch_obj,
			bool flush_caches);
#define i915_add_request(req) \
	__i915_add_request(req, NULL, true)
#define i915_add_request_no_flush(req) \
	__i915_add_request(req, NULL, false)

struct intel_rps_client;

int __i915_wait_request(struct drm_i915_gem_request *req,
			bool interruptible,
			s64 *timeout,
			struct intel_rps_client *rps);
int __must_check i915_wait_request(struct drm_i915_gem_request *req);

static inline u32 intel_engine_get_seqno(struct intel_engine_cs *engine);

/**
 * Returns true if seq1 is later than seq2.
 */
static inline bool i915_seqno_passed(u32 seq1, u32 seq2)
{
	return (s32)(seq1 - seq2) >= 0;
}

static inline bool
i915_gem_request_started(const struct drm_i915_gem_request *req)
{
	return i915_seqno_passed(intel_engine_get_seqno(req->engine),
				 req->previous_seqno);
}

static inline bool
i915_gem_request_completed(const struct drm_i915_gem_request *req)
{
	return i915_seqno_passed(intel_engine_get_seqno(req->engine),
				 req->seqno);
}

bool __i915_spin_request(const struct drm_i915_gem_request *request,
			 int state, unsigned long timeout_us);
static inline bool i915_spin_request(const struct drm_i915_gem_request *request,
				     int state, unsigned long timeout_us)
{
	return (i915_gem_request_started(request) &&
		__i915_spin_request(request, state, timeout_us));
}

#endif /* I915_GEM_REQUEST_H */
