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
extern int dump_sensor;

int s5k83a_probe(struct sd *sd);
int s5k83a_init(struct sd *sd);
int s5k83a_start(struct sd *sd);
int s5k83a_stop(struct sd *sd);
void s5k83a_disconnect(struct sd *sd);

static const struct m5602_sensor s5k83a = {
	.name = "S5K83A",
	.probe = s5k83a_probe,
	.init = s5k83a_init,
	.start = s5k83a_start,
	.stop = s5k83a_stop,
	.disconnect = s5k83a_disconnect,
	.i2c_slave_id = 0x5a,
	.i2c_regW = 2,
};

struct s5k83a_priv {
	/* We use another thread periodically
	   probing the orientation of the camera */
	struct task_struct *rotation_thread;
	s32 *settings;
};

static const unsigned char preinit_s5k83a[][4] = {
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d, 0x00},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00, 0x00},

	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},
};

/* This could probably be considerably shortened.
   I don't have the hardware to experiment with it, patches welcome
*/
static const unsigned char init_s5k83a[][4] = {
	/* The following sequence is useless after a clean boot
	   but is necessary after resume from suspend */
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K83A_PAGE_MAP, 0x04, 0x00},
	{SENSOR, 0xaf, 0x01, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x00, 0x00},
	{SENSOR, 0x7b, 0xff, 0x00},
	{SENSOR, S5K83A_PAGE_MAP, 0x05, 0x00},
	{SENSOR, 0x01, 0x50, 0x00},
	{SENSOR, 0x12, 0x20, 0x00},
	{SENSOR, 0x17, 0x40, 0x00},
	{SENSOR, 0x1c, 0x00, 0x00},
	{SENSOR, 0x02, 0x70, 0x00},
	{SENSOR, 0x03, 0x0b, 0x00},
	{SENSOR, 0x04, 0xf0, 0x00},
	{SENSOR, 0x05, 0x0b, 0x00},
	{SENSOR, 0x06, 0x71, 0x00},
	{SENSOR, 0x07, 0xe8, 0x00}, /* 488 */
	{SENSOR, 0x08, 0x02, 0x00},
	{SENSOR, 0x09, 0x88, 0x00}, /* 648 */
	{SENSOR, 0x14, 0x00, 0x00},
	{SENSOR, 0x15, 0x20, 0x00}, /* 32 */
	{SENSOR, 0x19, 0x00, 0x00},
	{SENSOR, 0x1a, 0x98, 0x00}, /* 152 */
	{SENSOR, 0x0f, 0x02, 0x00},
	{SENSOR, 0x10, 0xe5, 0x00}, /* 741 */
	/* normal colors
	(this is value after boot, but after tries can be different) */
	{SENSOR, 0x00, 0x06, 0x00},
};

static const unsigned char start_s5k83a[][4] = {
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x09, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe4, 0x00}, /* 484 */
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x7f, 0x00}, /* 639 */
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
};
#endif
