// SPDX-License-Identifier: GPL-2.0+
/*
 * shmob_drm_plane.c  --  SH Mobile DRM Planes
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_plane.h"
#include "shmob_drm_regs.h"

struct shmob_drm_plane {
	struct drm_plane plane;
	unsigned int index;
	unsigned int alpha;

	const struct shmob_drm_format_info *format;
	unsigned long dma[2];

	unsigned int src_x;
	unsigned int src_y;
	unsigned int crtc_x;
	unsigned int crtc_y;
	unsigned int crtc_w;
	unsigned int crtc_h;
};

#define to_shmob_plane(p)	container_of(p, struct shmob_drm_plane, plane)

static void shmob_drm_plane_compute_base(struct shmob_drm_plane *splane,
					 struct drm_framebuffer *fb,
					 int x, int y)
{
	struct drm_gem_cma_object *gem;
	unsigned int bpp;

	bpp = splane->format->yuv ? 8 : splane->format->bpp;
	gem = drm_fb_cma_get_gem_obj(fb, 0);
	splane->dma[0] = gem->paddr + fb->offsets[0]
		       + y * fb->pitches[0] + x * bpp / 8;

	if (splane->format->yuv) {
		bpp = splane->format->bpp - 8;
		gem = drm_fb_cma_get_gem_obj(fb, 1);
		splane->dma[1] = gem->paddr + fb->offsets[1]
			       + y / (bpp == 4 ? 2 : 1) * fb->pitches[1]
			       + x * (bpp == 16 ? 2 : 1);
	}
}

static void __shmob_drm_plane_setup(struct shmob_drm_plane *splane,
				    struct drm_framebuffer *fb)
{
	struct shmob_drm_device *sdev = splane->plane.dev->dev_private;
	u32 format;

	/* TODO: Support ROP3 mode */
	format = LDBBSIFR_EN | (splane->alpha << LDBBSIFR_LAY_SHIFT);

	switch (splane->format->fourcc) {
	case DRM_FORMAT_RGB565:
	case DRM_FORMAT_NV21:
	case DRM_FORMAT_NV61:
	case DRM_FORMAT_NV42:
		format |= LDBBSIFR_SWPL | LDBBSIFR_SWPW;
		break;
	case DRM_FORMAT_RGB888:
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV24:
		format |= LDBBSIFR_SWPL | LDBBSIFR_SWPW | LDBBSIFR_SWPB;
		break;
	case DRM_FORMAT_ARGB8888:
	default:
		format |= LDBBSIFR_SWPL;
		break;
	}

	switch (splane->format->fourcc) {
	case DRM_FORMAT_RGB565:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_RY | LDBBSIFR_RPKF_RGB16;
		break;
	case DRM_FORMAT_RGB888:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_RY | LDBBSIFR_RPKF_RGB24;
		break;
	case DRM_FORMAT_ARGB8888:
		format |= LDBBSIFR_AL_PK | LDBBSIFR_RY | LDDFR_PKF_ARGB32;
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_CHRR_420;
		break;
	case DRM_FORMAT_NV16:
	case DRM_FORMAT_NV61:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_CHRR_422;
		break;
	case DRM_FORMAT_NV24:
	case DRM_FORMAT_NV42:
		format |= LDBBSIFR_AL_1 | LDBBSIFR_CHRR_444;
		break;
	}

