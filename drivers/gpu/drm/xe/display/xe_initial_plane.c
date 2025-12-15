// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

/* for ioread64 */
#include <linux/io-64-nonatomic-lo-hi.h>

#include <drm/intel/display_parent_interface.h>

#include "regs/xe_gtt_defs.h"
#include "xe_ggtt.h"
#include "xe_mmio.h"

#include "i915_vma.h"
#include "intel_crtc.h"
#include "intel_display_regs.h"
#include "intel_display_types.h"
#include "intel_fb.h"
#include "intel_fb_pin.h"
#include "xe_bo.h"
#include "xe_vram_types.h"
#include "xe_wa.h"

#include <generated/xe_device_wa_oob.h>

/* Early xe has no irq */
static void xe_initial_plane_vblank_wait(struct drm_crtc *_crtc)
{
	struct intel_crtc *crtc = to_intel_crtc(_crtc);
	struct xe_device *xe = to_xe_device(crtc->base.dev);
	struct xe_reg pipe_frmtmstmp = XE_REG(i915_mmio_reg_offset(PIPE_FRMTMSTMP(crtc->pipe)));
	u32 timestamp;
	int ret;

	timestamp = xe_mmio_read32(xe_root_tile_mmio(xe), pipe_frmtmstmp);

	ret = xe_mmio_wait32_not(xe_root_tile_mmio(xe), pipe_frmtmstmp, ~0U, timestamp, 40000U, &timestamp, false);
	if (ret < 0)
		drm_warn(&xe->drm, "waiting for early vblank failed with %i\n", ret);
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

	flags = XE_BO_FLAG_SCANOUT | XE_BO_FLAG_GGTT;

	base = round_down(plane_config->base, page_size);
	if (IS_DGFX(xe)) {
		u64 pte = xe_ggtt_read_pte(tile0->mem.ggtt, base);

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
		if (phys_base >= xe_vram_region_usable_size(tile0->mem.vram)) {
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

		if (XE_DEVICE_WA(xe, 22019338487_display))
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

	bo = xe_bo_create_pin_map_at_novm(xe, tile0, size, phys_base,
					  ttm_bo_type_kernel, flags, 0, false);
	if (IS_ERR(bo)) {
		drm_dbg(&xe->drm,
			"Failed to create bo phys_base=%pa size %u with flags %x: %li\n",
			&phys_base, size, flags, PTR_ERR(bo));
		return NULL;
	}

	return bo;
}

static struct drm_gem_object *
xe_alloc_initial_plane_obj(struct drm_device *drm,
			   struct intel_initial_plane_config *plane_config)
{
	struct xe_device *xe = to_xe_device(drm);
	struct drm_mode_fb_cmd2 mode_cmd = { 0 };
	struct drm_framebuffer *fb = &plane_config->fb->base;
	struct xe_bo *bo;

	mode_cmd.pixel_format = fb->format->format;
	mode_cmd.width = fb->width;
	mode_cmd.height = fb->height;
	mode_cmd.pitches[0] = fb->pitches[0];
	mode_cmd.modifier[0] = fb->modifier;
	mode_cmd.flags = DRM_MODE_FB_MODIFIERS;

	bo = initial_plane_bo(xe, plane_config);
	if (!bo)
		return NULL;

	if (intel_framebuffer_init(to_intel_framebuffer(fb),
				   &bo->ttm.base, fb->format, &mode_cmd)) {
		drm_dbg_kms(&xe->drm, "intel fb init failed\n");
		goto err_bo;
	}
	/* Reference handed over to fb */
	xe_bo_put(bo);

	return &bo->ttm.base;

err_bo:
	xe_bo_unpin_map_no_vm(bo);
	return NULL;
}

static int
xe_initial_plane_setup(struct drm_plane_state *_plane_state,
		       struct intel_initial_plane_config *plane_config,
		       struct drm_framebuffer *fb,
		       struct i915_vma *_unused)
{
	struct intel_plane_state *plane_state = to_intel_plane_state(_plane_state);
	struct i915_vma *vma;

	vma = intel_fb_pin_to_ggtt(fb, &plane_state->view.gtt,
				   0, 0, 0, false, &plane_state->flags);
	if (IS_ERR(vma))
		return PTR_ERR(vma);

	plane_state->ggtt_vma = vma;

	plane_state->surf = i915_ggtt_offset(plane_state->ggtt_vma);

	plane_config->vma = vma;

	return 0;
}

static void xe_plane_config_fini(struct intel_initial_plane_config *plane_config)
{
}

const struct intel_display_initial_plane_interface xe_display_initial_plane_interface = {
	.vblank_wait = xe_initial_plane_vblank_wait,
	.alloc_obj = xe_alloc_initial_plane_obj,
	.setup = xe_initial_plane_setup,
	.config_fini = xe_plane_config_fini,
};
