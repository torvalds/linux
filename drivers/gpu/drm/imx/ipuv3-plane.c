/*
 * i.MX IPUv3 DP Overlay Planes
 *
 * Copyright (C) 2013 Philipp Zabel, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <drm/drmP.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "video/imx-ipu-v3.h"
#include "ipuv3-plane.h"

#define to_ipu_plane(x)	container_of(x, struct ipu_plane, base)

static const uint32_t ipu_plane_formats[] = {
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ABGR1555,
	DRM_FORMAT_XBGR1555,
	DRM_FORMAT_RGBA5551,
	DRM_FORMAT_BGRA5551,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ABGR8888,
	DRM_FORMAT_XBGR8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_BGRA8888,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_VYUY,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_YVYU,
	DRM_FORMAT_YUV420,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_RGB565,
};

int ipu_plane_irq(struct ipu_plane *ipu_plane)
{
	return ipu_idmac_channel_irq(ipu_plane->ipu, ipu_plane->ipu_ch,
				     IPU_IRQ_EOF);
}

static int calc_vref(struct drm_display_mode *mode)
{
	unsigned long htotal, vtotal;

	htotal = mode->htotal;
	vtotal = mode->vtotal;

	if (!htotal || !vtotal)
		return 60;

	return DIV_ROUND_UP(mode->clock * 1000, vtotal * htotal);
}

static inline int calc_bandwidth(int width, int height, unsigned int vref)
{
	return width * height * vref;
}

int ipu_plane_set_base(struct ipu_plane *ipu_plane, struct drm_framebuffer *fb,
		       int x, int y)
{
	struct drm_gem_cma_object *cma_obj[3];
	unsigned long eba, ubo, vbo;
	int active, i;

	for (i = 0; i < drm_format_num_planes(fb->pixel_format); i++) {
		cma_obj[i] = drm_fb_cma_get_gem_obj(fb, i);
		if (!cma_obj[i]) {
			DRM_DEBUG_KMS("plane %d entry is null.\n", i);
			return -EFAULT;
		}
	}

	eba = cma_obj[0]->paddr + fb->offsets[0] +
	      fb->pitches[0] * y + (fb->bits_per_pixel >> 3) * x;

	if (eba & 0x7) {
		DRM_DEBUG_KMS("base address must be a multiple of 8.\n");
		return -EINVAL;
	}

	if (fb->pitches[0] < 1 || fb->pitches[0] > 16384) {
		DRM_DEBUG_KMS("pitches out of range.\n");
		return -EINVAL;
	}

	if (ipu_plane->enabled && fb->pitches[0] != ipu_plane->stride[0]) {
		DRM_DEBUG_KMS("pitches must not change while plane is enabled.\n");
		return -EINVAL;
	}

	ipu_plane->stride[0] = fb->pitches[0];

	switch (fb->pixel_format) {
	case DRM_FORMAT_YUV420:
	case DRM_FORMAT_YVU420:
		/*
		 * Multiplanar formats have to meet the following restrictions:
		 * - The (up to) three plane addresses are EBA, EBA+UBO, EBA+VBO
		 * - EBA, UBO and VBO are a multiple of 8
		 * - UBO and VBO are unsigned and not larger than 0xfffff8
		 * - Only EBA may be changed while scanout is active
		 * - The strides of U and V planes must be identical.
		 */
		ubo = cma_obj[1]->paddr + fb->offsets[1] +
		      fb->pitches[1] * y / 2 + x / 2 - eba;
		vbo = cma_obj[2]->paddr + fb->offsets[2] +
		      fb->pitches[2] * y / 2 + x / 2 - eba;

		if ((ubo & 0x7) || (vbo & 0x7)) {
			DRM_DEBUG_KMS("U/V buffer offsets must be a multiple of 8.\n");
			return -EINVAL;
		}

		if ((ubo > 0xfffff8) || (vbo > 0xfffff8)) {
			DRM_DEBUG_KMS("U/V buffer offsets must be positive and not larger than 0xfffff8.\n");
			return -EINVAL;
		}

		if (ipu_plane->enabled && ((ipu_plane->u_offset != ubo) ||
					   (ipu_plane->v_offset != vbo))) {
			DRM_DEBUG_KMS("U/V buffer offsets must not change while plane is enabled.\n");
			return -EINVAL;
		}

		if (fb->pitches[1] != fb->pitches[2]) {
			DRM_DEBUG_KMS("U/V pitches must be identical.\n");
			return -EINVAL;
		}

		if (fb->pitches[1] < 1 || fb->pitches[1] > 16384) {
			DRM_DEBUG_KMS("U/V pitches out of range.\n");
			return -EINVAL;
		}

		if (ipu_plane->enabled &&
		    (ipu_plane->stride[1] != fb->pitches[1])) {
			DRM_DEBUG_KMS("U/V pitches must not change while plane is enabled.\n");
			return -EINVAL;
		}

		ipu_plane->u_offset = ubo;
		ipu_plane->v_offset = vbo;
		ipu_plane->stride[1] = fb->pitches[1];

		dev_dbg(ipu_plane->base.dev->dev,
			"phys = %pad %pad %pad, x = %d, y = %d",
			&cma_obj[0]->paddr, &cma_obj[1]->paddr,
			&cma_obj[2]->paddr, x, y);
		break;
	default:
		dev_dbg(ipu_plane->base.dev->dev, "phys = %pad, x = %d, y = %d",
			&cma_obj[0]->paddr, x, y);
		break;
	}

	if (ipu_plane->enabled) {
		active = ipu_idmac_get_current_buffer(ipu_plane->ipu_ch);
		ipu_cpmem_set_buffer(ipu_plane->ipu_ch, !active, eba);
		ipu_idmac_select_buffer(ipu_plane->ipu_ch, !active);
	} else {
		ipu_cpmem_set_buffer(ipu_plane->ipu_ch, 0, eba);
		ipu_cpmem_set_buffer(ipu_plane->ipu_ch, 1, eba);
	}

	/* cache offsets for subsequent pageflips */
	ipu_plane->x = x;
	ipu_plane->y = y;

	return 0;
}

