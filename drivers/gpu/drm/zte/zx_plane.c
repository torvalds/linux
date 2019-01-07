/*
 * Copyright 2016 Linaro Ltd.
 * Copyright 2016 ZTE Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_plane_helper.h>
#include <drm/drmP.h>

#include "zx_common_regs.h"
#include "zx_drm_drv.h"
#include "zx_plane.h"
#include "zx_plane_regs.h"
#include "zx_vou.h"

static const uint32_t gl_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_RGB565,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_ARGB4444,
};

static const uint32_t vl_formats[] = {
	DRM_FORMAT_NV12,	/* Semi-planar YUV420 */
	DRM_FORMAT_YUV420,	/* Planar YUV420 */
	DRM_FORMAT_YUYV,	/* Packed YUV422 */
	DRM_FORMAT_YVYU,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YUV444,	/* YUV444 8bit */
	/*
	 * TODO: add formats below that HW supports:
	 *  - YUV420 P010
	 *  - YUV420 Hantro
	 *  - YUV444 10bit
	 */
};

#define FRAC_16_16(mult, div)    (((mult) << 16) / (div))

static int zx_vl_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *plane_state)
{
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_crtc *crtc = plane_state->crtc;
	struct drm_crtc_state *crtc_state;
	int min_scale = FRAC_16_16(1, 8);
	int max_scale = FRAC_16_16(8, 1);

	if (!crtc || !fb)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(plane_state->state,
							crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	/* nothing to check when disabling or disabled */
	if (!crtc_state->enable)
		return 0;

	/* plane must be enabled */
	if (!plane_state->crtc)
		return -EINVAL;

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   min_scale, max_scale,
						   true, true);
}

static int zx_vl_get_fmt(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
		return VL_FMT_YUV420;
	case DRM_FORMAT_YUV420:
		return VL_YUV420_PLANAR | VL_FMT_YUV420;
	case DRM_FORMAT_YUYV:
		return VL_YUV422_YUYV | VL_FMT_YUV422;
	case DRM_FORMAT_YVYU:
		return VL_YUV422_YVYU | VL_FMT_YUV422;
	case DRM_FORMAT_UYVY:
		return VL_YUV422_UYVY | VL_FMT_YUV422;
	case DRM_FORMAT_VYUY:
		return VL_YUV422_VYUY | VL_FMT_YUV422;
	case DRM_FORMAT_YUV444:
		return VL_FMT_YUV444_8BIT;
	default:
		WARN_ONCE(1, "invalid pixel format %d\n", format);
		return -EINVAL;
	}
}

static inline void zx_vl_set_update(struct zx_plane *zplane)
{
	void __iomem *layer = zplane->layer;

	zx_writel_mask(layer + VL_CTRL0, VL_UPDATE, VL_UPDATE);
}

static inline void zx_vl_rsz_set_update(struct zx_plane *zplane)
{
	zx_writel(zplane->rsz + RSZ_VL_ENABLE_CFG, 1);
}

static int zx_vl_rsz_get_fmt(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_YUV420:
		return RSZ_VL_FMT_YCBCR420;
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
		return RSZ_VL_FMT_YCBCR422;
	case DRM_FORMAT_YUV444:
		return RSZ_VL_FMT_YCBCR444;
	default:
		WARN_ONCE(1, "invalid pixel format %d\n", format);
		return -EINVAL;
	}
}

static inline u32 rsz_step_value(u32 src, u32 dst)
{
	u32 val = 0;

	if (src == dst)
		val = 0;
	else if (src < dst)
		val = RSZ_PARA_STEP((src << 16) / dst);
	else if (src > dst)
		val = RSZ_DATA_STEP(src / dst) |
		      RSZ_PARA_STEP(((src << 16) / dst) & 0xffff);

	return val;
}

