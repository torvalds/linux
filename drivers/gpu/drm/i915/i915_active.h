/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 */

#ifndef _I915_ACTIVE_H_
#define _I915_ACTIVE_H_

#include "i915_active_types.h"

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

#endif /* _I915_ACTIVE_H_ */
