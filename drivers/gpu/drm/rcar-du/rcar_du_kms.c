/*
 * rcar_du_kms.c  --  R-Car Display Unit Mode Setting
 *
 * Copyright (C) 2013 Renesas Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
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

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvds.h"
#include "rcar_du_regs.h"
#include "rcar_du_vga.h"

/* -----------------------------------------------------------------------------
 * Format helpers
 */

static const struct rcar_du_format_info rcar_du_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_RGB565,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP | PnMR_DDDF_16BPP,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_ARGB1555,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_ALP | PnMR_DDDF_ARGB,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_XRGB1555,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_ALP | PnMR_DDDF_ARGB,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_XRGB8888,
		.bpp = 32,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP | PnMR_DDDF_16BPP,
		.edf = PnDDCR4_EDF_RGB888,
	}, {
		.fourcc = DRM_FORMAT_ARGB8888,
		.bpp = 32,
		.planes = 1,
		.pnmr = PnMR_SPIM_ALP | PnMR_DDDF_16BPP,
		.edf = PnDDCR4_EDF_ARGB8888,
	}, {
		.fourcc = DRM_FORMAT_UYVY,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_YUYV,
		.bpp = 16,
		.planes = 1,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_NV12,
		.bpp = 12,
		.planes = 2,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		.fourcc = DRM_FORMAT_NV21,
		.bpp = 12,
		.planes = 2,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	}, {
		/* In YUV 4:2:2, only NV16 is supported (NV61 isn't) */
		.fourcc = DRM_FORMAT_NV16,
		.bpp = 16,
		.planes = 2,
		.pnmr = PnMR_SPIM_TP_OFF | PnMR_DDDF_YC,
		.edf = PnDDCR4_EDF_NONE,
	},
};

const struct rcar_du_format_info *rcar_du_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rcar_du_format_infos); ++i) {
		if (rcar_du_format_infos[i].fourcc == fourcc)
			return &rcar_du_format_infos[i];
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Common connector and encoder functions
 */

struct drm_encoder *
rcar_du_connector_best_encoder(struct drm_connector *connector)
{
	struct rcar_du_connector *rcon = to_rcar_connector(connector);

	return &rcon->encoder->encoder;
}

void rcar_du_encoder_mode_prepare(struct drm_encoder *encoder)
{
}

void rcar_du_encoder_mode_set(struct drm_encoder *encoder,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_encoder *renc = to_rcar_encoder(encoder);

	rcar_du_crtc_route_output(encoder->crtc, renc->output);
}

void rcar_du_encoder_mode_commit(struct drm_encoder *encoder)
{
}

/* -----------------------------------------------------------------------------
 * Frame buffer
 */

static struct drm_framebuffer *
rcar_du_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		  struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct rcar_du_format_info *format;

	format = rcar_du_format_info(mode_cmd->pixel_format);
	if (format == NULL) {
		dev_dbg(dev->dev, "unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	if (mode_cmd->pitches[0] & 15 || mode_cmd->pitches[0] >= 8192) {
		dev_dbg(dev->dev, "invalid pitch value %u\n",
			mode_cmd->pitches[0]);
		return ERR_PTR(-EINVAL);
	}

	if (format->planes == 2) {
		if (mode_cmd->pitches[1] != mode_cmd->pitches[0]) {
			dev_dbg(dev->dev,
				"luma and chroma pitches do not match\n");
			return ERR_PTR(-EINVAL);
		}
	}

	return drm_fb_cma_create(dev, file_priv, mode_cmd);
}

static const struct drm_mode_config_funcs rcar_du_mode_config_funcs = {
	.fb_create = rcar_du_fb_create,
};

int rcar_du_modeset_init(struct rcar_du_device *rcdu)
{
	struct drm_device *dev = rcdu->ddev;
	struct drm_encoder *encoder;
	unsigned int i;
	int ret;

	drm_mode_config_init(rcdu->ddev);

	rcdu->ddev->mode_config.min_width = 0;
	rcdu->ddev->mode_config.min_height = 0;
	rcdu->ddev->mode_config.max_width = 4095;
	rcdu->ddev->mode_config.max_height = 2047;
	rcdu->ddev->mode_config.funcs = &rcar_du_mode_config_funcs;

	ret = rcar_du_plane_init(rcdu);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(rcdu->crtcs); ++i) {
		ret = rcar_du_crtc_create(rcdu, i);
		if (ret < 0)
			return ret;
	}

	rcdu->used_crtcs = 0;
	rcdu->num_crtcs = i;

	for (i = 0; i < rcdu->pdata->num_encoders; ++i) {
		const struct rcar_du_encoder_data *pdata =
			&rcdu->pdata->encoders[i];

		if (pdata->output >= ARRAY_SIZE(rcdu->crtcs)) {
			dev_warn(rcdu->dev,
				 "encoder %u references unexisting output %u, skipping\n",
				 i, pdata->output);
			continue;
		}

		switch (pdata->encoder) {
		case RCAR_DU_ENCODER_VGA:
			rcar_du_vga_init(rcdu, &pdata->u.vga, pdata->output);
			break;

		case RCAR_DU_ENCODER_LVDS:
			rcar_du_lvds_init(rcdu, &pdata->u.lvds, pdata->output);
			break;

		default:
			break;
		}
	}

	/* Set the possible CRTCs and possible clones. All encoders can be
	 * driven by the CRTC associated with the output they're connected to,
	 * as well as by CRTC 0.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct rcar_du_encoder *renc = to_rcar_encoder(encoder);

		encoder->possible_crtcs = (1 << 0) | (1 << renc->output);
		encoder->possible_clones = 1 << 0;
	}

	ret = rcar_du_plane_register(rcdu);
	if (ret < 0)
		return ret;

	drm_kms_helper_poll_init(rcdu->ddev);

	drm_helper_disable_unused_functions(rcdu->ddev);

	return 0;
}
