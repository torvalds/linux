/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder_slave.h>
#include "armada_output.h"
#include "armada_drm.h"

struct armada_connector {
	struct drm_connector conn;
	const struct armada_output_type *type;
};

#define drm_to_armada_conn(c) container_of(c, struct armada_connector, conn)

struct drm_encoder *armada_drm_connector_encoder(struct drm_connector *conn)
{
	struct drm_encoder *enc = conn->encoder;

	return enc ? enc : drm_encoder_find(conn->dev, conn->encoder_ids[0]);
}

static enum drm_connector_status armada_drm_connector_detect(
	struct drm_connector *conn, bool force)
{
	struct armada_connector *dconn = drm_to_armada_conn(conn);
	enum drm_connector_status status = connector_status_disconnected;

	if (dconn->type->detect) {
		status = dconn->type->detect(conn, force);
	} else {
		struct drm_encoder *enc = armada_drm_connector_encoder(conn);

		if (enc)
			status = encoder_helper_funcs(enc)->detect(enc, conn);
	}

	return status;
}

static void armada_drm_connector_destroy(struct drm_connector *conn)
{
	struct armada_connector *dconn = drm_to_armada_conn(conn);

	drm_connector_unregister(conn);
	drm_connector_cleanup(conn);
	kfree(dconn);
}

static int armada_drm_connector_set_property(struct drm_connector *conn,
	struct drm_property *property, uint64_t value)
{
	struct armada_connector *dconn = drm_to_armada_conn(conn);

	if (!dconn->type->set_property)
		return -EINVAL;

	return dconn->type->set_property(conn, property, value);
}

static const struct drm_connector_funcs armada_drm_conn_funcs = {
	.dpms		= drm_helper_connector_dpms,
	.fill_modes	= drm_helper_probe_single_connector_modes,
	.detect		= armada_drm_connector_detect,
	.destroy	= armada_drm_connector_destroy,
	.set_property	= armada_drm_connector_set_property,
};

void armada_drm_encoder_prepare(struct drm_encoder *encoder)
{
	encoder_helper_funcs(encoder)->dpms(encoder, DRM_MODE_DPMS_OFF);
}

void armada_drm_encoder_commit(struct drm_encoder *encoder)
{
	encoder_helper_funcs(encoder)->dpms(encoder, DRM_MODE_DPMS_ON);
}

bool armada_drm_encoder_mode_fixup(struct drm_encoder *encoder,
	const struct drm_display_mode *mode, struct drm_display_mode *adjusted)
{
	return true;
}

/* Shouldn't this be a generic helper function? */
int armada_drm_slave_encoder_mode_valid(struct drm_connector *conn,
	struct drm_display_mode *mode)
{
	struct drm_encoder *encoder = armada_drm_connector_encoder(conn);
	int valid = MODE_BAD;

	if (encoder) {
		struct drm_encoder_slave *slave = to_encoder_slave(encoder);

		valid = slave->slave_funcs->mode_valid(encoder, mode);
	}
	return valid;
}

int armada_drm_slave_encoder_set_property(struct drm_connector *conn,
	struct drm_property *property, uint64_t value)
{
	struct drm_encoder *encoder = armada_drm_connector_encoder(conn);
	int rc = -EINVAL;

	if (encoder) {
		struct drm_encoder_slave *slave = to_encoder_slave(encoder);

		rc = slave->slave_funcs->set_property(encoder, conn, property,
						      value);
	}
	return rc;
}

int armada_output_create(struct drm_device *dev,
	const struct armada_output_type *type, const void *data)
{
	struct armada_connector *dconn;
	int ret;

	dconn = kzalloc(sizeof(*dconn), GFP_KERNEL);
	if (!dconn)
		return -ENOMEM;

	dconn->type = type;

	ret = drm_connector_init(dev, &dconn->conn, &armada_drm_conn_funcs,
				 type->connector_type);
	if (ret) {
		DRM_ERROR("unable to init connector\n");
		goto err_destroy_dconn;
	}

	ret = type->create(&dconn->conn, data);
	if (ret)
		goto err_conn;

	ret = drm_connector_register(&dconn->conn);
	if (ret)
		goto err_sysfs;

	return 0;

 err_sysfs:
	if (dconn->conn.encoder)
		dconn->conn.encoder->funcs->destroy(dconn->conn.encoder);
 err_conn:
	drm_connector_cleanup(&dconn->conn);
 err_destroy_dconn:
	kfree(dconn);
	return ret;
}
