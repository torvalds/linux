/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * rcar_du_encoder.h  --  R-Car Display Unit Encoder
 *
 * Copyright (C) 2013-2014 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#ifndef __RCAR_DU_ENCODER_H__
#define __RCAR_DU_ENCODER_H__

#include <drm/drm_encoder.h>

struct rcar_du_device;

struct rcar_du_encoder {
	struct drm_encoder base;
	enum rcar_du_output output;
};

#define to_rcar_encoder(e) \
	container_of(e, struct rcar_du_encoder, base)

#define rcar_encoder_to_drm_encoder(e)	(&(e)->base)

int rcar_du_encoder_init(struct rcar_du_device *rcdu,
			 enum rcar_du_output output,
			 struct device_node *enc_node);

#endif /* __RCAR_DU_ENCODER_H__ */
