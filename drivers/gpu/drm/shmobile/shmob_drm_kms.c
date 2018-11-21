/*
 * shmob_drm_kms.c  --  SH Mobile DRM Mode Setting
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>

#include "shmob_drm_crtc.h"
#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_regs.h"

/* -----------------------------------------------------------------------------
 * Format helpers
 */

static const struct shmob_drm_format_info shmob_drm_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_RGB565,
		.bpp = 16,
		.yuv = false,
		.lddfr = LDDFR_PKF_RGB16,
	}, {
		.fourcc = DRM_FORMAT_RGB888,
		.bpp = 24,
		.yuv = false,
		.lddfr = LDDFR_PKF_RGB24,
	}, {
		.fourcc = DRM_FORMAT_ARGB8888,
		.bpp = 32,
		.yuv = false,
		.lddfr = LDDFR_PKF_ARGB32,
	}, {
		.fourcc = DRM_FORMAT_NV12,
		.bpp = 12,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_420,
	}, {
		.fourcc = DRM_FORMAT_NV21,
		.bpp = 12,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_420,
	}, {
		.fourcc = DRM_FORMAT_NV16,
		.bpp = 16,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_422,
	}, {
		.fourcc = DRM_FORMAT_NV61,
		.bpp = 16,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_422,
	}, {
		.fourcc = DRM_FORMAT_NV24,
		.bpp = 24,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_444,
	}, {
		.fourcc = DRM_FORMAT_NV42,
		.bpp = 24,
		.yuv = true,
		.lddfr = LDDFR_CC | LDDFR_YF_444,
	},
};

const struct shmob_drm_format_info *shmob_drm_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(shmob_drm_format_infos); ++i) {
		if (shmob_drm_format_infos[i].fourcc == fourcc)
			return &shmob_drm_format_infos[i];
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Frame buffer
 */

static struct drm_framebuffer *
shmob_drm_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		    const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct shmob_drm_format_info *format;

	format = shmob_drm_format_info(mode_cmd->pixel_format);
	if (format == NULL) {
		dev_dbg(dev->dev, "unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	if (mode_cmd->pitches[0] & 7 || mode_cmd->pitches[0] >= 65536) {
		dev_dbg(dev->dev, "invalid pitch value %u\n",
			mode_cmd->pitches[0]);
		return ERR_PTR(-EINVAL);
	}

	if (format->yuv) {
		unsigned int chroma_cpp = format->bpp == 24 ? 2 : 1;

		if (mode_cmd->pitches[1] != mode_cmd->pitches[0] * chroma_cpp) {
			dev_dbg(dev->dev,
				"luma and chroma pitches do not match\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return drm_gem_fb_create(dev, file_priv, mode_cmd);
}

static const struct drm_mode_config_funcs shmob_drm_mode_config_funcs = {
	.fb_create = shmob_drm_fb_create,
};

int shmob_drm_modeset_init(struct shmob_drm_device *sdev)
{
	drm_mode_config_init(sdev->ddev);

	shmob_drm_crtc_create(sdev);
	shmob_drm_encoder_create(sdev);
	shmob_drm_connector_create(sdev, &sdev->encoder.encoder);

	drm_kms_helper_poll_init(sdev->ddev);

	sdev->ddev->mode_config.min_width = 0;
	sdev->ddev->mode_config.min_height = 0;
	sdev->ddev->mode_config.max_width = 4095;
	sdev->ddev->mode_config.max_height = 4095;
	sdev->ddev->mode_config.funcs = &shmob_drm_mode_config_funcs;

	drm_helper_disable_unused_functions(sdev->ddev);

	return 0;
}
