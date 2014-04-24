/*
 * Copyright (C) ROCKCHIP, Inc.
 * Author:yzq<yzq@rock-chips.com>
 *
 * based on exynos_drm_plane.c
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>

#include <drm/rockchip_drm.h>
#include "rockchip_drm_drv.h"
#include "rockchip_drm_encoder.h"
#include "rockchip_drm_fb.h"
#include "rockchip_drm_gem.h"

#define to_rockchip_plane(x)	container_of(x, struct rockchip_plane, base)

struct rockchip_plane {
	struct drm_plane		base;
	struct rockchip_drm_overlay	overlay;
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
static int rockchip_plane_get_size(int start, unsigned length, unsigned last)
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

int rockchip_plane_mode_set(struct drm_plane *plane, struct drm_crtc *crtc,
			  struct drm_framebuffer *fb, int crtc_x, int crtc_y,
			  unsigned int crtc_w, unsigned int crtc_h,
			  uint32_t src_x, uint32_t src_y,
			  uint32_t src_w, uint32_t src_h)
{
	struct rockchip_plane *rockchip_plane = to_rockchip_plane(plane);
	struct rockchip_drm_overlay *overlay = &rockchip_plane->overlay;
	unsigned int actual_w;
	unsigned int actual_h;
	int nr;
	int i;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	nr = rockchip_drm_fb_get_buf_cnt(fb);
	for (i = 0; i < nr; i++) {
		struct rockchip_drm_gem_buf *buffer = rockchip_drm_fb_buffer(fb, i);

		if (!buffer) {
			DRM_LOG_KMS("buffer is null\n");
			return -EFAULT;
		}

		overlay->dma_addr[i] = buffer->dma_addr;

		DRM_DEBUG_KMS("buffer: %d, dma_addr = 0x%lx\n",
				i, (unsigned long)overlay->dma_addr[i]);
	}

	actual_w = rockchip_plane_get_size(crtc_x, crtc_w, crtc->mode.hdisplay);
	actual_h = rockchip_plane_get_size(crtc_y, crtc_h, crtc->mode.vdisplay);

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
	overlay->pixclock = crtc->mode.clock*1000;
	overlay->scan_flag = crtc->mode.flags;

//	printk("--->yzq %s crtc->mode->refresh =%d \n",__func__,crtc->mode.vrefresh);
	DRM_DEBUG_KMS("overlay : offset_x/y(%d,%d), width/height(%d,%d)",
			overlay->crtc_x, overlay->crtc_y,
			overlay->crtc_width, overlay->crtc_height);

	rockchip_drm_fn_encoder(crtc, overlay, rockchip_drm_encoder_plane_mode_set);

	return 0;
}

void rockchip_plane_commit(struct drm_plane *plane)
{
	struct rockchip_plane *rockchip_plane = to_rockchip_plane(plane);
	struct rockchip_drm_overlay *overlay = &rockchip_plane->overlay;

	rockchip_drm_fn_encoder(plane->crtc, &overlay->zpos,
			rockchip_drm_encoder_plane_commit);
}

void rockchip_plane_dpms(struct drm_plane *plane, int mode)
{
	struct rockchip_plane *rockchip_plane = to_rockchip_plane(plane);
	struct rockchip_drm_overlay *overlay = &rockchip_plane->overlay;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (mode == DRM_MODE_DPMS_ON) {
		if (rockchip_plane->enabled)
			return;

		rockchip_drm_fn_encoder(plane->crtc, &overlay->zpos,
				rockchip_drm_encoder_plane_enable);

		rockchip_plane->enabled = true;
	} else {
		if (!rockchip_plane->enabled)
			return;

		rockchip_drm_fn_encoder(plane->crtc, &overlay->zpos,
				rockchip_drm_encoder_plane_disable);

		rockchip_plane->enabled = false;
	}
}

static int
rockchip_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
		     struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		     unsigned int crtc_w, unsigned int crtc_h,
		     uint32_t src_x, uint32_t src_y,
		     uint32_t src_w, uint32_t src_h)
{
	int ret;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	ret = rockchip_plane_mode_set(plane, crtc, fb, crtc_x, crtc_y,
			crtc_w, crtc_h, src_x >> 16, src_y >> 16,
			src_w >> 16, src_h >> 16);
	if (ret < 0)
		return ret;

	plane->crtc = crtc;

	rockchip_plane_commit(plane);
	rockchip_plane_dpms(plane, DRM_MODE_DPMS_ON);

	return 0;
}

static int rockchip_disable_plane(struct drm_plane *plane)
{
	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	rockchip_plane_dpms(plane, DRM_MODE_DPMS_OFF);

	return 0;
}

static void rockchip_plane_destroy(struct drm_plane *plane)
{
	struct rockchip_plane *rockchip_plane = to_rockchip_plane(plane);

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	rockchip_disable_plane(plane);
	drm_plane_cleanup(plane);
	kfree(rockchip_plane);
}

static int rockchip_plane_set_property(struct drm_plane *plane,
				     struct drm_property *property,
				     uint64_t val)
{
	struct drm_device *dev = plane->dev;
	struct rockchip_plane *rockchip_plane = to_rockchip_plane(plane);
	struct rockchip_drm_private *dev_priv = dev->dev_private;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (property == dev_priv->plane_zpos_property) {
		rockchip_plane->overlay.zpos = val;
		return 0;
	}

	return -EINVAL;
}

static struct drm_plane_funcs rockchip_plane_funcs = {
	.update_plane	= rockchip_update_plane,
	.disable_plane	= rockchip_disable_plane,
	.destroy	= rockchip_plane_destroy,
	.set_property	= rockchip_plane_set_property,
};

static void rockchip_plane_attach_zpos_property(struct drm_plane *plane)
{
	struct drm_device *dev = plane->dev;
	struct rockchip_drm_private *dev_priv = dev->dev_private;
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

struct drm_plane *rockchip_plane_init(struct drm_device *dev,
				    unsigned int possible_crtcs, bool priv)
{
	struct rockchip_plane *rockchip_plane;
	int err;

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	rockchip_plane = kzalloc(sizeof(struct rockchip_plane), GFP_KERNEL);
	if (!rockchip_plane) {
		DRM_ERROR("failed to allocate plane\n");
		return NULL;
	}

	err = drm_plane_init(dev, &rockchip_plane->base, possible_crtcs,
			      &rockchip_plane_funcs, formats, ARRAY_SIZE(formats),
			      priv);
	if (err) {
		DRM_ERROR("failed to initialize plane\n");
		kfree(rockchip_plane);
		return NULL;
	}

	if (priv)
		rockchip_plane->overlay.zpos = DEFAULT_ZPOS;
	else
		rockchip_plane_attach_zpos_property(&rockchip_plane->base);

	return &rockchip_plane->base;
}
