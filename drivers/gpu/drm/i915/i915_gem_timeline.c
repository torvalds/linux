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

#include "i915_drv.h"

static int __i915_gem_timeline_init(struct drm_i915_private *i915,
				    struct i915_gem_timeline *timeline,
				    const char *name,
				    struct lock_class_key *lockclass,
				    const char *lockname)
{
	unsigned int i;
	u64 fences;

	lockdep_assert_held(&i915->drm.struct_mutex);

	timeline->i915 = i915;
	timeline->name = kstrdup(name ?: "[kernel]", GFP_KERNEL);
	if (!timeline->name)
		return -ENOMEM;

	list_add(&timeline->link, &i915->gt.timelines);

	/* Called during early_init before we know how many engines there are */
	fences = dma_fence_context_alloc(ARRAY_SIZE(timeline->engine));
	for (i = 0; i < ARRAY_SIZE(timeline->engine); i++) {
		struct intel_timeline *tl = &timeline->engine[i];

		tl->fence_context = fences++;
		tl->common = timeline;
#ifdef CONFIG_DEBUG_SPINLOCK
		__raw_spin_lock_init(&tl->lock.rlock, lockname, lockclass);
#else
		spin_lock_init(&tl->lock);
#endif
		init_request_active(&tl->last_request, NULL);
		INIT_LIST_HEAD(&tl->requests);
	}

	return 0;
}

int i915_gem_timeline_init(struct drm_i915_private *i915,
			   struct i915_gem_timeline *timeline,
			   const char *name)
{
	static struct lock_class_key class;

	return __i915_gem_timeline_init(i915, timeline, name,
					&class, "&timeline->lock");
}

int i915_gem_timeline_init__global(struct drm_i915_private *i915)
{
	static struct lock_class_key class;

	return __i915_gem_timeline_init(i915,
					&i915->gt.global_timeline,
					"[execution]",
					&class, "&global_timeline->lock");
}

void i915_gem_timeline_fini(struct i915_gem_timeline *tl)
{
	lockdep_assert_held(&tl->i915->drm.struct_mutex);

	list_del(&tl->link);
	kfree(tl->name);
}
