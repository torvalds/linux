/*
 * rcar_du_vga.h  --  R-Car Display Unit VGA DAC and Connector
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

#ifndef __RCAR_DU_VGA_H__
#define __RCAR_DU_VGA_H__

struct rcar_du_device;
struct rcar_du_encoder_vga_data;

int rcar_du_vga_init(struct rcar_du_device *rcdu,
		     const struct rcar_du_encoder_vga_data *data,
		     unsigned int output);

#endif /* __RCAR_DU_VGA_H__ */
