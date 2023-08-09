// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2015 MediaTek Inc.
 * Author: CK Hu <ck.hu@mediatek.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_blend.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <linux/align.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_drm_gem.h"
#include "mtk_drm_plane.h"

static const u64 modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
				AFBC_FORMAT_MOD_SPLIT |
				AFBC_FORMAT_MOD_SPARSE),
	DRM_FORMAT_MOD_INVALID,
};

static void mtk_plane_reset(struct drm_plane *plane)
{
	struct mtk_plane_state *state;

	if (plane->state) {
		__drm_atomic_helper_plane_destroy_state(plane->state);

		state = to_mtk_plane_state(plane->state);
		memset(state, 0, sizeof(*state));
	} else {
		state = kzalloc(sizeof(*state), GFP_KERNEL);
		if (!state)
			return;
	}

	__drm_atomic_helper_plane_reset(plane, &state->base);

	state->base.plane = plane;
	state->pending.format = DRM_FORMAT_RGB565;
	state->pending.modifier = DRM_FORMAT_MOD_LINEAR;
}

static struct drm_plane_state *mtk_plane_duplicate_state(struct drm_plane *plane)
{
	struct mtk_plane_state *old_state = to_mtk_plane_state(plane->state);
	struct mtk_plane_state *state;

	state = kmalloc(sizeof(*state), GFP_KERNEL);
	if (!state)
		return NULL;

	__drm_atomic_helper_plane_duplicate_state(plane, &state->base);

	WARN_ON(state->base.plane != plane);

	state->pending = old_state->pending;

	return &state->base;
}

static bool mtk_plane_format_mod_supported(struct drm_plane *plane,
					   uint32_t format,
					   uint64_t modifier)
{
	if (modifier == DRM_FORMAT_MOD_LINEAR)
		return true;

	if (modifier != DRM_FORMAT_MOD_ARM_AFBC(
				AFBC_FORMAT_MOD_BLOCK_SIZE_32x8 |
				AFBC_FORMAT_MOD_SPLIT |
				AFBC_FORMAT_MOD_SPARSE))
		return false;

	if (format != DRM_FORMAT_XRGB8888 &&
	    format != DRM_FORMAT_ARGB8888 &&
	    format != DRM_FORMAT_BGRX8888 &&
	    format != DRM_FORMAT_BGRA8888 &&
	    format != DRM_FORMAT_ABGR8888 &&
	    format != DRM_FORMAT_XBGR8888 &&
	    format != DRM_FORMAT_RGB888 &&
	    format != DRM_FORMAT_BGR888)
		return false;

	return true;
}

static void mtk_drm_plane_destroy_state(struct drm_plane *plane,
					struct drm_plane_state *state)
{
	__drm_atomic_helper_plane_destroy_state(state);
	kfree(to_mtk_plane_state(state));
}

static int mtk_plane_atomic_async_check(struct drm_plane *plane,
					struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_crtc_state *crtc_state;
	int ret;

	if (plane != new_plane_state->crtc->cursor)
		return -EINVAL;

	if (!plane->state)
		return -EINVAL;

	if (!plane->state->fb)
		return -EINVAL;

	ret = mtk_drm_crtc_plane_check(new_plane_state->crtc, plane,
				       to_mtk_plane_state(new_plane_state));
	if (ret)
		return ret;

	crtc_state = drm_atomic_get_existing_crtc_state(state, new_plane_state->crtc);

	return drm_atomic_helper_check_plane_state(plane->state, crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, true);
}

static void mtk_plane_update_new_state(struct drm_plane_state *new_state,
				       struct mtk_plane_state *mtk_plane_state)
{
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_gem_object *gem;
	struct mtk_drm_gem_obj *mtk_gem;
	unsigned int pitch, format;
	u64 modifier;
	dma_addr_t addr;
	dma_addr_t hdr_addr = 0;
	unsigned int hdr_pitch = 0;
	int offset;

