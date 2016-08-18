/*
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "atmel_hlcdc_dc.h"

/**
 * Atmel HLCDC Plane state structure.
 *
 * @base: DRM plane state
 * @crtc_x: x position of the plane relative to the CRTC
 * @crtc_y: y position of the plane relative to the CRTC
 * @crtc_w: visible width of the plane
 * @crtc_h: visible height of the plane
 * @src_x: x buffer position
 * @src_y: y buffer position
 * @src_w: buffer width
 * @src_h: buffer height
 * @alpha: alpha blending of the plane
 * @bpp: bytes per pixel deduced from pixel_format
 * @offsets: offsets to apply to the GEM buffers
 * @xstride: value to add to the pixel pointer between each line
 * @pstride: value to add to the pixel pointer between each pixel
 * @nplanes: number of planes (deduced from pixel_format)
 * @prepared: plane update has been prepared
 */
struct atmel_hlcdc_plane_state {
	struct drm_plane_state base;
	int crtc_x;
	int crtc_y;
	unsigned int crtc_w;
	unsigned int crtc_h;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;

	u8 alpha;

	bool disc_updated;

	int disc_x;
	int disc_y;
	int disc_w;
	int disc_h;

	int ahb_id;

	/* These fields are private and should not be touched */
	int bpp[ATMEL_HLCDC_MAX_PLANES];
	unsigned int offsets[ATMEL_HLCDC_MAX_PLANES];
	int xstride[ATMEL_HLCDC_MAX_PLANES];
	int pstride[ATMEL_HLCDC_MAX_PLANES];
	int nplanes;
	bool prepared;
};

static inline struct atmel_hlcdc_plane_state *
drm_plane_state_to_atmel_hlcdc_plane_state(struct drm_plane_state *s)
{
	return container_of(s, struct atmel_hlcdc_plane_state, base);
}

#define SUBPIXEL_MASK			0xffff

static uint32_t rgb_formats[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
};

struct atmel_hlcdc_formats atmel_hlcdc_plane_rgb_formats = {
	.formats = rgb_formats,
	.nformats = ARRAY_SIZE(rgb_formats),
};

static uint32_t rgb_and_yuv_formats[] = {
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_AYUV,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV61,
	DRM_FORMAT_YUV422,
	DRM_FORMAT_YUV420,
};

struct atmel_hlcdc_formats atmel_hlcdc_plane_rgb_and_yuv_formats = {
	.formats = rgb_and_yuv_formats,
	.nformats = ARRAY_SIZE(rgb_and_yuv_formats),
};

static int atmel_hlcdc_format_to_plane_mode(u32 format, u32 *mode)
{
	switch (format) {
	case DRM_FORMAT_XRGB4444:
		*mode = ATMEL_HLCDC_XRGB4444_MODE;
		break;
	case DRM_FORMAT_ARGB4444:
		*mode = ATMEL_HLCDC_ARGB4444_MODE;
		break;
	case DRM_FORMAT_RGBA4444:
		*mode = ATMEL_HLCDC_RGBA4444_MODE;
		break;
	case DRM_FORMAT_RGB565:
		*mode = ATMEL_HLCDC_RGB565_MODE;
		break;
	case DRM_FORMAT_RGB888:
		*mode = ATMEL_HLCDC_RGB888_MODE;
		break;
	case DRM_FORMAT_ARGB1555:
		*mode = ATMEL_HLCDC_ARGB1555_MODE;
		break;
	case DRM_FORMAT_XRGB8888:
		*mode = ATMEL_HLCDC_XRGB8888_MODE;
		break;
	case DRM_FORMAT_ARGB8888:
		*mode = ATMEL_HLCDC_ARGB8888_MODE;
		break;
	case DRM_FORMAT_RGBA8888:
		*mode = ATMEL_HLCDC_RGBA8888_MODE;
		break;
	case DRM_FORMAT_AYUV:
		*mode = ATMEL_HLCDC_AYUV_MODE;
		break;
	case DRM_FORMAT_YUYV:
		*mode = ATMEL_HLCDC_YUYV_MODE;
		break;
	case DRM_FORMAT_UYVY:
		*mode = ATMEL_HLCDC_UYVY_MODE;
		break;
	case DRM_FORMAT_YVYU:
		*mode = ATMEL_HLCDC_YVYU_MODE;
		break;
	case DRM_FORMAT_VYUY:
		*mode = ATMEL_HLCDC_VYUY_MODE;
		break;
	case DRM_FORMAT_NV21:
		*mode = ATMEL_HLCDC_NV21_MODE;
		break;
	case DRM_FORMAT_NV61:
		*mode = ATMEL_HLCDC_NV61_MODE;
		break;
	case DRM_FORMAT_YUV420:
		*mode = ATMEL_HLCDC_YUV420_MODE;
		break;
	case DRM_FORMAT_YUV422:
		*mode = ATMEL_HLCDC_YUV422_MODE;
		break;
	default:
		return -ENOTSUPP;
	}

	return 0;
}

