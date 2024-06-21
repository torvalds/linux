// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Free Electrons
 * Copyright (C) 2014 Atmel
 *
 * Author: Boris BREZILLON <boris.brezillon@free-electrons.com>
 */

#include <linux/dmapool.h>
#include <linux/mfd/atmel-hlcdc.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>

#include "atmel_hlcdc_dc.h"

/**
 * struct atmel_hlcdc_plane_state - Atmel HLCDC Plane state structure.
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
 * @disc_x: x discard position
 * @disc_y: y discard position
 * @disc_w: discard width
 * @disc_h: discard height
 * @ahb_id: AHB identification number
 * @bpp: bytes per pixel deduced from pixel_format
 * @offsets: offsets to apply to the GEM buffers
 * @xstride: value to add to the pixel pointer between each line
 * @pstride: value to add to the pixel pointer between each pixel
 * @nplanes: number of planes (deduced from pixel_format)
 * @dscrs: DMA descriptors
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

	int disc_x;
	int disc_y;
	int disc_w;
	int disc_h;

	int ahb_id;

	/* These fields are private and should not be touched */
	int bpp[ATMEL_HLCDC_LAYER_MAX_PLANES];
	unsigned int offsets[ATMEL_HLCDC_LAYER_MAX_PLANES];
	int xstride[ATMEL_HLCDC_LAYER_MAX_PLANES];
	int pstride[ATMEL_HLCDC_LAYER_MAX_PLANES];
	int nplanes;

	/* DMA descriptors. */
	struct atmel_hlcdc_dma_channel_dscr *dscrs[ATMEL_HLCDC_LAYER_MAX_PLANES];
};

static inline struct atmel_hlcdc_plane_state *
drm_plane_state_to_atmel_hlcdc_plane_state(struct drm_plane_state *s)
{
	return container_of(s, struct atmel_hlcdc_plane_state, base);
}

#define SUBPIXEL_MASK			0xffff

static uint32_t rgb_formats[] = {
	DRM_FORMAT_C8,
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
	DRM_FORMAT_C8,
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
	case DRM_FORMAT_C8:
		*mode = ATMEL_HLCDC_C8_MODE;
		break;
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

#define ATMEL_HLCDC_XPHIDEF	4
#define ATMEL_HLCDC_YPHIDEF	4

static u32 atmel_hlcdc_plane_phiscaler_get_factor(u32 srcsize,
						  u32 dstsize,
						  u32 phidef)
{
	u32 factor, max_memsize;

	factor = (256 * ((8 * (srcsize - 1)) - phidef)) / (dstsize - 1);
	max_memsize = ((factor * (dstsize - 1)) + (256 * phidef)) / 2048;

	if (max_memsize > srcsize - 1)
		factor--;

	return factor;
}

static void
atmel_hlcdc_plane_scaler_set_phicoeff(struct atmel_hlcdc_plane *plane,
				      const u32 *coeff_tab, int size,
				      unsigned int cfg_offs)
{
	int i;

	for (i = 0; i < size; i++)
		atmel_hlcdc_layer_write_cfg(&plane->layer, cfg_offs + i,
					    coeff_tab[i]);
}

static
void atmel_hlcdc_plane_setup_scaler(struct atmel_hlcdc_plane *plane,
				    struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	u32 xfactor, yfactor;

	if (!desc->layout.scaler_config)
		return;

	if (state->crtc_w == state->src_w && state->crtc_h == state->src_h) {
		atmel_hlcdc_layer_write_cfg(&plane->layer,
					    desc->layout.scaler_config, 0);
		return;
	}

	if (desc->layout.phicoeffs.x) {
		xfactor = atmel_hlcdc_plane_phiscaler_get_factor(state->src_w,
							state->crtc_w,
							ATMEL_HLCDC_XPHIDEF);

		yfactor = atmel_hlcdc_plane_phiscaler_get_factor(state->src_h,
							state->crtc_h,
							ATMEL_HLCDC_YPHIDEF);

		atmel_hlcdc_plane_scaler_set_phicoeff(plane,
				state->crtc_w < state->src_w ?
				heo_downscaling_xcoef :
				heo_upscaling_xcoef,
				ARRAY_SIZE(heo_upscaling_xcoef),
				desc->layout.phicoeffs.x);

		atmel_hlcdc_plane_scaler_set_phicoeff(plane,
				state->crtc_h < state->src_h ?
				heo_downscaling_ycoef :
				heo_upscaling_ycoef,
				ARRAY_SIZE(heo_upscaling_ycoef),
				desc->layout.phicoeffs.y);
	} else {
		xfactor = (1024 * state->src_w) / state->crtc_w;
		yfactor = (1024 * state->src_h) / state->crtc_h;
	}

	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.scaler_config,
				    ATMEL_HLCDC_LAYER_SCALER_ENABLE |
				    ATMEL_HLCDC_LAYER_SCALER_FACTORS(xfactor,
								     yfactor));
}

