/*
 * Copyright © 2008-2010 Intel Corporation
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
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *    Chris Wilson <chris@chris-wilson.co.uuk>
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drv.h"
#include "i915_drm.h"

static inline int
i915_gem_object_is_purgeable(struct drm_i915_gem_object *obj_priv)
{
	return obj_priv->madv == I915_MADV_DONTNEED;
}

static int
i915_gem_scan_inactive_list_and_evict(struct drm_device *dev, int min_size,
				      unsigned alignment, int *found)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_gem_object *obj;
	struct drm_i915_gem_object *obj_priv;
	struct drm_gem_object *best = NULL;
	struct drm_gem_object *first = NULL;

	/* Try to find the smallest clean object */
	list_for_each_entry(obj_priv, &dev_priv->mm.inactive_list, list) {
		struct drm_gem_object *obj = &obj_priv->base;
		if (obj->size >= min_size) {
			if ((!obj_priv->dirty ||
			     i915_gem_object_is_purgeable(obj_priv)) &&
			    (!best || obj->size < best->size)) {
				best = obj;
				if (best->size == min_size)
					break;
			}
			if (!first)
			    first = obj;
		}
	}

	obj = best ? best : first;

	if (!obj) {
		*found = 0;
		return 0;
	}

	*found = 1;

#if WATCH_LRU
	DRM_INFO("%s: evicting %p\n", __func__, obj);
#endif
	obj_priv = to_intel_bo(obj);
	BUG_ON(obj_priv->pin_count != 0);
	BUG_ON(obj_priv->active);

	/* Wait on the rendering and unbind the buffer. */
	return i915_gem_object_unbind(obj);
}

static void
i915_gem_flush_ring(struct drm_device *dev,
	       uint32_t invalidate_domains,
	       uint32_t flush_domains,
	       struct intel_ring_buffer *ring)
{
	if (flush_domains & I915_GEM_DOMAIN_CPU)
		drm_agp_chipset_flush(dev);
	ring->flush(dev, ring,
			invalidate_domains,
			flush_domains);
}

int
i915_gem_evict_something(struct drm_device *dev,
			 int min_size, unsigned alignment)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret, found;

	struct intel_ring_buffer *render_ring = &dev_priv->render_ring;
	struct intel_ring_buffer *bsd_ring = &dev_priv->bsd_ring;
	for (;;) {
		i915_gem_retire_requests(dev);

		/* If there's an inactive buffer available now, grab it
		 * and be done.
		 */
		ret = i915_gem_scan_inactive_list_and_evict(dev, min_size,
							    alignment,
							    &found);
		if (found)
			return ret;

		/* If we didn't get anything, but the ring is still processing
		 * things, wait for the next to finish and hopefully leave us
		 * a buffer to evict.
		 */
		if (!list_empty(&render_ring->request_list)) {
			struct drm_i915_gem_request *request;

			request = list_first_entry(&render_ring->request_list,
						   struct drm_i915_gem_request,
						   list);

			ret = i915_do_wait_request(dev, request->seqno, true, request->ring);
			if (ret)
				return ret;

			continue;
		}

		if (HAS_BSD(dev) && !list_empty(&bsd_ring->request_list)) {
			struct drm_i915_gem_request *request;

			request = list_first_entry(&bsd_ring->request_list,
						   struct drm_i915_gem_request,
						   list);

			ret = i915_do_wait_request(dev, request->seqno, true, request->ring);
			if (ret)
				return ret;

			continue;
		}

		/* If we didn't have anything on the request list but there
		 * are buffers awaiting a flush, emit one and try again.
		 * When we wait on it, those buffers waiting for that flush
		 * will get moved to inactive.
		 */
		if (!list_empty(&dev_priv->mm.flushing_list)) {
			struct drm_gem_object *obj = NULL;
			struct drm_i915_gem_object *obj_priv;

			/* Find an object that we can immediately reuse */
			list_for_each_entry(obj_priv, &dev_priv->mm.flushing_list, list) {
				obj = &obj_priv->base;
				if (obj->size >= min_size)
					break;

				obj = NULL;
			}

			if (obj != NULL) {
				uint32_t seqno;

				i915_gem_flush_ring(dev,
					       obj->write_domain,
					       obj->write_domain,
					       obj_priv->ring);
				seqno = i915_add_request(dev, NULL,
						obj->write_domain,
						obj_priv->ring);
				if (seqno == 0)
					return -ENOMEM;
				continue;
			}
		}

		/* If we didn't do any of the above, there's no single buffer
		 * large enough to swap out for the new one, so just evict
		 * everything and start again. (This should be rare.)
		 */
		if (!list_empty(&dev_priv->mm.inactive_list))
			return i915_gem_evict_inactive(dev);
		else
			return i915_gem_evict_everything(dev);
	}
}

int
i915_gem_evict_everything(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;
	bool lists_empty;

	spin_lock(&dev_priv->mm.active_list_lock);
	lists_empty = (list_empty(&dev_priv->mm.inactive_list) &&
		       list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->render_ring.active_list) &&
		       (!HAS_BSD(dev)
			|| list_empty(&dev_priv->bsd_ring.active_list)));
	spin_unlock(&dev_priv->mm.active_list_lock);

	if (lists_empty)
		return -ENOSPC;

	/* Flush everything (on to the inactive lists) and evict */
	ret = i915_gpu_idle(dev);
	if (ret)
		return ret;

	BUG_ON(!list_empty(&dev_priv->mm.flushing_list));

	ret = i915_gem_evict_inactive(dev);
	if (ret)
		return ret;

	spin_lock(&dev_priv->mm.active_list_lock);
	lists_empty = (list_empty(&dev_priv->mm.inactive_list) &&
		       list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->render_ring.active_list) &&
		       (!HAS_BSD(dev)
			|| list_empty(&dev_priv->bsd_ring.active_list)));
	spin_unlock(&dev_priv->mm.active_list_lock);
	BUG_ON(!lists_empty);

	return 0;
}

/** Unbinds all inactive objects. */
int
i915_gem_evict_inactive(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;

	while (!list_empty(&dev_priv->mm.inactive_list)) {
		struct drm_gem_object *obj;
		int ret;

		obj = &list_first_entry(&dev_priv->mm.inactive_list,
					struct drm_i915_gem_object,
					list)->base;

		ret = i915_gem_object_unbind(obj);
		if (ret != 0) {
			DRM_ERROR("Error unbinding object: %d\n", ret);
			return ret;
		}
	}

	return 0;
}