#define plane_reg_dump(sdev, splane, reg) \
	dev_dbg(sdev->ddev->dev, "%s(%u): %s 0x%08x 0x%08x\n", __func__, \
		splane->index, #reg, \
		lcdc_read(sdev, reg(splane->index)), \
		lcdc_read(sdev, reg(splane->index) + LCDC_SIDE_B_OFFSET))

	plane_reg_dump(sdev, splane, LDBnBSIFR);
	plane_reg_dump(sdev, splane, LDBnBSSZR);
	plane_reg_dump(sdev, splane, LDBnBLOCR);
	plane_reg_dump(sdev, splane, LDBnBSMWR);
	plane_reg_dump(sdev, splane, LDBnBSAYR);
	plane_reg_dump(sdev, splane, LDBnBSACR);

	lcdc_write(sdev, LDBCR, LDBCR_UPC(splane->index));
	dev_dbg(sdev->ddev->dev, "%s(%u): %s 0x%08x\n", __func__, splane->index,
		"LDBCR", lcdc_read(sdev, LDBCR));

	lcdc_write(sdev, LDBnBSIFR(splane->index), format);

	lcdc_write(sdev, LDBnBSSZR(splane->index),
		   (splane->crtc_h << LDBBSSZR_BVSS_SHIFT) |
		   (splane->crtc_w << LDBBSSZR_BHSS_SHIFT));
	lcdc_write(sdev, LDBnBLOCR(splane->index),
		   (splane->crtc_y << LDBBLOCR_CVLC_SHIFT) |
		   (splane->crtc_x << LDBBLOCR_CHLC_SHIFT));
	lcdc_write(sdev, LDBnBSMWR(splane->index),
		   fb->pitches[0] << LDBBSMWR_BSMW_SHIFT);

	shmob_drm_plane_compute_base(splane, fb, splane->src_x, splane->src_y);

	lcdc_write(sdev, LDBnBSAYR(splane->index), splane->dma[0]);
	if (splane->format->yuv)
		lcdc_write(sdev, LDBnBSACR(splane->index), splane->dma[1]);

	lcdc_write(sdev, LDBCR,
		   LDBCR_UPF(splane->index) | LDBCR_UPD(splane->index));
	dev_dbg(sdev->ddev->dev, "%s(%u): %s 0x%08x\n", __func__, splane->index,
		"LDBCR", lcdc_read(sdev, LDBCR));

	plane_reg_dump(sdev, splane, LDBnBSIFR);
	plane_reg_dump(sdev, splane, LDBnBSSZR);
	plane_reg_dump(sdev, splane, LDBnBLOCR);
	plane_reg_dump(sdev, splane, LDBnBSMWR);
	plane_reg_dump(sdev, splane, LDBnBSAYR);
	plane_reg_dump(sdev, splane, LDBnBSACR);
}

void shmob_drm_plane_setup(struct drm_plane *plane)
{
	struct shmob_drm_plane *splane = to_shmob_plane(plane);

	if (plane->fb == NULL)
		return;

	__shmob_drm_plane_setup(splane, plane->fb);
}

static int
shmob_drm_plane_update(struct drm_plane *plane, struct drm_crtc *crtc,
		       struct drm_framebuffer *fb, int crtc_x, int crtc_y,
		       unsigned int crtc_w, unsigned int crtc_h,
		       uint32_t src_x, uint32_t src_y,
		       uint32_t src_w, uint32_t src_h,
		       struct drm_modeset_acquire_ctx *ctx)
{
	struct shmob_drm_plane *splane = to_shmob_plane(plane);
	struct shmob_drm_device *sdev = plane->dev->dev_private;
	const struct shmob_drm_format_info *format;

	format = shmob_drm_format_info(fb->format->format);
	if (format == NULL) {
		dev_dbg(sdev->dev, "update_plane: unsupported format %08x\n",
			fb->format->format);
		return -EINVAL;
	}

	if (src_w >> 16 != crtc_w || src_h >> 16 != crtc_h) {
		dev_dbg(sdev->dev, "%s: scaling not supported\n", __func__);
		return -EINVAL;
	}

	splane->format = format;

	splane->src_x = src_x >> 16;
	splane->src_y = src_y >> 16;
	splane->crtc_x = crtc_x;
	splane->crtc_y = crtc_y;
	splane->crtc_w = crtc_w;
	splane->crtc_h = crtc_h;

	__shmob_drm_plane_setup(splane, fb);
	return 0;
}

static int shmob_drm_plane_disable(struct drm_plane *plane,
				   struct drm_modeset_acquire_ctx *ctx)
{
	struct shmob_drm_plane *splane = to_shmob_plane(plane);
	struct shmob_drm_device *sdev = plane->dev->dev_private;

	splane->format = NULL;

	lcdc_write(sdev, LDBnBSIFR(splane->index), 0);
	return 0;
}

static void shmob_drm_plane_destroy(struct drm_plane *plane)
{
	drm_plane_force_disable(plane);
	drm_plane_cleanup(plane);
}

static const struct drm_plane_funcs shmob_drm_plane_funcs = {
	.update_plane = shmob_drm_plane_update,
	.disable_plane = shmob_drm_plane_disable,
	.destroy = shmob_drm_plane_destroy,
};

static const uint32_t formats[] = {
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGB888,
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV21,
	DRM_FORMAT_NV16,
	DRM_FORMAT_NV61,
	DRM_FORMAT_NV24,
	DRM_FORMAT_NV42,
};

int shmob_drm_plane_create(struct shmob_drm_device *sdev, unsigned int index)
{
	struct shmob_drm_plane *splane;
	int ret;

	splane = devm_kzalloc(sdev->dev, sizeof(*splane), GFP_KERNEL);
	if (splane == NULL)
		return -ENOMEM;

	splane->index = index;
	splane->alpha = 255;

	ret = drm_plane_init(sdev->ddev, &splane->plane, 1,
			     &shmob_drm_plane_funcs, formats,
			     ARRAY_SIZE(formats), false);

	return ret;
}