	gem = fb->obj[0];
	mtk_gem = to_mtk_gem_obj(gem);
	addr = mtk_gem->dma_addr;
	pitch = fb->pitches[0];
	format = fb->format->format;
	modifier = fb->modifier;

	if (modifier == DRM_FORMAT_MOD_LINEAR) {
		/*
		 * Using dma_addr_t variable to calculate with multiplier of different types,
		 * for example: addr += (new_state->src.x1 >> 16) * fb->format->cpp[0];
		 * may cause coverity issue with unintentional overflow.
		 */
		offset = (new_state->src.x1 >> 16) * fb->format->cpp[0];
		addr += offset;
		offset = (new_state->src.y1 >> 16) * pitch;
		addr += offset;
	} else {
		int width_in_blocks = ALIGN(fb->width, AFBC_DATA_BLOCK_WIDTH)
				      / AFBC_DATA_BLOCK_WIDTH;
		int height_in_blocks = ALIGN(fb->height, AFBC_DATA_BLOCK_HEIGHT)
				       / AFBC_DATA_BLOCK_HEIGHT;
		int x_offset_in_blocks = (new_state->src.x1 >> 16) / AFBC_DATA_BLOCK_WIDTH;
		int y_offset_in_blocks = (new_state->src.y1 >> 16) / AFBC_DATA_BLOCK_HEIGHT;
		int hdr_size, hdr_offset;

		hdr_pitch = width_in_blocks * AFBC_HEADER_BLOCK_SIZE;
		pitch = width_in_blocks * AFBC_DATA_BLOCK_WIDTH *
			AFBC_DATA_BLOCK_HEIGHT * fb->format->cpp[0];

		hdr_size = ALIGN(hdr_pitch * height_in_blocks, AFBC_HEADER_ALIGNMENT);
		hdr_offset = hdr_pitch * y_offset_in_blocks +
			AFBC_HEADER_BLOCK_SIZE * x_offset_in_blocks;

		/*
		 * Using dma_addr_t variable to calculate with multiplier of different types,
		 * for example: addr += hdr_pitch * y_offset_in_blocks;
		 * may cause coverity issue with unintentional overflow.
		 */
		hdr_addr = addr + hdr_offset;

		/* The data plane is offset by 1 additional block. */
		offset = pitch * y_offset_in_blocks +
			 AFBC_DATA_BLOCK_WIDTH * AFBC_DATA_BLOCK_HEIGHT *
			 fb->format->cpp[0] * (x_offset_in_blocks + 1);

		/*
		 * Using dma_addr_t variable to calculate with multiplier of different types,
		 * for example: addr += pitch * y_offset_in_blocks;
		 * may cause coverity issue with unintentional overflow.
		 */
		addr = addr + hdr_size + offset;
	}

	mtk_plane_state->pending.enable = true;
	mtk_plane_state->pending.pitch = pitch;
	mtk_plane_state->pending.hdr_pitch = hdr_pitch;
	mtk_plane_state->pending.format = format;
	mtk_plane_state->pending.modifier = modifier;
	mtk_plane_state->pending.addr = addr;
	mtk_plane_state->pending.hdr_addr = hdr_addr;
	mtk_plane_state->pending.x = new_state->dst.x1;
	mtk_plane_state->pending.y = new_state->dst.y1;
	mtk_plane_state->pending.width = drm_rect_width(&new_state->dst);
	mtk_plane_state->pending.height = drm_rect_height(&new_state->dst);
	mtk_plane_state->pending.rotation = new_state->rotation;
	mtk_plane_state->pending.color_encoding = new_state->color_encoding;
}

