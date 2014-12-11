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
 * @dev: the drm device
 * @pool: the batch buffer pool
 */
void i915_gem_batch_pool_init(struct drm_device *dev,
			      struct i915_gem_batch_pool *pool)
{
	pool->dev = dev;
	INIT_LIST_HEAD(&pool->cache_list);
}

/**
 * i915_gem_batch_pool_fini() - clean up a batch buffer pool
 * @pool: the pool to clean up
 *
 * Note: Callers must hold the struct_mutex.
 */
void i915_gem_batch_pool_fini(struct i915_gem_batch_pool *pool)
{
	WARN_ON(!mutex_is_locked(&pool->dev->struct_mutex));

	while (!list_empty(&pool->cache_list)) {
		struct drm_i915_gem_object *obj =
			list_first_entry(&pool->cache_list,
					 struct drm_i915_gem_object,
					 batch_pool_list);

		WARN_ON(obj->active);

		list_del_init(&obj->batch_pool_list);
		drm_gem_object_unreference(&obj->base);
	}
}

/**
 * i915_gem_batch_pool_get() - select a buffer from the pool
 * @pool: the batch buffer pool
 * @size: the minimum desired size of the returned buffer
 *
 * Finds or allocates a batch buffer in the pool with at least the requested
 * size. The caller is responsible for any domain, active/inactive, or
 * purgeability management for the returned buffer.
 *
 * Note: Callers must hold the struct_mutex
 *
 * Return: the selected batch buffer object
 */
struct drm_i915_gem_object *
i915_gem_batch_pool_get(struct i915_gem_batch_pool *pool,
			size_t size)
{
	struct drm_i915_gem_object *obj = NULL;
	struct drm_i915_gem_object *tmp, *next;

	WARN_ON(!mutex_is_locked(&pool->dev->struct_mutex));

	list_for_each_entry_safe(tmp, next,
			&pool->cache_list, batch_pool_list) {

		if (tmp->active)
			continue;

		/* While we're looping, do some clean up */
		if (tmp->madv == __I915_MADV_PURGED) {
			list_del(&tmp->batch_pool_list);
			drm_gem_object_unreference(&tmp->base);
			continue;
		}

		/*
		 * Select a buffer that is at least as big as needed
		 * but not 'too much' bigger. A better way to do this
		 * might be to bucket the pool objects based on size.
		 */
		if (tmp->base.size >= size &&
		    tmp->base.size <= (2 * size)) {
			obj = tmp;
			break;
		}
	}

	if (!obj) {
		obj = i915_gem_alloc_object(pool->dev, size);
		if (!obj)
			return ERR_PTR(-ENOMEM);

		list_add_tail(&obj->batch_pool_list, &pool->cache_list);
	}
	else
		/* Keep list in LRU order */
		list_move_tail(&obj->batch_pool_list, &pool->cache_list);

	return obj;
}
