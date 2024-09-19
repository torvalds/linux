// SPDX-License-Identifier: GPL-2.0+
/*
 * RZ/G2L Display Unit Mode Setting
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_kms.c
 */

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_managed.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "rzg2l_du_crtc.h"
#include "rzg2l_du_drv.h"
#include "rzg2l_du_encoder.h"
#include "rzg2l_du_kms.h"
#include "rzg2l_du_vsp.h"

/* -----------------------------------------------------------------------------
 * Format helpers
 */

static const struct rzg2l_du_format_info rzg2l_du_format_infos[] = {
	{
		.fourcc = DRM_FORMAT_XRGB8888,
		.v4l2 = V4L2_PIX_FMT_XBGR32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_ARGB8888,
		.v4l2 = V4L2_PIX_FMT_ABGR32,
		.bpp = 32,
		.planes = 1,
		.hsub = 1,
	}, {
		.fourcc = DRM_FORMAT_RGB888,
		.v4l2 = V4L2_PIX_FMT_BGR24,
		.bpp = 24,
		.planes = 1,
		.hsub = 1,
	}
};

const struct rzg2l_du_format_info *rzg2l_du_format_info(u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(rzg2l_du_format_infos); ++i) {
		if (rzg2l_du_format_infos[i].fourcc == fourcc)
			return &rzg2l_du_format_infos[i];
	}

	return NULL;
}

/* -----------------------------------------------------------------------------
 * Frame buffer
 */

int rzg2l_du_dumb_create(struct drm_file *file, struct drm_device *dev,
			 struct drm_mode_create_dumb *args)
{
	unsigned int min_pitch = DIV_ROUND_UP(args->width * args->bpp, 8);
	unsigned int align = 16 * args->bpp / 8;

	args->pitch = roundup(min_pitch, align);

	return drm_gem_dma_dumb_create_internal(file, dev, args);
}

