/*
 * rcar_du_encoder.h  --  R-Car Display Unit Encoder
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

#ifndef __RCAR_DU_ENCODER_H__
#define __RCAR_DU_ENCODER_H__

#include <linux/platform_data/rcar-du.h>

#include <drm/drm_crtc.h>

struct rcar_du_device;

struct rcar_du_encoder {
	struct drm_encoder encoder;
	unsigned int output;
};

#define to_rcar_encoder(e) \
	container_of(e, struct rcar_du_encoder, encoder)

struct rcar_du_connector {
	struct drm_connector connector;
	struct rcar_du_encoder *encoder;
};

#define to_rcar_connector(c) \
	container_of(c, struct rcar_du_connector, connector)

struct drm_encoder *
rcar_du_connector_best_encoder(struct drm_connector *connector);

int rcar_du_encoder_init(struct rcar_du_device *rcdu,
			 enum rcar_du_encoder_type type, unsigned int output,
			 const struct rcar_du_encoder_data *data);

#endif /* __RCAR_DU_ENCODER_H__ */