static void mtk_plane_atomic_async_update(struct drm_plane *plane,
					  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct mtk_plane_state *new_plane_state = to_mtk_plane_state(plane->state);

	plane->state->crtc_x = new_state->crtc_x;
	plane->state->crtc_y = new_state->crtc_y;
	plane->state->crtc_h = new_state->crtc_h;
	plane->state->crtc_w = new_state->crtc_w;
	plane->state->src_x = new_state->src_x;
	plane->state->src_y = new_state->src_y;
	plane->state->src_h = new_state->src_h;
	plane->state->src_w = new_state->src_w;

	mtk_plane_update_new_state(new_state, new_plane_state);
	swap(plane->state->fb, new_state->fb);
	wmb(); /* Make sure the above parameters are set before update */
	new_plane_state->pending.async_dirty = true;
	mtk_drm_crtc_async_update(new_state->crtc, plane, state);
}

static const struct drm_plane_funcs mtk_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = mtk_plane_reset,
	.atomic_duplicate_state = mtk_plane_duplicate_state,
	.atomic_destroy_state = mtk_drm_plane_destroy_state,
	.format_mod_supported = mtk_plane_format_mod_supported,
};

static int mtk_plane_atomic_check(struct drm_plane *plane,
				  struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct drm_framebuffer *fb = new_plane_state->fb;
	struct drm_crtc_state *crtc_state;
	int ret;

	if (!fb)
		return 0;

	if (WARN_ON(!new_plane_state->crtc))
		return 0;

	ret = mtk_drm_crtc_plane_check(new_plane_state->crtc, plane,
				       to_mtk_plane_state(new_plane_state));
	if (ret)
		return ret;

	crtc_state = drm_atomic_get_crtc_state(state,
					       new_plane_state->crtc);
	if (IS_ERR(crtc_state))
		return PTR_ERR(crtc_state);

	return drm_atomic_helper_check_plane_state(new_plane_state,
						   crtc_state,
						   DRM_PLANE_NO_SCALING,
						   DRM_PLANE_NO_SCALING,
						   true, true);
}

static void mtk_plane_atomic_disable(struct drm_plane *plane,
				     struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct mtk_plane_state *mtk_plane_state = to_mtk_plane_state(new_state);
	mtk_plane_state->pending.enable = false;
	wmb(); /* Make sure the above parameter is set before update */
	mtk_plane_state->pending.dirty = true;
}

static void mtk_plane_atomic_update(struct drm_plane *plane,
				    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct mtk_plane_state *mtk_plane_state = to_mtk_plane_state(new_state);

	if (!new_state->crtc || WARN_ON(!new_state->fb))
		return;

	if (!new_state->visible) {
		mtk_plane_atomic_disable(plane, state);
		return;
	}

	mtk_plane_update_new_state(new_state, mtk_plane_state);
	wmb(); /* Make sure the above parameters are set before update */
	mtk_plane_state->pending.dirty = true;
}

static const struct drm_plane_helper_funcs mtk_plane_helper_funcs = {
	.atomic_check = mtk_plane_atomic_check,
	.atomic_update = mtk_plane_atomic_update,
	.atomic_disable = mtk_plane_atomic_disable,
	.atomic_async_update = mtk_plane_atomic_async_update,
	.atomic_async_check = mtk_plane_atomic_async_check,
};

int mtk_plane_init(struct drm_device *dev, struct drm_plane *plane,
		   unsigned long possible_crtcs, enum drm_plane_type type,
		   unsigned int supported_rotations, const u32 *formats,
		   size_t num_formats)
{
	int err;

	if (!formats || !num_formats) {
		DRM_ERROR("no formats for plane\n");
		return -EINVAL;
	}

	err = drm_universal_plane_init(dev, plane, possible_crtcs,
				       &mtk_plane_funcs, formats,
				       num_formats, modifiers, type, NULL);
	if (err) {
		DRM_ERROR("failed to initialize plane\n");
		return err;
	}

	if (supported_rotations & ~DRM_MODE_ROTATE_0) {
		err = drm_plane_create_rotation_property(plane,
							 DRM_MODE_ROTATE_0,
							 supported_rotations);
		if (err)
			DRM_INFO("Create rotation property failed\n");
	}

	drm_plane_helper_add(plane, &mtk_plane_helper_funcs);

	return 0;
}