static struct drm_framebuffer *
rzg2l_du_fb_create(struct drm_device *dev, struct drm_file *file_priv,
		   const struct drm_mode_fb_cmd2 *mode_cmd)
{
	const struct rzg2l_du_format_info *format;
	unsigned int max_pitch;

	format = rzg2l_du_format_info(mode_cmd->pixel_format);
	if (!format) {
		dev_dbg(dev->dev, "unsupported pixel format %p4cc\n",
			&mode_cmd->pixel_format);
		return ERR_PTR(-EINVAL);
	}

	/*
	 * On RZ/G2L the memory interface is handled by the VSP that limits the
	 * pitch to 65535 bytes.
	 */
	max_pitch = 65535;
	if (mode_cmd->pitches[0] > max_pitch) {
		dev_dbg(dev->dev, "invalid pitch value %u\n",
			mode_cmd->pitches[0]);
		return ERR_PTR(-EINVAL);
	}

	return drm_gem_fb_create(dev, file_priv, mode_cmd);
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

static const struct drm_mode_config_helper_funcs rzg2l_du_mode_config_helper = {
	.atomic_commit_tail = drm_atomic_helper_commit_tail_rpm,
};

static const struct drm_mode_config_funcs rzg2l_du_mode_config_funcs = {
	.fb_create = rzg2l_du_fb_create,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int rzg2l_du_encoders_init_one(struct rzg2l_du_device *rcdu,
				      enum rzg2l_du_output output,
				      struct of_endpoint *ep)
{
	struct device_node *entity;
	int ret;

	/* Locate the connected entity and initialize the encoder. */
	entity = of_graph_get_remote_port_parent(ep->local_node);
	if (!entity) {
		dev_dbg(rcdu->dev, "unconnected endpoint %pOF, skipping\n",
			ep->local_node);
		return -ENODEV;
	}

	if (!of_device_is_available(entity)) {
		dev_dbg(rcdu->dev,
			"connected entity %pOF is disabled, skipping\n",
			entity);
		of_node_put(entity);
		return -ENODEV;
	}

	ret = rzg2l_du_encoder_init(rcdu, output, entity);
	if (ret && ret != -EPROBE_DEFER && ret != -ENOLINK)
		dev_warn(rcdu->dev,
			 "failed to initialize encoder %pOF on output %s (%d), skipping\n",
			 entity, rzg2l_du_output_name(output), ret);

	of_node_put(entity);

	return ret;
}

static int rzg2l_du_encoders_init(struct rzg2l_du_device *rcdu)
{
	struct device_node *np = rcdu->dev->of_node;
	struct device_node *ep_node;
	unsigned int num_encoders = 0;

	/*
	 * Iterate over the endpoints and create one encoder for each output
	 * pipeline.
	 */
	for_each_endpoint_of_node(np, ep_node) {
		enum rzg2l_du_output output;
		struct of_endpoint ep;
		unsigned int i;
		int ret;

		ret = of_graph_parse_endpoint(ep_node, &ep);
		if (ret < 0) {
			of_node_put(ep_node);
			return ret;
		}

		/* Find the output route corresponding to the port number. */
		for (i = 0; i < RZG2L_DU_OUTPUT_MAX; ++i) {
			if (rcdu->info->routes[i].possible_outputs &&
			    rcdu->info->routes[i].port == ep.port) {
				output = i;
				break;
			}
		}

		if (i == RZG2L_DU_OUTPUT_MAX) {
			dev_warn(rcdu->dev,
				 "port %u references unexisting output, skipping\n",
				 ep.port);
			continue;
		}

		/* Process the output pipeline. */
		ret = rzg2l_du_encoders_init_one(rcdu, output, &ep);
		if (ret < 0) {
			if (ret == -EPROBE_DEFER) {
				of_node_put(ep_node);
				return ret;
			}

			continue;
		}

		num_encoders++;
	}

	return num_encoders;
}

static int rzg2l_du_vsps_init(struct rzg2l_du_device *rcdu)
{
	const struct device_node *np = rcdu->dev->of_node;
	const char *vsps_prop_name = "renesas,vsps";
	struct of_phandle_args args;
	struct {
		struct device_node *np;
		unsigned int crtcs_mask;
	} vsps[RZG2L_DU_MAX_VSPS] = { { NULL, }, };
	unsigned int vsps_count = 0;
	unsigned int cells;
	unsigned int i;
	int ret;

	/*
	 * First parse the DT vsps property to populate the list of VSPs. Each
	 * entry contains a pointer to the VSP DT node and a bitmask of the
	 * connected DU CRTCs.
	 */
	ret = of_property_count_u32_elems(np, vsps_prop_name);
	cells = ret / rcdu->num_crtcs - 1;
	if (cells != 1)
		return -EINVAL;

	for (i = 0; i < rcdu->num_crtcs; ++i) {
		unsigned int j;

		ret = of_parse_phandle_with_fixed_args(np, vsps_prop_name,
						       cells, i, &args);
		if (ret < 0)
			goto done;

		/*
		 * Add the VSP to the list or update the corresponding existing
		 * entry if the VSP has already been added.
		 */
		for (j = 0; j < vsps_count; ++j) {
			if (vsps[j].np == args.np)
				break;
		}

		if (j < vsps_count)
			of_node_put(args.np);
		else
			vsps[vsps_count++].np = args.np;

		vsps[j].crtcs_mask |= BIT(i);

		/*
		 * Store the VSP pointer and pipe index in the CRTC. If the
		 * second cell of the 'renesas,vsps' specifier isn't present,
		 * default to 0.
		 */
		rcdu->crtcs[i].vsp = &rcdu->vsps[j];
		rcdu->crtcs[i].vsp_pipe = cells >= 1 ? args.args[0] : 0;
	}

	/*
	 * Then initialize all the VSPs from the node pointers and CRTCs bitmask
	 * computed previously.
	 */
	for (i = 0; i < vsps_count; ++i) {
		struct rzg2l_du_vsp *vsp = &rcdu->vsps[i];

		vsp->index = i;
		vsp->dev = rcdu;

		ret = rzg2l_du_vsp_init(vsp, vsps[i].np, vsps[i].crtcs_mask);
		if (ret)
			goto done;
	}

done:
	for (i = 0; i < ARRAY_SIZE(vsps); ++i)
		of_node_put(vsps[i].np);

	return ret;
}

int rzg2l_du_modeset_init(struct rzg2l_du_device *rcdu)
{
	struct drm_device *dev = &rcdu->ddev;
	struct drm_encoder *encoder;
	unsigned int num_encoders;
	int ret;

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = 0;
	dev->mode_config.min_height = 0;
	dev->mode_config.normalize_zpos = true;
	dev->mode_config.funcs = &rzg2l_du_mode_config_funcs;
	dev->mode_config.helper_private = &rzg2l_du_mode_config_helper;

	/*
	 * The RZ DU uses the VSP1 for memory access, and is limited
	 * to frame sizes of 1920x1080.
	 */
	dev->mode_config.max_width = 1920;
	dev->mode_config.max_height = 1080;

	rcdu->num_crtcs = hweight8(rcdu->info->channels_mask);

	/*
	 * Initialize vertical blanking interrupts handling. Start with vblank
	 * disabled for all CRTCs.
	 */
	ret = drm_vblank_init(dev, rcdu->num_crtcs);
	if (ret < 0)
		return ret;

	/* Initialize the compositors. */
	ret = rzg2l_du_vsps_init(rcdu);
	if (ret < 0)
		return ret;

	/* Create the CRTCs. */
	ret = rzg2l_du_crtc_create(rcdu);
	if (ret < 0)
		return ret;

	/* Initialize the encoders. */
	ret = rzg2l_du_encoders_init(rcdu);
	if (ret < 0)
		return dev_err_probe(rcdu->dev, ret,
				     "failed to initialize encoders\n");

	if (ret == 0) {
		dev_err(rcdu->dev, "error: no encoder could be initialized\n");
		return -EINVAL;
	}

	num_encoders = ret;

	/*
	 * Set the possible CRTCs and possible clones. There's always at least
	 * one way for all encoders to clone each other, set all bits in the
	 * possible clones field.
	 */
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head) {
		struct rzg2l_du_encoder *renc = to_rzg2l_encoder(encoder);
		const struct rzg2l_du_output_routing *route =
			&rcdu->info->routes[renc->output];

		encoder->possible_crtcs = route->possible_outputs;
		encoder->possible_clones = (1 << num_encoders) - 1;
	}

	drm_mode_config_reset(dev);

	drm_kms_helper_poll_init(dev);

	return 0;
}
