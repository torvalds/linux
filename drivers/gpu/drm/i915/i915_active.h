/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_ACTIVE_H_
#define _I915_ACTIVE_H_

#include <linux/lockdep.h>

#include "i915_active_types.h"
#include "i915_request.h"

/*
 * We treat requests as fences. This is not be to confused with our
 * "fence registers" but pipeline synchronisation objects ala GL_ARB_sync.
 * We use the fences to synchronize access from the CPU with activity on the
 * GPU, for example, we should not rewrite an object's PTE whilst the GPU
 * is reading them. We also track fences at a higher level to provide
 * implicit synchronisation around GEM objects, e.g. set-domain will wait
 * for outstanding GPU rendering before marking the object ready for CPU
 * access, or a pageflip will wait until the GPU is complete before showing
 * the frame on the scanout.
 *
 * In order to use a fence, the object must track the fence it needs to
 * serialise with. For example, GEM objects want to track both read and
 * write access so that we can perform concurrent read operations between
 * the CPU and GPU engines, as well as waiting for all rendering to
 * complete, or waiting for the last GPU user of a "fence register". The
 * object then embeds a #i915_active_request to track the most recent (in
 * retirement order) request relevant for the desired mode of access.
 * The #i915_active_request is updated with i915_active_request_set() to
 * track the most recent fence request, typically this is done as part of
 * i915_vma_move_to_active().
 *
 * When the #i915_active_request completes (is retired), it will
 * signal its completion to the owner through a callback as well as mark
 * itself as idle (i915_active_request.request == NULL). The owner
 * can then perform any action, such as delayed freeing of an active
 * resource including itself.
 */

void i915_active_retire_noop(struct i915_active_request *active,
			     struct i915_request *request);

/**
 * i915_active_request_init - prepares the activity tracker for use
 * @active - the active tracker
 * @rq - initial request to track, can be NULL
 * @func - a callback when then the tracker is retired (becomes idle),
 *         can be NULL
 *
 * i915_active_request_init() prepares the embedded @active struct for use as
 * an activity tracker, that is for tracking the last known active request
 * associated with it. When the last request becomes idle, when it is retired
 * after completion, the optional callback @func is invoked.
 */
static inline void
i915_active_request_init(struct i915_active_request *active,
			 struct i915_request *rq,
			 i915_active_retire_fn retire)
{
	RCU_INIT_POINTER(active->request, rq);
	INIT_LIST_HEAD(&active->link);
	active->retire = retire ?: i915_active_retire_noop;
}

#define INIT_ACTIVE_REQUEST(name) i915_active_request_init((name), NULL, NULL)

/**
 * i915_active_request_set - updates the tracker to watch the current request
 * @active - the active tracker
 * @request - the request to watch
 *
 * __i915_active_request_set() watches the given @request for completion. Whilst
 * that @request is busy, the @active reports busy. When that @request is
 * retired, the @active tracker is updated to report idle.
 */
static inline void
__i915_active_request_set(struct i915_active_request *active,
			  struct i915_request *request)
{
	list_move(&active->link, &request->active_list);
	rcu_assign_pointer(active->request, request);
}

int __must_check
i915_active_request_set(struct i915_active_request *active,
			struct i915_request *rq);

/**
 * i915_active_request_set_retire_fn - updates the retirement callback
 * @active - the active tracker
 * @fn - the routine called when the request is retired
 * @mutex - struct_mutex used to guard retirements
 *
 * i915_active_request_set_retire_fn() updates the function pointer that
 * is called when the final request associated with the @active tracker
 * is retired.
 */
static inline void
i915_active_request_set_retire_fn(struct i915_active_request *active,
				  i915_active_retire_fn fn,
				  struct mutex *mutex)
{
	lockdep_assert_held(mutex);
	active->retire = fn ?: i915_active_retire_noop;
}

/**
 * i915_active_request_raw - return the active request
 * @active - the active tracker
 *
 * i915_active_request_raw() returns the current request being tracked, or NULL.
 * It does not obtain a reference on the request for the caller, so the caller
 * must hold struct_mutex.
 */
static inline struct i915_request *
i915_active_request_raw(const struct i915_active_request *active,
			struct mutex *mutex)
{
	return rcu_dereference_protected(active->request,
					 lockdep_is_held(mutex));
}

/**
 * i915_active_request_peek - report the active request being monitored
 * @active - the active tracker
 *
 * i915_active_request_peek() returns the current request being tracked if
 * still active, or NULL. It does not obtain a reference on the request
 * for the caller, so the caller must hold struct_mutex.
 */
static inline struct i915_request *
i915_active_request_peek(const struct i915_active_request *active,
			 struct mutex *mutex)
{
	struct i915_request *request;

