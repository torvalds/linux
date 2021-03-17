/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * include/media/smiapp/smiapp-regs.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 */

#ifndef SMIAPP_REGS_H
#define SMIAPP_REGS_H

#include <linux/i2c.h>
#include <linux/types.h>

#define SMIAPP_REG_ADDR(reg)		((u16)reg)
#define SMIAPP_REG_WIDTH(reg)		((u8)(reg >> 16))
#define SMIAPP_REG_FLAGS(reg)		((u8)(reg >> 24))

/* Use upper 8 bits of the type field for flags */
#define SMIAPP_REG_FLAG_FLOAT		(1 << 24)

#define SMIAPP_REG_8BIT			1
#define SMIAPP_REG_16BIT		2
#define SMIAPP_REG_32BIT		4

struct smiapp_sensor;

int smiapp_read_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_read(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_read_8only(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_write_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 val);
int smiapp_write(struct smiapp_sensor *sensor, u32 reg, u32 val);

#endif
