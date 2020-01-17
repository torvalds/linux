/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 */

#include "gem/i915_gem_object.h"

#include "i915_drv.h"
#include "intel_engine_pm.h"
#include "intel_engine_pool.h"

static struct intel_engine_cs *to_engine(struct intel_engine_pool *pool)
{
	return container_of(pool, struct intel_engine_cs, pool);
}

static struct list_head *
bucket_for_size(struct intel_engine_pool *pool, size_t sz)
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

static void node_free(struct intel_engine_pool_node *node)
{
	i915_gem_object_put(node->obj);
	i915_active_fini(&node->active);
	kfree(node);
}

static int pool_active(struct i915_active *ref)
{
	struct intel_engine_pool_node *node =
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

static void pool_retire(struct i915_active *ref)
{
	struct intel_engine_pool_node *node =
		container_of(ref, typeof(*node), active);
	struct intel_engine_pool *pool = node->pool;
	struct list_head *list = bucket_for_size(pool, node->obj->base.size);
	unsigned long flags;

	GEM_BUG_ON(!intel_engine_pm_is_awake(to_engine(pool)));

	i915_gem_object_unpin_pages(node->obj);

	/* Return this object to the shrinker pool */
	i915_gem_object_make_purgeable(node->obj);

	spin_lock_irqsave(&pool->lock, flags);
	list_add(&node->link, list);
	spin_unlock_irqrestore(&pool->lock, flags);
}

static struct intel_engine_pool_node *
node_create(struct intel_engine_pool *pool, size_t sz)
{
	struct intel_engine_cs *engine = to_engine(pool);
	struct intel_engine_pool_node *node;
	struct drm_i915_gem_object *obj;

	node = kmalloc(sizeof(*node),
		       GFP_KERNEL | __GFP_RETRY_MAYFAIL | __GFP_NOWARN);
	if (!node)
		return ERR_PTR(-ENOMEM);

	node->pool = pool;
	i915_active_init(engine->i915, &node->active, pool_active, pool_retire);

	obj = i915_gem_object_create_internal(engine->i915, sz);
	if (IS_ERR(obj)) {
		i915_active_fini(&node->active);
		kfree(node);
		return ERR_CAST(obj);
	}

	i915_gem_object_set_readonly(obj);

	node->obj = obj;
	return node;
}

struct intel_engine_pool_node *
intel_engine_pool_get(struct intel_engine_pool *pool, size_t size)
{
	struct intel_engine_pool_node *node;
	struct list_head *list;
	unsigned long flags;
	int ret;

	GEM_BUG_ON(!intel_engine_pm_is_awake(to_engine(pool)));

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

void intel_engine_pool_init(struct intel_engine_pool *pool)
{
	int n;

	spin_lock_init(&pool->lock);
	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		INIT_LIST_HEAD(&pool->cache_list[n]);
}

void intel_engine_pool_park(struct intel_engine_pool *pool)
{
	int n;

	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++) {
		struct list_head *list = &pool->cache_list[n];
		struct intel_engine_pool_node *node, *nn;

		list_for_each_entry_safe(node, nn, list, link)
			node_free(node);

		INIT_LIST_HEAD(list);
	}
}

void intel_engine_pool_fini(struct intel_engine_pool *pool)
{
	int n;

	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		GEM_BUG_ON(!list_empty(&pool->cache_list[n]));
}
