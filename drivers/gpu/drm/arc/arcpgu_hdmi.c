/*
 * ARC PGU DRM driver.
 *
 * Copyright (C) 2016 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <drm/drm_crtc_helper.h>
#include <drm/drm_encoder_slave.h>
#include <drm/drm_atomic_helper.h>

#include "arcpgu.h"

struct arcpgu_drm_connector {
	struct drm_connector connector;
	struct drm_encoder_slave *encoder_slave;
};

static int arcpgu_drm_connector_get_modes(struct drm_connector *connector)
{
	const struct drm_encoder_slave_funcs *sfuncs;
	struct drm_encoder_slave *slave;
	struct arcpgu_drm_connector *con =
		container_of(connector, struct arcpgu_drm_connector, connector);

	slave = con->encoder_slave;
	if (slave == NULL) {
		dev_err(connector->dev->dev,
			"connector_get_modes: cannot find slave encoder for connector\n");
		return 0;
	}

	sfuncs = slave->slave_funcs;
	if (sfuncs->get_modes == NULL)
		return 0;

	return sfuncs->get_modes(&slave->base, connector);
}

struct drm_encoder *
arcpgu_drm_connector_best_encoder(struct drm_connector *connector)
{
	struct drm_encoder_slave *slave;
	struct arcpgu_drm_connector *con =
		container_of(connector, struct arcpgu_drm_connector, connector);

	slave = con->encoder_slave;
	if (slave == NULL) {
		dev_err(connector->dev->dev,
			"connector_best_encoder: cannot find slave encoder for connector\n");
		return NULL;
	}

	return &slave->base;
}

static enum drm_connector_status
arcpgu_drm_connector_detect(struct drm_connector *connector, bool force)
{
	enum drm_connector_status status = connector_status_unknown;
	const struct drm_encoder_slave_funcs *sfuncs;
	struct drm_encoder_slave *slave;

	struct arcpgu_drm_connector *con =
		container_of(connector, struct arcpgu_drm_connector, connector);

	slave = con->encoder_slave;
	if (slave == NULL) {
		dev_err(connector->dev->dev,
			"connector_detect: cannot find slave encoder for connector\n");
		return status;
	}

	sfuncs = slave->slave_funcs;
	if (sfuncs && sfuncs->detect)
		return sfuncs->detect(&slave->base, connector);

	dev_err(connector->dev->dev, "connector_detect: could not detect slave funcs\n");
	return status;
}

static void arcpgu_drm_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_helper_funcs
arcpgu_drm_connector_helper_funcs = {
	.get_modes = arcpgu_drm_connector_get_modes,
	.best_encoder = arcpgu_drm_connector_best_encoder,
};

static const struct drm_connector_funcs arcpgu_drm_connector_funcs = {
	.dpms = drm_helper_connector_dpms,
	.reset = drm_atomic_helper_connector_reset,
	.detect = arcpgu_drm_connector_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = arcpgu_drm_connector_destroy,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static struct drm_encoder_helper_funcs arcpgu_drm_encoder_helper_funcs = {
	.dpms = drm_i2c_encoder_dpms,
	.mode_fixup = drm_i2c_encoder_mode_fixup,
	.mode_set = drm_i2c_encoder_mode_set,
	.prepare = drm_i2c_encoder_prepare,
	.commit = drm_i2c_encoder_commit,
	.detect = drm_i2c_encoder_detect,
};

static struct drm_encoder_funcs arcpgu_drm_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

int arcpgu_drm_hdmi_init(struct drm_device *drm, struct device_node *np)
{
	struct arcpgu_drm_connector *arcpgu_connector;
	struct drm_i2c_encoder_driver *driver;
	struct drm_encoder_slave *encoder;
	struct drm_connector *connector;
	struct i2c_client *i2c_slave;
	int ret;

	encoder = devm_kzalloc(drm->dev, sizeof(*encoder), GFP_KERNEL);
	if (encoder == NULL)
		return -ENOMEM;

	i2c_slave = of_find_i2c_device_by_node(np);
	if (!i2c_slave || !i2c_get_clientdata(i2c_slave)) {
		dev_err(drm->dev, "failed to find i2c slave encoder\n");
		return -EPROBE_DEFER;
	}

	if (i2c_slave->dev.driver == NULL) {
		dev_err(drm->dev, "failed to find i2c slave driver\n");
		return -EPROBE_DEFER;
	}

	driver =
	    to_drm_i2c_encoder_driver(to_i2c_driver(i2c_slave->dev.driver));
	ret = driver->encoder_init(i2c_slave, drm, encoder);
	if (ret) {
		dev_err(drm->dev, "failed to initialize i2c encoder slave\n");
		return ret;
	}

	encoder->base.possible_crtcs = 1;
	encoder->base.possible_clones = 0;
	ret = drm_encoder_init(drm, &encoder->base, &arcpgu_drm_encoder_funcs,
			       DRM_MODE_ENCODER_TMDS, NULL);
	if (ret)
		return ret;

	drm_encoder_helper_add(&encoder->base,
			       &arcpgu_drm_encoder_helper_funcs);

	arcpgu_connector = devm_kzalloc(drm->dev, sizeof(*arcpgu_connector),
					GFP_KERNEL);
	if (!arcpgu_connector) {
		ret = -ENOMEM;
		goto error_encoder_cleanup;
	}

	connector = &arcpgu_connector->connector;
	drm_connector_helper_add(connector, &arcpgu_drm_connector_helper_funcs);
	ret = drm_connector_init(drm, connector, &arcpgu_drm_connector_funcs,
			DRM_MODE_CONNECTOR_HDMIA);
	if (ret < 0) {
		dev_err(drm->dev, "failed to initialize drm connector\n");
		goto error_encoder_cleanup;
	}

	ret = drm_mode_connector_attach_encoder(connector, &encoder->base);
	if (ret < 0) {
		dev_err(drm->dev, "could not attach connector to encoder\n");
		drm_connector_unregister(connector);
		goto error_connector_cleanup;
	}

	arcpgu_connector->encoder_slave = encoder;

	return 0;

error_connector_cleanup:
	drm_connector_cleanup(connector);

error_encoder_cleanup:
	drm_encoder_cleanup(&encoder->base);
	return ret;
}
