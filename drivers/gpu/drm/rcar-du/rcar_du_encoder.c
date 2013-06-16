/*
 * rcar_du_encoder.c  --  R-Car Display Unit Encoder
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

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_kms.h"
#include "rcar_du_lvdscon.h"
#include "rcar_du_vgacon.h"

/* -----------------------------------------------------------------------------
 * Common connector functions
 */

struct drm_encoder *
rcar_du_connector_best_encoder(struct drm_connector *connector)
{
	struct rcar_du_connector *rcon = to_rcar_connector(connector);

	return &rcon->encoder->encoder;
}

/* -----------------------------------------------------------------------------
 * Encoder
 */

static void rcar_du_encoder_dpms(struct drm_encoder *encoder, int mode)
{
}

static bool rcar_du_encoder_mode_fixup(struct drm_encoder *encoder,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	const struct drm_display_mode *panel_mode;
	struct drm_device *dev = encoder->dev;
	struct drm_connector *connector;
	bool found = false;

	/* DAC encoders have currently no restriction on the mode. */
	if (encoder->encoder_type == DRM_MODE_ENCODER_DAC)
		return true;

	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->encoder == encoder) {
			found = true;
			break;
		}
	}

	if (!found) {
		dev_dbg(dev->dev, "mode_fixup: no connector found\n");
		return false;
	}

	if (list_empty(&connector->modes)) {
		dev_dbg(dev->dev, "mode_fixup: empty modes list\n");
		return false;
	}

	panel_mode = list_first_entry(&connector->modes,
				      struct drm_display_mode, head);

	/* We're not allowed to modify the resolution. */
	if (mode->hdisplay != panel_mode->hdisplay ||
	    mode->vdisplay != panel_mode->vdisplay)
		return false;

	/* The flat panel mode is fixed, just copy it to the adjusted mode. */
	drm_mode_copy(adjusted_mode, panel_mode);

	return true;
}

static void rcar_du_encoder_mode_prepare(struct drm_encoder *encoder)
{
}

static void rcar_du_encoder_mode_commit(struct drm_encoder *encoder)
{
}

static void rcar_du_encoder_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_encoder *renc = to_rcar_encoder(encoder);

	rcar_du_crtc_route_output(encoder->crtc, renc->output);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.dpms = rcar_du_encoder_dpms,
	.mode_fixup = rcar_du_encoder_mode_fixup,
	.prepare = rcar_du_encoder_mode_prepare,
	.commit = rcar_du_encoder_mode_commit,
	.mode_set = rcar_du_encoder_mode_set,
};

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int rcar_du_encoder_init(struct rcar_du_device *rcdu,
			 enum rcar_du_encoder_type type, unsigned int output,
			 const struct rcar_du_encoder_data *data)
{
	struct rcar_du_encoder *renc;
	int ret;

	renc = devm_kzalloc(rcdu->dev, sizeof(*renc), GFP_KERNEL);
	if (renc == NULL)
		return -ENOMEM;

	renc->output = output;

	ret = drm_encoder_init(rcdu->ddev, &renc->encoder, &encoder_funcs,
			       type);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(&renc->encoder, &encoder_helper_funcs);

	switch (type) {
	case RCAR_DU_ENCODER_LVDS:
		return rcar_du_lvds_connector_init(rcdu, renc,
						   &data->connector.lvds.panel);

	case RCAR_DU_ENCODER_VGA:
		return rcar_du_vga_connector_init(rcdu, renc);

	default:
		return -EINVAL;
	}
}
