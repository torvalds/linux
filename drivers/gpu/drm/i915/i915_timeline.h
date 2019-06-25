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

#include <linux/lockdep.h>

#include "i915_active.h"
#include "i915_syncmap.h"
#include "i915_timeline_types.h"

int i915_timeline_init(struct drm_i915_private *i915,
		       struct i915_timeline *tl,
		       struct i915_vma *hwsp);
void i915_timeline_fini(struct i915_timeline *tl);

struct i915_timeline *
i915_timeline_create(struct drm_i915_private *i915,
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
int i915_timeline_get_seqno(struct i915_timeline *tl,
			    struct i915_request *rq,
			    u32 *seqno);
void i915_timeline_unpin(struct i915_timeline *tl);

int i915_timeline_read_hwsp(struct i915_request *from,
			    struct i915_request *until,
			    u32 *hwsp_offset);

void i915_timelines_init(struct drm_i915_private *i915);
void i915_timelines_park(struct drm_i915_private *i915);
void i915_timelines_fini(struct drm_i915_private *i915);

#endif