int ipu_plane_mode_set(struct ipu_plane *ipu_plane, struct drm_crtc *crtc,
		       struct drm_display_mode *mode,
		       struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		       unsigned int crtc_w, unsigned int crtc_h,
		       uint32_t src_x, uint32_t src_y,
		       uint32_t src_w, uint32_t src_h, bool interlaced)
{
	struct device *dev = ipu_plane->base.dev->dev;
	int ret;

	/* no scaling */
	if (src_w != crtc_w || src_h != crtc_h)
		return -EINVAL;

	/* clip to crtc bounds */
	if (crtc_x < 0) {
		if (-crtc_x > crtc_w)
			return -EINVAL;
		src_x += -crtc_x;
		src_w -= -crtc_x;
		crtc_w -= -crtc_x;
		crtc_x = 0;
	}
	if (crtc_y < 0) {
		if (-crtc_y > crtc_h)
			return -EINVAL;
		src_y += -crtc_y;
		src_h -= -crtc_y;
		crtc_h -= -crtc_y;
		crtc_y = 0;
	}
	if (crtc_x + crtc_w > mode->hdisplay) {
		if (crtc_x > mode->hdisplay)
			return -EINVAL;
		crtc_w = mode->hdisplay - crtc_x;
		src_w = crtc_w;
	}
	if (crtc_y + crtc_h > mode->vdisplay) {
		if (crtc_y > mode->vdisplay)
			return -EINVAL;
		crtc_h = mode->vdisplay - crtc_y;
		src_h = crtc_h;
	}
	/* full plane minimum width is 13 pixels */
	if (crtc_w < 13 && (ipu_plane->dp_flow != IPU_DP_FLOW_SYNC_FG))
		return -EINVAL;
	if (crtc_h < 2)
		return -EINVAL;

	/*
	 * since we cannot touch active IDMAC channels, we do not support
	 * resizing the enabled plane or changing its format
	 */
	if (ipu_plane->enabled) {
		if (src_w != ipu_plane->w || src_h != ipu_plane->h ||
		    fb->pixel_format != ipu_plane->base.fb->pixel_format)
			return -EINVAL;

		return ipu_plane_set_base(ipu_plane, fb, src_x, src_y);
	}

	switch (ipu_plane->dp_flow) {
	case IPU_DP_FLOW_SYNC_BG:
		ret = ipu_dp_setup_channel(ipu_plane->dp,
				IPUV3_COLORSPACE_RGB,
				IPUV3_COLORSPACE_RGB);
		if (ret) {
			dev_err(dev,
				"initializing display processor failed with %d\n",
				ret);
			return ret;
		}
		ipu_dp_set_global_alpha(ipu_plane->dp, true, 0, true);
		break;
	case IPU_DP_FLOW_SYNC_FG:
		ipu_dp_setup_channel(ipu_plane->dp,
				ipu_drm_fourcc_to_colorspace(fb->pixel_format),
				IPUV3_COLORSPACE_UNKNOWN);
		ipu_dp_set_window_pos(ipu_plane->dp, crtc_x, crtc_y);
		/* Enable local alpha on partial plane */
		switch (fb->pixel_format) {
		case DRM_FORMAT_ARGB1555:
		case DRM_FORMAT_ABGR1555:
		case DRM_FORMAT_RGBA5551:
		case DRM_FORMAT_BGRA5551:
		case DRM_FORMAT_ARGB4444:
		case DRM_FORMAT_ARGB8888:
		case DRM_FORMAT_ABGR8888:
		case DRM_FORMAT_RGBA8888:
		case DRM_FORMAT_BGRA8888:
			ipu_dp_set_global_alpha(ipu_plane->dp, false, 0, false);
			break;
		default:
			break;
		}
	}

	ret = ipu_dmfc_alloc_bandwidth(ipu_plane->dmfc,
			calc_bandwidth(crtc_w, crtc_h,
				       calc_vref(mode)), 64);
	if (ret) {
		dev_err(dev, "allocating dmfc bandwidth failed with %d\n", ret);
		return ret;
	}

	ipu_dmfc_config_wait4eot(ipu_plane->dmfc, crtc_w);

	ipu_cpmem_zero(ipu_plane->ipu_ch);
	ipu_cpmem_set_resolution(ipu_plane->ipu_ch, src_w, src_h);
	ret = ipu_cpmem_set_fmt(ipu_plane->ipu_ch, fb->pixel_format);
	if (ret < 0) {
		dev_err(dev, "unsupported pixel format 0x%08x\n",
			fb->pixel_format);
		return ret;
	}
	ipu_cpmem_set_high_priority(ipu_plane->ipu_ch);
	ipu_idmac_set_double_buffer(ipu_plane->ipu_ch, 1);
	ipu_cpmem_set_stride(ipu_plane->ipu_ch, fb->pitches[0]);

	ret = ipu_plane_set_base(ipu_plane, fb, src_x, src_y);
	if (ret < 0)
		return ret;
	if (interlaced)
		ipu_cpmem_interlaced_scan(ipu_plane->ipu_ch, fb->pitches[0]);

	if (fb->pixel_format == DRM_FORMAT_YUV420) {
		ipu_cpmem_set_yuv_planar_full(ipu_plane->ipu_ch,
					      ipu_plane->stride[1],
					      ipu_plane->u_offset,
					      ipu_plane->v_offset);
	} else if (fb->pixel_format == DRM_FORMAT_YVU420) {
		ipu_cpmem_set_yuv_planar_full(ipu_plane->ipu_ch,
					      ipu_plane->stride[1],
					      ipu_plane->v_offset,
					      ipu_plane->u_offset);
	}

	ipu_plane->w = src_w;
	ipu_plane->h = src_h;

	return 0;
}