static bool atmel_hlcdc_format_embeds_alpha(u32 format)
{
	int i;

	for (i = 0; i < sizeof(format); i++) {
		char tmp = (format >> (8 * i)) & 0xff;

		if (tmp == 'A')
			return true;
	}

	return false;
}

static u32 heo_downscaling_xcoef[] = {
	0x11343311,
	0x000000f7,
	0x1635300c,
	0x000000f9,
	0x1b362c08,
	0x000000fb,
	0x1f372804,
	0x000000fe,
	0x24382400,
	0x00000000,
	0x28371ffe,
	0x00000004,
	0x2c361bfb,
	0x00000008,
	0x303516f9,
	0x0000000c,
};

static u32 heo_downscaling_ycoef[] = {
	0x00123737,
	0x00173732,
	0x001b382d,
	0x001f3928,
	0x00243824,
	0x0028391f,
	0x002d381b,
	0x00323717,
};

static u32 heo_upscaling_xcoef[] = {
	0xf74949f7,
	0x00000000,
	0xf55f33fb,
	0x000000fe,
	0xf5701efe,
	0x000000ff,
	0xf87c0dff,
	0x00000000,
	0x00800000,
	0x00000000,
	0x0d7cf800,
	0x000000ff,
	0x1e70f5ff,
	0x000000fe,
	0x335ff5fe,
	0x000000fb,
};

static u32 heo_upscaling_ycoef[] = {
	0x00004040,
	0x00075920,
	0x00056f0c,
	0x00027b03,
	0x00008000,
	0x00037b02,
	0x000c6f05,
	0x00205907,
};

static void
atmel_hlcdc_plane_update_pos_and_size(struct atmel_hlcdc_plane *plane,
				      struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_cfg_layout *layout =
						&plane->layer.desc->layout;

	if (layout->size)
		atmel_hlcdc_layer_update_cfg(&plane->layer,
					     layout->size,
					     0xffffffff,
					     (state->crtc_w - 1) |
					     ((state->crtc_h - 1) << 16));

	if (layout->memsize)
		atmel_hlcdc_layer_update_cfg(&plane->layer,
					     layout->memsize,
					     0xffffffff,
					     (state->src_w - 1) |
					     ((state->src_h - 1) << 16));

	if (layout->pos)
		atmel_hlcdc_layer_update_cfg(&plane->layer,
					     layout->pos,
					     0xffffffff,
					     state->crtc_x |
					     (state->crtc_y  << 16));

	/* TODO: rework the rescaling part */
	if (state->crtc_w != state->src_w || state->crtc_h != state->src_h) {
		u32 factor_reg = 0;

		if (state->crtc_w != state->src_w) {
			int i;
			u32 factor;
			u32 *coeff_tab = heo_upscaling_xcoef;
			u32 max_memsize;

			if (state->crtc_w < state->src_w)
				coeff_tab = heo_downscaling_xcoef;
			for (i = 0; i < ARRAY_SIZE(heo_upscaling_xcoef); i++)
				atmel_hlcdc_layer_update_cfg(&plane->layer,
							     17 + i,
							     0xffffffff,
							     coeff_tab[i]);
			factor = ((8 * 256 * state->src_w) - (256 * 4)) /
				 state->crtc_w;
			factor++;
			max_memsize = ((factor * state->crtc_w) + (256 * 4)) /
				      2048;
			if (max_memsize > state->src_w)
				factor--;
			factor_reg |= factor | 0x80000000;
		}

		if (state->crtc_h != state->src_h) {
			int i;
			u32 factor;
			u32 *coeff_tab = heo_upscaling_ycoef;
			u32 max_memsize;

			if (state->crtc_w < state->src_w)
				coeff_tab = heo_downscaling_ycoef;
			for (i = 0; i < ARRAY_SIZE(heo_upscaling_ycoef); i++)
				atmel_hlcdc_layer_update_cfg(&plane->layer,
							     33 + i,
							     0xffffffff,
							     coeff_tab[i]);
			factor = ((8 * 256 * state->src_w) - (256 * 4)) /
				 state->crtc_w;
			factor++;
			max_memsize = ((factor * state->crtc_w) + (256 * 4)) /
				      2048;
			if (max_memsize > state->src_w)
				factor--;
			factor_reg |= (factor << 16) | 0x80000000;
		}

		atmel_hlcdc_layer_update_cfg(&plane->layer, 13, 0xffffffff,
					     factor_reg);
	} else {
		atmel_hlcdc_layer_update_cfg(&plane->layer, 13, 0xffffffff, 0);
	}
}