	request = i915_active_request_raw(active, mutex);
	if (!request || i915_request_completed(request))
		return NULL;

	return request;
}

/**
 * i915_active_request_get - return a reference to the active request
 * @active - the active tracker
 *
 * i915_active_request_get() returns a reference to the active request, or NULL
 * if the active tracker is idle. The caller must hold struct_mutex.
 */
static inline struct i915_request *
i915_active_request_get(const struct i915_active_request *active,
			struct mutex *mutex)
{
	return i915_request_get(i915_active_request_peek(active, mutex));
}

/**
 * __i915_active_request_get_rcu - return a reference to the active request
 * @active - the active tracker
 *
 * __i915_active_request_get() returns a reference to the active request,
 * or NULL if the active tracker is idle. The caller must hold the RCU read
 * lock, but the returned pointer is safe to use outside of RCU.
 */
static inline struct i915_request *
__i915_active_request_get_rcu(const struct i915_active_request *active)
{
	/*
	 * Performing a lockless retrieval of the active request is super
	 * tricky. SLAB_TYPESAFE_BY_RCU merely guarantees that the backing
	 * slab of request objects will not be freed whilst we hold the
	 * RCU read lock. It does not guarantee that the request itself
	 * will not be freed and then *reused*. Viz,
	 *
	 * Thread A			Thread B
	 *
	 * rq = active.request
	 *				retire(rq) -> free(rq);
	 *				(rq is now first on the slab freelist)
	 *				active.request = NULL
	 *
	 *				rq = new submission on a new object
	 * ref(rq)
	 *
	 * To prevent the request from being reused whilst the caller
	 * uses it, we take a reference like normal. Whilst acquiring
	 * the reference we check that it is not in a destroyed state
	 * (refcnt == 0). That prevents the request being reallocated
	 * whilst the caller holds on to it. To check that the request
	 * was not reallocated as we acquired the reference we have to
	 * check that our request remains the active request across
	 * the lookup, in the same manner as a seqlock. The visibility
	 * of the pointer versus the reference counting is controlled
	 * by using RCU barriers (rcu_dereference and rcu_assign_pointer).
	 *
	 * In the middle of all that, we inspect whether the request is
	 * complete. Retiring is lazy so the request may be completed long
	 * before the active tracker is updated. Querying whether the
	 * request is complete is far cheaper (as it involves no locked
	 * instructions setting cachelines to exclusive) than acquiring
	 * the reference, so we do it first. The RCU read lock ensures the
	 * pointer dereference is valid, but does not ensure that the
	 * seqno nor HWS is the right one! However, if the request was
	 * reallocated, that means the active tracker's request was complete.
	 * If the new request is also complete, then both are and we can
	 * just report the active tracker is idle. If the new request is
	 * incomplete, then we acquire a reference on it and check that
	 * it remained the active request.
	 *
	 * It is then imperative that we do not zero the request on
	 * reallocation, so that we can chase the dangling pointers!
	 * See i915_request_alloc().
	 */
	do {
		struct i915_request *request;

		request = rcu_dereference(active->request);
		if (!request || i915_request_completed(request))
			return NULL;

		/*
		 * An especially silly compiler could decide to recompute the
		 * result of i915_request_completed, more specifically
		 * re-emit the load for request->fence.seqno. A race would catch
		 * a later seqno value, which could flip the result from true to
		 * false. Which means part of the instructions below might not
		 * be executed, while later on instructions are executed. Due to
		 * barriers within the refcounting the inconsistency can't reach
		 * past the call to i915_request_get_rcu, but not executing
		 * that while still executing i915_request_put() creates
		 * havoc enough.  Prevent this with a compiler barrier.
		 */
		barrier();

		request = i915_request_get_rcu(request);

		/*
		 * What stops the following rcu_access_pointer() from occurring
		 * before the above i915_request_get_rcu()? If we were
		 * to read the value before pausing to get the reference to
		 * the request, we may not notice a change in the active
		 * tracker.
		 *
		 * The rcu_access_pointer() is a mere compiler barrier, which
		 * means both the CPU and compiler are free to perform the
		 * memory read without constraint. The compiler only has to
		 * ensure that any operations after the rcu_access_pointer()
		 * occur afterwards in program order. This means the read may
		 * be performed earlier by an out-of-order CPU, or adventurous
		 * compiler.
		 *
		 * The atomic operation at the heart of
		 * i915_request_get_rcu(), see dma_fence_get_rcu(), is
		 * atomic_inc_not_zero() which is only a full memory barrier
		 * when successful. That is, if i915_request_get_rcu()
		 * returns the request (and so with the reference counted
		 * incremented) then the following read for rcu_access_pointer()
		 * must occur after the atomic operation and so confirm
		 * that this request is the one currently being tracked.
		 *
		 * The corresponding write barrier is part of
		 * rcu_assign_pointer().
		 */
		if (!request || request == rcu_access_pointer(active->request))
			return rcu_pointer_handoff(request);

		i915_request_put(request);
	} while (1);
}

