// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) Icenowy Zheng <icenowy@aosc.io>
 *
 * Based on sun4i_layer.h, which is:
 *   Copyright (C) 2015 Free Electrons
 *   Copyright (C) 2015 NextThing Co
 *
 *   Maxime Ripard <maxime.ripard@free-electrons.com>
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "sun8i_mixer.h"
#include "sun8i_ui_layer.h"
#include "sun8i_ui_scaler.h"
#include "sun8i_vi_scaler.h"

static void sun8i_ui_layer_disable(struct sun8i_layer *layer)
{
	u32 ch_base = sun8i_channel_base(layer);

	regmap_write(layer->regs,
		     SUN8I_MIXER_CHAN_UI_LAYER_ATTR(ch_base, layer->overlay), 0);
}

static void sun8i_ui_layer_update_attributes(struct sun8i_layer *layer,
					     struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	const struct drm_format_info *fmt;
	u32 val, ch_base, hw_fmt;

	ch_base = sun8i_channel_base(layer);
	fmt = state->fb->format;
	sun8i_mixer_drm_format_to_hw(fmt->format, &hw_fmt);

	val = SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA(state->alpha >> 8);
	val |= (state->alpha == DRM_BLEND_ALPHA_OPAQUE) ?
		SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_MODE_PIXEL :
		SUN8I_MIXER_CHAN_UI_LAYER_ATTR_ALPHA_MODE_COMBINED;
	val |= hw_fmt << SUN8I_MIXER_CHAN_UI_LAYER_ATTR_FBFMT_OFFSET;
	val |= SUN8I_MIXER_CHAN_UI_LAYER_ATTR_EN;

	regmap_write(layer->regs,
		     SUN8I_MIXER_CHAN_UI_LAYER_ATTR(ch_base, layer->overlay), val);
}

static void sun8i_ui_layer_update_coord(struct sun8i_layer *layer,
					struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	u32 src_w, src_h, dst_w, dst_h;
	u32 outsize, insize;
	u32 hphase, vphase;
	u32 ch_base;

	DRM_DEBUG_DRIVER("Updating UI channel %d overlay %d\n",
			 layer->channel, layer->overlay);

	ch_base = sun8i_channel_base(layer);

	src_w = drm_rect_width(&state->src) >> 16;
	src_h = drm_rect_height(&state->src) >> 16;
	dst_w = drm_rect_width(&state->dst);
	dst_h = drm_rect_height(&state->dst);

	hphase = state->src.x1 & 0xffff;
	vphase = state->src.y1 & 0xffff;

	insize = SUN8I_MIXER_SIZE(src_w, src_h);
	outsize = SUN8I_MIXER_SIZE(dst_w, dst_h);

	/* Set height and width */
	DRM_DEBUG_DRIVER("Layer source offset X: %d Y: %d\n",
			 state->src.x1 >> 16, state->src.y1 >> 16);
	DRM_DEBUG_DRIVER("Layer source size W: %d H: %d\n", src_w, src_h);
	regmap_write(layer->regs,
		     SUN8I_MIXER_CHAN_UI_LAYER_SIZE(ch_base, layer->overlay),
		     insize);
	regmap_write(layer->regs,
		     SUN8I_MIXER_CHAN_UI_OVL_SIZE(ch_base),
		     insize);

	if (insize != outsize || hphase || vphase) {
		u32 hscale, vscale;

		DRM_DEBUG_DRIVER("HW scaling is enabled\n");

		hscale = state->src_w / state->crtc_w;
		vscale = state->src_h / state->crtc_h;

		if (layer->cfg->de_type == SUN8I_MIXER_DE33) {
			sun8i_vi_scaler_setup(layer, src_w, src_h, dst_w, dst_h,
					      hscale, vscale, hphase, vphase,
					      state->fb->format);
			sun8i_vi_scaler_enable(layer, true);
		} else {
			sun8i_ui_scaler_setup(layer, src_w, src_h, dst_w, dst_h,
					      hscale, vscale, hphase, vphase);
			sun8i_ui_scaler_enable(layer, true);
		}
	} else {
		DRM_DEBUG_DRIVER("HW scaling is not needed\n");
		if (layer->cfg->de_type == SUN8I_MIXER_DE33)
			sun8i_vi_scaler_enable(layer, false);
		else
			sun8i_ui_scaler_enable(layer, false);
	}
}