static void zx_vl_rsz_setup(struct zx_plane *zplane, uint32_t format,
			    u32 src_w, u32 src_h, u32 dst_w, u32 dst_h)
{
	void __iomem *rsz = zplane->rsz;
	u32 src_chroma_w = src_w;
	u32 src_chroma_h = src_h;
	int fmt;

	/* Set up source and destination resolution */
	zx_writel(rsz + RSZ_SRC_CFG, RSZ_VER(src_h - 1) | RSZ_HOR(src_w - 1));
	zx_writel(rsz + RSZ_DEST_CFG, RSZ_VER(dst_h - 1) | RSZ_HOR(dst_w - 1));

	/* Configure data format for VL RSZ */
	fmt = zx_vl_rsz_get_fmt(format);
	if (fmt >= 0)
		zx_writel_mask(rsz + RSZ_VL_CTRL_CFG, RSZ_VL_FMT_MASK, fmt);

	/* Calculate Chroma height and width */
	if (fmt == RSZ_VL_FMT_YCBCR420) {
		src_chroma_w = src_w >> 1;
		src_chroma_h = src_h >> 1;
	} else if (fmt == RSZ_VL_FMT_YCBCR422) {
		src_chroma_w = src_w >> 1;
	}

	/* Set up Luma and Chroma step registers */
	zx_writel(rsz + RSZ_VL_LUMA_HOR, rsz_step_value(src_w, dst_w));
	zx_writel(rsz + RSZ_VL_LUMA_VER, rsz_step_value(src_h, dst_h));
	zx_writel(rsz + RSZ_VL_CHROMA_HOR, rsz_step_value(src_chroma_w, dst_w));
	zx_writel(rsz + RSZ_VL_CHROMA_VER, rsz_step_value(src_chroma_h, dst_h));

	zx_vl_rsz_set_update(zplane);
}

static void zx_vl_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct zx_plane *zplane = to_zx_plane(plane);
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_rect *src = &state->src;
	struct drm_rect *dst = &state->dst;
	struct drm_gem_cma_object *cma_obj;
	void __iomem *layer = zplane->layer;
	void __iomem *hbsc = zplane->hbsc;
	void __iomem *paddr_reg;
	dma_addr_t paddr;
	u32 src_x, src_y, src_w, src_h;
	u32 dst_x, dst_y, dst_w, dst_h;
	uint32_t format;
	int fmt;
	int num_planes;
	int i;

	if (!fb)
		return;

	format = fb->format->format;

	src_x = src->x1 >> 16;
	src_y = src->y1 >> 16;
	src_w = drm_rect_width(src) >> 16;
	src_h = drm_rect_height(src) >> 16;

	dst_x = dst->x1;
	dst_y = dst->y1;
	dst_w = drm_rect_width(dst);
	dst_h = drm_rect_height(dst);

	/* Set up data address registers for Y, Cb and Cr planes */
	num_planes = drm_format_num_planes(format);
	paddr_reg = layer + VL_Y;
	for (i = 0; i < num_planes; i++) {
		cma_obj = drm_fb_cma_get_gem_obj(fb, i);
		paddr = cma_obj->paddr + fb->offsets[i];
		paddr += src_y * fb->pitches[i];
		paddr += src_x * drm_format_plane_cpp(format, i);
		zx_writel(paddr_reg, paddr);
		paddr_reg += 4;
	}

	/* Set up source height/width register */
	zx_writel(layer + VL_SRC_SIZE, GL_SRC_W(src_w) | GL_SRC_H(src_h));

	/* Set up start position register */
	zx_writel(layer + VL_POS_START, GL_POS_X(dst_x) | GL_POS_Y(dst_y));

	/* Set up end position register */
	zx_writel(layer + VL_POS_END,
		  GL_POS_X(dst_x + dst_w) | GL_POS_Y(dst_y + dst_h));

	/* Strides of Cb and Cr planes should be identical */
	zx_writel(layer + VL_STRIDE, LUMA_STRIDE(fb->pitches[0]) |
		  CHROMA_STRIDE(fb->pitches[1]));

	/* Set up video layer data format */
	fmt = zx_vl_get_fmt(format);
	if (fmt >= 0)
		zx_writel(layer + VL_CTRL1, fmt);

	/* Always use scaler since it exists (set for not bypass) */
	zx_writel_mask(layer + VL_CTRL2, VL_SCALER_BYPASS_MODE,
		       VL_SCALER_BYPASS_MODE);

	zx_vl_rsz_setup(zplane, format, src_w, src_h, dst_w, dst_h);

	/* Enable HBSC block */
	zx_writel_mask(hbsc + HBSC_CTRL0, HBSC_CTRL_EN, HBSC_CTRL_EN);

	zx_vou_layer_enable(plane);

	zx_vl_set_update(zplane);
}

static void zx_plane_atomic_disable(struct drm_plane *plane,
				    struct drm_plane_state *old_state)
{
	struct zx_plane *zplane = to_zx_plane(plane);
	void __iomem *hbsc = zplane->hbsc;

	zx_vou_layer_disable(plane, old_state);

	/* Disable HBSC block */
	zx_writel_mask(hbsc + HBSC_CTRL0, HBSC_CTRL_EN, 0);
}

static const struct drm_plane_helper_funcs zx_vl_plane_helper_funcs = {
	.atomic_check = zx_vl_plane_atomic_check,
	.atomic_update = zx_vl_plane_atomic_update,
	.atomic_disable = zx_plane_atomic_disable,
};