static void
atmel_hlcdc_plane_update_general_settings(struct atmel_hlcdc_plane *plane,
					struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_cfg_layout *layout =
						&plane->layer.desc->layout;
	unsigned int cfg = ATMEL_HLCDC_LAYER_DMA;

	if (plane->base.type != DRM_PLANE_TYPE_PRIMARY) {
		cfg |= ATMEL_HLCDC_LAYER_OVR | ATMEL_HLCDC_LAYER_ITER2BL |
		       ATMEL_HLCDC_LAYER_ITER;

		if (atmel_hlcdc_format_embeds_alpha(state->base.fb->pixel_format))
			cfg |= ATMEL_HLCDC_LAYER_LAEN;
		else
			cfg |= ATMEL_HLCDC_LAYER_GAEN |
			       ATMEL_HLCDC_LAYER_GA(state->alpha);
	}

	atmel_hlcdc_layer_update_cfg(&plane->layer,
				     ATMEL_HLCDC_LAYER_DMA_CFG_ID,
				     ATMEL_HLCDC_LAYER_DMA_BLEN_MASK |
				     ATMEL_HLCDC_LAYER_DMA_SIF,
				     ATMEL_HLCDC_LAYER_DMA_BLEN_INCR16 |
				     state->ahb_id);

	atmel_hlcdc_layer_update_cfg(&plane->layer, layout->general_config,
				     ATMEL_HLCDC_LAYER_ITER2BL |
				     ATMEL_HLCDC_LAYER_ITER |
				     ATMEL_HLCDC_LAYER_GAEN |
				     ATMEL_HLCDC_LAYER_GA_MASK |
				     ATMEL_HLCDC_LAYER_LAEN |
				     ATMEL_HLCDC_LAYER_OVR |
				     ATMEL_HLCDC_LAYER_DMA, cfg);
}

static void atmel_hlcdc_plane_update_format(struct atmel_hlcdc_plane *plane,
					struct atmel_hlcdc_plane_state *state)
{
	u32 cfg;
	int ret;

	ret = atmel_hlcdc_format_to_plane_mode(state->base.fb->pixel_format,
					       &cfg);
	if (ret)
		return;

	if ((state->base.fb->pixel_format == DRM_FORMAT_YUV422 ||
	     state->base.fb->pixel_format == DRM_FORMAT_NV61) &&
	    (state->base.rotation & (DRM_ROTATE_90 | DRM_ROTATE_270)))
		cfg |= ATMEL_HLCDC_YUV422ROT;

	atmel_hlcdc_layer_update_cfg(&plane->layer,
				     ATMEL_HLCDC_LAYER_FORMAT_CFG_ID,
				     0xffffffff,
				     cfg);

	/*
	 * Rotation optimization is not working on RGB888 (rotation is still
	 * working but without any optimization).
	 */
	if (state->base.fb->pixel_format == DRM_FORMAT_RGB888)
		cfg = ATMEL_HLCDC_LAYER_DMA_ROTDIS;
	else
		cfg = 0;

	atmel_hlcdc_layer_update_cfg(&plane->layer,
				     ATMEL_HLCDC_LAYER_DMA_CFG_ID,
				     ATMEL_HLCDC_LAYER_DMA_ROTDIS, cfg);
}

static void atmel_hlcdc_plane_update_buffers(struct atmel_hlcdc_plane *plane,
					struct atmel_hlcdc_plane_state *state)
{
	struct atmel_hlcdc_layer *layer = &plane->layer;
	const struct atmel_hlcdc_layer_cfg_layout *layout =
							&layer->desc->layout;
	int i;

	atmel_hlcdc_layer_update_set_fb(&plane->layer, state->base.fb,
					state->offsets);

	for (i = 0; i < state->nplanes; i++) {
		if (layout->xstride[i]) {
			atmel_hlcdc_layer_update_cfg(&plane->layer,
						layout->xstride[i],
						0xffffffff,
						state->xstride[i]);
		}

		if (layout->pstride[i]) {
			atmel_hlcdc_layer_update_cfg(&plane->layer,
						layout->pstride[i],
						0xffffffff,
						state->pstride[i]);
		}
	}
}

