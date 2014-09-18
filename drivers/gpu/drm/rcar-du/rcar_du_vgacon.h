/*
 * rcar_du_vgacon.h  --  R-Car Display Unit VGA Connector
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

#ifndef __RCAR_DU_VGACON_H__
#define __RCAR_DU_VGACON_H__

struct rcar_du_device;
struct rcar_du_encoder;

int rcar_du_vga_connector_init(struct rcar_du_device *rcdu,
			       struct rcar_du_encoder *renc);

#endif /* __RCAR_DU_VGACON_H__ */