static int zx_gl_plane_atomic_check(struct drm_plane *plane,
				    struct drm_plane_state *plane_state)
{
	struct drm_framebuffer *fb = plane_state->fb;
	struct drm_crtc *crtc = plane_state->crtc;
	struct drm_crtc_state *crtc_state;

	if (!crtc || !fb)
		return 0;

	crtc_state = drm_atomic_get_existing_crtc_state(plane_state->state,
							crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	/* nothing to check when disabling or disabled */
	if (!crtc_state->enable)
		return 0;

	/* plane must be enabled */
	if (!plane_state->crtc)
		return -EINVAL;

	return drm_atomic_helper_check_plane_state(plane_state, crtc_state,
						   DRM_PLANE_HELPER_NO_SCALING,
						   DRM_PLANE_HELPER_NO_SCALING,
						   false, true);
}

static int zx_gl_get_fmt(uint32_t format)
{
	switch (format) {
	case DRM_FORMAT_ARGB8888:
	case DRM_FORMAT_XRGB8888:
		return GL_FMT_ARGB8888;
	case DRM_FORMAT_RGB888:
		return GL_FMT_RGB888;
	case DRM_FORMAT_RGB565:
		return GL_FMT_RGB565;
	case DRM_FORMAT_ARGB1555:
		return GL_FMT_ARGB1555;
	case DRM_FORMAT_ARGB4444:
		return GL_FMT_ARGB4444;
	default:
		WARN_ONCE(1, "invalid pixel format %d\n", format);
		return -EINVAL;
	}
}

static inline void zx_gl_set_update(struct zx_plane *zplane)
{
	void __iomem *layer = zplane->layer;

	zx_writel_mask(layer + GL_CTRL0, GL_UPDATE, GL_UPDATE);
}

static inline void zx_gl_rsz_set_update(struct zx_plane *zplane)
{
	zx_writel(zplane->rsz + RSZ_ENABLE_CFG, 1);
}

static void zx_gl_rsz_setup(struct zx_plane *zplane, u32 src_w, u32 src_h,
			    u32 dst_w, u32 dst_h)
{
	void __iomem *rsz = zplane->rsz;

	zx_writel(rsz + RSZ_SRC_CFG, RSZ_VER(src_h - 1) | RSZ_HOR(src_w - 1));
	zx_writel(rsz + RSZ_DEST_CFG, RSZ_VER(dst_h - 1) | RSZ_HOR(dst_w - 1));

	zx_gl_rsz_set_update(zplane);
}

static void zx_gl_plane_atomic_update(struct drm_plane *plane,
				      struct drm_plane_state *old_state)
{
	struct zx_plane *zplane = to_zx_plane(plane);
	struct drm_framebuffer *fb = plane->state->fb;
	struct drm_gem_cma_object *cma_obj;
	void __iomem *layer = zplane->layer;
	void __iomem *csc = zplane->csc;
	void __iomem *hbsc = zplane->hbsc;
	u32 src_x, src_y, src_w, src_h;
	u32 dst_x, dst_y, dst_w, dst_h;
	unsigned int bpp;
	uint32_t format;
	dma_addr_t paddr;
	u32 stride;
	int fmt;

	if (!fb)
		return;

	format = fb->format->format;
	stride = fb->pitches[0];

	src_x = plane->state->src_x >> 16;
	src_y = plane->state->src_y >> 16;
	src_w = plane->state->src_w >> 16;
	src_h = plane->state->src_h >> 16;

	dst_x = plane->state->crtc_x;
	dst_y = plane->state->crtc_y;
	dst_w = plane->state->crtc_w;
	dst_h = plane->state->crtc_h;

	bpp = fb->format->cpp[0];

	cma_obj = drm_fb_cma_get_gem_obj(fb, 0);
	paddr = cma_obj->paddr + fb->offsets[0];
	paddr += src_y * stride + src_x * bpp / 8;
	zx_writel(layer + GL_ADDR, paddr);

	/* Set up source height/width register */
	zx_writel(layer + GL_SRC_SIZE, GL_SRC_W(src_w) | GL_SRC_H(src_h));

	/* Set up start position register */
	zx_writel(layer + GL_POS_START, GL_POS_X(dst_x) | GL_POS_Y(dst_y));

	/* Set up end position register */
	zx_writel(layer + GL_POS_END,
		  GL_POS_X(dst_x + dst_w) | GL_POS_Y(dst_y + dst_h));

	/* Set up stride register */
	zx_writel(layer + GL_STRIDE, stride & 0xffff);

	/* Set up graphic layer data format */
	fmt = zx_gl_get_fmt(format);
	if (fmt >= 0)
		zx_writel_mask(layer + GL_CTRL1, GL_DATA_FMT_MASK,
			       fmt << GL_DATA_FMT_SHIFT);

