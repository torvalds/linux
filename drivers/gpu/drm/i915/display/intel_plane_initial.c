// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "i915_drv.h"
#include "intel_atomic_plane.h"
#include "intel_crtc.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_frontbuffer.h"
#include "intel_plane_initial.h"

static bool
intel_reuse_initial_plane_obj(struct intel_crtc *this,
			      const struct intel_initial_plane_config plane_configs[],
			      struct drm_framebuffer **fb,
			      struct i915_vma **vma)
{
	struct intel_display *display = to_intel_display(this);
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_plane *plane =
			to_intel_plane(crtc->base.primary);
		const struct intel_plane_state *plane_state =
			to_intel_plane_state(plane->base.state);
		const struct intel_crtc_state *crtc_state =
			to_intel_crtc_state(crtc->base.state);

		if (!crtc_state->uapi.active)
			continue;

		if (!plane_state->ggtt_vma)
			continue;

		if (plane_configs[this->pipe].base == plane_configs[crtc->pipe].base) {
			*fb = plane_state->hw.fb;
			*vma = plane_state->ggtt_vma;
			return true;
		}
	}

	return false;
}

static bool
initial_plane_phys_lmem(struct intel_display *display,
			struct intel_initial_plane_config *plane_config)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	gen8_pte_t __iomem *gte = to_gt(i915)->ggtt->gsm;
	struct intel_memory_region *mem;
	dma_addr_t dma_addr;
	gen8_pte_t pte;
	u32 base;

	base = round_down(plane_config->base, I915_GTT_MIN_ALIGNMENT);

	gte += base / I915_GTT_PAGE_SIZE;

	pte = ioread64(gte);
	if (!(pte & GEN12_GGTT_PTE_LM)) {
		drm_err(display->drm,
			"Initial plane programming missing PTE_LM bit\n");
		return false;
	}

	dma_addr = pte & GEN12_GGTT_PTE_ADDR_MASK;

	if (IS_DGFX(i915))
		mem = i915->mm.regions[INTEL_REGION_LMEM_0];
	else
		mem = i915->mm.stolen_region;
	if (!mem) {
		drm_dbg_kms(display->drm,
			    "Initial plane memory region not initialized\n");
		return false;
	}

	/*
	 * On lmem we don't currently expect this to
	 * ever be placed in the stolen portion.
	 */
	if (dma_addr < mem->region.start || dma_addr > mem->region.end) {
		drm_err(display->drm,
			"Initial plane programming using invalid range, dma_addr=%pa (%s [%pa-%pa])\n",
			&dma_addr, mem->region.name, &mem->region.start, &mem->region.end);
		return false;
	}

	drm_dbg(display->drm,
		"Using dma_addr=%pa, based on initial plane programming\n",
		&dma_addr);

	plane_config->phys_base = dma_addr - mem->region.start;
	plane_config->mem = mem;

	return true;
}

static bool
initial_plane_phys_smem(struct intel_display *display,
			struct intel_initial_plane_config *plane_config)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_memory_region *mem;
	u32 base;

	base = round_down(plane_config->base, I915_GTT_MIN_ALIGNMENT);

	mem = i915->mm.stolen_region;
	if (!mem) {
		drm_dbg_kms(display->drm,
			    "Initial plane memory region not initialized\n");
		return false;
	}

	/* FIXME get and validate the dma_addr from the PTE */
	plane_config->phys_base = base;
	plane_config->mem = mem;

	return true;
}

static bool
initial_plane_phys(struct intel_display *display,
		   struct intel_initial_plane_config *plane_config)
{
	struct drm_i915_private *i915 = to_i915(display->drm);

	if (IS_DGFX(i915) || HAS_LMEMBAR_SMEM_STOLEN(i915))
		return initial_plane_phys_lmem(display, plane_config);
	else
		return initial_plane_phys_smem(display, plane_config);
}

static struct i915_vma *
initial_plane_vma(struct intel_display *display,
		  struct intel_initial_plane_config *plane_config)
{
	struct drm_i915_private *i915 = to_i915(display->drm);
	struct intel_memory_region *mem;
	struct drm_i915_gem_object *obj;
	struct drm_mm_node orig_mm = {};
	struct i915_vma *vma;
	resource_size_t phys_base;
	u32 base, size;
	u64 pinctl;

	if (plane_config->size == 0)
		return NULL;