static void sun8i_ui_layer_update_buffer(struct sun8i_layer *layer,
					 struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_dma_object *gem;
	dma_addr_t dma_addr;
	u32 ch_base;
	int bpp;

	ch_base = sun8i_channel_base(layer);

	/* Get the physical address of the buffer in memory */
	gem = drm_fb_dma_get_gem_obj(fb, 0);

	DRM_DEBUG_DRIVER("Using GEM @ %pad\n", &gem->dma_addr);

	/* Compute the start of the displayed memory */
	bpp = fb->format->cpp[0];
	dma_addr = gem->dma_addr + fb->offsets[0];

	/* Fixup framebuffer address for src coordinates */
	dma_addr += (state->src.x1 >> 16) * bpp;
	dma_addr += (state->src.y1 >> 16) * fb->pitches[0];

	/* Set the line width */
	DRM_DEBUG_DRIVER("Layer line width: %d bytes\n", fb->pitches[0]);
	regmap_write(layer->regs,
		     SUN8I_MIXER_CHAN_UI_LAYER_PITCH(ch_base, layer->overlay),
		     fb->pitches[0]);

	DRM_DEBUG_DRIVER("Setting buffer address to %pad\n", &dma_addr);

	regmap_write(layer->regs,
		     SUN8I_MIXER_CHAN_UI_LAYER_TOP_LADDR(ch_base, layer->overlay),
		     lower_32_bits(dma_addr));
}

static int sun8i_ui_layer_atomic_check(struct drm_plane *plane,
				       struct drm_atomic_state *state)
{
	struct drm_plane_state *new_plane_state = drm_atomic_get_new_plane_state(state,
										 plane);
	struct sun8i_layer *layer = plane_to_sun8i_layer(plane);
	struct drm_crtc *crtc = new_plane_state->crtc;
	struct drm_crtc_state *crtc_state;
	const struct drm_format_info *fmt;
	int min_scale, max_scale, ret;
	u32 hw_fmt;

	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	fmt = new_plane_state->fb->format;
	ret = sun8i_mixer_drm_format_to_hw(fmt->format, &hw_fmt);
	if (ret || fmt->is_yuv) {
		DRM_DEBUG_DRIVER("Invalid plane format\n");
		return -EINVAL;
	}

	min_scale = DRM_PLANE_NO_SCALING;
	max_scale = DRM_PLANE_NO_SCALING;

	if (layer->cfg->scaler_mask & BIT(layer->channel)) {
		min_scale = SUN8I_UI_SCALER_SCALE_MIN;
		max_scale = SUN8I_UI_SCALER_SCALE_MAX;
	}

	return drm_atomic_helper_check_plane_state(new_plane_state,
						   crtc_state,
						   min_scale, max_scale,
						   true, true);
}


static void sun8i_ui_layer_atomic_update(struct drm_plane *plane,
					 struct drm_atomic_state *state)
{
	struct drm_plane_state *new_state = drm_atomic_get_new_plane_state(state,
									   plane);
	struct sun8i_layer *layer = plane_to_sun8i_layer(plane);

	if (!new_state->crtc || !new_state->visible) {
		sun8i_ui_layer_disable(layer);
		return;
	}

	sun8i_ui_layer_update_attributes(layer, plane);
	sun8i_ui_layer_update_coord(layer, plane);
	sun8i_ui_layer_update_buffer(layer, plane);
}

static const struct drm_plane_helper_funcs sun8i_ui_layer_helper_funcs = {
	.atomic_check	= sun8i_ui_layer_atomic_check,
	.atomic_update	= sun8i_ui_layer_atomic_update,
};

static const struct drm_plane_funcs sun8i_ui_layer_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.destroy		= drm_plane_cleanup,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.update_plane		= drm_atomic_helper_update_plane,
};

static const u32 sun8i_ui_layer_formats[] = {
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
};

static const uint64_t sun8i_layer_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
	DRM_FORMAT_MOD_INVALID
};

struct sun8i_layer *sun8i_ui_layer_init_one(struct drm_device *drm,
					    enum drm_plane_type type,
					    struct regmap *regs,
					    int index, int phy_index,
					    int plane_cnt,
					    const struct sun8i_layer_cfg *cfg)
{
	struct sun8i_layer *layer;
	int ret;

	layer = devm_kzalloc(drm->dev, sizeof(*layer), GFP_KERNEL);
	if (!layer)
		return ERR_PTR(-ENOMEM);

	layer->type = SUN8I_LAYER_TYPE_UI;
	layer->index = index;
	layer->channel = phy_index;
	layer->overlay = 0;
	layer->regs = regs;
	layer->cfg = cfg;

	/* possible crtcs are set later */
	ret = drm_universal_plane_init(drm, &layer->plane, 0,
				       &sun8i_ui_layer_funcs,
				       sun8i_ui_layer_formats,
				       ARRAY_SIZE(sun8i_ui_layer_formats),
				       sun8i_layer_modifiers, type, NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialize layer\n");
		return ERR_PTR(ret);
	}

	ret = drm_plane_create_alpha_property(&layer->plane);
	if (ret) {
		dev_err(drm->dev, "Couldn't add alpha property\n");
		return ERR_PTR(ret);
	}

	ret = drm_plane_create_zpos_property(&layer->plane, index,
					     0, plane_cnt - 1);
	if (ret) {
		dev_err(drm->dev, "Couldn't add zpos property\n");
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(&layer->plane, &sun8i_ui_layer_helper_funcs);

	return layer;
}