int atmel_hlcdc_plane_prepare_ahb_routing(struct drm_crtc_state *c_state)
{
	unsigned int ahb_load[2] = { };
	struct drm_plane *plane;

	drm_atomic_crtc_state_for_each_plane(plane, c_state) {
		struct atmel_hlcdc_plane_state *plane_state;
		struct drm_plane_state *plane_s;
		unsigned int pixels, load = 0;
		int i;

		plane_s = drm_atomic_get_plane_state(c_state->state, plane);
		if (IS_ERR(plane_s))
			return PTR_ERR(plane_s);

		plane_state =
			drm_plane_state_to_atmel_hlcdc_plane_state(plane_s);

		pixels = (plane_state->src_w * plane_state->src_h) -
			 (plane_state->disc_w * plane_state->disc_h);

		for (i = 0; i < plane_state->nplanes; i++)
			load += pixels * plane_state->bpp[i];

		if (ahb_load[0] <= ahb_load[1])
			plane_state->ahb_id = 0;
		else
			plane_state->ahb_id = 1;

		ahb_load[plane_state->ahb_id] += load;
	}

	return 0;
}

int
atmel_hlcdc_plane_prepare_disc_area(struct drm_crtc_state *c_state)
{
	int disc_x = 0, disc_y = 0, disc_w = 0, disc_h = 0;
	const struct atmel_hlcdc_layer_cfg_layout *layout;
	struct atmel_hlcdc_plane_state *primary_state;
	struct drm_plane_state *primary_s;
	struct atmel_hlcdc_plane *primary;
	struct drm_plane *ovl;

	primary = drm_plane_to_atmel_hlcdc_plane(c_state->crtc->primary);
	layout = &primary->layer.desc->layout;
	if (!layout->disc_pos || !layout->disc_size)
		return 0;

	primary_s = drm_atomic_get_plane_state(c_state->state,
					       &primary->base);
	if (IS_ERR(primary_s))
		return PTR_ERR(primary_s);

	primary_state = drm_plane_state_to_atmel_hlcdc_plane_state(primary_s);

	drm_atomic_crtc_state_for_each_plane(ovl, c_state) {
		struct atmel_hlcdc_plane_state *ovl_state;
		struct drm_plane_state *ovl_s;

		if (ovl == c_state->crtc->primary)
			continue;

		ovl_s = drm_atomic_get_plane_state(c_state->state, ovl);
		if (IS_ERR(ovl_s))
			return PTR_ERR(ovl_s);

		ovl_state = drm_plane_state_to_atmel_hlcdc_plane_state(ovl_s);

		if (!ovl_s->fb ||
		    atmel_hlcdc_format_embeds_alpha(ovl_s->fb->pixel_format) ||
		    ovl_state->alpha != 255)
			continue;

		/* TODO: implement a smarter hidden area detection */
		if (ovl_state->crtc_h * ovl_state->crtc_w < disc_h * disc_w)
			continue;

		disc_x = ovl_state->crtc_x;
		disc_y = ovl_state->crtc_y;
		disc_h = ovl_state->crtc_h;
		disc_w = ovl_state->crtc_w;
	}

	if (disc_x == primary_state->disc_x &&
	    disc_y == primary_state->disc_y &&
	    disc_w == primary_state->disc_w &&
	    disc_h == primary_state->disc_h)
		return 0;


	primary_state->disc_x = disc_x;
	primary_state->disc_y = disc_y;
	primary_state->disc_w = disc_w;
	primary_state->disc_h = disc_h;
	primary_state->disc_updated = true;

	return 0;
}

static void
atmel_hlcdc_plane_update_disc_area(struct atmel_hlcdc_plane *plane,
				   struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_cfg_layout *layout =
						&plane->layer.desc->layout;
	int disc_surface = 0;

	if (!state->disc_updated)
		return;

	disc_surface = state->disc_h * state->disc_w;

	atmel_hlcdc_layer_update_cfg(&plane->layer, layout->general_config,
				ATMEL_HLCDC_LAYER_DISCEN,
				disc_surface ? ATMEL_HLCDC_LAYER_DISCEN : 0);

	if (!disc_surface)
		return;

	atmel_hlcdc_layer_update_cfg(&plane->layer,
				     layout->disc_pos,
				     0xffffffff,
				     state->disc_x | (state->disc_y << 16));

	atmel_hlcdc_layer_update_cfg(&plane->layer,
				     layout->disc_size,
				     0xffffffff,
				     (state->disc_w - 1) |
				     ((state->disc_h - 1) << 16));
}

