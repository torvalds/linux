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
#include "i915_syncmap.h"

static void __intel_timeline_init(struct intel_timeline *tl,
				  struct i915_gem_timeline *parent,
				  u64 context,
				  struct lock_class_key *lockclass,
				  const char *lockname)
{
	tl->fence_context = context;
	tl->common = parent;
	spin_lock_init(&tl->lock);
	lockdep_set_class_and_name(&tl->lock, lockclass, lockname);
	init_request_active(&tl->last_request, NULL);
	INIT_LIST_HEAD(&tl->requests);
	i915_syncmap_init(&tl->sync);
}

static void __intel_timeline_fini(struct intel_timeline *tl)
{
	GEM_BUG_ON(!list_empty(&tl->requests));

	i915_syncmap_free(&tl->sync);
}

static int __i915_gem_timeline_init(struct drm_i915_private *i915,
				    struct i915_gem_timeline *timeline,
				    const char *name,
				    struct lock_class_key *lockclass,
				    const char *lockname)
{
	unsigned int i;
	u64 fences;

	lockdep_assert_held(&i915->drm.struct_mutex);

	/*
	 * Ideally we want a set of engines on a single leaf as we expect
	 * to mostly be tracking synchronisation between engines. It is not
	 * a huge issue if this is not the case, but we may want to mitigate
	 * any page crossing penalties if they become an issue.
	 */
	BUILD_BUG_ON(KSYNCMAP < I915_NUM_ENGINES);

	timeline->i915 = i915;
	timeline->name = kstrdup(name ?: "[kernel]", GFP_KERNEL);
	if (!timeline->name)
		return -ENOMEM;

	list_add(&timeline->link, &i915->gt.timelines);

	/* Called during early_init before we know how many engines there are */
	fences = dma_fence_context_alloc(ARRAY_SIZE(timeline->engine));
	for (i = 0; i < ARRAY_SIZE(timeline->engine); i++)
		__intel_timeline_init(&timeline->engine[i],
				      timeline, fences++,
				      lockclass, lockname);

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

/**
 * i915_gem_timelines_park - called when the driver idles
 * @i915: the drm_i915_private device
 *
 * When the driver is completely idle, we know that all of our sync points
 * have been signaled and our tracking is then entirely redundant. Any request
 * to wait upon an older sync point will be completed instantly as we know
 * the fence is signaled and therefore we will not even look them up in the
 * sync point map.
 */
void i915_gem_timelines_park(struct drm_i915_private *i915)
{
	struct i915_gem_timeline *timeline;
	int i;

	lockdep_assert_held(&i915->drm.struct_mutex);

	list_for_each_entry(timeline, &i915->gt.timelines, link) {
		for (i = 0; i < ARRAY_SIZE(timeline->engine); i++) {
			struct intel_timeline *tl = &timeline->engine[i];

			/*
			 * All known fences are completed so we can scrap
			 * the current sync point tracking and start afresh,
			 * any attempt to wait upon a previous sync point
			 * will be skipped as the fence was signaled.
			 */
			i915_syncmap_free(&tl->sync);
		}
	}
}

void i915_gem_timeline_fini(struct i915_gem_timeline *timeline)
{
	int i;

	lockdep_assert_held(&timeline->i915->drm.struct_mutex);

	for (i = 0; i < ARRAY_SIZE(timeline->engine); i++)
		__intel_timeline_fini(&timeline->engine[i]);

	list_del(&timeline->link);
	kfree(timeline->name);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_timeline.c"
#include "selftests/i915_gem_timeline.c"
#endif
