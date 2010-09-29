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

static struct drm_i915_gem_object *
i915_gem_next_active_object(struct drm_device *dev,
			    struct list_head **render_iter,
			    struct list_head **bsd_iter)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *render_obj = NULL, *bsd_obj = NULL;

	if (*render_iter != &dev_priv->render_ring.active_list)
		render_obj = list_entry(*render_iter,
					struct drm_i915_gem_object,
					list);

	if (HAS_BSD(dev)) {
		if (*bsd_iter != &dev_priv->bsd_ring.active_list)
			bsd_obj = list_entry(*bsd_iter,
					     struct drm_i915_gem_object,
					     list);

		if (render_obj == NULL) {
			*bsd_iter = (*bsd_iter)->next;
			return bsd_obj;
		}

		if (bsd_obj == NULL) {
			*render_iter = (*render_iter)->next;
			return render_obj;
		}

		/* XXX can we handle seqno wrapping? */
		if (render_obj->last_rendering_seqno < bsd_obj->last_rendering_seqno) {
			*render_iter = (*render_iter)->next;
			return render_obj;
		} else {
			*bsd_iter = (*bsd_iter)->next;
			return bsd_obj;
		}
	} else {
		*render_iter = (*render_iter)->next;
		return render_obj;
	}
}

static bool
mark_free(struct drm_i915_gem_object *obj_priv,
	   struct list_head *unwind)
{
	list_add(&obj_priv->evict_list, unwind);
	drm_gem_object_reference(&obj_priv->base);
	return drm_mm_scan_add_block(obj_priv->gtt_space);
}

#define i915_for_each_active_object(OBJ, R, B) \
	*(R) = dev_priv->render_ring.active_list.next; \
	*(B) = dev_priv->bsd_ring.active_list.next; \
	while (((OBJ) = i915_gem_next_active_object(dev, (R), (B))) != NULL)

int
i915_gem_evict_something(struct drm_device *dev, int min_size, unsigned alignment)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct list_head eviction_list, unwind_list;
	struct drm_i915_gem_object *obj_priv, *tmp_obj_priv;
	struct list_head *render_iter, *bsd_iter;
	int ret = 0;

	i915_gem_retire_requests(dev);

	/* Re-check for free space after retiring requests */
	if (drm_mm_search_free(&dev_priv->mm.gtt_space,
			       min_size, alignment, 0))
		return 0;

	/*
	 * The goal is to evict objects and amalgamate space in LRU order.
	 * The oldest idle objects reside on the inactive list, which is in
	 * retirement order. The next objects to retire are those on the (per
	 * ring) active list that do not have an outstanding flush. Once the
	 * hardware reports completion (the seqno is updated after the
	 * batchbuffer has been finished) the clean buffer objects would
	 * be retired to the inactive list. Any dirty objects would be added
	 * to the tail of the flushing list. So after processing the clean
	 * active objects we need to emit a MI_FLUSH to retire the flushing
	 * list, hence the retirement order of the flushing list is in
	 * advance of the dirty objects on the active lists.
	 *
	 * The retirement sequence is thus:
	 *   1. Inactive objects (already retired)
	 *   2. Clean active objects
	 *   3. Flushing list
	 *   4. Dirty active objects.
	 *
	 * On each list, the oldest objects lie at the HEAD with the freshest
	 * object on the TAIL.
	 */

	INIT_LIST_HEAD(&unwind_list);
	drm_mm_init_scan(&dev_priv->mm.gtt_space, min_size, alignment);

	/* First see if there is a large enough contiguous idle region... */
	list_for_each_entry(obj_priv, &dev_priv->mm.inactive_list, list) {
		if (mark_free(obj_priv, &unwind_list))
			goto found;
	}

	/* Now merge in the soon-to-be-expired objects... */
	i915_for_each_active_object(obj_priv, &render_iter, &bsd_iter) {
		/* Does the object require an outstanding flush? */
		if (obj_priv->base.write_domain || obj_priv->pin_count)
			continue;

		if (mark_free(obj_priv, &unwind_list))
			goto found;
	}

	/* Finally add anything with a pending flush (in order of retirement) */
	list_for_each_entry(obj_priv, &dev_priv->mm.flushing_list, list) {
		if (obj_priv->pin_count)
			continue;

		if (mark_free(obj_priv, &unwind_list))
			goto found;
	}
	i915_for_each_active_object(obj_priv, &render_iter, &bsd_iter) {
		if (! obj_priv->base.write_domain || obj_priv->pin_count)
			continue;

		if (mark_free(obj_priv, &unwind_list))
			goto found;
	}

	/* Nothing found, clean up and bail out! */
	list_for_each_entry(obj_priv, &unwind_list, evict_list) {
		ret = drm_mm_scan_remove_block(obj_priv->gtt_space);
		BUG_ON(ret);
		drm_gem_object_unreference(&obj_priv->base);
	}

	/* We expect the caller to unpin, evict all and try again, or give up.
	 * So calling i915_gem_evict_everything() is unnecessary.
	 */
	return -ENOSPC;

found:
	INIT_LIST_HEAD(&eviction_list);
	list_for_each_entry_safe(obj_priv, tmp_obj_priv,
				 &unwind_list, evict_list) {
		if (drm_mm_scan_remove_block(obj_priv->gtt_space)) {
			/* drm_mm doesn't allow any other other operations while
			 * scanning, therefore store to be evicted objects on a
			 * temporary list. */
			list_move(&obj_priv->evict_list, &eviction_list);
		} else
			drm_gem_object_unreference(&obj_priv->base);
	}

	/* Unbinding will emit any required flushes */
	list_for_each_entry_safe(obj_priv, tmp_obj_priv,
				 &eviction_list, evict_list) {
		ret = i915_gem_object_unbind(&obj_priv->base);
		if (ret)
			return ret;

		drm_gem_object_unreference(&obj_priv->base);
	}

	/* The just created free hole should be on the top of the free stack
	 * maintained by drm_mm, so this BUG_ON actually executes in O(1).
	 * Furthermore all accessed data has just recently been used, so it
	 * should be really fast, too. */
	BUG_ON(!drm_mm_search_free(&dev_priv->mm.gtt_space, min_size,
				   alignment, 0));

	return 0;
}

int
i915_gem_evict_everything(struct drm_device *dev)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;
	bool lists_empty;

	lists_empty = (list_empty(&dev_priv->mm.inactive_list) &&
		       list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->render_ring.active_list) &&
		       (!HAS_BSD(dev)
			|| list_empty(&dev_priv->bsd_ring.active_list)));
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

	lists_empty = (list_empty(&dev_priv->mm.inactive_list) &&
		       list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->render_ring.active_list) &&
		       (!HAS_BSD(dev)
			|| list_empty(&dev_priv->bsd_ring.active_list)));
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