/**
 * i915_active_request_get_unlocked - return a reference to the active request
 * @active - the active tracker
 *
 * i915_active_request_get_unlocked() returns a reference to the active request,
 * or NULL if the active tracker is idle. The reference is obtained under RCU,
 * so no locking is required by the caller.
 *
 * The reference should be freed with i915_request_put().
 */
static inline struct i915_request *
i915_active_request_get_unlocked(const struct i915_active_request *active)
{
	struct i915_request *request;

	rcu_read_lock();
	request = __i915_active_request_get_rcu(active);
	rcu_read_unlock();

	return request;
}

/**
 * i915_active_request_isset - report whether the active tracker is assigned
 * @active - the active tracker
 *
 * i915_active_request_isset() returns true if the active tracker is currently
 * assigned to a request. Due to the lazy retiring, that request may be idle
 * and this may report stale information.
 */
static inline bool
i915_active_request_isset(const struct i915_active_request *active)
{
	return rcu_access_pointer(active->request);
}

/**
 * i915_active_request_retire - waits until the request is retired
 * @active - the active request on which to wait
 *
 * i915_active_request_retire() waits until the request is completed,
 * and then ensures that at least the retirement handler for this
 * @active tracker is called before returning. If the @active
 * tracker is idle, the function returns immediately.
 */
static inline int __must_check
i915_active_request_retire(struct i915_active_request *active,
			   struct mutex *mutex)
{
	struct i915_request *request;
	long ret;

	request = i915_active_request_raw(active, mutex);
	if (!request)
		return 0;

	ret = i915_request_wait(request,
				I915_WAIT_INTERRUPTIBLE | I915_WAIT_LOCKED,
				MAX_SCHEDULE_TIMEOUT);
	if (ret < 0)
		return ret;

	list_del_init(&active->link);
	RCU_INIT_POINTER(active->request, NULL);

	active->retire(active, request);

	return 0;
}

/*
 * GPU activity tracking
 *
 * Each set of commands submitted to the GPU compromises a single request that
 * signals a fence upon completion. struct i915_request combines the
 * command submission, scheduling and fence signaling roles. If we want to see
 * if a particular task is complete, we need to grab the fence (struct
 * i915_request) for that task and check or wait for it to be signaled. More
 * often though we want to track the status of a bunch of tasks, for example
 * to wait for the GPU to finish accessing some memory across a variety of
 * different command pipelines from different clients. We could choose to
 * track every single request associated with the task, but knowing that
 * each request belongs to an ordered timeline (later requests within a
 * timeline must wait for earlier requests), we need only track the
 * latest request in each timeline to determine the overall status of the
 * task.
 *
 * struct i915_active provides this tracking across timelines. It builds a
 * composite shared-fence, and is updated as new work is submitted to the task,
 * forming a snapshot of the current status. It should be embedded into the
 * different resources that need to track their associated GPU activity to
 * provide a callback when that GPU activity has ceased, or otherwise to
 * provide a serialisation point either for request submission or for CPU
 * synchronisation.
 */

void i915_active_init(struct drm_i915_private *i915,
		      struct i915_active *ref,
		      void (*retire)(struct i915_active *ref));

int i915_active_ref(struct i915_active *ref,
		    u64 timeline,
		    struct i915_request *rq);

int i915_active_wait(struct i915_active *ref);

int i915_request_await_active(struct i915_request *rq,
			      struct i915_active *ref);
int i915_request_await_active_request(struct i915_request *rq,
				      struct i915_active_request *active);

bool i915_active_acquire(struct i915_active *ref);

static inline void i915_active_cancel(struct i915_active *ref)
{
	GEM_BUG_ON(ref->count != 1);
	ref->count = 0;
}

void i915_active_release(struct i915_active *ref);

static inline bool
i915_active_is_idle(const struct i915_active *ref)
{
	return !ref->count;
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GEM)
void i915_active_fini(struct i915_active *ref);
#else
static inline void i915_active_fini(struct i915_active *ref) { }
#endif

int i915_active_acquire_preallocate_barrier(struct i915_active *ref,
					    struct intel_engine_cs *engine);
void i915_active_acquire_barrier(struct i915_active *ref);
void i915_request_add_barriers(struct i915_request *rq);

#endif /* _I915_ACTIVE_H_ */
