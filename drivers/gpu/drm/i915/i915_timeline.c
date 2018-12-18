/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016-2018 Intel Corporation
 */

#include "i915_drv.h"

#include "i915_timeline.h"
#include "i915_syncmap.h"

void i915_timeline_init(struct drm_i915_private *i915,
			struct i915_timeline *timeline,
			const char *name)
{
	lockdep_assert_held(&i915->drm.struct_mutex);

	/*
	 * Ideally we want a set of engines on a single leaf as we expect
	 * to mostly be tracking synchronisation between engines. It is not
	 * a huge issue if this is not the case, but we may want to mitigate
	 * any page crossing penalties if they become an issue.
	 */
	BUILD_BUG_ON(KSYNCMAP < I915_NUM_ENGINES);

	timeline->name = name;

	list_add(&timeline->link, &i915->gt.timelines);

	/* Called during early_init before we know how many engines there are */

	timeline->fence_context = dma_fence_context_alloc(1);

	spin_lock_init(&timeline->lock);

	init_request_active(&timeline->last_request, NULL);
	INIT_LIST_HEAD(&timeline->requests);

	i915_syncmap_init(&timeline->sync);
}

/**
 * i915_timelines_park - called when the driver idles
 * @i915: the drm_i915_private device
 *
 * When the driver is completely idle, we know that all of our sync points
 * have been signaled and our tracking is then entirely redundant. Any request
 * to wait upon an older sync point will be completed instantly as we know
 * the fence is signaled and therefore we will not even look them up in the
 * sync point map.
 */
void i915_timelines_park(struct drm_i915_private *i915)
{
	struct i915_timeline *timeline;

	lockdep_assert_held(&i915->drm.struct_mutex);

	list_for_each_entry(timeline, &i915->gt.timelines, link) {
		/*
		 * All known fences are completed so we can scrap
		 * the current sync point tracking and start afresh,
		 * any attempt to wait upon a previous sync point
		 * will be skipped as the fence was signaled.
		 */
		i915_syncmap_free(&timeline->sync);
	}
}

void i915_timeline_fini(struct i915_timeline *timeline)
{
	GEM_BUG_ON(!list_empty(&timeline->requests));

	i915_syncmap_free(&timeline->sync);

	list_del(&timeline->link);
}

struct i915_timeline *
i915_timeline_create(struct drm_i915_private *i915, const char *name)
{
	struct i915_timeline *timeline;

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline)
		return ERR_PTR(-ENOMEM);

	i915_timeline_init(i915, timeline, name);
	kref_init(&timeline->kref);

	return timeline;
}

void __i915_timeline_free(struct kref *kref)
{
	struct i915_timeline *timeline =
		container_of(kref, typeof(*timeline), kref);

	i915_timeline_fini(timeline);
	kfree(timeline);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_timeline.c"
#include "selftests/i915_timeline.c"
#endif
