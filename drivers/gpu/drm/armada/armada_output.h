/*
 * Copyright (C) 2012 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ARMADA_CONNETOR_H
#define ARMADA_CONNETOR_H

#define encoder_helper_funcs(encoder) \
	((struct drm_encoder_helper_funcs *)encoder->helper_private)

struct armada_output_type {
	int connector_type;
	enum drm_connector_status (*detect)(struct drm_connector *, bool);
	int (*create)(struct drm_connector *, const void *);
	int (*set_property)(struct drm_connector *, struct drm_property *,
			    uint64_t);
};

struct drm_encoder *armada_drm_connector_encoder(struct drm_connector *conn);

void armada_drm_encoder_prepare(struct drm_encoder *encoder);
void armada_drm_encoder_commit(struct drm_encoder *encoder);

bool armada_drm_encoder_mode_fixup(struct drm_encoder *encoder,
	const struct drm_display_mode *mode, struct drm_display_mode *adj);

int armada_drm_slave_encoder_mode_valid(struct drm_connector *conn,
	struct drm_display_mode *mode);

int armada_drm_slave_encoder_set_property(struct drm_connector *conn,
	struct drm_property *property, uint64_t value);

int armada_output_create(struct drm_device *dev,
	const struct armada_output_type *type, const void *data);

#endif
