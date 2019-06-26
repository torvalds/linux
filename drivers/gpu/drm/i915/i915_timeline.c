/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2016-2018 Intel Corporation
 */

#include "i915_drv.h"

#include "i915_active.h"
#include "i915_syncmap.h"
#include "i915_timeline.h"

#define ptr_set_bit(ptr, bit) ((typeof(ptr))((unsigned long)(ptr) | BIT(bit)))
#define ptr_test_bit(ptr, bit) ((unsigned long)(ptr) & BIT(bit))

struct i915_timeline_hwsp {
	struct i915_gt_timelines *gt;
	struct list_head free_link;
	struct i915_vma *vma;
	u64 free_bitmap;
};

struct i915_timeline_cacheline {
	struct i915_active active;
	struct i915_timeline_hwsp *hwsp;
	void *vaddr;
#define CACHELINE_BITS 6
#define CACHELINE_FREE CACHELINE_BITS
};

static inline struct drm_i915_private *
hwsp_to_i915(struct i915_timeline_hwsp *hwsp)
{
	return container_of(hwsp->gt, struct drm_i915_private, gt.timelines);
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

	spin_lock_irq(&gt->hwsp_lock);

	/* hwsp_free_list only contains HWSP that have available cachelines */
	hwsp = list_first_entry_or_null(&gt->hwsp_free_list,
					typeof(*hwsp), free_link);
	if (!hwsp) {
		struct i915_vma *vma;

		spin_unlock_irq(&gt->hwsp_lock);

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
		hwsp->gt = gt;

		spin_lock_irq(&gt->hwsp_lock);
		list_add(&hwsp->free_link, &gt->hwsp_free_list);
	}

	GEM_BUG_ON(!hwsp->free_bitmap);
	*cacheline = __ffs64(hwsp->free_bitmap);
	hwsp->free_bitmap &= ~BIT_ULL(*cacheline);
	if (!hwsp->free_bitmap)
		list_del(&hwsp->free_link);

	spin_unlock_irq(&gt->hwsp_lock);

	GEM_BUG_ON(hwsp->vma->private != hwsp);
	return hwsp->vma;
}

static void __idle_hwsp_free(struct i915_timeline_hwsp *hwsp, int cacheline)
{
	struct i915_gt_timelines *gt = hwsp->gt;
	unsigned long flags;

	spin_lock_irqsave(&gt->hwsp_lock, flags);

	/* As a cacheline becomes available, publish the HWSP on the freelist */
	if (!hwsp->free_bitmap)
		list_add_tail(&hwsp->free_link, &gt->hwsp_free_list);

	GEM_BUG_ON(cacheline >= BITS_PER_TYPE(hwsp->free_bitmap));
	hwsp->free_bitmap |= BIT_ULL(cacheline);

	/* And if no one is left using it, give the page back to the system */
	if (hwsp->free_bitmap == ~0ull) {
		i915_vma_put(hwsp->vma);
		list_del(&hwsp->free_link);
		kfree(hwsp);
	}

	spin_unlock_irqrestore(&gt->hwsp_lock, flags);
}

static void __idle_cacheline_free(struct i915_timeline_cacheline *cl)
{
	GEM_BUG_ON(!i915_active_is_idle(&cl->active));

	i915_gem_object_unpin_map(cl->hwsp->vma->obj);
	i915_vma_put(cl->hwsp->vma);
	__idle_hwsp_free(cl->hwsp, ptr_unmask_bits(cl->vaddr, CACHELINE_BITS));

	i915_active_fini(&cl->active);
	kfree(cl);
}

static void __cacheline_retire(struct i915_active *active)
{
	struct i915_timeline_cacheline *cl =
		container_of(active, typeof(*cl), active);

	i915_vma_unpin(cl->hwsp->vma);
	if (ptr_test_bit(cl->vaddr, CACHELINE_FREE))
		__idle_cacheline_free(cl);
}

static struct i915_timeline_cacheline *
cacheline_alloc(struct i915_timeline_hwsp *hwsp, unsigned int cacheline)
{
	struct i915_timeline_cacheline *cl;
	void *vaddr;

	GEM_BUG_ON(cacheline >= BIT(CACHELINE_BITS));

	cl = kmalloc(sizeof(*cl), GFP_KERNEL);
	if (!cl)
		return ERR_PTR(-ENOMEM);

	vaddr = i915_gem_object_pin_map(hwsp->vma->obj, I915_MAP_WB);
	if (IS_ERR(vaddr)) {
		kfree(cl);
		return ERR_CAST(vaddr);
	}

	i915_vma_get(hwsp->vma);
	cl->hwsp = hwsp;
	cl->vaddr = page_pack_bits(vaddr, cacheline);

	i915_active_init(hwsp_to_i915(hwsp), &cl->active, __cacheline_retire);

	return cl;
}

