// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/* for ioread64 */
#include <linux/io-64-nonatomic-lo-hi.h>

#include "regs/xe_gtt_defs.h"
#include "xe_ggtt.h"

#include "intel_atomic_plane.h"
#include "intel_crtc.h"
#include "intel_display.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "intel_frontbuffer.h"
#include "intel_plane_initial.h"
#include "xe_bo.h"
#include "xe_wa.h"

#include <generated/xe_wa_oob.h>

static bool
intel_reuse_initial_plane_obj(struct intel_crtc *this,
			      const struct intel_initial_plane_config plane_configs[],
			      struct drm_framebuffer **fb)
{
	struct xe_device *xe = to_xe_device(this->base.dev);
	struct intel_crtc *crtc;

	for_each_intel_crtc(&xe->drm, crtc) {
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
			return true;
		}
	}

	return false;
}

static struct xe_bo *
initial_plane_bo(struct xe_device *xe,
		 struct intel_initial_plane_config *plane_config)
{
	struct xe_tile *tile0 = xe_device_get_root_tile(xe);
	struct xe_bo *bo;
	resource_size_t phys_base;
	u32 base, size, flags;
	u64 page_size = xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K ? SZ_64K : SZ_4K;

	if (plane_config->size == 0)
		return NULL;

	flags = XE_BO_FLAG_PINNED | XE_BO_FLAG_SCANOUT | XE_BO_FLAG_GGTT;

	base = round_down(plane_config->base, page_size);
	if (IS_DGFX(xe)) {
		u64 __iomem *gte = tile0->mem.ggtt->gsm;
		u64 pte;

		gte += base / XE_PAGE_SIZE;

		pte = ioread64(gte);
		if (!(pte & XE_GGTT_PTE_DM)) {
			drm_err(&xe->drm,
				"Initial plane programming missing DM bit\n");
			return NULL;
		}

		phys_base = pte & ~(page_size - 1);
		flags |= XE_BO_FLAG_VRAM0;

		/*
		 * We don't currently expect this to ever be placed in the
		 * stolen portion.
		 */
		if (phys_base >= tile0->mem.vram.usable_size) {
			drm_err(&xe->drm,
				"Initial plane programming using invalid range, phys_base=%pa\n",
				&phys_base);
			return NULL;
		}

		drm_dbg(&xe->drm,
			"Using phys_base=%pa, based on initial plane programming\n",
			&phys_base);
	} else {
		struct ttm_resource_manager *stolen = ttm_manager_type(&xe->ttm, XE_PL_STOLEN);

		if (!stolen)
			return NULL;
		phys_base = base;
		flags |= XE_BO_FLAG_STOLEN;

		if (XE_WA(xe_root_mmio_gt(xe), 22019338487_display))
			return NULL;

		/*
		 * If the FB is too big, just don't use it since fbdev is not very
		 * important and we should probably use that space with FBC or other
		 * features.
		 */
		if (IS_ENABLED(CONFIG_FRAMEBUFFER_CONSOLE) &&
		    plane_config->size * 2 >> PAGE_SHIFT >= stolen->size)
			return NULL;
	}

	size = round_up(plane_config->base + plane_config->size,
			page_size);
	size -= base;

	bo = xe_bo_create_pin_map_at(xe, tile0, NULL, size, phys_base,
				     ttm_bo_type_kernel, flags);
	if (IS_ERR(bo)) {
		drm_dbg(&xe->drm,
			"Failed to create bo phys_base=%pa size %u with flags %x: %li\n",
			&phys_base, size, flags, PTR_ERR(bo));
		return NULL;
	}

	return bo;
}

static bool
intel_alloc_initial_plane_obj(struct intel_crtc *crtc,
			      struct intel_initial_plane_config *plane_config)
{
	struct xe_device *xe = to_xe_device(crtc->base.dev);
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_framebuffer *fb = &plane_config->fb->base;
	struct xe_bo *bo;

	switch (fb->modifier) {
	case DRM_FORMAT_MOD_LINEAR:
	case I915_FORMAT_MOD_X_TILED:
	case I915_FORMAT_MOD_Y_TILED:
	case I915_FORMAT_MOD_4_TILED:
		break;
	default:
		drm_dbg_kms(&xe->drm,
			    "Unsupported modifier for initial FB: 0x%llx\n",
			    fb->modifier);
		return false;
	}