static int atmel_hlcdc_plane_atomic_check(struct drm_plane *p,
					  struct drm_plane_state *s)
{
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_state *state =
				drm_plane_state_to_atmel_hlcdc_plane_state(s);
	const struct atmel_hlcdc_layer_cfg_layout *layout =
						&plane->layer.desc->layout;
	struct drm_framebuffer *fb = state->base.fb;
	const struct drm_display_mode *mode;
	struct drm_crtc_state *crtc_state;
	unsigned int patched_crtc_w;
	unsigned int patched_crtc_h;
	unsigned int patched_src_w;
	unsigned int patched_src_h;
	unsigned int tmp;
	int x_offset = 0;
	int y_offset = 0;
	int hsub = 1;
	int vsub = 1;
	int i;

	if (!state->base.crtc || !fb)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(s->state, s->crtc);
	mode = &crtc_state->adjusted_mode;

	state->src_x = s->src_x;
	state->src_y = s->src_y;
	state->src_h = s->src_h;
	state->src_w = s->src_w;
	state->crtc_x = s->crtc_x;
	state->crtc_y = s->crtc_y;
	state->crtc_h = s->crtc_h;
	state->crtc_w = s->crtc_w;
	if ((state->src_x | state->src_y | state->src_w | state->src_h) &
	    SUBPIXEL_MASK)
		return -EINVAL;

	state->src_x >>= 16;
	state->src_y >>= 16;
	state->src_w >>= 16;
	state->src_h >>= 16;

	state->nplanes = drm_format_num_planes(fb->pixel_format);
	if (state->nplanes > ATMEL_HLCDC_MAX_PLANES)
		return -EINVAL;

	/*
	 * Swap width and size in case of 90 or 270 degrees rotation
	 */
	if (state->base.rotation & (DRM_ROTATE_90 | DRM_ROTATE_270)) {
		tmp = state->crtc_w;
		state->crtc_w = state->crtc_h;
		state->crtc_h = tmp;
		tmp = state->src_w;
		state->src_w = state->src_h;
		state->src_h = tmp;
	}

	if (state->crtc_x + state->crtc_w > mode->hdisplay)
		patched_crtc_w = mode->hdisplay - state->crtc_x;
	else
		patched_crtc_w = state->crtc_w;

	if (state->crtc_x < 0) {
		patched_crtc_w += state->crtc_x;
		x_offset = -state->crtc_x;
		state->crtc_x = 0;
	}

	if (state->crtc_y + state->crtc_h > mode->vdisplay)
		patched_crtc_h = mode->vdisplay - state->crtc_y;
	else
		patched_crtc_h = state->crtc_h;

	if (state->crtc_y < 0) {
		patched_crtc_h += state->crtc_y;
		y_offset = -state->crtc_y;
		state->crtc_y = 0;
	}

	patched_src_w = DIV_ROUND_CLOSEST(patched_crtc_w * state->src_w,
					  state->crtc_w);
	patched_src_h = DIV_ROUND_CLOSEST(patched_crtc_h * state->src_h,
					  state->crtc_h);

	hsub = drm_format_horz_chroma_subsampling(fb->pixel_format);
	vsub = drm_format_vert_chroma_subsampling(fb->pixel_format);

	for (i = 0; i < state->nplanes; i++) {
		unsigned int offset = 0;
		int xdiv = i ? hsub : 1;
		int ydiv = i ? vsub : 1;

		state->bpp[i] = drm_format_plane_cpp(fb->pixel_format, i);
		if (!state->bpp[i])
			return -EINVAL;

		switch (state->base.rotation & DRM_ROTATE_MASK) {
		case DRM_ROTATE_90:
			offset = ((y_offset + state->src_y + patched_src_w - 1) /
				  ydiv) * fb->pitches[i];
			offset += ((x_offset + state->src_x) / xdiv) *
				  state->bpp[i];
			state->xstride[i] = ((patched_src_w - 1) / ydiv) *
					  fb->pitches[i];
			state->pstride[i] = -fb->pitches[i] - state->bpp[i];
			break;
		case DRM_ROTATE_180:
			offset = ((y_offset + state->src_y + patched_src_h - 1) /
				  ydiv) * fb->pitches[i];
			offset += ((x_offset + state->src_x + patched_src_w - 1) /
				   xdiv) * state->bpp[i];
			state->xstride[i] = ((((patched_src_w - 1) / xdiv) - 1) *
					   state->bpp[i]) - fb->pitches[i];
			state->pstride[i] = -2 * state->bpp[i];
			break;
		case DRM_ROTATE_270:
			offset = ((y_offset + state->src_y) / ydiv) *
				 fb->pitches[i];
			offset += ((x_offset + state->src_x + patched_src_h - 1) /
				   xdiv) * state->bpp[i];
			state->xstride[i] = -(((patched_src_w - 1) / ydiv) *
					    fb->pitches[i]) -
					  (2 * state->bpp[i]);
			state->pstride[i] = fb->pitches[i] - state->bpp[i];
			break;
		case DRM_ROTATE_0:
		default:
			offset = ((y_offset + state->src_y) / ydiv) *
				 fb->pitches[i];
			offset += ((x_offset + state->src_x) / xdiv) *
				  state->bpp[i];
			state->xstride[i] = fb->pitches[i] -
					  ((patched_src_w / xdiv) *
					   state->bpp[i]);
			state->pstride[i] = 0;
			break;
		}

		state->offsets[i] = offset + fb->offsets[i];
	}

