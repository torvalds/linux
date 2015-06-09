/*
 * rcar_du_lvdscon.h  --  R-Car Display Unit LVDS Connector
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

#ifndef __RCAR_DU_LVDSCON_H__
#define __RCAR_DU_LVDSCON_H__

struct rcar_du_device;
struct rcar_du_encoder;

int rcar_du_lvds_connector_init(struct rcar_du_device *rcdu,
				struct rcar_du_encoder *renc,
				struct device_node *np);

#endif /* __RCAR_DU_LVDSCON_H__ */