static
void atmel_xlcdc_plane_setup_scaler(struct atmel_hlcdc_plane *plane,
				    struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	u32 xfactor, yfactor;

	if (!desc->layout.scaler_config)
		return;

	if (state->crtc_w == state->src_w && state->crtc_h == state->src_h) {
		atmel_hlcdc_layer_write_cfg(&plane->layer,
					    desc->layout.scaler_config, 0);
		return;
	}

	/* xfactor = round[(2^20 * XMEMSIZE)/XSIZE)] */
	xfactor = (u32)(((1 << 20) * state->src_w) / state->crtc_w);

	/* yfactor = round[(2^20 * YMEMSIZE)/YSIZE)] */
	yfactor = (u32)(((1 << 20) * state->src_h) / state->crtc_h);

	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.scaler_config,
				    ATMEL_XLCDC_LAYER_VSCALER_LUMA_ENABLE |
				    ATMEL_XLCDC_LAYER_VSCALER_CHROMA_ENABLE |
				    ATMEL_XLCDC_LAYER_HSCALER_LUMA_ENABLE |
				    ATMEL_XLCDC_LAYER_HSCALER_CHROMA_ENABLE);

	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.scaler_config + 1,
				    yfactor);
	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.scaler_config + 3,
				    xfactor);

	/*
	 * With YCbCr 4:2:2 and YCbYcr 4:2:0 window resampling, configuration
	 * register LCDC_HEOCFG25.VXSCFACT and LCDC_HEOCFG27.HXSCFACT is half
	 * the value of yfactor and xfactor.
	 */
	if (state->base.fb->format->format == DRM_FORMAT_YUV420) {
		yfactor /= 2;
		xfactor /= 2;
	}

	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.scaler_config + 2,
				    yfactor);
	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.scaler_config + 4,
				    xfactor);
}

static void
atmel_hlcdc_plane_update_pos_and_size(struct atmel_hlcdc_plane *plane,
				      struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	struct atmel_hlcdc_dc *dc = plane->base.dev->dev_private;

	if (desc->layout.size)
		atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.size,
					ATMEL_HLCDC_LAYER_SIZE(state->crtc_w,
							       state->crtc_h));

	if (desc->layout.memsize)
		atmel_hlcdc_layer_write_cfg(&plane->layer,
					desc->layout.memsize,
					ATMEL_HLCDC_LAYER_SIZE(state->src_w,
							       state->src_h));

	if (desc->layout.pos)
		atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.pos,
					ATMEL_HLCDC_LAYER_POS(state->crtc_x,
							      state->crtc_y));

	dc->desc->ops->plane_setup_scaler(plane, state);
}

static
void atmel_hlcdc_plane_update_general_settings(struct atmel_hlcdc_plane *plane,
					       struct atmel_hlcdc_plane_state *state)
{
	unsigned int cfg = ATMEL_HLCDC_LAYER_DMA_BLEN_INCR16 | state->ahb_id;
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	const struct drm_format_info *format = state->base.fb->format;

	/*
	 * Rotation optimization is not working on RGB888 (rotation is still
	 * working but without any optimization).
	 */
	if (format->format == DRM_FORMAT_RGB888)
		cfg |= ATMEL_HLCDC_LAYER_DMA_ROTDIS;

	atmel_hlcdc_layer_write_cfg(&plane->layer, ATMEL_HLCDC_LAYER_DMA_CFG,
				    cfg);

	cfg = ATMEL_HLCDC_LAYER_DMA | ATMEL_HLCDC_LAYER_REP;

	if (plane->base.type != DRM_PLANE_TYPE_PRIMARY) {
		cfg |= ATMEL_HLCDC_LAYER_OVR | ATMEL_HLCDC_LAYER_ITER2BL |
		       ATMEL_HLCDC_LAYER_ITER;

		if (format->has_alpha)
			cfg |= ATMEL_HLCDC_LAYER_LAEN;
		else
			cfg |= ATMEL_HLCDC_LAYER_GAEN |
			       ATMEL_HLCDC_LAYER_GA(state->base.alpha);
	}

	if (state->disc_h && state->disc_w)
		cfg |= ATMEL_HLCDC_LAYER_DISCEN;

	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.general_config,
				    cfg);
}