	state->src_w = patched_src_w;
	state->src_h = patched_src_h;
	state->crtc_w = patched_crtc_w;
	state->crtc_h = patched_crtc_h;

	if (!layout->size &&
	    (mode->hdisplay != state->crtc_w ||
	     mode->vdisplay != state->crtc_h))
		return -EINVAL;

	if (plane->layer.desc->max_height &&
	    state->crtc_h > plane->layer.desc->max_height)
		return -EINVAL;

	if (plane->layer.desc->max_width &&
	    state->crtc_w > plane->layer.desc->max_width)
		return -EINVAL;

	if ((state->crtc_h != state->src_h || state->crtc_w != state->src_w) &&
	    (!layout->memsize ||
	     atmel_hlcdc_format_embeds_alpha(state->base.fb->pixel_format)))
		return -EINVAL;

	if (state->crtc_x < 0 || state->crtc_y < 0)
		return -EINVAL;

	if (state->crtc_w + state->crtc_x > mode->hdisplay ||
	    state->crtc_h + state->crtc_y > mode->vdisplay)
		return -EINVAL;

	return 0;
}

static int atmel_hlcdc_plane_prepare_fb(struct drm_plane *p,
					struct drm_plane_state *new_state)
{
	/*
	 * FIXME: we should avoid this const -> non-const cast but it's
	 * currently the only solution we have to modify the ->prepared
	 * state and rollback the update request.
	 * Ideally, we should rework the code to attach all the resources
	 * to atmel_hlcdc_plane_state (including the DMA desc allocation),
	 * but this require a complete rework of the atmel_hlcdc_layer
	 * code.
	 */
	struct drm_plane_state *s = (struct drm_plane_state *)new_state;
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_state *state =
			drm_plane_state_to_atmel_hlcdc_plane_state(s);
	int ret;

	ret = atmel_hlcdc_layer_update_start(&plane->layer);
	if (!ret)
		state->prepared = true;

	return ret;
}

static void atmel_hlcdc_plane_cleanup_fb(struct drm_plane *p,
					 struct drm_plane_state *old_state)
{
	/*
	 * FIXME: we should avoid this const -> non-const cast but it's
	 * currently the only solution we have to modify the ->prepared
	 * state and rollback the update request.
	 * Ideally, we should rework the code to attach all the resources
	 * to atmel_hlcdc_plane_state (including the DMA desc allocation),
	 * but this require a complete rework of the atmel_hlcdc_layer
	 * code.
	 */
	struct drm_plane_state *s = (struct drm_plane_state *)old_state;
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_state *state =
			drm_plane_state_to_atmel_hlcdc_plane_state(s);

	/*
	 * The Request has already been applied or cancelled, nothing to do
	 * here.
	 */
	if (!state->prepared)
		return;

	atmel_hlcdc_layer_update_rollback(&plane->layer);
	state->prepared = false;
}

static void atmel_hlcdc_plane_atomic_update(struct drm_plane *p,
					    struct drm_plane_state *old_s)
{
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_state *state =
			drm_plane_state_to_atmel_hlcdc_plane_state(p->state);

	if (!p->state->crtc || !p->state->fb)
		return;

	atmel_hlcdc_plane_update_pos_and_size(plane, state);
	atmel_hlcdc_plane_update_general_settings(plane, state);
	atmel_hlcdc_plane_update_format(plane, state);
	atmel_hlcdc_plane_update_buffers(plane, state);
	atmel_hlcdc_plane_update_disc_area(plane, state);

	atmel_hlcdc_layer_update_commit(&plane->layer);
}

static void atmel_hlcdc_plane_atomic_disable(struct drm_plane *p,
					     struct drm_plane_state *old_state)
{
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);

	atmel_hlcdc_layer_disable(&plane->layer);
}

static void atmel_hlcdc_plane_destroy(struct drm_plane *p)
{
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);

	if (plane->base.fb)
		drm_framebuffer_unreference(plane->base.fb);

	atmel_hlcdc_layer_cleanup(p->dev, &plane->layer);

	drm_plane_cleanup(p);
	devm_kfree(p->dev->dev, plane);
}

