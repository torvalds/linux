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

#ifndef __RCAR_DU_HDMIENC_H__
#define __RCAR_DU_HDMIENC_H__

#include <linux/module.h>

struct device_node;
struct rcar_du_device;
struct rcar_du_encoder;

#if IS_ENABLED(CONFIG_DRM_RCAR_HDMI)
int rcar_du_hdmienc_init(struct rcar_du_device *rcdu,
			 struct rcar_du_encoder *renc, struct device_node *np);
#else
static inline int rcar_du_hdmienc_init(struct rcar_du_device *rcdu,
				       struct rcar_du_encoder *renc,
				       struct device_node *np)
{
	return -ENOSYS;
}
#endif

#endif /* __RCAR_DU_HDMIENC_H__ */
