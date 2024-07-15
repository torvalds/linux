// SPDX-License-Identifier: GPL-2.0+
/*
 * shmob_drm_kms.c  --  SH Mobile DRM Mode Setting
 *
 * Copyright (C) 2012 Renesas Electronics Corporation
 *
 * Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_probe_helper.h>

#include "shmob_drm_crtc.h"
#include "shmob_drm_drv.h"
#include "shmob_drm_kms.h"
#include "shmob_drm_plane.h"
#include "shmob_drm_regs.h"

/* -----------------------------------------------------------------------------
 * Format helpers
 */

static const struct shmob_drm_format_info shmob_drm_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_RGB565,
		.bpp = 16,
		.lddfr = LDDFR_PKF_RGB16,
		.ldddsr = LDDDSR_LS | LDDDSR_WS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_RY | LDBBSIFR_RPKF_RGB16,
	}, {
		.fourcc = DRM_FORMAT_RGB888,
		.bpp = 24,
		.lddfr = LDDFR_PKF_RGB24,
		.ldddsr = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_SWPB | LDBBSIFR_RY | LDBBSIFR_RPKF_RGB24,
	}, {
		.fourcc = DRM_FORMAT_ARGB8888,
		.bpp = 32,
		.lddfr = LDDFR_PKF_ARGB32,
		.ldddsr = LDDDSR_LS,
		.ldbbsifr = LDBBSIFR_AL_PK | LDBBSIFR_SWPL | LDBBSIFR_RY |
			    LDBBSIFR_RPKF_ARGB32,
	}, {
		.fourcc = DRM_FORMAT_XRGB8888,
		.bpp = 32,
		.lddfr = LDDFR_PKF_ARGB32,
		.ldddsr = LDDDSR_LS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_RY |
			    LDBBSIFR_RPKF_ARGB32,
	}, {
		.fourcc = DRM_FORMAT_NV12,
		.bpp = 12,
		.lddfr = LDDFR_CC | LDDFR_YF_420,
		.ldddsr = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_SWPB | LDBBSIFR_CHRR_420,
	}, {
		.fourcc = DRM_FORMAT_NV21,
		.bpp = 12,
		.lddfr = LDDFR_CC | LDDFR_YF_420,
		.ldddsr = LDDDSR_LS | LDDDSR_WS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_CHRR_420,
	}, {
		.fourcc = DRM_FORMAT_NV16,
		.bpp = 16,
		.lddfr = LDDFR_CC | LDDFR_YF_422,
		.ldddsr = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_SWPB | LDBBSIFR_CHRR_422,
	}, {
		.fourcc = DRM_FORMAT_NV61,
		.bpp = 16,
		.lddfr = LDDFR_CC | LDDFR_YF_422,
		.ldddsr = LDDDSR_LS | LDDDSR_WS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_CHRR_422,
	}, {
		.fourcc = DRM_FORMAT_NV24,
		.bpp = 24,
		.lddfr = LDDFR_CC | LDDFR_YF_444,
		.ldddsr = LDDDSR_LS | LDDDSR_WS | LDDDSR_BS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_SWPB | LDBBSIFR_CHRR_444,
	}, {
		.fourcc = DRM_FORMAT_NV42,
		.bpp = 24,
		.lddfr = LDDFR_CC | LDDFR_YF_444,
		.ldddsr = LDDDSR_LS | LDDDSR_WS,
		.ldbbsifr = LDBBSIFR_AL_1 | LDBBSIFR_SWPL | LDBBSIFR_SWPW |
			    LDBBSIFR_CHRR_444,
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
		dev_dbg(dev->dev, "unsupported pixel format %p4cc\n",
			&mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	if (mode_cmd->pitches[0] & 7 || mode_cmd->pitches[0] >= 65536) {
		dev_dbg(dev->dev, "invalid pitch value %u\n",
			mode_cmd->pitches[0]);
		return ERR_PTR(-EINVAL);
	}

	if (shmob_drm_format_is_yuv(format)) {
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
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

int shmob_drm_modeset_init(struct shmob_drm_device *sdev)
{
	struct drm_device *dev = &sdev->ddev;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	ret = shmob_drm_crtc_create(sdev);
	if (ret < 0)
		return ret;

	ret = shmob_drm_encoder_create(sdev);
	if (ret < 0)
		return ret;

	ret = shmob_drm_connector_create(sdev, &sdev->encoder);
	if (ret < 0)
		return ret;

	drm_mode_config_reset(dev);

	drm_kms_helper_poll_init(dev);

	sdev->ddev.mode_config.min_width = 0;
	sdev->ddev.mode_config.min_height = 0;
	sdev->ddev.mode_config.max_width = 4095;
	sdev->ddev.mode_config.max_height = 4095;
	sdev->ddev.mode_config.funcs = &shmob_drm_mode_config_funcs;

	return 0;
}
