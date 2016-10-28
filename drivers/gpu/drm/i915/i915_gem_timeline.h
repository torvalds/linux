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

#include "i915_gem_request.h"

struct i915_gem_timeline;

struct intel_timeline {
	u64 fence_context;
	u32 last_submitted_seqno;
	u32 last_pending_seqno;

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
	u32 sync_seqno[I915_NUM_ENGINES];

	struct i915_gem_timeline *common;
};

struct i915_gem_timeline {
	struct list_head link;
	atomic_t next_seqno;

	struct drm_i915_private *i915;
	const char *name;

	struct intel_timeline engine[I915_NUM_ENGINES];
};

int i915_gem_timeline_init(struct drm_i915_private *i915,
			   struct i915_gem_timeline *tl,
			   const char *name);
void i915_gem_timeline_fini(struct i915_gem_timeline *tl);

#endif