static void cacheline_acquire(struct i915_timeline_cacheline *cl)
{
	if (cl && i915_active_acquire(&cl->active))
		__i915_vma_pin(cl->hwsp->vma);
}

static void cacheline_release(struct i915_timeline_cacheline *cl)
{
	if (cl)
		i915_active_release(&cl->active);
}

static void cacheline_free(struct i915_timeline_cacheline *cl)
{
	GEM_BUG_ON(ptr_test_bit(cl->vaddr, CACHELINE_FREE));
	cl->vaddr = ptr_set_bit(cl->vaddr, CACHELINE_FREE);

	if (i915_active_is_idle(&cl->active))
		__idle_cacheline_free(cl);
}

int i915_timeline_init(struct drm_i915_private *i915,
		       struct i915_timeline *timeline,
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
	timeline->pin_count = 0;
	timeline->has_initial_breadcrumb = !hwsp;
	timeline->hwsp_cacheline = NULL;

	if (!hwsp) {
		struct i915_timeline_cacheline *cl;
		unsigned int cacheline;

		hwsp = hwsp_alloc(timeline, &cacheline);
		if (IS_ERR(hwsp))
			return PTR_ERR(hwsp);

		cl = cacheline_alloc(hwsp->private, cacheline);
		if (IS_ERR(cl)) {
			__idle_hwsp_free(hwsp->private, cacheline);
			return PTR_ERR(cl);
		}

		timeline->hwsp_cacheline = cl;
		timeline->hwsp_offset = cacheline * CACHELINE_BYTES;

		vaddr = page_mask_bits(cl->vaddr);
	} else {
		timeline->hwsp_offset = I915_GEM_HWS_SEQNO_ADDR;

		vaddr = i915_gem_object_pin_map(hwsp->obj, I915_MAP_WB);
		if (IS_ERR(vaddr))
			return PTR_ERR(vaddr);
	}

	timeline->hwsp_seqno =
		memset(vaddr + timeline->hwsp_offset, 0, CACHELINE_BYTES);

	timeline->hwsp_ggtt = i915_vma_get(hwsp);
	GEM_BUG_ON(timeline->hwsp_offset >= hwsp->size);

	timeline->fence_context = dma_fence_context_alloc(1);

	mutex_init(&timeline->mutex);

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

	i915_syncmap_free(&timeline->sync);

	if (timeline->hwsp_cacheline)
		cacheline_free(timeline->hwsp_cacheline);
	else
		i915_gem_object_unpin_map(timeline->hwsp_ggtt->obj);

	i915_vma_put(timeline->hwsp_ggtt);
}

struct i915_timeline *
i915_timeline_create(struct drm_i915_private *i915,
		     struct i915_vma *global_hwsp)
{
	struct i915_timeline *timeline;
	int err;

	timeline = kzalloc(sizeof(*timeline), GFP_KERNEL);
	if (!timeline)
		return ERR_PTR(-ENOMEM);

	err = i915_timeline_init(i915, timeline, global_hwsp);
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

	cacheline_acquire(tl->hwsp_cacheline);
	timeline_add_to_active(tl);

	return 0;

unpin:
	tl->pin_count = 0;
	return err;
}

static u32 timeline_advance(struct i915_timeline *tl)
{
	GEM_BUG_ON(!tl->pin_count);
	GEM_BUG_ON(tl->seqno & tl->has_initial_breadcrumb);

	return tl->seqno += 1 + tl->has_initial_breadcrumb;
}

static void timeline_rollback(struct i915_timeline *tl)
{
	tl->seqno -= 1 + tl->has_initial_breadcrumb;
}