	if (!initial_plane_phys(display, plane_config))
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
		drm_dbg_kms(display->drm, "Initial FB size exceeds half of stolen, discarding\n");
		return NULL;
	}

	obj = i915_gem_object_create_region_at(mem, phys_base, size,
					       I915_BO_ALLOC_USER |
					       I915_BO_PREALLOC);
	if (IS_ERR(obj)) {
		drm_dbg_kms(display->drm, "Failed to preallocate initial FB in %s\n",
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

	switch (plane_config->tiling) {
	case I915_TILING_NONE:
		break;
	case I915_TILING_X:
	case I915_TILING_Y:
		obj->tiling_and_stride =
			plane_config->fb->base.pitches[0] |
			plane_config->tiling;
		break;
	default:
		MISSING_CASE(plane_config->tiling);
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

	drm_dbg_kms(display->drm,
		    "Initial plane fb bound to 0x%x in the ggtt (original 0x%x)\n",
		    i915_ggtt_offset(vma), plane_config->base);

	return vma;

err_obj:
	if (drm_mm_node_allocated(&orig_mm))
		drm_mm_remove_node(&orig_mm);
	i915_gem_object_put(obj);
	return NULL;
}

static bool
intel_alloc_initial_plane_obj(struct intel_crtc *crtc,
			      struct intel_initial_plane_config *plane_config)
{
	struct intel_display *display = to_intel_display(crtc);
	struct drm_mode_fb_cmd2 mode_cmd = {};
	struct drm_framebuffer *fb = &plane_config->fb->base;
	struct i915_vma *vma;

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_4_TILED:
		break;
	default:
		drm_dbg(display->drm,
			"Unsupported modifier for initial FB: 0x%llx\n",
			fb->modifier);
		return false;
	}

	vma = initial_plane_vma(display, plane_config);
	if (!vma)
		return false;

	mode_cmd.pixel_format = fb->format->format;
	mode_cmd.width = fb->width;
	mode_cmd.height = fb->height;
	mode_cmd.pitches[0] = fb->pitches[0];
	mode_cmd.modifier[0] = fb->modifier;
	mode_cmd.flags = DRM_MODE_FB_MODIFIERS;

	if (intel_framebuffer_init(to_intel_framebuffer(fb),
				   intel_bo_to_drm_bo(vma->obj), &mode_cmd)) {
		drm_dbg_kms(display->drm, "intel fb init failed\n");
		goto err_vma;
	}

	plane_config->vma = vma;
	return true;

err_vma:
	i915_vma_put(vma);
	return false;
}

static void
intel_find_initial_plane_obj(struct intel_crtc *crtc,
			     struct intel_initial_plane_config plane_configs[])
{
	struct drm_i915_private *dev_priv = to_i915(crtc->base.dev);
	struct intel_initial_plane_config *plane_config =
		&plane_configs[crtc->pipe];
	struct intel_plane *plane =
		to_intel_plane(crtc->base.primary);
	struct intel_plane_state *plane_state =
		to_intel_plane_state(plane->base.state);
	struct drm_framebuffer *fb;
	struct i915_vma *vma;

	/*
	 * TODO:
	 *   Disable planes if get_initial_plane_config() failed.
	 *   Make sure things work if the surface base is not page aligned.
	 */
	if (!plane_config->fb)
		return;

	if (intel_alloc_initial_plane_obj(crtc, plane_config)) {
		fb = &plane_config->fb->base;
		vma = plane_config->vma;
		goto valid_fb;
	}

	/*
	 * Failed to alloc the obj, check to see if we should share
	 * an fb with another CRTC instead
	 */
	if (intel_reuse_initial_plane_obj(crtc, plane_configs, &fb, &vma))
		goto valid_fb;

	/*
	 * We've failed to reconstruct the BIOS FB.  Current display state
	 * indicates that the primary plane is visible, but has a NULL FB,
	 * which will lead to problems later if we don't fix it up.  The
	 * simplest solution is to just disable the primary plane now and
	 * pretend the BIOS never had it enabled.
	 */
	intel_plane_disable_noatomic(crtc, plane);

	return;

valid_fb:
	plane_state->uapi.rotation = plane_config->rotation;
	intel_fb_fill_view(to_intel_framebuffer(fb),
			   plane_state->uapi.rotation, &plane_state->view);

	__i915_vma_pin(vma);
	plane_state->ggtt_vma = i915_vma_get(vma);
	if (intel_plane_uses_fence(plane_state) &&
	    i915_vma_pin_fence(vma) == 0 && vma->fence)
		plane_state->flags |= PLANE_HAS_FENCE;

	plane_state->uapi.src_x = 0;
	plane_state->uapi.src_y = 0;
	plane_state->uapi.src_w = fb->width << 16;
	plane_state->uapi.src_h = fb->height << 16;

	plane_state->uapi.crtc_x = 0;
	plane_state->uapi.crtc_y = 0;
	plane_state->uapi.crtc_w = fb->width;
	plane_state->uapi.crtc_h = fb->height;

	if (plane_config->tiling)
		dev_priv->preserve_bios_swizzle = true;

	plane_state->uapi.fb = fb;
	drm_framebuffer_get(fb);

	plane_state->uapi.crtc = &crtc->base;
	intel_plane_copy_uapi_to_hw_state(plane_state, plane_state, crtc);

	atomic_or(plane->frontbuffer_bit, &to_intel_frontbuffer(fb)->bits);
}

static void plane_config_fini(struct intel_initial_plane_config *plane_config)
{
	if (plane_config->fb) {
		struct drm_framebuffer *fb = &plane_config->fb->base;

		/* We may only have the stub and not a full framebuffer */
		if (drm_framebuffer_read_refcount(fb))
			drm_framebuffer_put(fb);
		else
			kfree(fb);
	}

	if (plane_config->vma)
		i915_vma_put(plane_config->vma);
}

void intel_initial_plane_config(struct intel_display *display)
{
	struct intel_initial_plane_config plane_configs[I915_MAX_PIPES] = {};
	struct intel_crtc *crtc;

	for_each_intel_crtc(display->drm, crtc) {
		struct intel_initial_plane_config *plane_config =
			&plane_configs[crtc->pipe];

		if (!to_intel_crtc_state(crtc->base.state)->uapi.active)
			continue;

		/*
		 * Note that reserving the BIOS fb up front prevents us
		 * from stuffing other stolen allocations like the ring
		 * on top.  This prevents some ugliness at boot time, and
		 * can even allow for smooth boot transitions if the BIOS
		 * fb is large enough for the active pipe configuration.
		 */
		display->funcs.display->get_initial_plane_config(crtc, plane_config);

		/*
		 * If the fb is shared between multiple heads, we'll
		 * just get the first one.
		 */
		intel_find_initial_plane_obj(crtc, plane_configs);

		if (display->funcs.display->fixup_initial_plane_config(crtc, plane_config))
			intel_crtc_wait_for_next_vblank(crtc);

		plane_config_fini(plane_config);
	}
}
