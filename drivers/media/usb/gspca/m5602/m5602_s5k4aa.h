/*
 * Driver for the s5k4aa sensor
 *
 * Copyright (C) 2008 Erik Andrén
 * Copyright (C) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (C) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#ifndef M5602_S5K4AA_H_
#define M5602_S5K4AA_H_

#include <linux/dmi.h>

#include "m5602_sensor.h"

/*****************************************************************************/

#define S5K4AA_PAGE_MAP			0xec

#define S5K4AA_PAGE_MAP_0		0x00
#define S5K4AA_PAGE_MAP_1		0x01
#define S5K4AA_PAGE_MAP_2		0x02

/* Sensor register definitions for page 0x02 */
#define S5K4AA_READ_MODE		0x03
#define S5K4AA_ROWSTART_HI		0x04
#define S5K4AA_ROWSTART_LO		0x05
#define S5K4AA_COLSTART_HI		0x06
#define S5K4AA_COLSTART_LO		0x07
#define S5K4AA_WINDOW_HEIGHT_HI		0x08
#define S5K4AA_WINDOW_HEIGHT_LO		0x09
#define S5K4AA_WINDOW_WIDTH_HI		0x0a
#define S5K4AA_WINDOW_WIDTH_LO		0x0b
#define S5K4AA_GLOBAL_GAIN__		0x0f
/* sync lost, if too low, reduces frame rate if too high */
#define S5K4AA_H_BLANK_HI__		0x1d
#define S5K4AA_H_BLANK_LO__		0x1e
#define S5K4AA_EXPOSURE_HI		0x17
#define S5K4AA_EXPOSURE_LO		0x18
#define S5K4AA_BRIGHTNESS		0x1f /* (digital?) gain : 5 bits */
#define S5K4AA_GAIN			0x20 /* (analogue?) gain : 7 bits */
#define S5K4AA_NOISE_SUPP		0x37

#define S5K4AA_RM_ROW_SKIP_4X		0x08
#define S5K4AA_RM_ROW_SKIP_2X		0x04
#define S5K4AA_RM_COL_SKIP_4X		0x02
#define S5K4AA_RM_COL_SKIP_2X		0x01
#define S5K4AA_RM_H_FLIP		0x40
#define S5K4AA_RM_V_FLIP		0x80

#define S5K4AA_DEFAULT_GAIN		0x5f
#define S5K4AA_DEFAULT_BRIGHTNESS	0x10

/*****************************************************************************/

/* Kernel module parameters */
extern int force_sensor;
extern bool dump_sensor;

int s5k4aa_probe(struct sd *sd);
int s5k4aa_init(struct sd *sd);
int s5k4aa_init_controls(struct sd *sd);
int s5k4aa_start(struct sd *sd);
void s5k4aa_disconnect(struct sd *sd);

static const struct m5602_sensor s5k4aa = {
	.name = "S5K4AA",
	.i2c_slave_id = 0x5a,
	.i2c_regW = 2,

	.probe = s5k4aa_probe,
	.init = s5k4aa_init,
	.init_controls = s5k4aa_init_controls,
	.start = s5k4aa_start,
	.disconnect = s5k4aa_disconnect,
};

#endif
