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
				  int overlay, bool enable, unsigned int zpos,
				  unsigned int old_zpos)
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

	if (!enable || zpos != old_zpos) {
		regmap_update_bits(mixer->engine.regs,
				   SUN8I_MIXER_BLEND_PIPE_CTL,
				   SUN8I_MIXER_BLEND_PIPE_CTL_EN(old_zpos),
				   0);

		regmap_update_bits(mixer->engine.regs,
				   SUN8I_MIXER_BLEND_ROUTE,
				   SUN8I_MIXER_BLEND_ROUTE_PIPE_MSK(old_zpos),
				   0);
	}

	if (enable) {
		val = SUN8I_MIXER_BLEND_PIPE_CTL_EN(zpos);

		regmap_update_bits(mixer->engine.regs,
				   SUN8I_MIXER_BLEND_PIPE_CTL, val, val);

		val = channel << SUN8I_MIXER_BLEND_ROUTE_PIPE_SHIFT(zpos);

		regmap_update_bits(mixer->engine.regs,
				   SUN8I_MIXER_BLEND_ROUTE,
				   SUN8I_MIXER_BLEND_ROUTE_PIPE_MSK(zpos),
				   val);
	}
}

static int sun8i_vi_layer_update_coord(struct sun8i_mixer *mixer, int channel,
				       int overlay, struct drm_plane *plane,
				       unsigned int zpos)
{
	struct drm_plane_state *state = plane->state;
	const struct drm_format_info *format = state->fb->format;
	u32 src_w, src_h, dst_w, dst_h;
	u32 outsize, insize;
	u32 hphase, vphase;
	bool subsampled;

	DRM_DEBUG_DRIVER("Updating VI channel %d overlay %d\n",
			 channel, overlay);

	src_w = drm_rect_width(&state->src) >> 16;
	src_h = drm_rect_height(&state->src) >> 16;
	dst_w = drm_rect_width(&state->dst);
	dst_h = drm_rect_height(&state->dst);

	hphase = state->src.x1 & 0xffff;
	vphase = state->src.y1 & 0xffff;

	/* make coordinates dividable by subsampling factor */
	if (format->hsub > 1) {
		int mask, remainder;

		mask = format->hsub - 1;
		remainder = (state->src.x1 >> 16) & mask;
		src_w = (src_w + remainder) & ~mask;
		hphase += remainder << 16;
	}

	if (format->vsub > 1) {
		int mask, remainder;

		mask = format->vsub - 1;
		remainder = (state->src.y1 >> 16) & mask;
		src_h = (src_h + remainder) & ~mask;
		vphase += remainder << 16;
	}

	insize = SUN8I_MIXER_SIZE(src_w, src_h);
	outsize = SUN8I_MIXER_SIZE(dst_w, dst_h);

	/* Set height and width */
	DRM_DEBUG_DRIVER("Layer source offset X: %d Y: %d\n",
			 (state->src.x1 >> 16) & ~(format->hsub - 1),
			 (state->src.y1 >> 16) & ~(format->vsub - 1));
	DRM_DEBUG_DRIVER("Layer source size W: %d H: %d\n", src_w, src_h);
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_CHAN_VI_LAYER_SIZE(channel, overlay),
		     insize);
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_CHAN_VI_OVL_SIZE(channel),
		     insize);

	/*
	 * Scaler must be enabled for subsampled formats, so it scales
	 * chroma to same size as luma.
	 */
	subsampled = format->hsub > 1 || format->vsub > 1;

	if (insize != outsize || subsampled || hphase || vphase) {
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
		     SUN8I_MIXER_BLEND_ATTR_COORD(zpos),
		     SUN8I_MIXER_COORD(state->dst.x1, state->dst.y1));
	regmap_write(mixer->engine.regs,
		     SUN8I_MIXER_BLEND_ATTR_INSIZE(zpos),
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
	if (!fmt_info) {
		DRM_DEBUG_DRIVER("Invalid format\n");
		return -EINVAL;
	}

	val = fmt_info->de2_fmt << SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_OFFSET;
	regmap_update_bits(mixer->engine.regs,
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR(channel, overlay),
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR_FBFMT_MASK, val);

	if (fmt_info->csc != SUN8I_CSC_MODE_OFF) {
		sun8i_csc_set_ccsc_coefficients(mixer, channel, fmt_info->csc);
		sun8i_csc_enable_ccsc(mixer, channel, true);
	} else {
		sun8i_csc_enable_ccsc(mixer, channel, false);
	}

	if (fmt_info->rgb)
		val = SUN8I_MIXER_CHAN_VI_LAYER_ATTR_RGB_MODE;
	else
		val = 0;

	regmap_update_bits(mixer->engine.regs,
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR(channel, overlay),
			   SUN8I_MIXER_CHAN_VI_LAYER_ATTR_RGB_MODE, val);

	return 0;
}

