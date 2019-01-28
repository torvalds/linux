/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016-2018 Intel Corporation
 */

#include "i915_drv.h"

#include "i915_timeline.h"
#include "i915_syncmap.h"

static struct i915_vma *__hwsp_alloc(struct drm_i915_private *i915)
{
	struct drm_i915_gem_object *obj;
	struct i915_vma *vma;

	obj = i915_gem_object_create_internal(i915, PAGE_SIZE);
	if (IS_ERR(obj))
		return ERR_CAST(obj);

	i915_gem_object_set_cache_coherency(obj, I915_CACHE_LLC);

	vma = i915_vma_instance(obj, &i915->ggtt.vm, NULL);
	if (IS_ERR(vma))
		i915_gem_object_put(obj);

	return vma;
}

static int hwsp_alloc(struct i915_timeline *timeline)
{
	struct i915_vma *vma;

	vma = __hwsp_alloc(timeline->i915);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	timeline->hwsp_ggtt = vma;
	timeline->hwsp_offset = 0;

	return 0;
}

int i915_timeline_init(struct drm_i915_private *i915,
		       struct i915_timeline *timeline,
		       const char *name,
		       struct i915_vma *global_hwsp)
{
	struct i915_gt_timelines *gt = &i915->gt.timelines;
	void *vaddr;
	int err;

	/*
	 * Ideally we want a set of engines on a single leaf as we expect
	 * to mostly be tracking synchronisation between engines. It is not
	 * a huge issue if this is not the case, but we may want to mitigate
	 * any page crossing penalties if they become an issue.
	 *
	 * Called during early_init before we know how many engines there are.
	 */
	BUILD_BUG_ON(KSYNCMAP < I915_NUM_ENGINES);

	timeline->i915 = i915;
	timeline->name = name;
	timeline->pin_count = 0;

	if (global_hwsp) {
		timeline->hwsp_ggtt = i915_vma_get(global_hwsp);
		timeline->hwsp_offset = I915_GEM_HWS_SEQNO_ADDR;
	} else {
		err = hwsp_alloc(timeline);
		if (err)
			return err;
	}

	vaddr = i915_gem_object_pin_map(timeline->hwsp_ggtt->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		i915_vma_put(timeline->hwsp_ggtt);
		return PTR_ERR(vaddr);
	}

	timeline->hwsp_seqno =
		memset(vaddr + timeline->hwsp_offset, 0, CACHELINE_BYTES);

	timeline->fence_context = dma_fence_context_alloc(1);

	spin_lock_init(&timeline->lock);

	init_request_active(&timeline->last_request, NULL);
	INIT_LIST_HEAD(&timeline->requests);

	i915_syncmap_init(&timeline->sync);

	mutex_lock(&gt->mutex);
	list_add(&timeline->link, &gt->list);
	mutex_unlock(&gt->mutex);

	return 0;
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

	GEM_BUG_ON(timeline->pin_count);
	GEM_BUG_ON(!list_empty(&timeline->requests));

	i915_syncmap_free(&timeline->sync);

	mutex_lock(&gt->mutex);
	list_del(&timeline->link);
	mutex_unlock(&gt->mutex);

	i915_gem_object_unpin_map(timeline->hwsp_ggtt->obj);
	i915_vma_put(timeline->hwsp_ggtt);
}

struct i915_timeline *
i915_timeline_create(struct drm_i915_private *i915,
		     const char *name,
		     struct i915_vma *global_hwsp)
{
	struct i915_timeline *timeline;
	int err;

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline)
		return ERR_PTR(-ENOMEM);

	err = i915_timeline_init(i915, timeline, name, global_hwsp);
	if (err) {
		kfree(timeline);
		return ERR_PTR(err);
	}

	kref_init(&timeline->kref);

	return timeline;
}

int i915_timeline_pin(struct i915_timeline *tl)
{
	int err;

	if (tl->pin_count++)
		return 0;
	GEM_BUG_ON(!tl->pin_count);

	err = i915_vma_pin(tl->hwsp_ggtt, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (err)
		goto unpin;

	return 0;

unpin:
	tl->pin_count = 0;
	return err;
}

void i915_timeline_unpin(struct i915_timeline *tl)
{
	GEM_BUG_ON(!tl->pin_count);
	if (--tl->pin_count)
		return;

	/*
	 * Since this timeline is idle, all bariers upon which we were waiting
	 * must also be complete and so we can discard the last used barriers
	 * without loss of information.
	 */
	i915_syncmap_free(&tl->sync);

	__i915_vma_unpin(tl->hwsp_ggtt);
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
