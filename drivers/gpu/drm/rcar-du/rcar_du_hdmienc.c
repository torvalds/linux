/*
 * R-Car Display Unit HDMI Encoder
 *
 * Copyright (C) 2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/slab.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_hdmienc.h"
#include "rcar_du_lvdsenc.h"

struct rcar_du_hdmienc {
	struct rcar_du_encoder *renc;
	bool enabled;
};

#define to_rcar_hdmienc(e)	(to_rcar_encoder(e)->hdmi)

static void rcar_du_hdmienc_disable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       false);

	hdmienc->enabled = false;
}

static void rcar_du_hdmienc_enable(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_enable(hdmienc->renc->lvds, encoder->crtc,
				       true);

	hdmienc->enabled = true;
}

static int rcar_du_hdmienc_atomic_check(struct drm_encoder *encoder,
					struct drm_crtc_state *crtc_state,
					struct drm_connector_state *conn_state)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;

	if (hdmienc->renc->lvds)
		rcar_du_lvdsenc_atomic_check(hdmienc->renc->lvds,
					     adjusted_mode);

	return 0;
}


static void rcar_du_hdmienc_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	rcar_du_crtc_route_output(encoder->crtc, hdmienc->renc->output);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.mode_set = rcar_du_hdmienc_mode_set,
	.disable = rcar_du_hdmienc_disable,
	.enable = rcar_du_hdmienc_enable,
	.atomic_check = rcar_du_hdmienc_atomic_check,
};

static void rcar_du_hdmienc_cleanup(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	if (hdmienc->enabled)
		rcar_du_hdmienc_disable(encoder);

	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = rcar_du_hdmienc_cleanup,
};

int rcar_du_hdmienc_init(struct rcar_du_device *rcdu,
			 struct rcar_du_encoder *renc, struct device_node *np)
{
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(renc);
	struct drm_bridge *bridge;
	struct rcar_du_hdmienc *hdmienc;
	int ret;

	hdmienc = devm_kzalloc(rcdu->dev, sizeof(*hdmienc), GFP_KERNEL);
	if (hdmienc == NULL)
		return -ENOMEM;

	/* Locate the DRM bridge from the HDMI encoder DT node. */
	bridge = of_drm_find_bridge(np);
	if (!bridge)
		return -EPROBE_DEFER;

	ret = drm_encoder_init(rcdu->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret < 0)
		return ret;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	renc->hdmi = hdmienc;
	hdmienc->renc = renc;

	/* Link the bridge to the encoder. */
	bridge->encoder = encoder;
	encoder->bridge = bridge;

	ret = drm_bridge_attach(rcdu->ddev, bridge);
	if (ret) {
		drm_encoder_cleanup(encoder);
		return ret;
	}

	return 0;
}
