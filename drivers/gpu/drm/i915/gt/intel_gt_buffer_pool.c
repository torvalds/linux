// SPDX-License-Identifier: MIT
/*
 * Copyright © 2014-2018 Intel Corporation
 */

#include "gem/i915_gem_object.h"

#include "i915_drv.h"
#include "intel_engine_pm.h"
#include "intel_gt_buffer_pool.h"

static struct intel_gt *to_gt(struct intel_gt_buffer_pool *pool)
{
	return container_of(pool, struct intel_gt, buffer_pool);
}

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

static void node_free(struct intel_gt_buffer_pool_node *node)
{
	i915_gem_object_put(node->obj);
	i915_active_fini(&node->active);
	kfree(node);
}

static void pool_free_work(struct work_struct *wrk)
{
	struct intel_gt_buffer_pool *pool =
		container_of(wrk, typeof(*pool), work.work);
	struct intel_gt_buffer_pool_node *node, *next;
	unsigned long old = jiffies - HZ;
	bool active = false;
	LIST_HEAD(stale);
	int n;

	/* Free buffers that have not been used in the past second */
	spin_lock_irq(&pool->lock);
	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++) {
		struct list_head *list = &pool->cache_list[n];

		/* Most recent at head; oldest at tail */
		list_for_each_entry_safe_reverse(node, next, list, link) {
			if (time_before(node->age, old))
				break;

			list_move(&node->link, &stale);
		}
		active |= !list_empty(list);
	}
	spin_unlock_irq(&pool->lock);

	list_for_each_entry_safe(node, next, &stale, link)
		node_free(node);

	if (active)
		schedule_delayed_work(&pool->work,
				      round_jiffies_up_relative(HZ));
}

static int pool_active(struct i915_active *ref)
{
	struct intel_gt_buffer_pool_node *node =
		container_of(ref, typeof(*node), active);
	struct dma_resv *resv = node->obj->base.resv;
	int err;

	if (dma_resv_trylock(resv)) {
		dma_resv_add_excl_fence(resv, NULL);
		dma_resv_unlock(resv);
	}

	err = i915_gem_object_pin_pages(node->obj);
	if (err)
		return err;

	/* Hide this pinned object from the shrinker until retired */
	i915_gem_object_make_unshrinkable(node->obj);

	return 0;
}

__i915_active_call
static void pool_retire(struct i915_active *ref)
{
	struct intel_gt_buffer_pool_node *node =
		container_of(ref, typeof(*node), active);
	struct intel_gt_buffer_pool *pool = node->pool;
	struct list_head *list = bucket_for_size(pool, node->obj->base.size);
	unsigned long flags;

	i915_gem_object_unpin_pages(node->obj);

	/* Return this object to the shrinker pool */
	i915_gem_object_make_purgeable(node->obj);

	spin_lock_irqsave(&pool->lock, flags);
	node->age = jiffies;
	list_add(&node->link, list);
	spin_unlock_irqrestore(&pool->lock, flags);

	schedule_delayed_work(&pool->work,
			      round_jiffies_up_relative(HZ));
}

static struct intel_gt_buffer_pool_node *
node_create(struct intel_gt_buffer_pool *pool, size_t sz)
{
	struct intel_gt *gt = to_gt(pool);
	struct intel_gt_buffer_pool_node *node;
	struct drm_i915_gem_object *obj;

	node = kmalloc(sizeof(*node),
		       GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->pool = pool;
	i915_active_init(&node->active, pool_active, pool_retire);

	obj = i915_gem_object_create_internal(gt->i915, sz);
	if (IS_ERR(obj)) {
		i915_active_fini(&node->active);
		kfree(node);
		return ERR_CAST(obj);
	}

	i915_gem_object_set_readonly(obj);

	node->obj = obj;
	return node;
}

struct intel_gt_buffer_pool_node *
intel_gt_get_buffer_pool(struct intel_gt *gt, size_t size)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	struct intel_gt_buffer_pool_node *node;
	struct list_head *list;
	unsigned long flags;
	int ret;

	size = PAGE_ALIGN(size);
	list = bucket_for_size(pool, size);

	spin_lock_irqsave(&pool->lock, flags);
	list_for_each_entry(node, list, link) {
		if (node->obj->base.size < size)
			continue;
		list_del(&node->link);
		break;
	}
	spin_unlock_irqrestore(&pool->lock, flags);

	if (&node->link == list) {
		node = node_create(pool, size);
		if (IS_ERR(node))
			return node;
	}

	ret = i915_active_acquire(&node->active);
	if (ret) {
		node_free(node);
		return ERR_PTR(ret);
	}

	return node;
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

static void pool_free_imm(struct intel_gt_buffer_pool *pool)
{
	int n;

	spin_lock_irq(&pool->lock);
	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++) {
		struct intel_gt_buffer_pool_node *node, *next;
		struct list_head *list = &pool->cache_list[n];

		list_for_each_entry_safe(node, next, list, link)
			node_free(node);
		INIT_LIST_HEAD(list);
	}
	spin_unlock_irq(&pool->lock);
}

void intel_gt_flush_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;

	if (cancel_delayed_work_sync(&pool->work))
		pool_free_imm(pool);
}

void intel_gt_fini_buffer_pool(struct intel_gt *gt)
{
	struct intel_gt_buffer_pool *pool = &gt->buffer_pool;
	int n;

	intel_gt_flush_buffer_pool(gt);

	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		GEM_BUG_ON(!list_empty(&pool->cache_list[n]));
}
