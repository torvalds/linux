/*
 * rcar_du_lvds.h  --  R-Car Display Unit LVDS Encoder and Connector
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

#ifndef __RCAR_DU_LVDS_H__
#define __RCAR_DU_LVDS_H__

struct rcar_du_device;
struct rcar_du_encoder_lvds_data;

int rcar_du_lvds_init(struct rcar_du_device *rcdu,
		      const struct rcar_du_encoder_lvds_data *data,
		      unsigned int output);

#endif /* __RCAR_DU_LVDS_H__ */