	mode_cmd.pixel_format = fb->format->format;
	mode_cmd.width = fb->width;
	mode_cmd.height = fb->height;
	mode_cmd.pitches[0] = fb->pitches[0];
	mode_cmd.modifier[0] = fb->modifier;
	mode_cmd.flags = DRM_MODE_FB_MODIFIERS;

	bo = initial_plane_bo(xe, plane_config);
	if (!bo)
		return false;

	if (intel_framebuffer_init(to_intel_framebuffer(fb),
				   &bo->ttm.base, &mode_cmd)) {
		drm_dbg_kms(&xe->drm, "intel fb init failed\n");
		goto err_bo;
	}
	/* Reference handed over to fb */
	xe_bo_put(bo);

	return true;

err_bo:
	xe_bo_unpin_map_no_vm(bo);
	return false;
}

static void
intel_find_initial_plane_obj(struct intel_crtc *crtc,
			     struct intel_initial_plane_config plane_configs[])
{
	struct intel_initial_plane_config *plane_config =
		&plane_configs[crtc->pipe];
	struct intel_plane *plane =
		to_intel_plane(crtc->base.primary);
	struct intel_plane_state *plane_state =
		to_intel_plane_state(plane->base.state);
	struct intel_crtc_state *crtc_state =
		to_intel_crtc_state(crtc->base.state);
	struct drm_framebuffer *fb;
	struct i915_vma *vma;

	/*
	 * TODO:
	 *   Disable planes if get_initial_plane_config() failed.
	 *   Make sure things work if the surface base is not page aligned.
	 */
	if (!plane_config->fb)
		return;

	if (intel_alloc_initial_plane_obj(crtc, plane_config))
		fb = &plane_config->fb->base;
	else if (!intel_reuse_initial_plane_obj(crtc, plane_configs, &fb))
		goto nofb;

	plane_state->uapi.rotation = plane_config->rotation;
	intel_fb_fill_view(to_intel_framebuffer(fb),
			   plane_state->uapi.rotation, &plane_state->view);

	vma = intel_fb_pin_to_ggtt(fb, &plane_state->view.gtt,
				   0, 0, false, &plane_state->flags);
	if (IS_ERR(vma))
		goto nofb;

	plane_state->ggtt_vma = vma;
	plane_state->uapi.src_x = 0;
	plane_state->uapi.src_y = 0;
	plane_state->uapi.src_w = fb->width << 16;
	plane_state->uapi.src_h = fb->height << 16;

	plane_state->uapi.crtc_x = 0;
	plane_state->uapi.crtc_y = 0;
	plane_state->uapi.crtc_w = fb->width;
	plane_state->uapi.crtc_h = fb->height;

	plane_state->uapi.fb = fb;
	drm_framebuffer_get(fb);

	plane_state->uapi.crtc = &crtc->base;
	intel_plane_copy_uapi_to_hw_state(plane_state, plane_state, crtc);

	atomic_or(plane->frontbuffer_bit, &to_intel_frontbuffer(fb)->bits);

	plane_config->vma = vma;

	/*
	 * Flip to the newly created mapping ASAP, so we can re-use the
	 * first part of GGTT for WOPCM, prevent flickering, and prevent
	 * the lookup of sysmem scratch pages.
	 */
	plane->check_plane(crtc_state, plane_state);
	plane->async_flip(NULL, plane, crtc_state, plane_state, true);
	return;

nofb:
	/*
	 * We've failed to reconstruct the BIOS FB.  Current display state
	 * indicates that the primary plane is visible, but has a NULL FB,
	 * which will lead to problems later if we don't fix it up.  The
	 * simplest solution is to just disable the primary plane now and
	 * pretend the BIOS never had it enabled.
	 */
	intel_plane_disable_noatomic(crtc, plane);
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
}

void intel_initial_plane_config(struct drm_i915_private *i915)
{
	struct intel_initial_plane_config plane_configs[I915_MAX_PIPES] = {};
	struct intel_crtc *crtc;

	for_each_intel_crtc(&i915->drm, crtc) {
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
		i915->display.funcs.display->get_initial_plane_config(crtc, plane_config);

		/*
		 * If the fb is shared between multiple heads, we'll
		 * just get the first one.
		 */
		intel_find_initial_plane_obj(crtc, plane_configs);

		if (i915->display.funcs.display->fixup_initial_plane_config(crtc, plane_config))
			intel_crtc_wait_for_next_vblank(crtc);

		plane_config_fini(plane_config);
	}
}
