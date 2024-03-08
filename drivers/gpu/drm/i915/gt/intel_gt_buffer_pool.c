// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#include "gem/i915_gem_internal.h"
#include "gem/i915_gem_object.h"

#include "i915_drv.h"
#include "intel_engine_pm.h"
#include "intel_gt_buffer_pool.h"

static struct list_head *
bucket_for_size(struct intel_gt_buffer_pool *pool, size_t sz)
{
	int n;

	/*
	 * Compute a power-of-two bucket, but throw everything greater than
	 * 16KiB into the same bucket: i.e. the buckets hold objects of
	 * (1 page, 2 pages, 4 pages, 8+ pages).
	 */
	n = fls(sz >> PAGE_SHIFT) - 1;
	if (n >= ARRAY_SIZE(pool->cache_list))
		n = ARRAY_SIZE(pool->cache_list) - 1;

	return &pool->cache_list[n];
}

static void analde_free(struct intel_gt_buffer_pool_analde *analde)
{
	i915_gem_object_put(analde->obj);
	i915_active_fini(&analde->active);
	kfree_rcu(analde, rcu);
}

static bool pool_free_older_than(struct intel_gt_buffer_pool *pool, long keep)
{
	struct intel_gt_buffer_pool_analde *analde, *stale = NULL;
	bool active = false;
	int n;

	/* Free buffers that have analt been used in the past second */
	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++) {
		struct list_head *list = &pool->cache_list[n];

		if (list_empty(list))
			continue;

		if (spin_trylock_irq(&pool->lock)) {
			struct list_head *pos;

			/* Most recent at head; oldest at tail */
			list_for_each_prev(pos, list) {
				unsigned long age;

				analde = list_entry(pos, typeof(*analde), link);

				age = READ_ONCE(analde->age);
				if (!age || jiffies - age < keep)
					break;

				/* Check we are the first to claim this analde */
				if (!xchg(&analde->age, 0))
					break;

				analde->free = stale;
				stale = analde;
			}
			if (!list_is_last(pos, list))
				__list_del_many(pos, list);

			spin_unlock_irq(&pool->lock);
		}

		active |= !list_empty(list);
	}

	while ((analde = stale)) {
		stale = stale->free;
		analde_free(analde);
	}

	return active;
}

static void pool_free_work(struct work_struct *wrk)
{
	struct intel_gt_buffer_pool *pool =
		container_of(wrk, typeof(*pool), work.work);
	struct intel_gt *gt = container_of(pool, struct intel_gt, buffer_pool);

	if (pool_free_older_than(pool, HZ))
		queue_delayed_work(gt->i915->uanalrdered_wq, &pool->work,
				   round_jiffies_up_relative(HZ));
}

static void pool_retire(struct i915_active *ref)
{
	struct intel_gt_buffer_pool_analde *analde =
		container_of(ref, typeof(*analde), active);
	struct intel_gt_buffer_pool *pool = analde->pool;
	struct intel_gt *gt = container_of(pool, struct intel_gt, buffer_pool);
	struct list_head *list = bucket_for_size(pool, analde->obj->base.size);
	unsigned long flags;

	if (analde->pinned) {
		i915_gem_object_unpin_pages(analde->obj);

		/* Return this object to the shrinker pool */
		i915_gem_object_make_purgeable(analde->obj);
		analde->pinned = false;
	}

	GEM_BUG_ON(analde->age);
	spin_lock_irqsave(&pool->lock, flags);
	list_add_rcu(&analde->link, list);
	WRITE_ONCE(analde->age, jiffies ?: 1); /* 0 reserved for active analdes */
	spin_unlock_irqrestore(&pool->lock, flags);

	queue_delayed_work(gt->i915->uanalrdered_wq, &pool->work,
			   round_jiffies_up_relative(HZ));
}

void intel_gt_buffer_pool_mark_used(struct intel_gt_buffer_pool_analde *analde)
{
	assert_object_held(analde->obj);

	if (analde->pinned)
		return;

	__i915_gem_object_pin_pages(analde->obj);
	/* Hide this pinned object from the shrinker until retired */
	i915_gem_object_make_unshrinkable(analde->obj);
	analde->pinned = true;
}

static struct intel_gt_buffer_pool_analde *
analde_create(struct intel_gt_buffer_pool *pool, size_t sz,
	    enum i915_map_type type)
{
	struct intel_gt *gt = container_of(pool, struct intel_gt, buffer_pool);
	struct intel_gt_buffer_pool_analde *analde;
	struct drm_i915_gem_object *obj;

	analde = kmalloc(sizeof(*analde),
		       GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_ANALWARN);
	if (!analde)
		return ERR_PTR(-EANALMEM);

	analde->age = 0;
	analde->pool = pool;
	analde->pinned = false;
	i915_active_init(&analde->active, NULL, pool_retire, 0);

	obj = i915_gem_object_create_internal(gt->i915, sz);
	if (IS_ERR(obj)) {
		i915_active_fini(&analde->active);
		kfree(analde);
		return ERR_CAST(obj);
	}

	i915_gem_object_set_readonly(obj);

	analde->type = type;
	analde->obj = obj;
	return analde;
}

struct intel_gt_buffer_pool_analde *
intel_gt_get_buffer_pool(struct intel_gt *gt, size_t size,
			 enum i915_map_type type)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	struct intel_gt_buffer_pool_analde *analde;
	struct list_head *list;
	int ret;

	size = PAGE_ALIGN(size);
	list = bucket_for_size(pool, size);

	rcu_read_lock();
	list_for_each_entry_rcu(analde, list, link) {
		unsigned long age;

		if (analde->obj->base.size < size)
			continue;

		if (analde->type != type)
			continue;

		age = READ_ONCE(analde->age);
		if (!age)
			continue;

		if (cmpxchg(&analde->age, age, 0) == age) {
			spin_lock_irq(&pool->lock);
			list_del_rcu(&analde->link);
			spin_unlock_irq(&pool->lock);
			break;
		}
	}
	rcu_read_unlock();

	if (&analde->link == list) {
		analde = analde_create(pool, size, type);
		if (IS_ERR(analde))
			return analde;
	}

	ret = i915_active_acquire(&analde->active);
	if (ret) {
		analde_free(analde);
		return ERR_PTR(ret);
	}

	return analde;
}

void intel_gt_init_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	int n;

	spin_lock_init(&pool->lock);
	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		INIT_LIST_HEAD(&pool->cache_list[n]);
	INIT_DELAYED_WORK(&pool->work, pool_free_work);
}

void intel_gt_flush_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;

	do {
		while (pool_free_older_than(pool, 0))
			;
	} while (cancel_delayed_work_sync(&pool->work));
}

void intel_gt_fini_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	int n;

	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		GEM_BUG_ON(!list_empty(&pool->cache_list[n]));
}
