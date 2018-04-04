/*
 * Copyright © 2016 Intel Corporation
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

#ifndef I915_GEM_TIMELINE_H
#define I915_GEM_TIMELINE_H

#include <linux/list.h>

#include "i915_utils.h"
#include "i915_gem_request.h"
#include "i915_syncmap.h"

struct i915_gem_timeline;

struct intel_timeline {
	u64 fence_context;
	u32 seqno;

	/**
	 * Count of outstanding requests, from the time they are constructed
	 * to the moment they are retired. Loosely coupled to hardware.
	 */
	u32 inflight_seqnos;

	spinlock_t lock;

	/**
	 * List of breadcrumbs associated with GPU requests currently
	 * outstanding.
	 */
	struct list_head requests;

	/* Contains an RCU guarded pointer to the last request. No reference is
	 * held to the request, users must carefully acquire a reference to
	 * the request using i915_gem_active_get_request_rcu(), or hold the
	 * struct_mutex.
	 */
	struct i915_gem_active last_request;

	/**
	 * We track the most recent seqno that we wait on in every context so
	 * that we only have to emit a new await and dependency on a more
	 * recent sync point. As the contexts may be executed out-of-order, we
	 * have to track each individually and can not rely on an absolute
	 * global_seqno. When we know that all tracked fences are completed
	 * (i.e. when the driver is idle), we know that the syncmap is
	 * redundant and we can discard it without loss of generality.
	 */
	struct i915_syncmap *sync;
	/**
	 * Separately to the inter-context seqno map above, we track the last
	 * barrier (e.g. semaphore wait) to the global engine timelines. Note
	 * that this tracks global_seqno rather than the context.seqno, and
	 * so it is subject to the limitations of hw wraparound and that we
	 * may need to revoke global_seqno (on pre-emption).
	 */
	u32 global_sync[I915_NUM_ENGINES];

	struct i915_gem_timeline *common;
};

struct i915_gem_timeline {
	struct list_head link;

	struct drm_i915_private *i915;
	const char *name;

	struct intel_timeline engine[I915_NUM_ENGINES];
};

int i915_gem_timeline_init(struct drm_i915_private *i915,
			   struct i915_gem_timeline *tl,
			   const char *name);
int i915_gem_timeline_init__global(struct drm_i915_private *i915);
void i915_gem_timelines_park(struct drm_i915_private *i915);
void i915_gem_timeline_fini(struct i915_gem_timeline *tl);

static inline int __intel_timeline_sync_set(struct intel_timeline *tl,
					    u64 context, u32 seqno)
{
	return i915_syncmap_set(&tl->sync, context, seqno);
}

static inline int intel_timeline_sync_set(struct intel_timeline *tl,
					  const struct dma_fence *fence)
{
	return __intel_timeline_sync_set(tl, fence->context, fence->seqno);
}

static inline bool __intel_timeline_sync_is_later(struct intel_timeline *tl,
						  u64 context, u32 seqno)
{
	return i915_syncmap_is_later(&tl->sync, context, seqno);
}

static inline bool intel_timeline_sync_is_later(struct intel_timeline *tl,
						const struct dma_fence *fence)
{
	return __intel_timeline_sync_is_later(tl, fence->context, fence->seqno);
}

#endif
