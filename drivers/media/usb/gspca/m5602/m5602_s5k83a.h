/*
 * Driver for the s5k83a sensor
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

#ifndef M5602_S5K83A_H_
#define M5602_S5K83A_H_

#include "m5602_sensor.h"

#define S5K83A_FLIP			0x01
#define S5K83A_HFLIP_TUNE		0x03
#define S5K83A_VFLIP_TUNE		0x05
#define S5K83A_BRIGHTNESS		0x0a
#define S5K83A_EXPOSURE			0x18
#define S5K83A_GAIN			0x1b
#define S5K83A_PAGE_MAP			0xec

#define S5K83A_DEFAULT_GAIN		0x71
#define S5K83A_DEFAULT_BRIGHTNESS	0x7e
#define S5K83A_DEFAULT_EXPOSURE		0x00
#define S5K83A_MAXIMUM_EXPOSURE		0x3c
#define S5K83A_FLIP_MASK		0x10
#define S5K83A_GPIO_LED_MASK		0x10
#define S5K83A_GPIO_ROTATION_MASK	0x40

/*****************************************************************************/

/* Kernel module parameters */
extern int force_sensor;
extern bool dump_sensor;

int s5k83a_probe(struct sd *sd);
int s5k83a_init(struct sd *sd);
int s5k83a_init_controls(struct sd *sd);
int s5k83a_start(struct sd *sd);
int s5k83a_stop(struct sd *sd);
void s5k83a_disconnect(struct sd *sd);

static const struct m5602_sensor s5k83a = {
	.name = "S5K83A",
	.probe = s5k83a_probe,
	.init = s5k83a_init,
	.init_controls = s5k83a_init_controls,
	.start = s5k83a_start,
	.stop = s5k83a_stop,
	.disconnect = s5k83a_disconnect,
	.i2c_slave_id = 0x5a,
	.i2c_regW = 2,
};
#endif