static int atmel_hlcdc_plane_atomic_set_property(struct drm_plane *p,
						 struct drm_plane_state *s,
						 struct drm_property *property,
						 uint64_t val)
{
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_properties *props = plane->properties;
	struct atmel_hlcdc_plane_state *state =
			drm_plane_state_to_atmel_hlcdc_plane_state(s);

	if (property == props->alpha)
		state->alpha = val;
	else
		return -EINVAL;

	return 0;
}

static int atmel_hlcdc_plane_atomic_get_property(struct drm_plane *p,
					const struct drm_plane_state *s,
					struct drm_property *property,
					uint64_t *val)
{
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_properties *props = plane->properties;
	const struct atmel_hlcdc_plane_state *state =
		container_of(s, const struct atmel_hlcdc_plane_state, base);

	if (property == props->alpha)
		*val = state->alpha;
	else
		return -EINVAL;

	return 0;
}

static void atmel_hlcdc_plane_init_properties(struct atmel_hlcdc_plane *plane,
				const struct atmel_hlcdc_layer_desc *desc,
				struct atmel_hlcdc_plane_properties *props)
{
	struct regmap *regmap = plane->layer.hlcdc->regmap;

	if (desc->type == ATMEL_HLCDC_OVERLAY_LAYER ||
	    desc->type == ATMEL_HLCDC_CURSOR_LAYER) {
		drm_object_attach_property(&plane->base.base,
					   props->alpha, 255);

		/* Set default alpha value */
		regmap_update_bits(regmap,
				desc->regs_offset +
				ATMEL_HLCDC_LAYER_GENERAL_CFG(&plane->layer),
				ATMEL_HLCDC_LAYER_GA_MASK,
				ATMEL_HLCDC_LAYER_GA_MASK);
	}

	if (desc->layout.xstride && desc->layout.pstride)
		drm_object_attach_property(&plane->base.base,
				plane->base.dev->mode_config.rotation_property,
				DRM_ROTATE_0);

	if (desc->layout.csc) {
		/*
		 * TODO: decare a "yuv-to-rgb-conv-factors" property to let
		 * userspace modify these factors (using a BLOB property ?).
		 */
		regmap_write(regmap,
			     desc->regs_offset +
			     ATMEL_HLCDC_LAYER_CSC_CFG(&plane->layer, 0),
			     0x4c900091);
		regmap_write(regmap,
			     desc->regs_offset +
			     ATMEL_HLCDC_LAYER_CSC_CFG(&plane->layer, 1),
			     0x7a5f5090);
		regmap_write(regmap,
			     desc->regs_offset +
			     ATMEL_HLCDC_LAYER_CSC_CFG(&plane->layer, 2),
			     0x40040890);
	}
}

static struct drm_plane_helper_funcs atmel_hlcdc_layer_plane_helper_funcs = {
	.prepare_fb = atmel_hlcdc_plane_prepare_fb,
	.cleanup_fb = atmel_hlcdc_plane_cleanup_fb,
	.atomic_check = atmel_hlcdc_plane_atomic_check,
	.atomic_update = atmel_hlcdc_plane_atomic_update,
	.atomic_disable = atmel_hlcdc_plane_atomic_disable,
};

static void atmel_hlcdc_plane_reset(struct drm_plane *p)
{
	struct atmel_hlcdc_plane_state *state;

	if (p->state) {
		state = drm_plane_state_to_atmel_hlcdc_plane_state(p->state);

		if (state->base.fb)
			drm_framebuffer_unreference(state->base.fb);

		kfree(state);
		p->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		state->alpha = 255;
		p->state = &state->base;
		p->state->plane = p;
	}
}

static struct drm_plane_state *
atmel_hlcdc_plane_atomic_duplicate_state(struct drm_plane *p)
{
	struct atmel_hlcdc_plane_state *state =
			drm_plane_state_to_atmel_hlcdc_plane_state(p->state);
	struct atmel_hlcdc_plane_state *copy;

	copy = kmemdup(state, sizeof(*state), GFP_KERNEL);
	if (!copy)
		return NULL;

	copy->disc_updated = false;
	copy->prepared = false;

	if (copy->base.fb)
		drm_framebuffer_reference(copy->base.fb);

	return &copy->base;
}

static void atmel_hlcdc_plane_atomic_destroy_state(struct drm_plane *plane,
						   struct drm_plane_state *s)
{
	struct atmel_hlcdc_plane_state *state =
			drm_plane_state_to_atmel_hlcdc_plane_state(s);

	if (s->fb)
		drm_framebuffer_unreference(s->fb);

	kfree(state);
}

