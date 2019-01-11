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

#ifndef I915_TIMELINE_H
#define I915_TIMELINE_H

#include <linux/list.h>
#include <linux/kref.h>

#include "i915_request.h"
#include "i915_syncmap.h"
#include "i915_utils.h"

struct i915_timeline {
	u64 fence_context;
	u32 seqno;

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

	struct list_head link;
	const char *name;

	struct kref kref;
};

void i915_timeline_init(struct drm_i915_private *i915,
			struct i915_timeline *tl,
			const char *name);
void i915_timeline_fini(struct i915_timeline *tl);

struct i915_timeline *
i915_timeline_create(struct drm_i915_private *i915, const char *name);

static inline struct i915_timeline *
i915_timeline_get(struct i915_timeline *timeline)
{
	kref_get(&timeline->kref);
	return timeline;
}

void __i915_timeline_free(struct kref *kref);
static inline void i915_timeline_put(struct i915_timeline *timeline)
{
	kref_put(&timeline->kref, __i915_timeline_free);
}

static inline int __i915_timeline_sync_set(struct i915_timeline *tl,
					   u64 context, u32 seqno)
{
	return i915_syncmap_set(&tl->sync, context, seqno);
}

static inline int i915_timeline_sync_set(struct i915_timeline *tl,
					 const struct dma_fence *fence)
{
	return __i915_timeline_sync_set(tl, fence->context, fence->seqno);
}

static inline bool __i915_timeline_sync_is_later(struct i915_timeline *tl,
						 u64 context, u32 seqno)
{
	return i915_syncmap_is_later(&tl->sync, context, seqno);
}

static inline bool i915_timeline_sync_is_later(struct i915_timeline *tl,
					       const struct dma_fence *fence)
{
	return __i915_timeline_sync_is_later(tl, fence->context, fence->seqno);
}

void i915_timelines_park(struct drm_i915_private *i915);

#endif
