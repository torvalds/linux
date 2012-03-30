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
#include "i915_trace.h"

static bool
mark_free(struct drm_i915_gem_object *obj, struct list_head *unwind)
{
	list_add(&obj->exec_list, unwind);
	return drm_mm_scan_add_block(obj->gtt_space);
}

int
i915_gem_evict_something(struct drm_device *dev, int min_size,
			 unsigned alignment, bool mappable)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct list_head eviction_list, unwind_list;
	struct drm_i915_gem_object *obj;
	int ret = 0;

	trace_i915_gem_evict(dev, min_size, alignment, mappable);

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
	if (mappable)
		drm_mm_init_scan_with_range(&dev_priv->mm.gtt_space, min_size,
					    alignment, 0,
					    dev_priv->mm.gtt_mappable_end);
	else
		drm_mm_init_scan(&dev_priv->mm.gtt_space, min_size, alignment);

	/* First see if there is a large enough contiguous idle region... */
	list_for_each_entry(obj, &dev_priv->mm.inactive_list, mm_list) {
		if (mark_free(obj, &unwind_list))
			goto found;
	}

	/* Now merge in the soon-to-be-expired objects... */
	list_for_each_entry(obj, &dev_priv->mm.active_list, mm_list) {
		/* Does the object require an outstanding flush? */
		if (obj->base.write_domain || obj->pin_count)
			continue;

		if (mark_free(obj, &unwind_list))
			goto found;
	}

	/* Finally add anything with a pending flush (in order of retirement) */
	list_for_each_entry(obj, &dev_priv->mm.flushing_list, mm_list) {
		if (obj->pin_count)
			continue;

		if (mark_free(obj, &unwind_list))
			goto found;
	}
	list_for_each_entry(obj, &dev_priv->mm.active_list, mm_list) {
		if (!obj->base.write_domain || obj->pin_count)
			continue;

		if (mark_free(obj, &unwind_list))
			goto found;
	}

	/* Nothing found, clean up and bail out! */
	while (!list_empty(&unwind_list)) {
		obj = list_first_entry(&unwind_list,
				       struct drm_i915_gem_object,
				       exec_list);

		ret = drm_mm_scan_remove_block(obj->gtt_space);
		BUG_ON(ret);

		list_del_init(&obj->exec_list);
	}

	/* We expect the caller to unpin, evict all and try again, or give up.
	 * So calling i915_gem_evict_everything() is unnecessary.
	 */
	return -ENOSPC;

found:
	/* drm_mm doesn't allow any other other operations while
	 * scanning, therefore store to be evicted objects on a
	 * temporary list. */
	INIT_LIST_HEAD(&eviction_list);
	while (!list_empty(&unwind_list)) {
		obj = list_first_entry(&unwind_list,
				       struct drm_i915_gem_object,
				       exec_list);
		if (drm_mm_scan_remove_block(obj->gtt_space)) {
			list_move(&obj->exec_list, &eviction_list);
			drm_gem_object_reference(&obj->base);
			continue;
		}
		list_del_init(&obj->exec_list);
	}

	/* Unbinding will emit any required flushes */
	while (!list_empty(&eviction_list)) {
		obj = list_first_entry(&eviction_list,
				       struct drm_i915_gem_object,
				       exec_list);
		if (ret == 0)
			ret = i915_gem_object_unbind(obj);

		list_del_init(&obj->exec_list);
		drm_gem_object_unreference(&obj->base);
	}

	return ret;
}

int
i915_gem_evict_everything(struct drm_device *dev, bool purgeable_only)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	int ret;
	bool lists_empty;

	lists_empty = (list_empty(&dev_priv->mm.inactive_list) &&
		       list_empty(&dev_priv->mm.flushing_list) &&
		       list_empty(&dev_priv->mm.active_list));
	if (lists_empty)
		return -ENOSPC;

	trace_i915_gem_evict_everything(dev, purgeable_only);

	/* Flush everything (on to the inactive lists) and evict */
	ret = i915_gpu_idle(dev, true);
	if (ret)
		return ret;

	BUG_ON(!list_empty(&dev_priv->mm.flushing_list));

	return i915_gem_evict_inactive(dev, purgeable_only);
}

/** Unbinds all inactive objects. */
int
i915_gem_evict_inactive(struct drm_device *dev, bool purgeable_only)
{
	drm_i915_private_t *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj, *next;

	list_for_each_entry_safe(obj, next,
				 &dev_priv->mm.inactive_list, mm_list) {
		if (!purgeable_only || obj->madv != I915_MADV_WILLNEED) {
			int ret = i915_gem_object_unbind(obj);
			if (ret)
				return ret;
		}
	}

	return 0;
}