static
void atmel_xlcdc_plane_update_general_settings(struct atmel_hlcdc_plane *plane,
					       struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	const struct drm_format_info *format = state->base.fb->format;
	unsigned int cfg;

	atmel_hlcdc_layer_write_cfg(&plane->layer, ATMEL_XLCDC_LAYER_DMA_CFG,
				    ATMEL_HLCDC_LAYER_DMA_BLEN_INCR16 | state->ahb_id);

	cfg = ATMEL_XLCDC_LAYER_DMA | ATMEL_XLCDC_LAYER_REP;

	if (plane->base.type != DRM_PLANE_TYPE_PRIMARY) {
		/*
		 * Alpha Blending bits specific to SAM9X7 SoC
		 */
		cfg |= ATMEL_XLCDC_LAYER_SFACTC_A0_MULT_AS |
		       ATMEL_XLCDC_LAYER_SFACTA_ONE |
		       ATMEL_XLCDC_LAYER_DFACTC_M_A0_MULT_AS |
		       ATMEL_XLCDC_LAYER_DFACTA_ONE;
		if (format->has_alpha)
			cfg |= ATMEL_XLCDC_LAYER_A0(0xff);
		else
			cfg |= ATMEL_XLCDC_LAYER_A0(state->base.alpha);
	}

	if (state->disc_h && state->disc_w)
		cfg |= ATMEL_XLCDC_LAYER_DISCEN;

	atmel_hlcdc_layer_write_cfg(&plane->layer, desc->layout.general_config,
				    cfg);
}

static void atmel_hlcdc_plane_update_format(struct atmel_hlcdc_plane *plane,
					struct atmel_hlcdc_plane_state *state)
{
	u32 cfg;
	int ret;

	ret = atmel_hlcdc_format_to_plane_mode(state->base.fb->format->format,
					       &cfg);
	if (ret)
		return;

	if ((state->base.fb->format->format == DRM_FORMAT_YUV422 ||
	     state->base.fb->format->format == DRM_FORMAT_NV61) &&
	    drm_rotation_90_or_270(state->base.rotation))
		cfg |= ATMEL_HLCDC_YUV422ROT;

	atmel_hlcdc_layer_write_cfg(&plane->layer,
				    ATMEL_HLCDC_LAYER_FORMAT_CFG, cfg);
}

static void atmel_hlcdc_plane_update_clut(struct atmel_hlcdc_plane *plane,
					  struct atmel_hlcdc_plane_state *state)
{
	struct drm_crtc *crtc = state->base.crtc;
	struct drm_color_lut *lut;
	int idx;

	if (!crtc || !crtc->state)
		return;

	if (!crtc->state->color_mgmt_changed || !crtc->state->gamma_lut)
		return;

	lut = (struct drm_color_lut *)crtc->state->gamma_lut->data;

	for (idx = 0; idx < ATMEL_HLCDC_CLUT_SIZE; idx++, lut++) {
		u32 val = ((lut->red << 8) & 0xff0000) |
			(lut->green & 0xff00) |
			(lut->blue >> 8);

		atmel_hlcdc_layer_write_clut(&plane->layer, idx, val);
	}
}

static void atmel_hlcdc_update_buffers(struct atmel_hlcdc_plane *plane,
				       struct atmel_hlcdc_plane_state *state,
				       u32 sr, int i)
{
	atmel_hlcdc_layer_write_reg(&plane->layer,
				    ATMEL_HLCDC_LAYER_PLANE_HEAD(i),
				    state->dscrs[i]->self);

	if (sr & ATMEL_HLCDC_LAYER_EN)
		return;

	atmel_hlcdc_layer_write_reg(&plane->layer,
				    ATMEL_HLCDC_LAYER_PLANE_ADDR(i),
				    state->dscrs[i]->addr);
	atmel_hlcdc_layer_write_reg(&plane->layer,
				    ATMEL_HLCDC_LAYER_PLANE_CTRL(i),
				    state->dscrs[i]->ctrl);
	atmel_hlcdc_layer_write_reg(&plane->layer,
				    ATMEL_HLCDC_LAYER_PLANE_NEXT(i),
				    state->dscrs[i]->self);
}

static void atmel_xlcdc_update_buffers(struct atmel_hlcdc_plane *plane,
				       struct atmel_hlcdc_plane_state *state,
				       u32 sr, int i)
{
	atmel_hlcdc_layer_write_reg(&plane->layer,
				    ATMEL_XLCDC_LAYER_PLANE_ADDR(i),
				    state->dscrs[i]->addr);
}

