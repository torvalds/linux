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
#define S5K4AA_GAIN_1			0x1f /* (digital?) gain : 5 bits */
#define S5K4AA_GAIN_2			0x20 /* (analogue?) gain : 7 bits */

#define S5K4AA_RM_ROW_SKIP_4X		0x08
#define S5K4AA_RM_ROW_SKIP_2X		0x04
#define S5K4AA_RM_COL_SKIP_4X		0x02
#define S5K4AA_RM_COL_SKIP_2X		0x01
#define S5K4AA_RM_H_FLIP		0x40
#define S5K4AA_RM_V_FLIP		0x80

/*****************************************************************************/

/* Kernel module parameters */
extern int force_sensor;
extern int dump_sensor;

int s5k4aa_probe(struct sd *sd);
int s5k4aa_init(struct sd *sd);
int s5k4aa_start(struct sd *sd);
int s5k4aa_power_down(struct sd *sd);

int s5k4aa_get_exposure(struct gspca_dev *gspca_dev, __s32 *val);
int s5k4aa_set_exposure(struct gspca_dev *gspca_dev, __s32 val);
int s5k4aa_get_vflip(struct gspca_dev *gspca_dev, __s32 *val);
int s5k4aa_set_vflip(struct gspca_dev *gspca_dev, __s32 val);
int s5k4aa_get_hflip(struct gspca_dev *gspca_dev, __s32 *val);
int s5k4aa_set_hflip(struct gspca_dev *gspca_dev, __s32 val);
int s5k4aa_get_gain(struct gspca_dev *gspca_dev, __s32 *val);
int s5k4aa_set_gain(struct gspca_dev *gspca_dev, __s32 val);

static const struct m5602_sensor s5k4aa = {
	.name = "S5K4AA",
	.probe = s5k4aa_probe,
	.init = s5k4aa_init,
	.start = s5k4aa_start,
	.power_down = s5k4aa_power_down,
	.i2c_slave_id = 0x5a,
	.i2c_regW = 2,
};

static const unsigned char preinit_s5k4aa[][4] =
{
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d, 0x00},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00, 0x00},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},

	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x14, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K4AA_PAGE_MAP, 0x00, 0x00}
};

static const unsigned char init_s5k4aa[][4] =
{
	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x0d, 0x00},
	{BRIDGE, M5602_XB_SENSOR_CTRL, 0x00, 0x00},

	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x08, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0xb0, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0x80, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x3f, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_L, 0xff, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},

	{BRIDGE, M5602_XB_MCU_CLK_DIV, 0x02, 0x00},
	{BRIDGE, M5602_XB_MCU_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x14, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xf0, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR, 0x1d, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT, 0x1c, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DIR_H, 0x06, 0x00},
	{BRIDGE, M5602_XB_GPIO_DAT_H, 0x00, 0x00},
	{BRIDGE, M5602_XB_GPIO_EN_L, 0x00, 0x00},
	{BRIDGE, M5602_XB_I2C_CLK_DIV, 0x20, 0x00},

	{SENSOR, S5K4AA_PAGE_MAP, 0x07, 0x00},
	{SENSOR, 0x36, 0x01, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x00, 0x00},
	{SENSOR, 0x7b, 0xff, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, 0x0c, 0x05, 0x00},
	{SENSOR, 0x02, 0x0e, 0x00},
	{SENSOR, S5K4AA_GAIN_1, 0x0f, 0x00},
	{SENSOR, S5K4AA_GAIN_2, 0x00, 0x00},
	{SENSOR, S5K4AA_GLOBAL_GAIN__, 0x01, 0x00},
	{SENSOR, 0x11, 0x00, 0x00},
	{SENSOR, 0x12, 0x00, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, S5K4AA_READ_MODE, 0xa0, 0x00},
	{SENSOR, 0x37, 0x00, 0x00},
	{SENSOR, S5K4AA_ROWSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_ROWSTART_LO, 0x2a, 0x00},
	{SENSOR, S5K4AA_COLSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_COLSTART_LO, 0x0b, 0x00},
	{SENSOR, S5K4AA_WINDOW_HEIGHT_HI, 0x03, 0x00},
	{SENSOR, S5K4AA_WINDOW_HEIGHT_LO, 0xc4, 0x00},
	{SENSOR, S5K4AA_WINDOW_WIDTH_HI, 0x05, 0x00},
	{SENSOR, S5K4AA_WINDOW_WIDTH_LO, 0x08, 0x00},
	{SENSOR, S5K4AA_H_BLANK_HI__, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_LO__, 0x48, 0x00},
	{SENSOR, S5K4AA_EXPOSURE_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_EXPOSURE_LO, 0x43, 0x00},
	{SENSOR, 0x11, 0x04, 0x00},
	{SENSOR, 0x12, 0xc3, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},

	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	/* VSYNC_PARA, VSYNC_PARA : img height 480 = 0x01e0 */
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe0, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	/* HSYNC_PARA, HSYNC_PARA : img width 640 = 0x0280 */
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x80, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xa0, 0x00}, /* 48 MHz */

	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, S5K4AA_READ_MODE, S5K4AA_RM_H_FLIP | S5K4AA_RM_ROW_SKIP_2X
		| S5K4AA_RM_COL_SKIP_2X, 0x00},
	/* 0x37 : Fix image stability when light is too bright and improves
	 * image quality in 640x480, but worsens it in 1280x1024 */
	{SENSOR, 0x37, 0x01, 0x00},
	/* ROWSTART_HI, ROWSTART_LO : 10 + (1024-960)/2 = 42 = 0x002a */
	{SENSOR, S5K4AA_ROWSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_ROWSTART_LO, 0x2a, 0x00},
	{SENSOR, S5K4AA_COLSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_COLSTART_LO, 0x0c, 0x00},
	/* window_height_hi, window_height_lo : 960 = 0x03c0 */
	{SENSOR, S5K4AA_WINDOW_HEIGHT_HI, 0x03, 0x00},
	{SENSOR, S5K4AA_WINDOW_HEIGHT_LO, 0xc0, 0x00},
	/* window_width_hi, window_width_lo : 1280 = 0x0500 */
	{SENSOR, S5K4AA_WINDOW_WIDTH_HI, 0x05, 0x00},
	{SENSOR, S5K4AA_WINDOW_WIDTH_LO, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_HI__, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_LO__, 0xa8, 0x00}, /* helps to sync... */
	{SENSOR, S5K4AA_EXPOSURE_HI, 0x01, 0x00},
	{SENSOR, S5K4AA_EXPOSURE_LO, 0x00, 0x00},
	{SENSOR, 0x11, 0x04, 0x00},
	{SENSOR, 0x12, 0xc3, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, 0x02, 0x0e, 0x00},
	{SENSOR_LONG, S5K4AA_GLOBAL_GAIN__, 0x0f, 0x00},
	{SENSOR, S5K4AA_GAIN_1, 0x0b, 0x00},
	{SENSOR, S5K4AA_GAIN_2, 0xa0, 0x00}
};

