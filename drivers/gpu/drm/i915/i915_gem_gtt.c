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

/* XXX kill agp_type! */
static unsigned int cache_level_to_agp_type(struct drm_device *dev,
					    enum i915_cache_level cache_level)
{
	switch (cache_level) {
	case I915_CACHE_LLC_MLC:
		if (INTEL_INFO(dev)->gen >= 6)
			return AGP_USER_CACHED_MEMORY_LLC_MLC;
		/* Older chipsets do not have this extra level of CPU
		 * cacheing, so fallthrough and request the PTE simply
		 * as cached.
		 */
	case I915_CACHE_LLC:
		return AGP_USER_CACHED_MEMORY;
	default:
	case I915_CACHE_NONE:
		return AGP_USER_MEMORY;
	}
}

static bool do_idling(struct drm_i915_private *dev_priv)
{
	bool ret = dev_priv->mm.interruptible;

	if (unlikely(dev_priv->mm.gtt->do_idle_maps)) {
		dev_priv->mm.interruptible = false;
		if (i915_gpu_idle(dev_priv->dev)) {
			DRM_ERROR("Couldn't idle GPU\n");
			/* Wait a bit, in hopes it avoids the hang */
			udelay(10);
		}
	}

	return ret;
}

static void undo_idling(struct drm_i915_private *dev_priv, bool interruptible)
{
	if (unlikely(dev_priv->mm.gtt->do_idle_maps))
		dev_priv->mm.interruptible = interruptible;
}

void i915_gem_restore_gtt_mappings(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	struct drm_i915_gem_object *obj;

	/* First fill our portion of the GTT with scratch pages */
	intel_gtt_clear_range(dev_priv->mm.gtt_start / PAGE_SIZE,
			      (dev_priv->mm.gtt_end - dev_priv->mm.gtt_start) / PAGE_SIZE);

	list_for_each_entry(obj, &dev_priv->mm.gtt_list, gtt_list) {
		i915_gem_clflush_object(obj);
		i915_gem_gtt_rebind_object(obj, obj->cache_level);
	}

	intel_gtt_chipset_flush();
}

int i915_gem_gtt_bind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int agp_type = cache_level_to_agp_type(dev, obj->cache_level);
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
					    agp_type);
	} else
		intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
				       obj->base.size >> PAGE_SHIFT,
				       obj->pages,
				       agp_type);

	return 0;
}

void i915_gem_gtt_rebind_object(struct drm_i915_gem_object *obj,
				enum i915_cache_level cache_level)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned int agp_type = cache_level_to_agp_type(dev, cache_level);

	if (dev_priv->mm.gtt->needs_dmar) {
		BUG_ON(!obj->sg_list);

		intel_gtt_insert_sg_entries(obj->sg_list,
					    obj->num_sg,
					    obj->gtt_space->start >> PAGE_SHIFT,
					    agp_type);
	} else
		intel_gtt_insert_pages(obj->gtt_space->start >> PAGE_SHIFT,
				       obj->base.size >> PAGE_SHIFT,
				       obj->pages,
				       agp_type);
}

void i915_gem_gtt_unbind_object(struct drm_i915_gem_object *obj)
{
	struct drm_device *dev = obj->base.dev;
	struct drm_i915_private *dev_priv = dev->dev_private;
	bool interruptible;

	interruptible = do_idling(dev_priv);

	intel_gtt_clear_range(obj->gtt_space->start >> PAGE_SHIFT,
			      obj->base.size >> PAGE_SHIFT);

	if (obj->sg_list) {
		intel_gtt_unmap_memory(obj->sg_list, obj->num_sg);
		obj->sg_list = NULL;
	}

	undo_idling(dev_priv, interruptible);
}
