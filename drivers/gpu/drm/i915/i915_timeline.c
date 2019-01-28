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
	struct i915_gt_timelines *gt = &i915->gt.timelines;

	/*
	 * Ideally we want a set of engines on a single leaf as we expect
	 * to mostly be tracking synchronisation between engines. It is not
	 * a huge issue if this is not the case, but we may want to mitigate
	 * any page crossing penalties if they become an issue.
	 */
	BUILD_BUG_ON(KSYNCMAP < I915_NUM_ENGINES);

	timeline->i915 = i915;
	timeline->name = name;

	mutex_lock(&gt->mutex);
	list_add(&timeline->link, &gt->list);
	mutex_unlock(&gt->mutex);

	/* Called during early_init before we know how many engines there are */

	timeline->fence_context = dma_fence_context_alloc(1);

	spin_lock_init(&timeline->lock);

	init_request_active(&timeline->last_request, NULL);
	INIT_LIST_HEAD(&timeline->requests);

	i915_syncmap_init(&timeline->sync);
}

void i915_timelines_init(struct drm_i915_private *i915)
{
	struct i915_gt_timelines *gt = &i915->gt.timelines;

	mutex_init(&gt->mutex);
	INIT_LIST_HEAD(&gt->list);

	/* via i915_gem_wait_for_idle() */
	i915_gem_shrinker_taints_mutex(i915, &gt->mutex);
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
	struct i915_gt_timelines *gt = &i915->gt.timelines;
	struct i915_timeline *timeline;

	mutex_lock(&gt->mutex);
	list_for_each_entry(timeline, &gt->list, link) {
		/*
		 * All known fences are completed so we can scrap
		 * the current sync point tracking and start afresh,
		 * any attempt to wait upon a previous sync point
		 * will be skipped as the fence was signaled.
		 */
		i915_syncmap_free(&timeline->sync);
	}
	mutex_unlock(&gt->mutex);
}

void i915_timeline_fini(struct i915_timeline *timeline)
{
	struct i915_gt_timelines *gt = &timeline->i915->gt.timelines;

	GEM_BUG_ON(!list_empty(&timeline->requests));

	i915_syncmap_free(&timeline->sync);

	mutex_lock(&gt->mutex);
	list_del(&timeline->link);
	mutex_unlock(&gt->mutex);
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

void i915_timelines_fini(struct drm_i915_private *i915)
{
	struct i915_gt_timelines *gt = &i915->gt.timelines;

	GEM_BUG_ON(!list_empty(&gt->list));

	mutex_destroy(&gt->mutex);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_timeline.c"
#include "selftests/i915_timeline.c"
#endif
