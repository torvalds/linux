/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * RZ/G2L Display Unit Encoder
 *
 * Copyright (C) 2023 Renesas Electronics Corporation
 *
 * Based on rcar_du_encoder.h
 */

#ifndef __RZG2L_DU_ENCODER_H__
#define __RZG2L_DU_ENCODER_H__

#include <drm/drm_encoder.h>
#include <linux/container_of.h>

struct rzg2l_du_device;

struct rzg2l_du_encoder {
	struct drm_encoder base;
	enum rzg2l_du_output output;
};

static inline struct rzg2l_du_encoder *to_rzg2l_encoder(struct drm_encoder *e)
{
	return container_of(e, struct rzg2l_du_encoder, base);
}

int rzg2l_du_encoder_init(struct rzg2l_du_device *rcdu,
			  enum rzg2l_du_output output,
			  struct device_node *enc_node);

#endif /* __RZG2L_DU_ENCODER_H__ */
