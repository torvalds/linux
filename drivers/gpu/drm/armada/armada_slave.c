/*
 * Copyright (C) 2012 Russell King
 *  Rewritten from the dovefb driver, and Armada510 manuals.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>
#include "armada_drm.h"
#include "armada_output.h"
#include "armada_slave.h"

static int armada_drm_slave_get_modes(struct drm_connector *conn)
{
	struct drm_encoder *enc = armada_drm_connector_encoder(conn);
	int count = 0;

	if (enc) {
		struct drm_encoder_slave *slave = to_encoder_slave(enc);

		count = slave->slave_funcs->get_modes(enc, conn);
	}

	return count;
}

static void armada_drm_slave_destroy(struct drm_encoder *enc)
{
	struct drm_encoder_slave *slave = to_encoder_slave(enc);
	struct i2c_client *client = drm_i2c_encoder_get_client(enc);

	if (slave->slave_funcs)
		slave->slave_funcs->destroy(enc);
	if (client)
		i2c_put_adapter(client->adapter);

	drm_encoder_cleanup(&slave->base);
	kfree(slave);
}

static const struct drm_encoder_funcs armada_drm_slave_encoder_funcs = {
	.destroy	= armada_drm_slave_destroy,
};

static const struct drm_connector_helper_funcs armada_drm_slave_helper_funcs = {
	.get_modes	= armada_drm_slave_get_modes,
	.mode_valid	= armada_drm_slave_encoder_mode_valid,
	.best_encoder	= armada_drm_connector_encoder,
};

static const struct drm_encoder_helper_funcs drm_slave_encoder_helpers = {
	.dpms = drm_i2c_encoder_dpms,
	.save = drm_i2c_encoder_save,
	.restore = drm_i2c_encoder_restore,
	.mode_fixup = drm_i2c_encoder_mode_fixup,
	.prepare = drm_i2c_encoder_prepare,
	.commit = drm_i2c_encoder_commit,
	.mode_set = drm_i2c_encoder_mode_set,
	.detect = drm_i2c_encoder_detect,
};

static int
armada_drm_conn_slave_create(struct drm_connector *conn, const void *data)
{
	const struct armada_drm_slave_config *config = data;
	struct drm_encoder_slave *slave;
	struct i2c_adapter *adap;
	int ret;

	conn->interlace_allowed = config->interlace_allowed;
	conn->doublescan_allowed = config->doublescan_allowed;
	conn->polled = config->polled;

	drm_connector_helper_add(conn, &armada_drm_slave_helper_funcs);

	slave = kzalloc(sizeof(*slave), GFP_KERNEL);
	if (!slave)
		return -ENOMEM;

	slave->base.possible_crtcs = config->crtcs;

	adap = i2c_get_adapter(config->i2c_adapter_id);
	if (!adap) {
		kfree(slave);
		return -EPROBE_DEFER;
	}

	ret = drm_encoder_init(conn->dev, &slave->base,
			       &armada_drm_slave_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS);
	if (ret) {
		DRM_ERROR("unable to init encoder\n");
		i2c_put_adapter(adap);
		kfree(slave);
		return ret;
	}

	ret = drm_i2c_encoder_init(conn->dev, slave, adap, &config->info);
	i2c_put_adapter(adap);
	if (ret) {
		DRM_ERROR("unable to init encoder slave\n");
		armada_drm_slave_destroy(&slave->base);
		return ret;
	}

	drm_encoder_helper_add(&slave->base, &drm_slave_encoder_helpers);

	ret = slave->slave_funcs->create_resources(&slave->base, conn);
	if (ret) {
		armada_drm_slave_destroy(&slave->base);
		return ret;
	}

	ret = drm_mode_connector_attach_encoder(conn, &slave->base);
	if (ret) {
		armada_drm_slave_destroy(&slave->base);
		return ret;
	}

	conn->encoder = &slave->base;

	return ret;
}

static const struct armada_output_type armada_drm_conn_slave = {
	.connector_type	= DRM_MODE_CONNECTOR_HDMIA,
	.create		= armada_drm_conn_slave_create,
	.set_property	= armada_drm_slave_encoder_set_property,
};

int armada_drm_connector_slave_create(struct drm_device *dev,
	const struct armada_drm_slave_config *config)
{
	return armada_output_create(dev, &armada_drm_conn_slave, config);
}