static void atmel_hlcdc_plane_update_buffers(struct atmel_hlcdc_plane *plane,
					     struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	struct atmel_hlcdc_dc *dc = plane->base.dev->dev_private;
	struct drm_framebuffer *fb = state->base.fb;
	u32 sr;
	int i;

	if (!dc->desc->is_xlcdc)
		sr = atmel_hlcdc_layer_read_reg(&plane->layer, ATMEL_HLCDC_LAYER_CHSR);

	for (i = 0; i < state->nplanes; i++) {
		struct drm_gem_dma_object *gem = drm_fb_dma_get_gem_obj(fb, i);

		state->dscrs[i]->addr = gem->dma_addr + state->offsets[i];

		dc->desc->ops->lcdc_update_buffers(plane, state, sr, i);

		if (desc->layout.xstride[i])
			atmel_hlcdc_layer_write_cfg(&plane->layer,
						    desc->layout.xstride[i],
						    state->xstride[i]);

		if (desc->layout.pstride[i])
			atmel_hlcdc_layer_write_cfg(&plane->layer,
						    desc->layout.pstride[i],
						    state->pstride[i]);
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

		if (!ovl_s->visible ||
		    !ovl_s->fb ||
		    ovl_s->fb->format->has_alpha ||
		    ovl_s->alpha != DRM_BLEND_ALPHA_OPAQUE)
			continue;

		/* TODO: implement a smarter hidden area detection */
		if (ovl_state->crtc_h * ovl_state->crtc_w < disc_h * disc_w)
			continue;

		disc_x = ovl_state->crtc_x;
		disc_y = ovl_state->crtc_y;
		disc_h = ovl_state->crtc_h;
		disc_w = ovl_state->crtc_w;
	}

	primary_state->disc_x = disc_x;
	primary_state->disc_y = disc_y;
	primary_state->disc_w = disc_w;
	primary_state->disc_h = disc_h;

	return 0;
}

static void
atmel_hlcdc_plane_update_disc_area(struct atmel_hlcdc_plane *plane,
				   struct atmel_hlcdc_plane_state *state)
{
	const struct atmel_hlcdc_layer_cfg_layout *layout;

	layout = &plane->layer.desc->layout;
	if (!layout->disc_pos || !layout->disc_size)
		return;

	atmel_hlcdc_layer_write_cfg(&plane->layer, layout->disc_pos,
				ATMEL_HLCDC_LAYER_DISC_POS(state->disc_x,
							   state->disc_y));

	atmel_hlcdc_layer_write_cfg(&plane->layer, layout->disc_size,
				ATMEL_HLCDC_LAYER_DISC_SIZE(state->disc_w,
							    state->disc_h));
}

static int atmel_hlcdc_plane_atomic_check(struct drm_plane *p,
					  struct drm_atomic_state *state)
{
	struct drm_plane_state *s = drm_atomic_get_new_plane_state(state, p);
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_state *hstate =
				drm_plane_state_to_atmel_hlcdc_plane_state(s);
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	struct drm_framebuffer *fb = hstate->base.fb;
	const struct drm_display_mode *mode;
	struct drm_crtc_state *crtc_state;
	int ret;
	int i;

	if (!hstate->base.crtc || WARN_ON(!fb))
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(state, s->crtc);
	mode = &crtc_state->adjusted_mode;

	ret = drm_atomic_helper_check_plane_state(s, crtc_state,
						  (1 << 16) / 2048,
						  INT_MAX, true, true);
	if (ret || !s->visible)
		return ret;

	hstate->src_x = s->src.x1;
	hstate->src_y = s->src.y1;
	hstate->src_w = drm_rect_width(&s->src);
	hstate->src_h = drm_rect_height(&s->src);
	hstate->crtc_x = s->dst.x1;
	hstate->crtc_y = s->dst.y1;
	hstate->crtc_w = drm_rect_width(&s->dst);
	hstate->crtc_h = drm_rect_height(&s->dst);

	if ((hstate->src_x | hstate->src_y | hstate->src_w | hstate->src_h) &
	    SUBPIXEL_MASK)
		return -EINVAL;

	hstate->src_x >>= 16;
	hstate->src_y >>= 16;
	hstate->src_w >>= 16;
	hstate->src_h >>= 16;

	hstate->nplanes = fb->format->num_planes;
	if (hstate->nplanes > ATMEL_HLCDC_LAYER_MAX_PLANES)
		return -EINVAL;

	for (i = 0; i < hstate->nplanes; i++) {
		unsigned int offset = 0;
		int xdiv = i ? fb->format->hsub : 1;
		int ydiv = i ? fb->format->vsub : 1;

		hstate->bpp[i] = fb->format->cpp[i];
		if (!hstate->bpp[i])
			return -EINVAL;

		switch (hstate->base.rotation & DRM_MODE_ROTATE_MASK) {
		case DRM_MODE_ROTATE_90:
			offset = (hstate->src_y / ydiv) *
				 fb->pitches[i];
			offset += ((hstate->src_x + hstate->src_w - 1) /
				   xdiv) * hstate->bpp[i];
			hstate->xstride[i] = -(((hstate->src_h - 1) / ydiv) *
					    fb->pitches[i]) -
					  (2 * hstate->bpp[i]);
			hstate->pstride[i] = fb->pitches[i] - hstate->bpp[i];
			break;
		case DRM_MODE_ROTATE_180:
			offset = ((hstate->src_y + hstate->src_h - 1) /
				  ydiv) * fb->pitches[i];
			offset += ((hstate->src_x + hstate->src_w - 1) /
				   xdiv) * hstate->bpp[i];
			hstate->xstride[i] = ((((hstate->src_w - 1) / xdiv) - 1) *
					   hstate->bpp[i]) - fb->pitches[i];
			hstate->pstride[i] = -2 * hstate->bpp[i];
			break;
		case DRM_MODE_ROTATE_270:
			offset = ((hstate->src_y + hstate->src_h - 1) /
				  ydiv) * fb->pitches[i];
			offset += (hstate->src_x / xdiv) * hstate->bpp[i];
			hstate->xstride[i] = ((hstate->src_h - 1) / ydiv) *
					  fb->pitches[i];
			hstate->pstride[i] = -fb->pitches[i] - hstate->bpp[i];
			break;
		case DRM_MODE_ROTATE_0:
		default:
			offset = (hstate->src_y / ydiv) * fb->pitches[i];
			offset += (hstate->src_x / xdiv) * hstate->bpp[i];
			hstate->xstride[i] = fb->pitches[i] -
					  ((hstate->src_w / xdiv) *
					   hstate->bpp[i]);
			hstate->pstride[i] = 0;
			break;
		}

		hstate->offsets[i] = offset + fb->offsets[i];
	}

