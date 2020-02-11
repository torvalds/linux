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

int ccs_read_addr_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int ccs_read_addr(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int ccs_read_addr_8only(struct smiapp_sensor *sensor, u32 reg, u32 *val);
int ccs_write_addr_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 val);
int ccs_write_addr(struct smiapp_sensor *sensor, u32 reg, u32 val);

unsigned int ccs_reg_width(u32 reg);

#define ccs_read(sensor, reg_name, val) \
	ccs_read_addr(sensor, CCS_R_##reg_name, val)

#define ccs_write(sensor, reg_name, val) \
	ccs_write_addr(sensor, CCS_R_##reg_name, val)

#endif
