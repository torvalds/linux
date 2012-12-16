/*
 * Copyright (C) 2011 Samsung Electronics Co.Ltd
 * Authors: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <drm/drmP.h>

#include <drm/exynos_drm.h>
#include "exynos_drm_drv.h"
#include "exynos_drm_encoder.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_gem.h"

#define to_exynos_plane(x)	container_of(x, struct exynos_plane, base)

struct exynos_plane {
	struct drm_plane		base;
	struct exynos_drm_overlay	overlay;
	bool				enabled;
};

static const uint32_t formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV12MT,
};

/*
 * This function is to get X or Y size shown via screen. This needs length and
 * start position of CRTC.
 *
 *      <--- length --->
 * CRTC ----------------
 *      ^ start        ^ end
 *
 * There are six cases from a to f.
 *
 *             <----- SCREEN ----->
 *             0                 last
 *   ----------|------------------|----------
 * CRTCs
 * a -------
 *        b -------
 *        c --------------------------
 *                 d --------
 *                           e -------
 *                                  f -------
 */
static int exynos_plane_get_size(int start, unsigned length, unsigned last)
{
	int end = start + length;
	int size = 0;

	if (start <= 0) {
		if (end > 0)
			size = min_t(unsigned, end, last);
	} else if (start <= last) {
		size = min_t(unsigned, last - start, length);
	}

	return size;
}

int exynos_plane_mode_set(struct drm_plane *plane, struct drm_crtc *crtc,
			  struct drm_framebuffer *fb, int crtc_x, int crtc_y,
			  unsigned int crtc_w, unsigned int crtc_h,
			  uint32_t src_x, uint32_t src_y,
			  uint32_t src_w, uint32_t src_h)
{
	struct exynos_plane *exynos_plane = to_exynos_plane(plane);
	struct exynos_drm_overlay *overlay = &exynos_plane->overlay;
	unsigned int actual_w;
	unsigned int actual_h;
	int nr;
	int i;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	nr = exynos_drm_fb_get_buf_cnt(fb);
	for (i = 0; i < nr; i++) {
		struct exynos_drm_gem_buf *buffer = exynos_drm_fb_buffer(fb, i);

		if (!buffer) {
			DRM_LOG_KMS("buffer is null\n");
			return -EFAULT;
		}

		overlay->dma_addr[i] = buffer->dma_addr;

		DRM_DEBUG_KMS("buffer: %d, dma_addr = 0x%lx\n",
				i, (unsigned long)overlay->dma_addr[i]);
	}

	actual_w = exynos_plane_get_size(crtc_x, crtc_w, crtc->mode.hdisplay);
	actual_h = exynos_plane_get_size(crtc_y, crtc_h, crtc->mode.vdisplay);

	if (crtc_x < 0) {
		if (actual_w)
			src_x -= crtc_x;
		crtc_x = 0;
	}

	if (crtc_y < 0) {
		if (actual_h)
			src_y -= crtc_y;
		crtc_y = 0;
	}

	/* set drm framebuffer data. */
	overlay->fb_x = src_x;
	overlay->fb_y = src_y;
	overlay->fb_width = fb->width;
	overlay->fb_height = fb->height;
	overlay->src_width = src_w;
	overlay->src_height = src_h;
	overlay->bpp = fb->bits_per_pixel;
	overlay->pitch = fb->pitches[0];
	overlay->pixel_format = fb->pixel_format;

	/* set overlay range to be displayed. */
	overlay->crtc_x = crtc_x;
	overlay->crtc_y = crtc_y;
	overlay->crtc_width = actual_w;
	overlay->crtc_height = actual_h;

	/* set drm mode data. */
	overlay->mode_width = crtc->mode.hdisplay;
	overlay->mode_height = crtc->mode.vdisplay;
	overlay->refresh = crtc->mode.vrefresh;
	overlay->scan_flag = crtc->mode.flags;

	DRM_DEBUG_KMS("overlay : offset_x/y(%d,%d), width/height(%d,%d)",
			overlay->crtc_x, overlay->crtc_y,
			overlay->crtc_width, overlay->crtc_height);

	exynos_drm_fn_encoder(crtc, overlay, exynos_drm_encoder_plane_mode_set);

	return 0;
}