void ipu_plane_put_resources(struct ipu_plane *ipu_plane)
{
	if (!IS_ERR_OR_NULL(ipu_plane->dp))
		ipu_dp_put(ipu_plane->dp);
	if (!IS_ERR_OR_NULL(ipu_plane->dmfc))
		ipu_dmfc_put(ipu_plane->dmfc);
	if (!IS_ERR_OR_NULL(ipu_plane->ipu_ch))
		ipu_idmac_put(ipu_plane->ipu_ch);
}

int ipu_plane_get_resources(struct ipu_plane *ipu_plane)
{
	int ret;

	ipu_plane->ipu_ch = ipu_idmac_get(ipu_plane->ipu, ipu_plane->dma);
	if (IS_ERR(ipu_plane->ipu_ch)) {
		ret = PTR_ERR(ipu_plane->ipu_ch);
		DRM_ERROR("failed to get idmac channel: %d\n", ret);
		return ret;
	}

	ipu_plane->dmfc = ipu_dmfc_get(ipu_plane->ipu, ipu_plane->dma);
	if (IS_ERR(ipu_plane->dmfc)) {
		ret = PTR_ERR(ipu_plane->dmfc);
		DRM_ERROR("failed to get dmfc: ret %d\n", ret);
		goto err_out;
	}

	if (ipu_plane->dp_flow >= 0) {
		ipu_plane->dp = ipu_dp_get(ipu_plane->ipu, ipu_plane->dp_flow);
		if (IS_ERR(ipu_plane->dp)) {
			ret = PTR_ERR(ipu_plane->dp);
			DRM_ERROR("failed to get dp flow: %d\n", ret);
			goto err_out;
		}
	}

	return 0;
err_out:
	ipu_plane_put_resources(ipu_plane);

	return ret;
}

