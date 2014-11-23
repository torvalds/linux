/*
 * rcar_du_kms.c  --  R-Car Display Unit Mode Setting
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
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

#include <linux/of_graph.h>

#include "rcar_du_crtc.h"
#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvdsenc.h"
#include "rcar_du_regs.h"

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
 * Frame buffer
 */

int rcar_du_dumb_create(struct drm_file *file, struct drm_device *dev,
			struct drm_mode_create_dumb *args)
{
	struct rcar_du_device *rcdu = dev->dev_private;
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	unsigned int align;

	/* The R8A7779 DU requires a 16 pixels pitch alignment as documented,
	 * but the R8A7790 DU seems to require a 128 bytes pitch alignment.
	 */
	if (rcar_du_needs(rcdu, RCAR_DU_QUIRK_ALIGN_128B))
		align = 128;
	else
		align = 16 * args->bpp / 8;

	args->pitch = roundup(max(args->pitch, min_pitch), align);

	return drm_gem_cma_dumb_create(file, dev, args);
}

static struct drm_framebuffer *
rcar_du_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		  struct drm_mode_fb_cmd2 *mode_cmd)
{
	struct rcar_du_device *rcdu = dev->dev_private;
	const struct rcar_du_format_info *format;
	unsigned int max_pitch;
	unsigned int align;
	unsigned int bpp;

	format = rcar_du_format_info(mode_cmd->pixel_format);
	if (format == NULL) {
		dev_dbg(dev->dev, "unsupported pixel format %08x\n",
			mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * The pitch and alignment constraints are expressed in pixels on the
	 * hardware side and in bytes in the DRM API.
	 */
	bpp = format->planes == 2 ? 1 : format->bpp / 8;
	max_pitch =  4096 * bpp;

	if (rcar_du_needs(rcdu, RCAR_DU_QUIRK_ALIGN_128B))
		align = 128;
	else
		align = 16 * bpp;

	if (mode_cmd->pitches[0] & (align - 1) ||
	    mode_cmd->pitches[0] >= max_pitch) {
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

static void rcar_du_output_poll_changed(struct drm_device *dev)
{
	struct rcar_du_device *rcdu = dev->dev_private;

	drm_fbdev_cma_hotplug_event(rcdu->fbdev);
}

static const struct drm_mode_config_funcs rcar_du_mode_config_funcs = {
	.fb_create = rcar_du_fb_create,
	.output_poll_changed = rcar_du_output_poll_changed,
};

static int rcar_du_encoders_init_pdata(struct rcar_du_device *rcdu)
{
	unsigned int num_encoders = 0;
	unsigned int i;
	int ret;

	for (i = 0; i < rcdu->pdata->num_encoders; ++i) {
		const struct rcar_du_encoder_data *pdata =
			&rcdu->pdata->encoders[i];
		const struct rcar_du_output_routing *route =
			&rcdu->info->routes[pdata->output];

		if (pdata->type == RCAR_DU_ENCODER_UNUSED)
			continue;

		if (pdata->output >= RCAR_DU_OUTPUT_MAX ||
		    route->possible_crtcs == 0) {
			dev_warn(rcdu->dev,
				 "encoder %u references unexisting output %u, skipping\n",
				 i, pdata->output);
			continue;
		}

		ret = rcar_du_encoder_init(rcdu, pdata->type, pdata->output,
					   pdata, NULL);
		if (ret < 0)
			return ret;

		num_encoders++;
	}

	return num_encoders;
}

static int rcar_du_encoders_init_dt_one(struct rcar_du_device *rcdu,
					enum rcar_du_output output,
					struct of_endpoint *ep)
{
	static const struct {
		const char *compatible;
		enum rcar_du_encoder_type type;
	} encoders[] = {
		{ "adi,adv7123", RCAR_DU_ENCODER_VGA },
		{ "thine,thc63lvdm83d", RCAR_DU_ENCODER_LVDS },
	};

	enum rcar_du_encoder_type enc_type = RCAR_DU_ENCODER_NONE;
	struct device_node *connector = NULL;
	struct device_node *encoder = NULL;
	struct device_node *prev = NULL;
	struct device_node *entity_ep_node;
	struct device_node *entity;
	int ret;

	/*
	 * Locate the connected entity and infer its type from the number of
	 * endpoints.
	 */
	entity = of_graph_get_remote_port_parent(ep->local_node);
	if (!entity) {
		dev_dbg(rcdu->dev, "unconnected endpoint %s, skipping\n",
			ep->local_node->full_name);
		return 0;
	}

	entity_ep_node = of_parse_phandle(ep->local_node, "remote-endpoint", 0);

	while (1) {
		struct device_node *ep_node;

		ep_node = of_graph_get_next_endpoint(entity, prev);
		of_node_put(prev);
		prev = ep_node;

		if (!ep_node)
			break;

		if (ep_node == entity_ep_node)
			continue;

		/*
		 * We've found one endpoint other than the input, this must
		 * be an encoder. Locate the connector.
		 */
		encoder = entity;
		connector = of_graph_get_remote_port_parent(ep_node);
		of_node_put(ep_node);

		if (!connector) {
			dev_warn(rcdu->dev,
				 "no connector for encoder %s, skipping\n",
				 encoder->full_name);
			of_node_put(entity_ep_node);
			of_node_put(encoder);
			return 0;
		}

		break;
	}

	of_node_put(entity_ep_node);

	if (encoder) {
		/*
		 * If an encoder has been found, get its type based on its
		 * compatible string.
		 */
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(encoders); ++i) {
			if (of_device_is_compatible(encoder,
						    encoders[i].compatible)) {
				enc_type = encoders[i].type;
				break;
			}
		}

		if (i == ARRAY_SIZE(encoders)) {
			dev_warn(rcdu->dev,
				 "unknown encoder type for %s, skipping\n",
				 encoder->full_name);
			of_node_put(encoder);
			of_node_put(connector);
			return 0;
		}
	} else {
		/*
		 * If no encoder has been found the entity must be the
		 * connector.
		 */
		connector = entity;
	}

	ret = rcar_du_encoder_init(rcdu, enc_type, output, NULL, connector);
	of_node_put(encoder);
	of_node_put(connector);

	return ret < 0 ? ret : 1;
}

static int rcar_du_encoders_init_dt(struct rcar_du_device *rcdu)
{
	struct device_node *np = rcdu->dev->of_node;
	struct device_node *prev = NULL;
	unsigned int num_encoders = 0;

	/*
	 * Iterate over the endpoints and create one encoder for each output
	 * pipeline.
	 */
	while (1) {
		struct device_node *ep_node;
		enum rcar_du_output output;
		struct of_endpoint ep;
		unsigned int i;
		int ret;

		ep_node = of_graph_get_next_endpoint(np, prev);
		of_node_put(prev);
		prev = ep_node;

		if (ep_node == NULL)
			break;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret < 0) {
			of_node_put(ep_node);
			return ret;
		}

		/* Find the output route corresponding to the port number. */
		for (i = 0; i < RCAR_DU_OUTPUT_MAX; ++i) {
			if (rcdu->info->routes[i].possible_crtcs &&
			    rcdu->info->routes[i].port == ep.port) {
				output = i;
				break;
			}
		}

		if (i == RCAR_DU_OUTPUT_MAX) {
			dev_warn(rcdu->dev,
				 "port %u references unexisting output, skipping\n",
				 ep.port);
			continue;
		}

		/* Process the output pipeline. */
		ret = rcar_du_encoders_init_dt_one(rcdu, output, &ep);
		if (ret < 0) {
			of_node_put(ep_node);
			return ret;
		}

		num_encoders += ret;
	}

	return num_encoders;
}

int rcar_du_modeset_init(struct rcar_du_device *rcdu)
{
	static const unsigned int mmio_offsets[] = {
		DU0_REG_OFFSET, DU2_REG_OFFSET
	};

	struct drm_device *dev = rcdu->ddev;
	struct drm_encoder *encoder;
	struct drm_fbdev_cma *fbdev;
	unsigned int num_encoders;
	unsigned int num_groups;
	unsigned int i;
	int ret;

	drm_mode_config_init(dev);

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.max_width = 4095;
	dev->mode_config.max_height = 2047;
	dev->mode_config.funcs = &rcar_du_mode_config_funcs;

	rcdu->num_crtcs = rcdu->info->num_crtcs;

	/* Initialize the groups. */
	num_groups = DIV_ROUND_UP(rcdu->num_crtcs, 2);

	for (i = 0; i < num_groups; ++i) {
		struct rcar_du_group *rgrp = &rcdu->groups[i];

		rgrp->dev = rcdu;
		rgrp->mmio_offset = mmio_offsets[i];
		rgrp->index = i;

		ret = rcar_du_planes_init(rgrp);
		if (ret < 0)
			return ret;
	}

	/* Create the CRTCs. */
	for (i = 0; i < rcdu->num_crtcs; ++i) {
		struct rcar_du_group *rgrp = &rcdu->groups[i / 2];

		ret = rcar_du_crtc_create(rgrp, i);
		if (ret < 0)
			return ret;
	}

	/* Initialize the encoders. */
	ret = rcar_du_lvdsenc_init(rcdu);
	if (ret < 0)
		return ret;

	if (rcdu->pdata)
		ret = rcar_du_encoders_init_pdata(rcdu);
	else
		ret = rcar_du_encoders_init_dt(rcdu);

	if (ret < 0)
		return ret;

	num_encoders = ret;

	/* Set the possible CRTCs and possible clones. There's always at least
	 * one way for all encoders to clone each other, set all bits in the
	 * possible clones field.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct rcar_du_encoder *renc = to_rcar_encoder(encoder);
		const struct rcar_du_output_routing *route =
			&rcdu->info->routes[renc->output];

		encoder->possible_crtcs = route->possible_crtcs;
		encoder->possible_clones = (1 << num_encoders) - 1;
	}

	/* Now that the CRTCs have been initialized register the planes. */
	for (i = 0; i < num_groups; ++i) {
		ret = rcar_du_planes_register(&rcdu->groups[i]);
		if (ret < 0)
			return ret;
	}

	drm_kms_helper_poll_init(dev);

	drm_helper_disable_unused_functions(dev);

	fbdev = drm_fbdev_cma_init(dev, 32, dev->mode_config.num_crtc,
				   dev->mode_config.num_connector);
	if (IS_ERR(fbdev))
		return PTR_ERR(fbdev);

#ifndef CONFIG_FRAMEBUFFER_CONSOLE
	drm_fbdev_cma_restore_mode(fbdev);
#endif

	rcdu->fbdev = fbdev;

	return 0;
}
