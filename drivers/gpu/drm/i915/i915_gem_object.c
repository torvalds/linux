/*
 * Copyright © 2017 Intel Corporation
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
#include "i915_gem_object.h"

/**
 * Mark up the object's coherency levels for a given cache_level
 * @obj: #drm_i915_gem_object
 * @cache_level: cache level
 */
void i915_gem_object_set_cache_coherency(struct drm_i915_gem_object *obj,
					 unsigned int cache_level)
{
	obj->cache_level = cache_level;

	if (cache_level != I915_CACHE_NONE)
		obj->cache_coherent = (I915_BO_CACHE_COHERENT_FOR_READ |
				       I915_BO_CACHE_COHERENT_FOR_WRITE);
	else if (HAS_LLC(to_i915(obj->base.dev)))
		obj->cache_coherent = I915_BO_CACHE_COHERENT_FOR_READ;
	else
		obj->cache_coherent = 0;

	obj->cache_dirty =
		!(obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_WRITE);
}
