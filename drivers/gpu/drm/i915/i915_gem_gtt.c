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
	struct drm_i915_gem_object *obj;

	list_for_each_entry(obj, &dev_priv->mm.gtt_list, gtt_list) {
		i915_gem_clflush_object(obj);

		if (dev_priv->mm.gtt->needs_dmar) {
			BUG_ON(!obj->sg_list);

			intel_gtt_insert_sg_entries(obj->sg_list,
						    obj->num_sg,
						    obj->gtt_space->start
							>> PAGE_SHIFT,
						    obj->agp_type);
		} else
			intel_gtt_insert_pages(obj->gtt_space->start
						   >> PAGE_SHIFT,
					       obj->base.size >> PAGE_SHIFT,
					       obj->pages,
					       obj->agp_type);
	}

	intel_gtt_chipset_flush();
}

int i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	int ret;

	if (dev_priv->mm.gtt->needs_dmar) {
		ret = intel_gtt_map_memory(obj->pages,
					   obj->base.size >> PAGE_SHIFT,
					   &obj->sg_list,
					   &obj->num_sg);
		if (ret != 0)
			return ret;

		intel_gtt_insert_sg_entries(obj->sg_list,
					    obj->num_sg,
					    obj->gtt_space->start >> PAGE_SHIFT,
					    obj->agp_type);
	} else
		intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
				       obj->base.size >> PAGE_SHIFT,
				       obj->pages,
				       obj->agp_type);

	return 0;
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (dev_priv->mm.gtt->needs_dmar) {
		intel_gtt_unmap_memory(obj->sg_list, obj->num_sg);
		obj->sg_list = NULL;
		obj->num_sg = 0;
	}

	intel_gtt_clear_range(obj->gtt_space->start >> PAGE_SHIFT,
			      obj->base.size >> PAGE_SHIFT);
}