	/*
	 * Swap width and size in case of 90 or 270 degrees rotation
	 */
	if (drm_rotation_90_or_270(hstate->base.rotation)) {
		swap(hstate->src_w, hstate->src_h);
	}

	if (!desc->layout.size &&
	    (mode->hdisplay != hstate->crtc_w ||
	     mode->vdisplay != hstate->crtc_h))
		return -EINVAL;

	if ((hstate->crtc_h != hstate->src_h || hstate->crtc_w != hstate->src_w) &&
	    (!desc->layout.memsize ||
	     hstate->base.fb->format->has_alpha))
		return -EINVAL;

	return 0;
}

static void atmel_hlcdc_atomic_disable(struct atmel_hlcdc_plane *plane)
{
	/* Disable interrupts */
	atmel_hlcdc_layer_write_reg(&plane->layer, ATMEL_HLCDC_LAYER_IDR,
				    0xffffffff);

	/* Disable the layer */
	atmel_hlcdc_layer_write_reg(&plane->layer, ATMEL_HLCDC_LAYER_CHDR,
				    ATMEL_HLCDC_LAYER_RST |
				    ATMEL_HLCDC_LAYER_A2Q |
				    ATMEL_HLCDC_LAYER_UPDATE);

	/* Clear all pending interrupts */
	atmel_hlcdc_layer_read_reg(&plane->layer, ATMEL_HLCDC_LAYER_ISR);
}

static void atmel_xlcdc_atomic_disable(struct atmel_hlcdc_plane *plane)
{
	/* Disable interrupts */
	atmel_hlcdc_layer_write_reg(&plane->layer, ATMEL_XLCDC_LAYER_IDR,
				    0xffffffff);

	/* Disable the layer */
	atmel_hlcdc_layer_write_reg(&plane->layer,
				    ATMEL_XLCDC_LAYER_ENR, 0);

	/* Clear all pending interrupts */
	atmel_hlcdc_layer_read_reg(&plane->layer, ATMEL_XLCDC_LAYER_ISR);
}

static void atmel_hlcdc_plane_atomic_disable(struct drm_plane *p,
					     struct drm_atomic_state *state)
{
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_dc *dc = plane->base.dev->dev_private;

	dc->desc->ops->lcdc_atomic_disable(plane);
}

static void atmel_hlcdc_atomic_update(struct atmel_hlcdc_plane *plane,
				      struct atmel_hlcdc_dc *dc)
{
	u32 sr;

	/* Enable the overrun interrupts. */
	atmel_hlcdc_layer_write_reg(&plane->layer, ATMEL_HLCDC_LAYER_IER,
				    ATMEL_HLCDC_LAYER_OVR_IRQ(0) |
				    ATMEL_HLCDC_LAYER_OVR_IRQ(1) |
				    ATMEL_HLCDC_LAYER_OVR_IRQ(2));

	/* Apply the new config at the next SOF event. */
	sr = atmel_hlcdc_layer_read_reg(&plane->layer, ATMEL_HLCDC_LAYER_CHSR);
	atmel_hlcdc_layer_write_reg(&plane->layer, ATMEL_HLCDC_LAYER_CHER,
				    ATMEL_HLCDC_LAYER_UPDATE |
				    (sr & ATMEL_HLCDC_LAYER_EN ?
				    ATMEL_HLCDC_LAYER_A2Q : ATMEL_HLCDC_LAYER_EN));
}

static void atmel_xlcdc_atomic_update(struct atmel_hlcdc_plane *plane,
				      struct atmel_hlcdc_dc *dc)
{
	/* Enable the overrun interrupts. */
	atmel_hlcdc_layer_write_reg(&plane->layer, ATMEL_XLCDC_LAYER_IER,
				    ATMEL_XLCDC_LAYER_OVR_IRQ(0) |
				    ATMEL_XLCDC_LAYER_OVR_IRQ(1) |
				    ATMEL_XLCDC_LAYER_OVR_IRQ(2));

