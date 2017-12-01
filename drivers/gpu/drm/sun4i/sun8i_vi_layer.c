/*
 * Copyright (C) Jernej Skrabec <jernej.skrabec@siol.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>
#include <drm/drmP.h>

#include "sun8i_vi_layer.h"
#include "sun8i_mixer.h"
#include "sun8i_vi_scaler.h"

static void sun8i_vi_layer_enable(struct sun8i_mixer *mixer, int channel,
				  int overlay, bool enable)
{
	u32 val;

	DRM_DEBUG_DRIVER("%sabling VI channel %d overlay %d\n",
			 enable ? "En" : "Dis", channel, overlay);

	if (enable)
		val = SUN8I_MIXER_CHAN_VI_LAYER_ATTR_EN;
	else
		val = 0;

	regmap_update_bits(mixer->engine.regs,
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR(channel, overlay),
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR_EN, val);

	if (enable)
		val = SUN8I_MIXER_BLEND_PIPE_CTL_EN(channel);
	else
		val = 0;

	regmap_update_bits(mixer->engine.regs,
			   SUN8I_MIXER_BLEND_PIPE_CTL,
			   SUN8I_MIXER_BLEND_PIPE_CTL_EN(channel), val);
}

static int sun8i_vi_layer_update_coord(struct sun8i_mixer *mixer, int channel,
				       int overlay, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	const struct drm_format_info *format = state->fb->format;
	u32 src_w, src_h, dst_w, dst_h;
	u32 outsize, insize;
	u32 hphase, vphase;

	DRM_DEBUG_DRIVER("Updating VI channel %d overlay %d\n",
			 channel, overlay);

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
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_CHAN_VI_LAYER_SIZE(channel, overlay),
		     insize);
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_CHAN_VI_OVL_SIZE(channel),
		     insize);

	if (insize != outsize || hphase || vphase) {
		u32 hscale, vscale;

		DRM_DEBUG_DRIVER("HW scaling is enabled\n");

		hscale = state->src_w / state->crtc_w;
		vscale = state->src_h / state->crtc_h;

		sun8i_vi_scaler_setup(mixer, channel, src_w, src_h, dst_w,
				      dst_h, hscale, vscale, hphase, vphase,
				      format);
		sun8i_vi_scaler_enable(mixer, channel, true);
	} else {
		DRM_DEBUG_DRIVER("HW scaling is not needed\n");
		sun8i_vi_scaler_enable(mixer, channel, false);
	}

	/* Set base coordinates */
	DRM_DEBUG_DRIVER("Layer destination coordinates X: %d Y: %d\n",
			 state->dst.x1, state->dst.y1);
	DRM_DEBUG_DRIVER("Layer destination size W: %d H: %d\n", dst_w, dst_h);
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_BLEND_ATTR_COORD(channel),
		     SUN8I_MIXER_COORD(state->dst.x1, state->dst.y1));
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_BLEND_ATTR_INSIZE(channel),
		     outsize);

	return 0;
}

static int sun8i_vi_layer_update_formats(struct sun8i_mixer *mixer, int channel,
					 int overlay, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	const struct de2_fmt_info *fmt_info;
	u32 val;

	fmt_info = sun8i_mixer_format_info(state->fb->format->format);
	if (!fmt_info || !fmt_info->rgb) {
		DRM_DEBUG_DRIVER("Invalid format\n");
		return -EINVAL;
	}

	val = fmt_info->de2_fmt << SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_OFFSET;
	regmap_update_bits(mixer->engine.regs,
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR(channel, overlay),
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_MASK |
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR_RGB_MODE,
			   val | SUN8I_MIXER_CHAN_VI_LAYER_ATTR_RGB_MODE);

	return 0;
}

static int sun8i_vi_layer_update_buffer(struct sun8i_mixer *mixer, int channel,
					int overlay, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *gem;
	dma_addr_t paddr;
	int bpp;

	/* Get the physical address of the buffer in memory */
	gem = drm_fb_cma_get_gem_obj(fb, 0);

	DRM_DEBUG_DRIVER("Using GEM @ %pad\n", &gem->paddr);

	/* Compute the start of the displayed memory */
	bpp = fb->format->cpp[0];
	paddr = gem->paddr + fb->offsets[0];

	/* Fixup framebuffer address for src coordinates */
	paddr += (state->src.x1 >> 16) * bpp;
	paddr += (state->src.y1 >> 16) * fb->pitches[0];

	/* Set the line width */
	DRM_DEBUG_DRIVER("Layer line width: %d bytes\n", fb->pitches[0]);
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_CHAN_VI_LAYER_PITCH(channel, overlay, 0),
		     fb->pitches[0]);

	DRM_DEBUG_DRIVER("Setting buffer address to %pad\n", &paddr);

	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_CHAN_VI_LAYER_TOP_LADDR(channel, overlay, 0),
		     lower_32_bits(paddr));

	return 0;
}

