/*
 * Copyright © 2014 Intel Corporation
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
#include "i915_gem_batch_pool.h"

/**
 * DOC: batch pool
 *
 * In order to submit batch buffers as 'secure', the software command parser
 * must ensure that a batch buffer cannot be modified after parsing. It does
 * this by copying the user provided batch buffer contents to a kernel owned
 * buffer from which the hardware will actually execute, and by carefully
 * managing the address space bindings for such buffers.
 *
 * The batch pool framework provides a mechanism for the driver to manage a
 * set of scratch buffers to use for this purpose. The framework can be
 * extended to support other uses cases should they arise.
 */

/**
 * i915_gem_batch_pool_init() - initialize a batch buffer pool
 * @engine: the associated request submission engine
 * @pool: the batch buffer pool
 */
void i915_gem_batch_pool_init(struct intel_engine_cs *engine,
			      struct i915_gem_batch_pool *pool)
{
	int n;

	pool->engine = engine;

	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++)
		INIT_LIST_HEAD(&pool->cache_list[n]);
}

/**
 * i915_gem_batch_pool_fini() - clean up a batch buffer pool
 * @pool: the pool to clean up
 *
 * Note: Callers must hold the struct_mutex.
 */
void i915_gem_batch_pool_fini(struct i915_gem_batch_pool *pool)
{
	int n;

	lockdep_assert_held(&pool->engine->i915->drm.struct_mutex);

	for (n = 0; n < ARRAY_SIZE(pool->cache_list); n++) {
		struct drm_i915_gem_object *obj, *next;

		list_for_each_entry_safe(obj, next,
					 &pool->cache_list[n],
					 batch_pool_link)
			__i915_gem_object_release_unless_active(obj);

		INIT_LIST_HEAD(&pool->cache_list[n]);
	}
}

/**
 * i915_gem_batch_pool_get() - allocate a buffer from the pool
 * @pool: the batch buffer pool
 * @size: the minimum desired size of the returned buffer
 *
 * Returns an inactive buffer from @pool with at least @size bytes,
 * with the pages pinned. The caller must i915_gem_object_unpin_pages()
 * on the returned object.
 *
 * Note: Callers must hold the struct_mutex
 *
 * Return: the buffer object or an error pointer
 */
struct drm_i915_gem_object *
i915_gem_batch_pool_get(struct i915_gem_batch_pool *pool,
			size_t size)
{
	struct drm_i915_gem_object *obj;
	struct list_head *list;
	int n, ret;

	lockdep_assert_held(&pool->engine->i915->drm.struct_mutex);

	/* Compute a power-of-two bucket, but throw everything greater than
	 * 16KiB into the same bucket: i.e. the the buckets hold objects of
	 * (1 page, 2 pages, 4 pages, 8+ pages).
	 */
	n = fls(size >> PAGE_SHIFT) - 1;
	if (n >= ARRAY_SIZE(pool->cache_list))
		n = ARRAY_SIZE(pool->cache_list) - 1;
	list = &pool->cache_list[n];

	list_for_each_entry(obj, list, batch_pool_link) {
		/* The batches are strictly LRU ordered */
		if (i915_gem_object_is_active(obj)) {
			if (!reservation_object_test_signaled_rcu(obj->resv,
								  true))
				break;

			i915_gem_retire_requests(pool->engine->i915);
			GEM_BUG_ON(i915_gem_object_is_active(obj));
		}

		GEM_BUG_ON(!reservation_object_test_signaled_rcu(obj->resv,
								 true));

		if (obj->base.size >= size)
			goto found;
	}

	obj = i915_gem_object_create_internal(pool->engine->i915, size);
	if (IS_ERR(obj))
		return obj;

found:
	ret = i915_gem_object_pin_pages(obj);
	if (ret)
		return ERR_PTR(ret);

	list_move_tail(&obj->batch_pool_link, list);
	return obj;
}