static noinline int
__i915_timeline_get_seqno(struct i915_timeline *tl,
			  struct i915_request *rq,
			  u32 *seqno)
{
	struct i915_timeline_cacheline *cl;
	unsigned int cacheline;
	struct i915_vma *vma;
	void *vaddr;
	int err;

	/*
	 * If there is an outstanding GPU reference to this cacheline,
	 * such as it being sampled by a HW semaphore on another timeline,
	 * we cannot wraparound our seqno value (the HW semaphore does
	 * a strict greater-than-or-equals compare, not i915_seqno_passed).
	 * So if the cacheline is still busy, we must detach ourselves
	 * from it and leave it inflight alongside its users.
	 *
	 * However, if nobody is watching and we can guarantee that nobody
	 * will, we could simply reuse the same cacheline.
	 *
	 * if (i915_active_request_is_signaled(&tl->last_request) &&
	 *     i915_active_is_signaled(&tl->hwsp_cacheline->active))
	 *	return 0;
	 *
	 * That seems unlikely for a busy timeline that needed to wrap in
	 * the first place, so just replace the cacheline.
	 */

	vma = hwsp_alloc(tl, &cacheline);
	if (IS_ERR(vma)) {
		err = PTR_ERR(vma);
		goto err_rollback;
	}

	err = i915_vma_pin(vma, 0, 0, PIN_GLOBAL | PIN_HIGH);
	if (err) {
		__idle_hwsp_free(vma->private, cacheline);
		goto err_rollback;
	}

	cl = cacheline_alloc(vma->private, cacheline);
	if (IS_ERR(cl)) {
		err = PTR_ERR(cl);
		__idle_hwsp_free(vma->private, cacheline);
		goto err_unpin;
	}
	GEM_BUG_ON(cl->hwsp->vma != vma);

	/*
	 * Attach the old cacheline to the current request, so that we only
	 * free it after the current request is retired, which ensures that
	 * all writes into the cacheline from previous requests are complete.
	 */
	err = i915_active_ref(&tl->hwsp_cacheline->active,
			      tl->fence_context, rq);
	if (err)
		goto err_cacheline;

	cacheline_release(tl->hwsp_cacheline); /* ownership now xfered to rq */
	cacheline_free(tl->hwsp_cacheline);

	i915_vma_unpin(tl->hwsp_ggtt); /* binding kept alive by old cacheline */
	i915_vma_put(tl->hwsp_ggtt);

	tl->hwsp_ggtt = i915_vma_get(vma);

	vaddr = page_mask_bits(cl->vaddr);
	tl->hwsp_offset = cacheline * CACHELINE_BYTES;
	tl->hwsp_seqno =
		memset(vaddr + tl->hwsp_offset, 0, CACHELINE_BYTES);

	tl->hwsp_offset += i915_ggtt_offset(vma);

	cacheline_acquire(cl);
	tl->hwsp_cacheline = cl;

	*seqno = timeline_advance(tl);
	GEM_BUG_ON(i915_seqno_passed(*tl->hwsp_seqno, *seqno));
	return 0;

err_cacheline:
	cacheline_free(cl);
err_unpin:
	i915_vma_unpin(vma);
err_rollback:
	timeline_rollback(tl);
	return err;
}

int i915_timeline_get_seqno(struct i915_timeline *tl,
			    struct i915_request *rq,
			    u32 *seqno)
{
	*seqno = timeline_advance(tl);

	/* Replace the HWSP on wraparound for HW semaphores */
	if (unlikely(!*seqno && tl->hwsp_cacheline))
		return __i915_timeline_get_seqno(tl, rq, seqno);

	return 0;
}

static int cacheline_ref(struct i915_timeline_cacheline *cl,
			 struct i915_request *rq)
{
	return i915_active_ref(&cl->active, rq->fence.context, rq);
}

int i915_timeline_read_hwsp(struct i915_request *from,
			    struct i915_request *to,
			    u32 *hwsp)
{
	struct i915_timeline_cacheline *cl = from->hwsp_cacheline;
	struct i915_timeline *tl = from->timeline;
	int err;

	GEM_BUG_ON(to->timeline == tl);

	mutex_lock_nested(&tl->mutex, SINGLE_DEPTH_NESTING);
	err = i915_request_completed(from);
	if (!err)
		err = cacheline_ref(cl, to);
	if (!err) {
		if (likely(cl == tl->hwsp_cacheline)) {
			*hwsp = tl->hwsp_offset;
		} else { /* across a seqno wrap, recover the original offset */
			*hwsp = i915_ggtt_offset(cl->hwsp->vma) +
				ptr_unmask_bits(cl->vaddr, CACHELINE_BITS) *
				CACHELINE_BYTES;
		}
	}
	mutex_unlock(&tl->mutex);

	return err;
}

void i915_timeline_unpin(struct i915_timeline *tl)
{
	GEM_BUG_ON(!tl->pin_count);
	if (--tl->pin_count)
		return;

	timeline_remove_from_active(tl);
	cacheline_release(tl->hwsp_cacheline);

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
