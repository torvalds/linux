/*
 * rcar_du_kms.h  --  R-Car Display Unit Mode Setting
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

#ifndef __RCAR_DU_KMS_H__
#define __RCAR_DU_KMS_H__

#include <linux/types.h>

#include <drm/drm_crtc.h>

struct rcar_du_device;

struct rcar_du_format_info {
	u32 fourcc;
	unsigned int bpp;
	unsigned int planes;
	unsigned int pnmr;
	unsigned int edf;
};

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

const struct rcar_du_format_info *rcar_du_format_info(u32 fourcc);

struct drm_encoder *
rcar_du_connector_best_encoder(struct drm_connector *connector);
void rcar_du_encoder_mode_prepare(struct drm_encoder *encoder);
void rcar_du_encoder_mode_set(struct drm_encoder *encoder,
			      struct drm_display_mode *mode,
			      struct drm_display_mode *adjusted_mode);
void rcar_du_encoder_mode_commit(struct drm_encoder *encoder);

int rcar_du_modeset_init(struct rcar_du_device *rcdu);

#endif /* __RCAR_DU_KMS_H__ */