static int sun8i_vi_layer_update_buffer(struct sun8i_mixer *mixer, int channel,
					int overlay, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	const struct drm_format_info *format = fb->format;
	struct drm_gem_cma_object *gem;
	u32 dx, dy, src_x, src_y;
	dma_addr_t paddr;
	int i;

	/* Adjust x and y to be dividable by subsampling factor */
	src_x = (state->src.x1 >> 16) & ~(format->hsub - 1);
	src_y = (state->src.y1 >> 16) & ~(format->vsub - 1);

	for (i = 0; i < format->num_planes; i++) {
		/* Get the physical address of the buffer in memory */
		gem = drm_fb_cma_get_gem_obj(fb, i);

		DRM_DEBUG_DRIVER("Using GEM @ %pad\n", &gem->paddr);

		/* Compute the start of the displayed memory */
		paddr = gem->paddr + fb->offsets[i];

		dx = src_x;
		dy = src_y;

		if (i > 0) {
			dx /= format->hsub;
			dy /= format->vsub;
		}

		/* Fixup framebuffer address for src coordinates */
		paddr += dx * format->cpp[i];
		paddr += dy * fb->pitches[i];

		/* Set the line width */
		DRM_DEBUG_DRIVER("Layer %d. line width: %d bytes\n",
				 i + 1, fb->pitches[i]);
		regmap_write(mixer->engine.regs,
			     SUN8I_MIXER_CHAN_VI_LAYER_PITCH(channel,
							     overlay, i),
	       fb->pitches[i]);

		DRM_DEBUG_DRIVER("Setting %d. buffer address to %pad\n",
				 i + 1, &paddr);

		regmap_write(mixer->engine.regs,
			     SUN8I_MIXER_CHAN_VI_LAYER_TOP_LADDR(channel,
								 overlay, i),
	       lower_32_bits(paddr));
	}

	return 0;
}

static int sun8i_vi_layer_atomic_check(struct drm_plane *plane,
				       struct drm_plane_state *state)
{
	struct sun8i_vi_layer *layer = plane_to_sun8i_vi_layer(plane);
	struct drm_crtc *crtc = state->crtc;
	struct drm_crtc_state *crtc_state;
	int min_scale, max_scale;

	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state->state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	min_scale = DRM_PLANE_HELPER_NO_SCALING;
	max_scale = DRM_PLANE_HELPER_NO_SCALING;

	if (layer->mixer->cfg->scaler_mask & BIT(layer->channel)) {
		min_scale = SUN8I_VI_SCALER_SCALE_MIN;
		max_scale = SUN8I_VI_SCALER_SCALE_MAX;
	}

	return drm_atomic_helper_check_plane_state(state, crtc_state,
						   min_scale, max_scale,
						   true, true);
}

static void sun8i_vi_layer_atomic_disable(struct drm_plane *plane,
					  struct drm_plane_state *old_state)
{
	struct sun8i_vi_layer *layer = plane_to_sun8i_vi_layer(plane);
	unsigned int old_zpos = old_state->normalized_zpos;
	struct sun8i_mixer *mixer = layer->mixer;

	sun8i_vi_layer_enable(mixer, layer->channel, layer->overlay, false, 0,
			      old_zpos);
}

static void sun8i_vi_layer_atomic_update(struct drm_plane *plane,
					 struct drm_plane_state *old_state)
{
	struct sun8i_vi_layer *layer = plane_to_sun8i_vi_layer(plane);
	unsigned int zpos = plane->state->normalized_zpos;
	unsigned int old_zpos = old_state->normalized_zpos;
	struct sun8i_mixer *mixer = layer->mixer;

	if (!plane->state->visible) {
		sun8i_vi_layer_enable(mixer, layer->channel,
				      layer->overlay, false, 0, old_zpos);
		return;
	}

	sun8i_vi_layer_update_coord(mixer, layer->channel,
				    layer->overlay, plane, zpos);
	sun8i_vi_layer_update_formats(mixer, layer->channel,
				      layer->overlay, plane);
	sun8i_vi_layer_update_buffer(mixer, layer->channel,
				     layer->overlay, plane);
	sun8i_vi_layer_enable(mixer, layer->channel, layer->overlay,
			      true, zpos, old_zpos);
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
 * While DE2 VI layer supports same RGB formats as UI layer, alpha
 * channel is ignored. This structure lists all unique variants
 * where alpha channel is replaced with "don't care" (X) channel.
 */
static const u32 sun8i_vi_layer_formats[] = {
	DRM_FORMAT_BGR565,
	DRM_FORMAT_BGR888,
	DRM_FORMAT_BGRX4444,
	DRM_FORMAT_BGRX5551,
	DRM_FORMAT_BGRX8888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_RGBX5551,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_XBGR4444,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_XRGB8888,

	DRM_FORMAT_NV16,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV61,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_YUV411,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV444,
	DRM_FORMAT_YVU411,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_YVU422,
	DRM_FORMAT_YVU444,
};

struct sun8i_vi_layer *sun8i_vi_layer_init_one(struct drm_device *drm,
					       struct sun8i_mixer *mixer,
					       int index)
{
	struct sun8i_vi_layer *layer;
	unsigned int plane_cnt;
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

	plane_cnt = mixer->cfg->ui_num + mixer->cfg->vi_num;

	ret = drm_plane_create_zpos_property(&layer->plane, index,
					     0, plane_cnt - 1);
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
