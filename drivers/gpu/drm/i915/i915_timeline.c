/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016-2018 Intel Corporation
 */

#include "i915_drv.h"

#include "i915_timeline.h"
#include "i915_syncmap.h"

struct i915_timeline_hwsp {
	struct i915_vma *vma;
	struct list_head free_link;
	u64 free_bitmap;
};

static inline struct i915_timeline_hwsp *
i915_timeline_hwsp(const struct i915_timeline *tl)
{
	return tl->hwsp_ggtt->private;
}

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

static struct i915_vma *
hwsp_alloc(struct i915_timeline *timeline, unsigned int *cacheline)
{
	struct drm_i915_private *i915 = timeline->i915;
	struct i915_gt_timelines *gt = &i915->gt.timelines;
	struct i915_timeline_hwsp *hwsp;

	BUILD_BUG_ON(BITS_PER_TYPE(u64) * CACHELINE_BYTES > PAGE_SIZE);

	spin_lock(&gt->hwsp_lock);

	/* hwsp_free_list only contains HWSP that have available cachelines */
	hwsp = list_first_entry_or_null(&gt->hwsp_free_list,
					typeof(*hwsp), free_link);
	if (!hwsp) {
		struct i915_vma *vma;

		spin_unlock(&gt->hwsp_lock);

		hwsp = kmalloc(sizeof(*hwsp), GFP_KERNEL);
		if (!hwsp)
			return ERR_PTR(-ENOMEM);

		vma = __hwsp_alloc(i915);
		if (IS_ERR(vma)) {
			kfree(hwsp);
			return vma;
		}

		vma->private = hwsp;
		hwsp->vma = vma;
		hwsp->free_bitmap = ~0ull;

		spin_lock(&gt->hwsp_lock);
		list_add(&hwsp->free_link, &gt->hwsp_free_list);
	}

	GEM_BUG_ON(!hwsp->free_bitmap);
	*cacheline = __ffs64(hwsp->free_bitmap);
	hwsp->free_bitmap &= ~BIT_ULL(*cacheline);
	if (!hwsp->free_bitmap)
		list_del(&hwsp->free_link);

	spin_unlock(&gt->hwsp_lock);

	GEM_BUG_ON(hwsp->vma->private != hwsp);
	return hwsp->vma;
}

static void hwsp_free(struct i915_timeline *timeline)
{
	struct i915_gt_timelines *gt = &timeline->i915->gt.timelines;
	struct i915_timeline_hwsp *hwsp;

	hwsp = i915_timeline_hwsp(timeline);
	if (!hwsp) /* leave global HWSP alone! */
		return;

	spin_lock(&gt->hwsp_lock);

	/* As a cacheline becomes available, publish the HWSP on the freelist */
	if (!hwsp->free_bitmap)
		list_add_tail(&hwsp->free_link, &gt->hwsp_free_list);

	hwsp->free_bitmap |= BIT_ULL(timeline->hwsp_offset / CACHELINE_BYTES);

	/* And if no one is left using it, give the page back to the system */
	if (hwsp->free_bitmap == ~0ull) {
		i915_vma_put(hwsp->vma);
		list_del(&hwsp->free_link);
		kfree(hwsp);
	}

	spin_unlock(&gt->hwsp_lock);
}

int i915_timeline_init(struct drm_i915_private *i915,
		       struct i915_timeline *timeline,
		       const char *name,
		       struct i915_vma *hwsp)
{
	void *vaddr;

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
	timeline->has_initial_breadcrumb = !hwsp;

	timeline->hwsp_offset = I915_GEM_HWS_SEQNO_ADDR;
	if (!hwsp) {
		unsigned int cacheline;

		hwsp = hwsp_alloc(timeline, &cacheline);
		if (IS_ERR(hwsp))
			return PTR_ERR(hwsp);

		timeline->hwsp_offset = cacheline * CACHELINE_BYTES;
	}
	timeline->hwsp_ggtt = i915_vma_get(hwsp);

	vaddr = i915_gem_object_pin_map(hwsp->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		hwsp_free(timeline);
		i915_vma_put(hwsp);
		return PTR_ERR(vaddr);
	}

	timeline->hwsp_seqno =
		memset(vaddr + timeline->hwsp_offset, 0, CACHELINE_BYTES);

	timeline->fence_context = dma_fence_context_alloc(1);

	spin_lock_init(&timeline->lock);

	INIT_ACTIVE_REQUEST(&timeline->barrier);
	INIT_ACTIVE_REQUEST(&timeline->last_request);
	INIT_LIST_HEAD(&timeline->requests);

	i915_syncmap_init(&timeline->sync);

	return 0;
}

void i915_timelines_init(struct drm_i915_private *i915)
{
	struct i915_gt_timelines *gt = &i915->gt.timelines;

	mutex_init(&gt->mutex);
	INIT_LIST_HEAD(&gt->active_list);

	spin_lock_init(&gt->hwsp_lock);
	INIT_LIST_HEAD(&gt->hwsp_free_list);

	/* via i915_gem_wait_for_idle() */
	i915_gem_shrinker_taints_mutex(i915, &gt->mutex);
}

static void timeline_add_to_active(struct i915_timeline *tl)
{
	struct i915_gt_timelines *gt = &tl->i915->gt.timelines;

	mutex_lock(&gt->mutex);
	list_add(&tl->link, &gt->active_list);
	mutex_unlock(&gt->mutex);
}

static void timeline_remove_from_active(struct i915_timeline *tl)
{
	struct i915_gt_timelines *gt = &tl->i915->gt.timelines;

	mutex_lock(&gt->mutex);
	list_del(&tl->link);
	mutex_unlock(&gt->mutex);
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
	list_for_each_entry(timeline, &gt->active_list, link) {
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
	GEM_BUG_ON(timeline->pin_count);
	GEM_BUG_ON(!list_empty(&timeline->requests));
	GEM_BUG_ON(i915_active_request_isset(&timeline->barrier));

	i915_syncmap_free(&timeline->sync);
	hwsp_free(timeline);

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

	tl->hwsp_offset =
		i915_ggtt_offset(tl->hwsp_ggtt) +
		offset_in_page(tl->hwsp_offset);

	timeline_add_to_active(tl);

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

	timeline_remove_from_active(tl);

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

	GEM_BUG_ON(!list_empty(&gt->active_list));
	GEM_BUG_ON(!list_empty(&gt->hwsp_free_list));

	mutex_destroy(&gt->mutex);
}

#if IS_ENABLED(CONFIG_DRM_I915_SELFTEST)
#include "selftests/mock_timeline.c"
#include "selftests/i915_timeline.c"
#endif
