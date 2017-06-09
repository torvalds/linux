/*
 * rcar_du_encoder.h  --  R-Car Display Unit Encoder
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

#ifndef __RCAR_DU_ENCODER_H__
#define __RCAR_DU_ENCODER_H__

#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>

struct drm_panel;
struct rcar_du_device;
struct rcar_du_lvdsenc;

struct rcar_du_encoder {
	struct drm_encoder base;
	enum rcar_du_output output;
	struct rcar_du_connector *connector;
	struct rcar_du_lvdsenc *lvds;
};

#define to_rcar_encoder(e) \
	container_of(e, struct rcar_du_encoder, base)

#define rcar_encoder_to_drm_encoder(e)	(&(e)->base)

struct rcar_du_connector {
	struct drm_connector connector;
	struct rcar_du_encoder *encoder;
	struct drm_panel *panel;
};

#define to_rcar_connector(c) \
	container_of(c, struct rcar_du_connector, connector)

int rcar_du_encoder_init(struct rcar_du_device *rcdu,
			 enum rcar_du_output output,
			 struct device_node *enc_node,
			 struct device_node *con_node);

#endif /* __RCAR_DU_ENCODER_H__ */