static int sun8i_vi_layer_atomic_check(struct drm_plane *plane,
				       struct drm_plane_state *state)
{
	struct sun8i_vi_layer *layer = plane_to_sun8i_vi_layer(plane);
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *crtc_state;
	int min_scale, max_scale;
	struct drm_rect clip;

	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	clip.x1 = 0;
	clip.y1 = 0;
	clip.x2 = crtc_state->adjusted_mode.hdisplay;
	clip.y2 = crtc_state->adjusted_mode.vdisplay;

	if (layer->mixer->cfg->scaler_mask & BIT(layer->channel)) {
		min_scale = SUN8I_VI_SCALER_SCALE_MIN;
		max_scale = SUN8I_VI_SCALER_SCALE_MAX;
	}

	return drm_atomic_helper_check_plane_state(state, crtc_state, &clip,
						   min_scale, max_scale,
						   true, true);
}

static void sun8i_vi_layer_atomic_disable(struct drm_plane *plane,
					  struct drm_plane_state *old_state)
{
	struct sun8i_vi_layer *layer = plane_to_sun8i_vi_layer(plane);
	struct sun8i_mixer *mixer = layer->mixer;

	sun8i_vi_layer_enable(mixer, layer->channel, layer->overlay, false);
}

static void sun8i_vi_layer_atomic_update(struct drm_plane *plane,
					 struct drm_plane_state *old_state)
{
	struct sun8i_vi_layer *layer = plane_to_sun8i_vi_layer(plane);
	struct sun8i_mixer *mixer = layer->mixer;

	if (!plane->state->visible) {
		sun8i_vi_layer_enable(mixer, layer->channel,
				      layer->overlay, false);
		return;
	}

	sun8i_vi_layer_update_coord(mixer, layer->channel,
				    layer->overlay, plane);
	sun8i_vi_layer_update_formats(mixer, layer->channel,
				      layer->overlay, plane);
	sun8i_vi_layer_update_buffer(mixer, layer->channel,
				     layer->overlay, plane);
	sun8i_vi_layer_enable(mixer, layer->channel, layer->overlay, true);
}

static struct drm_plane_helper_funcs sun8i_vi_layer_helper_funcs = {
	.atomic_check	= sun8i_vi_layer_atomic_check,
	.atomic_disable	= sun8i_vi_layer_atomic_disable,
	.atomic_update	= sun8i_vi_layer_atomic_update,
};

static const struct drm_plane_funcs sun8i_vi_layer_funcs = {
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.destroy		= drm_plane_cleanup,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.reset			= drm_atomic_helper_plane_reset,
	.update_plane		= drm_atomic_helper_update_plane,
};

/*
 * While all RGB formats are supported, VI planes don't support
 * alpha blending, so there is no point having formats with alpha
 * channel if their opaque analog exist.
 */
static const u32 sun8i_vi_layer_formats[] = {
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_ABGR4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_BGRA4444,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB8888,
};

struct sun8i_vi_layer *sun8i_vi_layer_init_one(struct drm_device *drm,
					       struct sun8i_mixer *mixer,
					       int index)
{
	struct sun8i_vi_layer *layer;
	int ret;

	layer = devm_kzalloc(drm->dev, sizeof(*layer), GFP_KERNEL);
	if (!layer)
		return ERR_PTR(-ENOMEM);

	/* possible crtcs are set later */
	ret = drm_universal_plane_init(drm, &layer->plane, 0,
				       &sun8i_vi_layer_funcs,
				       sun8i_vi_layer_formats,
				       ARRAY_SIZE(sun8i_vi_layer_formats),
				       NULL, DRM_PLANE_TYPE_OVERLAY, NULL);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialize layer\n");
		return ERR_PTR(ret);
	}

	/* fixed zpos for now */
	ret = drm_plane_create_zpos_immutable_property(&layer->plane, index);
	if (ret) {
		dev_err(drm->dev, "Couldn't add zpos property\n");
		return ERR_PTR(ret);
	}

	drm_plane_helper_add(&layer->plane, &sun8i_vi_layer_helper_funcs);
	layer->mixer = mixer;
	layer->channel = index;
	layer->overlay = 0;

	return layer;
}