void exynos_plane_commit(struct drm_plane *plane)
{
	struct exynos_plane *exynos_plane = to_exynos_plane(plane);
	struct exynos_drm_overlay *overlay = &exynos_plane->overlay;

	exynos_drm_fn_encoder(plane->crtc, &overlay->zpos,
			exynos_drm_encoder_plane_commit);
}

void exynos_plane_dpms(struct drm_plane *plane, int mode)
{
	struct exynos_plane *exynos_plane = to_exynos_plane(plane);
	struct exynos_drm_overlay *overlay = &exynos_plane->overlay;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (mode == DRM_MODE_DPMS_ON) {
		if (exynos_plane->enabled)
			return;

		exynos_drm_fn_encoder(plane->crtc, &overlay->zpos,
				exynos_drm_encoder_plane_enable);

		exynos_plane->enabled = true;
	} else {
		if (!exynos_plane->enabled)
			return;

		exynos_drm_fn_encoder(plane->crtc, &overlay->zpos,
				exynos_drm_encoder_plane_disable);

		exynos_plane->enabled = false;
	}
}

static int
exynos_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		     struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		     unsigned int crtc_w, unsigned int crtc_h,
		     uint32_t src_x, uint32_t src_y,
		     uint32_t src_w, uint32_t src_h)
{
	int ret;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	ret = exynos_plane_mode_set(plane, crtc, fb, crtc_x, crtc_y,
			crtc_w, crtc_h, src_x >> 16, src_y >> 16,
			src_w >> 16, src_h >> 16);
	if (ret < 0)
		return ret;

	plane->crtc = crtc;

	exynos_plane_commit(plane);
	exynos_plane_dpms(plane, DRM_MODE_DPMS_ON);

	return 0;
}

static int exynos_disable_plane(struct drm_plane *plane)
{
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	exynos_plane_dpms(plane, DRM_MODE_DPMS_OFF);

	return 0;
}

static void exynos_plane_destroy(struct drm_plane *plane)
{
	struct exynos_plane *exynos_plane = to_exynos_plane(plane);

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	exynos_disable_plane(plane);
	drm_plane_cleanup(plane);
	kfree(exynos_plane);
}

static int exynos_plane_set_property(struct drm_plane *plane,
				     struct drm_property *property,
				     uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct exynos_plane *exynos_plane = to_exynos_plane(plane);
	struct exynos_drm_private *dev_priv = dev->dev_private;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (property == dev_priv->plane_zpos_property) {
		exynos_plane->overlay.zpos = val;
		return 0;
	}

	return -EINVAL;
}

static struct drm_plane_funcs exynos_plane_funcs = {
	.update_plane	= exynos_update_plane,
	.disable_plane	= exynos_disable_plane,
	.destroy	= exynos_plane_destroy,
	.set_property	= exynos_plane_set_property,
};

static void exynos_plane_attach_zpos_property(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct exynos_drm_private *dev_priv = dev->dev_private;
	struct drm_property *prop;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	prop = dev_priv->plane_zpos_property;
	if (!prop) {
		prop = drm_property_create_range(dev, 0, "zpos", 0,
						 MAX_PLANE - 1);
		if (!prop)
			return;

		dev_priv->plane_zpos_property = prop;
	}

	drm_object_attach_property(&plane->base, prop, 0);
}

struct drm_plane *exynos_plane_init(struct drm_device *dev,
				    unsigned int possible_crtcs, bool priv)
{
	struct exynos_plane *exynos_plane;
	int err;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	exynos_plane = kzalloc(sizeof(struct exynos_plane), GFP_KERNEL);
	if (!exynos_plane) {
		DRM_ERROR("failed to allocate plane\n");
		return NULL;
	}

	err = drm_plane_init(dev, &exynos_plane->base, possible_crtcs,
			      &exynos_plane_funcs, formats, ARRAY_SIZE(formats),
			      priv);
	if (err) {
		DRM_ERROR("failed to initialize plane\n");
		kfree(exynos_plane);
		return NULL;
	}

	if (priv)
		exynos_plane->overlay.zpos = DEFAULT_ZPOS;
	else
		exynos_plane_attach_zpos_property(&exynos_plane->base);

	return &exynos_plane->base;
}