static const unsigned char VGA_s5k4aa[][4] =
{
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x06, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xb0, 0x00},
	{BRIDGE, M5602_XB_ADC_CTRL, 0xc0, 0x00},
	{BRIDGE, M5602_XB_SENSOR_TYPE, 0x08, 0x00},
	{BRIDGE, M5602_XB_LINE_OF_FRAME_H, 0x81, 0x00},
	{BRIDGE, M5602_XB_PIX_OF_LINE_H, 0x82, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	/* VSYNC_PARA, VSYNC_PARA : img height 480 = 0x01e0 */
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x01, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0xe0, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_VSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x00, 0x00},
	/* HSYNC_PARA, HSYNC_PARA : img width 640 = 0x0280 */
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x02, 0x00},
	{BRIDGE, M5602_XB_HSYNC_PARA, 0x80, 0x00},
	{BRIDGE, M5602_XB_SIG_INI, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_DIV, 0x00, 0x00},
	{BRIDGE, M5602_XB_SEN_CLK_CTRL, 0xa0, 0x00}, /* 48 MHz */

	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, S5K4AA_READ_MODE, S5K4AA_RM_H_FLIP | S5K4AA_RM_ROW_SKIP_2X
		| S5K4AA_RM_COL_SKIP_2X, 0x00},
	/* 0x37 : Fix image stability when light is too bright and improves
	 * image quality in 640x480, but worsens it in 1280x1024 */
	{SENSOR, 0x37, 0x01, 0x00},
	/* ROWSTART_HI, ROWSTART_LO : 10 + (1024-960)/2 = 42 = 0x002a */
	{SENSOR, S5K4AA_ROWSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_ROWSTART_LO, 0x2a, 0x00},
	{SENSOR, S5K4AA_COLSTART_HI, 0x00, 0x00},
	{SENSOR, S5K4AA_COLSTART_LO, 0x0c, 0x00},
	/* window_height_hi, window_height_lo : 960 = 0x03c0 */
	{SENSOR, S5K4AA_WINDOW_HEIGHT_HI, 0x03, 0x00},
	{SENSOR, S5K4AA_WINDOW_HEIGHT_LO, 0xc0, 0x00},
	/* window_width_hi, window_width_lo : 1280 = 0x0500 */
	{SENSOR, S5K4AA_WINDOW_WIDTH_HI, 0x05, 0x00},
	{SENSOR, S5K4AA_WINDOW_WIDTH_LO, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_HI__, 0x00, 0x00},
	{SENSOR, S5K4AA_H_BLANK_LO__, 0xa8, 0x00}, /* helps to sync... */
	{SENSOR, S5K4AA_EXPOSURE_HI, 0x01, 0x00},
	{SENSOR, S5K4AA_EXPOSURE_LO, 0x00, 0x00},
	{SENSOR, 0x11, 0x04, 0x00},
	{SENSOR, 0x12, 0xc3, 0x00},
	{SENSOR, S5K4AA_PAGE_MAP, 0x02, 0x00},
	{SENSOR, 0x02, 0x0e, 0x00},
	{SENSOR_LONG, S5K4AA_GLOBAL_GAIN__, 0x0f, 0x00},
	{SENSOR, S5K4AA_GAIN_1, 0x0b, 0x00},
	{SENSOR, S5K4AA_GAIN_2, 0xa0, 0x00}
};

#endif