void ipu_plane_enable(struct ipu_plane *ipu_plane)
{
	if (ipu_plane->dp)
		ipu_dp_enable(ipu_plane->ipu);
	ipu_dmfc_enable_channel(ipu_plane->dmfc);
	ipu_idmac_enable_channel(ipu_plane->ipu_ch);
	if (ipu_plane->dp)
		ipu_dp_enable_channel(ipu_plane->dp);

	ipu_plane->enabled = true;
}

void ipu_plane_disable(struct ipu_plane *ipu_plane)
{
	ipu_plane->enabled = false;

	ipu_idmac_wait_busy(ipu_plane->ipu_ch, 50);

	if (ipu_plane->dp)
		ipu_dp_disable_channel(ipu_plane->dp);
	ipu_idmac_disable_channel(ipu_plane->ipu_ch);
	ipu_dmfc_disable_channel(ipu_plane->dmfc);
	if (ipu_plane->dp)
		ipu_dp_disable(ipu_plane->ipu);
}

/*
 * drm_plane API
 */

static int ipu_update_plane(struct drm_plane *plane, struct drm_crtc *crtc,
			    struct drm_framebuffer *fb, int crtc_x, int crtc_y,
			    unsigned int crtc_w, unsigned int crtc_h,
			    uint32_t src_x, uint32_t src_y,
			    uint32_t src_w, uint32_t src_h)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);
	int ret = 0;

	DRM_DEBUG_KMS("plane - %p\n", plane);

	if (!ipu_plane->enabled)
		ret = ipu_plane_get_resources(ipu_plane);
	if (ret < 0)
		return ret;

	ret = ipu_plane_mode_set(ipu_plane, crtc, &crtc->hwmode, fb,
			crtc_x, crtc_y, crtc_w, crtc_h,
			src_x >> 16, src_y >> 16, src_w >> 16, src_h >> 16,
			false);
	if (ret < 0) {
		ipu_plane_put_resources(ipu_plane);
		return ret;
	}

	if (crtc != plane->crtc)
		dev_dbg(plane->dev->dev, "crtc change: %p -> %p\n",
				plane->crtc, crtc);
	plane->crtc = crtc;

	if (!ipu_plane->enabled)
		ipu_plane_enable(ipu_plane);

	return 0;
}

static int ipu_disable_plane(struct drm_plane *plane)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	if (ipu_plane->enabled)
		ipu_plane_disable(ipu_plane);

	ipu_plane_put_resources(ipu_plane);

	return 0;
}

static void ipu_plane_destroy(struct drm_plane *plane)
{
	struct ipu_plane *ipu_plane = to_ipu_plane(plane);

	DRM_DEBUG_KMS("[%d] %s\n", __LINE__, __func__);

	ipu_disable_plane(plane);
	drm_plane_cleanup(plane);
	kfree(ipu_plane);
}

static const struct drm_plane_funcs ipu_plane_funcs = {
	.update_plane	= ipu_update_plane,
	.disable_plane	= ipu_disable_plane,
	.destroy	= ipu_plane_destroy,
};

struct ipu_plane *ipu_plane_init(struct drm_device *dev, struct ipu_soc *ipu,
				 int dma, int dp, unsigned int possible_crtcs,
				 enum drm_plane_type type)
{
	struct ipu_plane *ipu_plane;
	int ret;

	DRM_DEBUG_KMS("channel %d, dp flow %d, possible_crtcs=0x%x\n",
		      dma, dp, possible_crtcs);

	ipu_plane = kzalloc(sizeof(*ipu_plane), GFP_KERNEL);
	if (!ipu_plane) {
		DRM_ERROR("failed to allocate plane\n");
		return ERR_PTR(-ENOMEM);
	}

	ipu_plane->ipu = ipu;
	ipu_plane->dma = dma;
	ipu_plane->dp_flow = dp;

	ret = drm_universal_plane_init(dev, &ipu_plane->base, possible_crtcs,
				       &ipu_plane_funcs, ipu_plane_formats,
				       ARRAY_SIZE(ipu_plane_formats), type,
				       NULL);
	if (ret) {
		DRM_ERROR("failed to initialize plane\n");
		kfree(ipu_plane);
		return ERR_PTR(ret);
	}

	return ipu_plane;
}
