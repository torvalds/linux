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
#include <drm/drm_encoder_slave.h>

#include "rcar_du_drv.h"
#include "rcar_du_encoder.h"
#include "rcar_du_hdmienc.h"
#include "rcar_du_lvdsenc.h"

struct rcar_du_hdmienc {
	struct rcar_du_encoder *renc;
	struct device *dev;
	int dpms;
};

#define to_rcar_hdmienc(e)	(to_rcar_encoder(e)->hdmi)
#define to_slave_funcs(e)	(to_rcar_encoder(e)->slave.slave_funcs)

static void rcar_du_hdmienc_dpms(struct drm_encoder *encoder, int mode)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);

	if (mode != DRM_MODE_DPMS_ON)
		mode = DRM_MODE_DPMS_OFF;

	if (hdmienc->dpms == mode)
		return;

	if (mode == DRM_MODE_DPMS_ON && hdmienc->renc->lvds)
		rcar_du_lvdsenc_dpms(hdmienc->renc->lvds, encoder->crtc, mode);

	if (sfuncs->dpms)
		sfuncs->dpms(encoder, mode);

	if (mode != DRM_MODE_DPMS_ON && hdmienc->renc->lvds)
		rcar_du_lvdsenc_dpms(hdmienc->renc->lvds, encoder->crtc, mode);

	hdmienc->dpms = mode;
}

static bool rcar_du_hdmienc_mode_fixup(struct drm_encoder *encoder,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);

	/* The internal LVDS encoder has a clock frequency operating range of
	 * 30MHz to 150MHz. Clamp the clock accordingly.
	 */
	if (hdmienc->renc->lvds)
		adjusted_mode->clock = clamp(adjusted_mode->clock,
					     30000, 150000);

	if (sfuncs->mode_fixup == NULL)
		return true;

	return sfuncs->mode_fixup(encoder, mode, adjusted_mode);
}

static void rcar_du_hdmienc_mode_prepare(struct drm_encoder *encoder)
{
	rcar_du_hdmienc_dpms(encoder, DRM_MODE_DPMS_OFF);
}

static void rcar_du_hdmienc_mode_commit(struct drm_encoder *encoder)
{
	rcar_du_hdmienc_dpms(encoder, DRM_MODE_DPMS_ON);
}

static void rcar_du_hdmienc_mode_set(struct drm_encoder *encoder,
				     struct drm_display_mode *mode,
				     struct drm_display_mode *adjusted_mode)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);
	struct drm_encoder_slave_funcs *sfuncs = to_slave_funcs(encoder);

	if (sfuncs->mode_set)
		sfuncs->mode_set(encoder, mode, adjusted_mode);

	rcar_du_crtc_route_output(encoder->crtc, hdmienc->renc->output);
}

static const struct drm_encoder_helper_funcs encoder_helper_funcs = {
	.dpms = rcar_du_hdmienc_dpms,
	.mode_fixup = rcar_du_hdmienc_mode_fixup,
	.prepare = rcar_du_hdmienc_mode_prepare,
	.commit = rcar_du_hdmienc_mode_commit,
	.mode_set = rcar_du_hdmienc_mode_set,
};

static void rcar_du_hdmienc_cleanup(struct drm_encoder *encoder)
{
	struct rcar_du_hdmienc *hdmienc = to_rcar_hdmienc(encoder);

	rcar_du_hdmienc_dpms(encoder, DRM_MODE_DPMS_OFF);

	drm_encoder_cleanup(encoder);
	put_device(hdmienc->dev);
}

static const struct drm_encoder_funcs encoder_funcs = {
	.destroy = rcar_du_hdmienc_cleanup,
};

int rcar_du_hdmienc_init(struct rcar_du_device *rcdu,
			 struct rcar_du_encoder *renc, struct device_node *np)
{
	struct drm_encoder *encoder = rcar_encoder_to_drm_encoder(renc);
	struct drm_i2c_encoder_driver *driver;
	struct i2c_client *i2c_slave;
	struct rcar_du_hdmienc *hdmienc;
	int ret;

	hdmienc = devm_kzalloc(rcdu->dev, sizeof(*hdmienc), GFP_KERNEL);
	if (hdmienc == NULL)
		return -ENOMEM;

	/* Locate the slave I2C device and driver. */
	i2c_slave = of_find_i2c_device_by_node(np);
	if (!i2c_slave || !i2c_get_clientdata(i2c_slave))
		return -EPROBE_DEFER;

	hdmienc->dev = &i2c_slave->dev;

	if (hdmienc->dev->driver == NULL) {
		ret = -EPROBE_DEFER;
		goto error;
	}

	/* Initialize the slave encoder. */
	driver = to_drm_i2c_encoder_driver(to_i2c_driver(hdmienc->dev->driver));
	ret = driver->encoder_init(i2c_slave, rcdu->ddev, &renc->slave);
	if (ret < 0)
		goto error;

	ret = drm_encoder_init(rcdu->ddev, encoder, &encoder_funcs,
			       DRM_MODE_ENCODER_TMDS);
	if (ret < 0)
		goto error;

	drm_encoder_helper_add(encoder, &encoder_helper_funcs);

	renc->hdmi = hdmienc;
	hdmienc->renc = renc;

	return 0;

error:
	put_device(hdmienc->dev);
	return ret;
}
