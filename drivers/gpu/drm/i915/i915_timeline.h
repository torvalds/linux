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

#include "i915_active.h"
#include "i915_request.h"
#include "i915_syncmap.h"
#include "i915_utils.h"

struct i915_vma;
struct i915_timeline_hwsp;

struct i915_timeline {
	u64 fence_context;
	u32 seqno;

	spinlock_t lock;
#define TIMELINE_CLIENT 0 /* default subclass */
#define TIMELINE_ENGINE 1

	unsigned int pin_count;
	const u32 *hwsp_seqno;
	struct i915_vma *hwsp_ggtt;
	u32 hwsp_offset;

	bool has_initial_breadcrumb;

	/**
	 * List of breadcrumbs associated with GPU requests currently
	 * outstanding.
	 */
	struct list_head requests;

	/* Contains an RCU guarded pointer to the last request. No reference is
	 * held to the request, users must carefully acquire a reference to
	 * the request using i915_active_request_get_request_rcu(), or hold the
	 * struct_mutex.
	 */
	struct i915_active_request last_request;

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
	 * Barrier provides the ability to serialize ordering between different
	 * timelines.
	 *
	 * Users can call i915_timeline_set_barrier which will make all
	 * subsequent submissions to this timeline be executed only after the
	 * barrier has been completed.
	 */
	struct i915_active_request barrier;

	struct list_head link;
	const char *name;
	struct drm_i915_private *i915;

	struct kref kref;
};

int i915_timeline_init(struct drm_i915_private *i915,
		       struct i915_timeline *tl,
		       const char *name,
		       struct i915_vma *hwsp);
void i915_timeline_fini(struct i915_timeline *tl);

static inline void
i915_timeline_set_subclass(struct i915_timeline *timeline,
			   unsigned int subclass)
{
	lockdep_set_subclass(&timeline->lock, subclass);

	/*
	 * Due to an interesting quirk in lockdep's internal debug tracking,
	 * after setting a subclass we must ensure the lock is used. Otherwise,
	 * nr_unused_locks is incremented once too often.
	 */
#ifdef CONFIG_DEBUG_LOCK_ALLOC
	local_irq_disable();
	lock_map_acquire(&timeline->lock.dep_map);
	lock_map_release(&timeline->lock.dep_map);
	local_irq_enable();
#endif
}

struct i915_timeline *
i915_timeline_create(struct drm_i915_private *i915,
		     const char *name,
		     struct i915_vma *global_hwsp);

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

int i915_timeline_pin(struct i915_timeline *tl);
void i915_timeline_unpin(struct i915_timeline *tl);

void i915_timelines_init(struct drm_i915_private *i915);
void i915_timelines_park(struct drm_i915_private *i915);
void i915_timelines_fini(struct drm_i915_private *i915);

/**
 * i915_timeline_set_barrier - orders submission between different timelines
 * @timeline: timeline to set the barrier on
 * @rq: request after which new submissions can proceed
 *
 * Sets the passed in request as the serialization point for all subsequent
 * submissions on @timeline. Subsequent requests will not be submitted to GPU
 * until the barrier has been completed.
 */
static inline int
i915_timeline_set_barrier(struct i915_timeline *tl, struct i915_request *rq)
{
	return i915_active_request_set(&tl->barrier, rq);
}

#endif