	atmel_hlcdc_layer_write_reg(&plane->layer, ATMEL_XLCDC_LAYER_ENR,
				    ATMEL_XLCDC_LAYER_EN);

	/*
	 * Updating XLCDC_xxxCFGx, XLCDC_xxxFBA and XLCDC_xxxEN,
	 * (where xxx indicates each layer) requires writing one to the
	 * Update Attribute field for each layer in LCDC_ATTRE register for SAM9X7.
	 */
	regmap_write(dc->hlcdc->regmap, ATMEL_XLCDC_ATTRE, ATMEL_XLCDC_BASE_UPDATE |
		     ATMEL_XLCDC_OVR1_UPDATE | ATMEL_XLCDC_OVR3_UPDATE |
		     ATMEL_XLCDC_HEO_UPDATE);
}

static void atmel_hlcdc_plane_atomic_update(struct drm_plane *p,
					    struct drm_atomic_state *state)
{
	struct drm_plane_state *new_s = drm_atomic_get_new_plane_state(state,
								       p);
	struct atmel_hlcdc_plane *plane = drm_plane_to_atmel_hlcdc_plane(p);
	struct atmel_hlcdc_plane_state *hstate =
			drm_plane_state_to_atmel_hlcdc_plane_state(new_s);
	struct atmel_hlcdc_dc *dc = p->dev->dev_private;

	if (!new_s->crtc || !new_s->fb)
		return;

	if (!hstate->base.visible) {
		atmel_hlcdc_plane_atomic_disable(p, state);
		return;
	}

	atmel_hlcdc_plane_update_pos_and_size(plane, hstate);
	dc->desc->ops->lcdc_update_general_settings(plane, hstate);
	atmel_hlcdc_plane_update_format(plane, hstate);
	atmel_hlcdc_plane_update_clut(plane, hstate);
	atmel_hlcdc_plane_update_buffers(plane, hstate);
	atmel_hlcdc_plane_update_disc_area(plane, hstate);

	dc->desc->ops->lcdc_atomic_update(plane, dc);
}

static void atmel_hlcdc_csc_init(struct atmel_hlcdc_plane *plane,
				 const struct atmel_hlcdc_layer_desc *desc)
{
	/*
	 * TODO: declare a "yuv-to-rgb-conv-factors" property to let
	 * userspace modify these factors (using a BLOB property ?).
	 */
	static const u32 hlcdc_csc_coeffs[] = {
		0x4c900091,
		0x7a5f5090,
		0x40040890
	};

	for (int i = 0; i < ARRAY_SIZE(hlcdc_csc_coeffs); i++) {
		atmel_hlcdc_layer_write_cfg(&plane->layer,
					    desc->layout.csc + i,
					    hlcdc_csc_coeffs[i]);
	}
}

static void atmel_xlcdc_csc_init(struct atmel_hlcdc_plane *plane,
				 const struct atmel_hlcdc_layer_desc *desc)
{
	/*
	 * yuv-to-rgb-conv-factors are now defined from LCDC_HEOCFG16 to
	 * LCDC_HEOCFG21 registers in SAM9X7.
	 */
	static const u32 xlcdc_csc_coeffs[] = {
		0x00000488,
		0x00000648,
		0x1EA00480,
		0x00001D28,
		0x08100480,
		0x00000000,
		0x00000007
	};

	for (int i = 0; i < ARRAY_SIZE(xlcdc_csc_coeffs); i++) {
		atmel_hlcdc_layer_write_cfg(&plane->layer,
					    desc->layout.csc + i,
					    xlcdc_csc_coeffs[i]);
	}

	if (desc->layout.vxs_config && desc->layout.hxs_config) {
		/*
		 * Updating vxs.config and hxs.config fixes the
		 * Green Color Issue in SAM9X7 EGT Video Player App
		 */
		atmel_hlcdc_layer_write_cfg(&plane->layer,
					    desc->layout.vxs_config,
					    ATMEL_XLCDC_LAYER_VXSYCFG_ONE |
					    ATMEL_XLCDC_LAYER_VXSYTAP2_ENABLE |
					    ATMEL_XLCDC_LAYER_VXSCCFG_ONE |
					    ATMEL_XLCDC_LAYER_VXSCTAP2_ENABLE);

		atmel_hlcdc_layer_write_cfg(&plane->layer,
					    desc->layout.hxs_config,
					    ATMEL_XLCDC_LAYER_HXSYCFG_ONE |
					    ATMEL_XLCDC_LAYER_HXSYTAP2_ENABLE |
					    ATMEL_XLCDC_LAYER_HXSCCFG_ONE |
					    ATMEL_XLCDC_LAYER_HXSCTAP2_ENABLE);
	}
}