	/* Initialize global alpha with a sane value */
	zx_writel_mask(layer + GL_CTRL2, GL_GLOBAL_ALPHA_MASK,
		       0xff << GL_GLOBAL_ALPHA_SHIFT);

	/* Setup CSC for the GL */
	if (dst_h > 720)
		zx_writel_mask(csc + CSC_CTRL0, CSC_COV_MODE_MASK,
			       CSC_BT709_IMAGE_RGB2YCBCR << CSC_COV_MODE_SHIFT);
	else
		zx_writel_mask(csc + CSC_CTRL0, CSC_COV_MODE_MASK,
			       CSC_BT601_IMAGE_RGB2YCBCR << CSC_COV_MODE_SHIFT);
	zx_writel_mask(csc + CSC_CTRL0, CSC_WORK_ENABLE, CSC_WORK_ENABLE);

	/* Always use scaler since it exists (set for not bypass) */
	zx_writel_mask(layer + GL_CTRL3, GL_SCALER_BYPASS_MODE,
		       GL_SCALER_BYPASS_MODE);

	zx_gl_rsz_setup(zplane, src_w, src_h, dst_w, dst_h);

	/* Enable HBSC block */
	zx_writel_mask(hbsc + HBSC_CTRL0, HBSC_CTRL_EN, HBSC_CTRL_EN);

	zx_vou_layer_enable(plane);

	zx_gl_set_update(zplane);
}

static const struct drm_plane_helper_funcs zx_gl_plane_helper_funcs = {
	.atomic_check = zx_gl_plane_atomic_check,
	.atomic_update = zx_gl_plane_atomic_update,
	.atomic_disable = zx_plane_atomic_disable,
};

static void zx_plane_destroy(struct drm_plane *plane)
{
	drm_plane_cleanup(plane);
}

static const struct drm_plane_funcs zx_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = zx_plane_destroy,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

void zx_plane_set_update(struct drm_plane *plane)
{
	struct zx_plane *zplane = to_zx_plane(plane);

	/* Do nothing if the plane is not enabled */
	if (!plane->state->crtc)
		return;

	switch (plane->type) {
	case DRM_PLANE_TYPE_PRIMARY:
		zx_gl_rsz_set_update(zplane);
		zx_gl_set_update(zplane);
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		zx_vl_rsz_set_update(zplane);
		zx_vl_set_update(zplane);
		break;
	default:
		WARN_ONCE(1, "unsupported plane type %d\n", plane->type);
	}
}

static void zx_plane_hbsc_init(struct zx_plane *zplane)
{
	void __iomem *hbsc = zplane->hbsc;

	/*
	 *  Initialize HBSC block with a sane configuration per recommedation
	 *  from ZTE BSP code.
	 */
	zx_writel(hbsc + HBSC_SATURATION, 0x200);
	zx_writel(hbsc + HBSC_HUE, 0x0);
	zx_writel(hbsc + HBSC_BRIGHT, 0x0);
	zx_writel(hbsc + HBSC_CONTRAST, 0x200);

	zx_writel(hbsc + HBSC_THRESHOLD_COL1, (0x3ac << 16) | 0x40);
	zx_writel(hbsc + HBSC_THRESHOLD_COL2, (0x3c0 << 16) | 0x40);
	zx_writel(hbsc + HBSC_THRESHOLD_COL3, (0x3c0 << 16) | 0x40);
}

int zx_plane_init(struct drm_device *drm, struct zx_plane *zplane,
		  enum drm_plane_type type)
{
	const struct drm_plane_helper_funcs *helper;
	struct drm_plane *plane = &zplane->plane;
	struct device *dev = zplane->dev;
	const uint32_t *formats;
	unsigned int format_count;
	int ret;

	zx_plane_hbsc_init(zplane);

	switch (type) {
	case DRM_PLANE_TYPE_PRIMARY:
		helper = &zx_gl_plane_helper_funcs;
		formats = gl_formats;
		format_count = ARRAY_SIZE(gl_formats);
		break;
	case DRM_PLANE_TYPE_OVERLAY:
		helper = &zx_vl_plane_helper_funcs;
		formats = vl_formats;
		format_count = ARRAY_SIZE(vl_formats);
		break;
	default:
		return -ENODEV;
	}

	ret = drm_universal_plane_init(drm, plane, VOU_CRTC_MASK,
				       &zx_plane_funcs, formats, format_count,
				       NULL, type, NULL);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to init universal plane: %d\n", ret);
		return ret;
	}

	drm_plane_helper_add(plane, helper);

	return 0;
}
