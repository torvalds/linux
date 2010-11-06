/*
 * Copyright © 2010 Daniel Vetter
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

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"
#include "i915_trace.h"
#include "intel_drv.h"

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj_priv;
	int ret;

	list_for_each_entry(obj_priv,
			    &dev_priv->mm.gtt_list,
			    gtt_list) {
		/* Hack to force agp to reinsert buffer object. */
		obj_priv->agp_mem->is_bound = false;
		ret = agp_bind_memory(obj_priv->agp_mem, obj_priv->gtt_space->start / PAGE_SIZE);
		BUG_ON(ret != 0);
	}

	/* Be paranoid and flush the chipset cache. */
	intel_gtt_chipset_flush();
}

int i915_gem_gtt_bind_object(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);

	/* Create an AGP memory structure pointing at our pages, and bind it
	 * into the GTT.
	 */
	obj_priv->agp_mem = drm_agp_bind_pages(dev,
					       obj_priv->pages,
					       obj->size >> PAGE_SHIFT,
					       obj_priv->gtt_space->start,
					       obj_priv->agp_type);

	if (obj_priv->agp_mem)
		return 0;
	else
		return -ENOMEM;
}

void i915_gem_gtt_unbind_object(struct drm_gem_object *obj)
{
	struct drm_i915_gem_object *obj_priv = to_intel_bo(obj);

	drm_unbind_agp(obj_priv->agp_mem);
	drm_free_agp(obj_priv->agp_mem, obj->size / PAGE_SIZE);
}