static int atmel_hlcdc_plane_init_properties(struct atmel_hlcdc_plane *plane)
{
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	struct atmel_hlcdc_dc *dc = plane->base.dev->dev_private;

	if (desc->type == ATMEL_HLCDC_OVERLAY_LAYER ||
	    desc->type == ATMEL_HLCDC_CURSOR_LAYER) {
		int ret;

		ret = drm_plane_create_alpha_property(&plane->base);
		if (ret)
			return ret;
	}

	if (desc->layout.xstride[0] && desc->layout.pstride[0]) {
		int ret;

		ret = drm_plane_create_rotation_property(&plane->base,
							 DRM_MODE_ROTATE_0,
							 DRM_MODE_ROTATE_0 |
							 DRM_MODE_ROTATE_90 |
							 DRM_MODE_ROTATE_180 |
							 DRM_MODE_ROTATE_270);
		if (ret)
			return ret;
	}

	if (desc->layout.csc)
		dc->desc->ops->lcdc_csc_init(plane, desc);

	return 0;
}

static void atmel_hlcdc_irq_dbg(struct atmel_hlcdc_plane *plane,
				const struct atmel_hlcdc_layer_desc *desc)
{
	u32 isr = atmel_hlcdc_layer_read_reg(&plane->layer, ATMEL_HLCDC_LAYER_ISR);

	/*
	 * There's not much we can do in case of overrun except informing
	 * the user. However, we are in interrupt context here, hence the
	 * use of dev_dbg().
	 */
	if (isr &
	    (ATMEL_HLCDC_LAYER_OVR_IRQ(0) | ATMEL_HLCDC_LAYER_OVR_IRQ(1) |
	     ATMEL_HLCDC_LAYER_OVR_IRQ(2)))
		dev_dbg(plane->base.dev->dev, "overrun on plane %s\n",
			desc->name);
}

static void atmel_xlcdc_irq_dbg(struct atmel_hlcdc_plane *plane,
				const struct atmel_hlcdc_layer_desc *desc)
{
	u32 isr = atmel_hlcdc_layer_read_reg(&plane->layer, ATMEL_XLCDC_LAYER_ISR);

	/*
	 * There's not much we can do in case of overrun except informing
	 * the user. However, we are in interrupt context here, hence the
	 * use of dev_dbg().
	 */
	if (isr &
	    (ATMEL_XLCDC_LAYER_OVR_IRQ(0) | ATMEL_XLCDC_LAYER_OVR_IRQ(1) |
	     ATMEL_XLCDC_LAYER_OVR_IRQ(2)))
		dev_dbg(plane->base.dev->dev, "overrun on plane %s\n",
			desc->name);
}

void atmel_hlcdc_plane_irq(struct atmel_hlcdc_plane *plane)
{
	const struct atmel_hlcdc_layer_desc *desc = plane->layer.desc;
	struct atmel_hlcdc_dc *dc = plane->base.dev->dev_private;

	dc->desc->ops->lcdc_irq_dbg(plane, desc);
}

const struct atmel_lcdc_dc_ops atmel_hlcdc_ops = {
	.plane_setup_scaler = atmel_hlcdc_plane_setup_scaler,
	.lcdc_update_buffers = atmel_hlcdc_update_buffers,
	.lcdc_atomic_disable = atmel_hlcdc_atomic_disable,
	.lcdc_update_general_settings = atmel_hlcdc_plane_update_general_settings,
	.lcdc_atomic_update = atmel_hlcdc_atomic_update,
	.lcdc_csc_init = atmel_hlcdc_csc_init,
	.lcdc_irq_dbg = atmel_hlcdc_irq_dbg,
};

const struct atmel_lcdc_dc_ops atmel_xlcdc_ops = {
	.plane_setup_scaler = atmel_xlcdc_plane_setup_scaler,
	.lcdc_update_buffers = atmel_xlcdc_update_buffers,
	.lcdc_atomic_disable = atmel_xlcdc_atomic_disable,
	.lcdc_update_general_settings = atmel_xlcdc_plane_update_general_settings,
	.lcdc_atomic_update = atmel_xlcdc_atomic_update,
	.lcdc_csc_init = atmel_xlcdc_csc_init,
	.lcdc_irq_dbg = atmel_xlcdc_irq_dbg,
};

static const struct drm_plane_helper_funcs atmel_hlcdc_layer_plane_helper_funcs = {
	.atomic_check = atmel_hlcdc_plane_atomic_check,
	.atomic_update = atmel_hlcdc_plane_atomic_update,
	.atomic_disable = atmel_hlcdc_plane_atomic_disable,
};

static int atmel_hlcdc_plane_alloc_dscrs(struct drm_plane *p,
					 struct atmel_hlcdc_plane_state *state)
{
	struct atmel_hlcdc_dc *dc = p->dev->dev_private;
	int i;

