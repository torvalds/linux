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

#include "ccs-regs.h"

#define SMIAPP_REG_ADDR(reg)		((u16)reg)

struct smiapp_sensor;

int smiapp_read_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_read(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_read_8only(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int smiapp_write_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 val);
int smiapp_write(struct smiapp_sensor *sensor, u32 reg, u32 val);

unsigned int ccs_reg_width(u32 reg);

#define ccs_read(sensor, reg_name, val) \
	smiapp_read(sensor, CCS_R_##reg_name, val)

#define ccs_write(sensor, reg_name, val) \
	smiapp_write(sensor, CCS_R_##reg_name, val)

#endif
