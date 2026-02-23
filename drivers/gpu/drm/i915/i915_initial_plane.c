// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include <drm/drm_print.h>
#include <drm/intel/display_parent_interface.h>

#include "display/intel_crtc.h"
#include "display/intel_display_types.h"
#include "display/intel_fb.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"

#include "i915_drv.h"
#include "i915_initial_plane.h"

static void i915_initial_plane_vblank_wait(struct drm_crtc *crtc)
{
	intel_crtc_wait_for_next_vblank(to_intel_crtc(crtc));
}

static enum intel_memory_type
initial_plane_memory_type(struct drm_i915_private *i915)
{
	if (IS_DGFX(i915))
		return INTEL_MEMORY_LOCAL;
	else if (HAS_LMEMBAR_SMEM_STOLEN(i915))
		return INTEL_MEMORY_STOLEN_LOCAL;
	else
		return INTEL_MEMORY_STOLEN_SYSTEM;
}

static bool
initial_plane_phys(struct drm_i915_private *i915,
		   struct intel_initial_plane_config *plane_config)
{
	struct i915_ggtt *ggtt = to_gt(i915)->ggtt;
	struct intel_memory_region *mem;
	enum intel_memory_type mem_type;
	bool is_present, is_local;
	dma_addr_t dma_addr;
	u32 base;

	mem_type = initial_plane_memory_type(i915);
	mem = intel_memory_region_by_type(i915, mem_type);
	if (!mem) {
		drm_dbg_kms(&i915->drm,
			    "Initial plane memory region (type %s) not initialized\n",
			    intel_memory_type_str(mem_type));
		return false;
	}

	base = round_down(plane_config->base, I915_GTT_MIN_ALIGNMENT);

	dma_addr = intel_ggtt_read_entry(&ggtt->vm, base, &is_present, &is_local);

	if (!is_present) {
		drm_err(&i915->drm, "Initial plane FB PTE not present\n");
		return false;
	}

	if (intel_memory_type_is_local(mem->type) != is_local) {
		drm_err(&i915->drm, "Initial plane FB PTE unsuitable for %s\n",
			mem->region.name);
		return false;
	}

	if (dma_addr < mem->region.start || dma_addr > mem->region.end) {
		drm_err(&i915->drm,
			"Initial plane programming using invalid range, dma_addr=%pa (%s [%pa-%pa])\n",
			&dma_addr, mem->region.name, &mem->region.start, &mem->region.end);
		return false;
	}

	drm_dbg(&i915->drm, "Using dma_addr=%pa, based on initial plane programming\n",
		&dma_addr);

	plane_config->phys_base = dma_addr - mem->region.start;
	plane_config->mem = mem;

	return true;
}

static struct i915_vma *
initial_plane_vma(struct drm_i915_private *i915,
		  struct intel_initial_plane_config *plane_config)
{
	struct intel_memory_region *mem;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node orig_mm = {};
	struct i915_vma *vma;
	resource_size_t phys_base;
	unsigned int tiling;
	u32 base, size;
	u64 pinctl;

	if (plane_config->size == 0)
		return NULL;

	if (!initial_plane_phys(i915, plane_config))
		return NULL;

	phys_base = plane_config->phys_base;
	mem = plane_config->mem;

	base = round_down(plane_config->base, I915_GTT_MIN_ALIGNMENT);
	size = round_up(plane_config->base + plane_config->size,
			mem->min_page_size);
	size -= base;

	/*
	 * If the FB is too big, just don't use it since fbdev is not very
	 * important and we should probably use that space with FBC or other
	 * features.
	 */
	if (IS_ENABLED(CONFIG_FRAMEBUFFER_CONSOLE) &&
	    mem == i915->mm.stolen_region &&
	    size * 2 > i915->dsm.usable_size) {
		drm_dbg_kms(&i915->drm, "Initial FB size exceeds half of stolen, discarding\n");
		return NULL;
	}

	obj = i915_gem_object_create_region_at(mem, phys_base, size,
					       I915_BO_ALLOC_USER |
					       I915_BO_PREALLOC);
	if (IS_ERR(obj)) {
		drm_dbg_kms(&i915->drm, "Failed to preallocate initial FB in %s\n",
			    mem->region.name);
		return NULL;
	}

	/*
	 * Mark it WT ahead of time to avoid changing the
	 * cache_level during fbdev initialization. The
	 * unbind there would get stuck waiting for rcu.
	 */
	i915_gem_object_set_cache_coherency(obj, HAS_WT(i915) ?
					    I915_CACHE_WT : I915_CACHE_NONE);

	tiling = intel_fb_modifier_to_tiling(plane_config->fb->base.modifier);

	switch (tiling) {
	case I915_TILING_NONE:
		break;
	case I915_TILING_X:
	case I915_TILING_Y:
		obj->tiling_and_stride =
			plane_config->fb->base.pitches[0] |
			tiling;
		break;
	default:
		MISSING_CASE(tiling);
		goto err_obj;
	}