	for (i = 0; i < ARRAY_SIZE(state->dscrs); i++) {
		struct atmel_hlcdc_dma_channel_dscr *dscr;
		dma_addr_t dscr_dma;

		dscr = dma_pool_alloc(dc->dscrpool, GFP_KERNEL, &dscr_dma);
		if (!dscr)
			goto err;

		dscr->addr = 0;
		dscr->next = dscr_dma;
		dscr->self = dscr_dma;
		dscr->ctrl = ATMEL_HLCDC_LAYER_DFETCH;

		state->dscrs[i] = dscr;
	}

	return 0;

err:
	for (i--; i >= 0; i--) {
		dma_pool_free(dc->dscrpool, state->dscrs[i],
			      state->dscrs[i]->self);
	}

	return -ENOMEM;
}

static void atmel_hlcdc_plane_reset(struct drm_plane *p)
{
	struct atmel_hlcdc_plane_state *state;

	if (p->state) {
		state = drm_plane_state_to_atmel_hlcdc_plane_state(p->state);

		if (state->base.fb)
			drm_framebuffer_put(state->base.fb);

		kfree(state);
		p->state = NULL;
	}

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (state) {
		if (atmel_hlcdc_plane_alloc_dscrs(p, state)) {
			kfree(state);
			dev_err(p->dev->dev,
				"Failed to allocate initial plane state\n");
			return;
		}
		__drm_atomic_helper_plane_reset(p, &state->base);
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

	if (atmel_hlcdc_plane_alloc_dscrs(p, copy)) {
		kfree(copy);
		return NULL;
	}

	if (copy->base.fb)
		drm_framebuffer_get(copy->base.fb);

	return &copy->base;
}

static void atmel_hlcdc_plane_atomic_destroy_state(struct drm_plane *p,
						   struct drm_plane_state *s)
{
	struct atmel_hlcdc_plane_state *state =
			drm_plane_state_to_atmel_hlcdc_plane_state(s);
	struct atmel_hlcdc_dc *dc = p->dev->dev_private;
	int i;

	for (i = 0; i < ARRAY_SIZE(state->dscrs); i++) {
		dma_pool_free(dc->dscrpool, state->dscrs[i],
			      state->dscrs[i]->self);
	}

	if (s->fb)
		drm_framebuffer_put(s->fb);

	kfree(state);
}

static const struct drm_plane_funcs layer_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = drm_plane_cleanup,
	.reset = atmel_hlcdc_plane_reset,
	.atomic_duplicate_state = atmel_hlcdc_plane_atomic_duplicate_state,
	.atomic_destroy_state = atmel_hlcdc_plane_atomic_destroy_state,
};

static int atmel_hlcdc_plane_create(struct drm_device *dev,
				    const struct atmel_hlcdc_layer_desc *desc)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	struct atmel_hlcdc_plane *plane;
	enum drm_plane_type type;
	int ret;

	plane = devm_kzalloc(dev->dev, sizeof(*plane), GFP_KERNEL);
	if (!plane)
		return -ENOMEM;

	atmel_hlcdc_layer_init(&plane->layer, desc, dc->hlcdc->regmap);

	if (desc->type == ATMEL_HLCDC_BASE_LAYER)
		type = DRM_PLANE_TYPE_PRIMARY;
	else if (desc->type == ATMEL_HLCDC_CURSOR_LAYER)
		type = DRM_PLANE_TYPE_CURSOR;
	else
		type = DRM_PLANE_TYPE_OVERLAY;

	ret = drm_universal_plane_init(dev, &plane->base, 0,
				       &layer_plane_funcs,
				       desc->formats->formats,
				       desc->formats->nformats,
				       NULL, type, NULL);
	if (ret)
		return ret;

	drm_plane_helper_add(&plane->base,
			     &atmel_hlcdc_layer_plane_helper_funcs);

	/* Set default property values*/
	ret = atmel_hlcdc_plane_init_properties(plane);
	if (ret)
		return ret;

	dc->layers[desc->id] = &plane->layer;

	return 0;
}

int atmel_hlcdc_create_planes(struct drm_device *dev)
{
	struct atmel_hlcdc_dc *dc = dev->dev_private;
	const struct atmel_hlcdc_layer_desc *descs = dc->desc->layers;
	int nlayers = dc->desc->nlayers;
	int i, ret;

	dc->dscrpool = dmam_pool_create("atmel-hlcdc-dscr", dev->dev,
				sizeof(struct atmel_hlcdc_dma_channel_dscr),
				sizeof(u64), 0);
	if (!dc->dscrpool)
		return -ENOMEM;

	for (i = 0; i < nlayers; i++) {
		if (descs[i].type != ATMEL_HLCDC_BASE_LAYER &&
		    descs[i].type != ATMEL_HLCDC_OVERLAY_LAYER &&
		    descs[i].type != ATMEL_HLCDC_CURSOR_LAYER)
			continue;

		ret = atmel_hlcdc_plane_create(dev, &descs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