static struct drm_plane_funcs layer_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.set_property = drm_atomic_helper_plane_set_property,
	.destroy = atmel_hlcdc_plane_destroy,
	.reset = atmel_hlcdc_plane_reset,
	.atomic_duplicate_state = atmel_hlcdc_plane_atomic_duplicate_state,
	.atomic_destroy_state = atmel_hlcdc_plane_atomic_destroy_state,
	.atomic_set_property = atmel_hlcdc_plane_atomic_set_property,
	.atomic_get_property = atmel_hlcdc_plane_atomic_get_property,
};

static struct atmel_hlcdc_plane *
atmel_hlcdc_plane_create(struct drm_device *dev,
			 const struct atmel_hlcdc_layer_desc *desc,
			 struct atmel_hlcdc_plane_properties *props)
{
	struct atmel_hlcdc_plane *plane;
	enum drm_plane_type type;
	int ret;

	plane = devm_kzalloc(dev->dev, sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return ERR_PTR(-ENOMEM);

	ret = atmel_hlcdc_layer_init(dev, &plane->layer, desc);
	if (ret)
		return ERR_PTR(ret);

	if (desc->type == ATMEL_HLCDC_BASE_LAYER)
		type = DRM_PLANE_TYPE_PRIMARY;
	else if (desc->type == ATMEL_HLCDC_CURSOR_LAYER)
		type = DRM_PLANE_TYPE_CURSOR;
	else
		type = DRM_PLANE_TYPE_OVERLAY;

	ret = drm_universal_plane_init(dev, &plane->base, 0,
				       &layer_plane_funcs,
				       desc->formats->formats,
				       desc->formats->nformats, type, NULL);
	if (ret)
		return ERR_PTR(ret);

	drm_plane_helper_add(&plane->base,
			     &atmel_hlcdc_layer_plane_helper_funcs);

	/* Set default property values*/
	atmel_hlcdc_plane_init_properties(plane, desc, props);

	return plane;
}

static struct atmel_hlcdc_plane_properties *
atmel_hlcdc_plane_create_properties(struct drm_device *dev)
{
	struct atmel_hlcdc_plane_properties *props;

	props = devm_kzalloc(dev->dev, sizeof(*props), GFP_KERNEL);
	if (!props)
		return ERR_PTR(-ENOMEM);

	props->alpha = drm_property_create_range(dev, 0, "alpha", 0, 255);
	if (!props->alpha)
		return ERR_PTR(-ENOMEM);

	dev->mode_config.rotation_property =
			drm_mode_create_rotation_property(dev,
							  DRM_ROTATE_0 |
							  DRM_ROTATE_90 |
							  DRM_ROTATE_180 |
							  DRM_ROTATE_270);
	if (!dev->mode_config.rotation_property)
		return ERR_PTR(-ENOMEM);

	return props;
}

struct atmel_hlcdc_planes *
atmel_hlcdc_create_planes(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_plane_properties *props;
	struct atmel_hlcdc_planes *planes;
	const struct atmel_hlcdc_layer_desc *descs = dc->desc->layers;
	int nlayers = dc->desc->nlayers;
	int i;

	planes = devm_kzalloc(dev->dev, sizeof(*planes), GFP_KERNEL);
	if (!planes)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < nlayers; i++) {
		if (descs[i].type == ATMEL_HLCDC_OVERLAY_LAYER)
			planes->noverlays++;
	}

	if (planes->noverlays) {
		planes->overlays = devm_kzalloc(dev->dev,
						planes->noverlays *
						sizeof(*planes->overlays),
						GFP_KERNEL);
		if (!planes->overlays)
			return ERR_PTR(-ENOMEM);
	}

	props = atmel_hlcdc_plane_create_properties(dev);
	if (IS_ERR(props))
		return ERR_CAST(props);

	planes->noverlays = 0;
	for (i = 0; i < nlayers; i++) {
		struct atmel_hlcdc_plane *plane;

		if (descs[i].type == ATMEL_HLCDC_PP_LAYER)
			continue;

		plane = atmel_hlcdc_plane_create(dev, &descs[i], props);
		if (IS_ERR(plane))
			return ERR_CAST(plane);

		plane->properties = props;

		switch (descs[i].type) {
		case ATMEL_HLCDC_BASE_LAYER:
			if (planes->primary)
				return ERR_PTR(-EINVAL);
			planes->primary = plane;
			break;

		case ATMEL_HLCDC_OVERLAY_LAYER:
			planes->overlays[planes->noverlays++] = plane;
			break;

		case ATMEL_HLCDC_CURSOR_LAYER:
			if (planes->cursor)
				return ERR_PTR(-EINVAL);
			planes->cursor = plane;
			break;

		default:
			break;
		}
	}

	return planes;
}