	/*
	 * MTL GOP likes to place the framebuffer high up in ggtt,
	 * which can cause problems for ggtt_reserve_guc_top().
	 * Try to pin it to a low ggtt address instead to avoid that.
	 */
	base = 0;

	if (base != plane_config->base) {
		struct i915_ggtt *ggtt = to_gt(i915)->ggtt;
		int ret;

		/*
		 * Make sure the original and new locations
		 * can't overlap. That would corrupt the original
		 * PTEs which are still being used for scanout.
		 */
		ret = i915_gem_gtt_reserve(&ggtt->vm, NULL, &orig_mm,
					   size, plane_config->base,
					   I915_COLOR_UNEVICTABLE, PIN_NOEVICT);
		if (ret)
			goto err_obj;
	}

	vma = i915_vma_instance(obj, &to_gt(i915)->ggtt->vm, NULL);
	if (IS_ERR(vma))
		goto err_obj;

retry:
	pinctl = PIN_GLOBAL | PIN_OFFSET_FIXED | base;
	if (!i915_gem_object_is_lmem(obj))
		pinctl |= PIN_MAPPABLE;
	if (i915_vma_pin(vma, 0, 0, pinctl)) {
		if (drm_mm_node_allocated(&orig_mm)) {
			drm_mm_remove_node(&orig_mm);
			/*
			 * Try again, but this time pin
			 * it to its original location.
			 */
			base = plane_config->base;
			goto retry;
		}
		goto err_obj;
	}

	if (i915_gem_object_is_tiled(obj) &&
	    !i915_vma_is_map_and_fenceable(vma))
		goto err_obj;

	if (drm_mm_node_allocated(&orig_mm))
		drm_mm_remove_node(&orig_mm);

	drm_dbg_kms(&i915->drm,
		    "Initial plane fb bound to 0x%x in the ggtt (original 0x%x)\n",
		    i915_ggtt_offset(vma), plane_config->base);

	return vma;

err_obj:
	if (drm_mm_node_allocated(&orig_mm))
		drm_mm_remove_node(&orig_mm);
	i915_gem_object_put(obj);
	return NULL;
}

static struct drm_gem_object *
i915_alloc_initial_plane_obj(struct drm_device *drm,
			     struct intel_initial_plane_config *plane_config)
{
	struct drm_i915_private *i915 = to_i915(drm);
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct drm_framebuffer *fb = &plane_config->fb->base;
	struct i915_vma *vma;

	vma = initial_plane_vma(i915, plane_config);
	if (!vma)
		return NULL;

	mode_cmd.pixel_format = fb->format->format;
	mode_cmd.width = fb->width;
	mode_cmd.height = fb->height;
	mode_cmd.pitches[0] = fb->pitches[0];
	mode_cmd.modifier[0] = fb->modifier;
	mode_cmd.flags = DRM_MODE_FB_MODIFIERS;

	if (intel_framebuffer_init(to_intel_framebuffer(fb),
				   intel_bo_to_drm_bo(vma->obj),
				   fb->format, &mode_cmd)) {
		drm_dbg_kms(&i915->drm, "intel fb init failed\n");
		goto err_vma;
	}

	plane_config->vma = vma;
	return intel_bo_to_drm_bo(vma->obj);

err_vma:
	i915_vma_put(vma);
	return NULL;
}

static int
i915_initial_plane_setup(struct drm_plane_state *_plane_state,
			 struct intel_initial_plane_config *plane_config,
			 struct drm_framebuffer *fb,
			 struct i915_vma *vma)
{
	struct intel_plane_state *plane_state = to_intel_plane_state(_plane_state);
	struct drm_i915_private *dev_priv = to_i915(_plane_state->plane->dev);

	__i915_vma_pin(vma);
	plane_state->ggtt_vma = i915_vma_get(vma);
	if (intel_plane_uses_fence(plane_state) &&
	    i915_vma_pin_fence(vma) == 0 && vma->fence)
		plane_state->flags |= PLANE_HAS_FENCE;

	plane_state->surf = i915_ggtt_offset(plane_state->ggtt_vma);

	if (fb->modifier != DRM_FORMAT_MOD_LINEAR)
		dev_priv->preserve_bios_swizzle = true;

	return 0;
}

static void i915_plane_config_fini(struct intel_initial_plane_config *plane_config)
{
	if (plane_config->vma)
		i915_vma_put(plane_config->vma);
}

const struct intel_display_initial_plane_interface i915_display_initial_plane_interface = {
	.vblank_wait = i915_initial_plane_vblank_wait,
	.alloc_obj = i915_alloc_initial_plane_obj,
	.setup = i915_initial_plane_setup,
	.config_fini = i915_plane_config_fini,
};
