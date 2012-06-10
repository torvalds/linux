/*
 * include/media/smiapp/smiapp-regs.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef SMIAPP_REGS_H
#define SMIAPP_REGS_H

#include <linux/i2c.h>
#include <linux/types.h>

/* Use upper 8 bits of the type field for flags */
#define SMIA_REG_FLAG_FLOAT		(1 << 24)

#define SMIA_REG_8BIT			1
#define SMIA_REG_16BIT			2
#define SMIA_REG_32BIT			4
struct smia_reg {
	u16 type;
	u16 reg;			/* 16-bit offset */
	u32 val;			/* 8/16/32-bit value */
};

struct smiapp_sensor;

int smiapp_read(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_read_8only(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_write(struct smiapp_sensor *sensor, u32 reg, u32 val);

#endif
