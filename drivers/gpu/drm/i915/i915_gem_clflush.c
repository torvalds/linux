/*
 * Copyright © 2016 Intel Corporation
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
#include "intel_frontbuffer.h"
#include "i915_gem_clflush.h"

static DEFINE_SPINLOCK(clflush_lock);

struct clflush {
	struct dma_fence dma; /* Must be first for dma_fence_free() */
	struct i915_sw_fence wait;
	struct work_struct work;
	struct drm_i915_gem_object *obj;
};

static const char *i915_clflush_get_driver_name(struct dma_fence *fence)
{
	return DRIVER_NAME;
}

static const char *i915_clflush_get_timeline_name(struct dma_fence *fence)
{
	return "clflush";
}

static bool i915_clflush_enable_signaling(struct dma_fence *fence)
{
	return true;
}

static void i915_clflush_release(struct dma_fence *fence)
{
	struct clflush *clflush = container_of(fence, typeof(*clflush), dma);

	i915_sw_fence_fini(&clflush->wait);

	BUILD_BUG_ON(offsetof(typeof(*clflush), dma));
	dma_fence_free(&clflush->dma);
}

static const struct dma_fence_ops i915_clflush_ops = {
	.get_driver_name = i915_clflush_get_driver_name,
	.get_timeline_name = i915_clflush_get_timeline_name,
	.enable_signaling = i915_clflush_enable_signaling,
	.wait = dma_fence_default_wait,
	.release = i915_clflush_release,
};

static void __i915_do_clflush(struct drm_i915_gem_object *obj)
{
	GEM_BUG_ON(!i915_gem_object_has_pages(obj));
	drm_clflush_sg(obj->mm.pages);
	intel_fb_obj_flush(obj, ORIGIN_CPU);
}

static void i915_clflush_work(struct work_struct *work)
{
	struct clflush *clflush = container_of(work, typeof(*clflush), work);
	struct drm_i915_gem_object *obj = clflush->obj;

	if (i915_gem_object_pin_pages(obj)) {
		DRM_ERROR("Failed to acquire obj->pages for clflushing\n");
		goto out;
	}

	__i915_do_clflush(obj);

	i915_gem_object_unpin_pages(obj);

out:
	i915_gem_object_put(obj);

	dma_fence_signal(&clflush->dma);
	dma_fence_put(&clflush->dma);
}

static int __i915_sw_fence_call
i915_clflush_notify(struct i915_sw_fence *fence,
		    enum i915_sw_fence_notify state)
{
	struct clflush *clflush = container_of(fence, typeof(*clflush), wait);

	switch (state) {
	case FENCE_COMPLETE:
		schedule_work(&clflush->work);
		break;

	case FENCE_FREE:
		dma_fence_put(&clflush->dma);
		break;
	}

	return NOTIFY_DONE;
}

bool i915_gem_clflush_object(struct drm_i915_gem_object *obj,
			     unsigned int flags)
{
	struct clflush *clflush;

	/*
	 * Stolen memory is always coherent with the GPU as it is explicitly
	 * marked as wc by the system, or the system is cache-coherent.
	 * Similarly, we only access struct pages through the CPU cache, so
	 * anything not backed by physical memory we consider to be always
	 * coherent and not need clflushing.
	 */
	if (!i915_gem_object_has_struct_page(obj)) {
		obj->cache_dirty = false;
		return false;
	}

	/* If the GPU is snooping the contents of the CPU cache,
	 * we do not need to manually clear the CPU cache lines.  However,
	 * the caches are only snooped when the render cache is
	 * flushed/invalidated.  As we always have to emit invalidations
	 * and flushes when moving into and out of the RENDER domain, correct
	 * snooping behaviour occurs naturally as the result of our domain
	 * tracking.
	 */
	if (!(flags & I915_CLFLUSH_FORCE) &&
	    obj->cache_coherent & I915_BO_CACHE_COHERENT_FOR_READ)
		return false;

	trace_i915_gem_object_clflush(obj);

	clflush = NULL;
	if (!(flags & I915_CLFLUSH_SYNC))
		clflush = kmalloc(sizeof(*clflush), GFP_KERNEL);
	if (clflush) {
		GEM_BUG_ON(!obj->cache_dirty);

		dma_fence_init(&clflush->dma,
			       &i915_clflush_ops,
			       &clflush_lock,
			       to_i915(obj->base.dev)->mm.unordered_timeline,
			       0);
		i915_sw_fence_init(&clflush->wait, i915_clflush_notify);

		clflush->obj = i915_gem_object_get(obj);
		INIT_WORK(&clflush->work, i915_clflush_work);

		dma_fence_get(&clflush->dma);

		i915_sw_fence_await_reservation(&clflush->wait,
						obj->resv, NULL,
						true, I915_FENCE_TIMEOUT,
						I915_FENCE_GFP);

		reservation_object_lock(obj->resv, NULL);
		reservation_object_add_excl_fence(obj->resv, &clflush->dma);
		reservation_object_unlock(obj->resv);

		i915_sw_fence_commit(&clflush->wait);
	} else if (obj->mm.pages) {
		__i915_do_clflush(obj);
	} else {
		GEM_BUG_ON(obj->write_domain != I915_GEM_DOMAIN_CPU);
	}

	obj->cache_dirty = false;
	return true;
}
