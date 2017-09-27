/*
 * Driver for the po1030 sensor.
 *
 * Copyright (c) 2008 Erik Andrén
 * Copyright (c) 2007 Ilyes Gouta. Based on the m5603x Linux Driver Project.
 * Copyright (c) 2005 m5603x Linux Driver Project <m5602@x3ng.com.br>
 *
 * Portions of code to USB interface and ALi driver software,
 * Copyright (c) 2006 Willem Duinker
 * v4l2 interface modeled after the V4L2 driver
 * for SN9C10x PC Camera Controllers
 *
 * Register defines taken from Pascal Stangs Procyon Armlib
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2.
 *
 */

#ifndef M5602_PO1030_H_
#define M5602_PO1030_H_

#include "m5602_sensor.h"

/*****************************************************************************/

#define PO1030_DEVID_H		0x00
#define PO1030_DEVID_L		0x01
#define PO1030_FRAMEWIDTH_H	0x04
#define PO1030_FRAMEWIDTH_L	0x05
#define PO1030_FRAMEHEIGHT_H	0x06
#define PO1030_FRAMEHEIGHT_L	0x07
#define PO1030_WINDOWX_H	0x08
#define PO1030_WINDOWX_L	0x09
#define PO1030_WINDOWY_H	0x0a
#define PO1030_WINDOWY_L	0x0b
#define PO1030_WINDOWWIDTH_H	0x0c
#define PO1030_WINDOWWIDTH_L	0x0d
#define PO1030_WINDOWHEIGHT_H	0x0e
#define PO1030_WINDOWHEIGHT_L	0x0f

#define PO1030_GLOBALIBIAS	0x12
#define PO1030_PIXELIBIAS	0x13

#define PO1030_GLOBALGAIN	0x15
#define PO1030_RED_GAIN		0x16
#define PO1030_GREEN_1_GAIN	0x17
#define PO1030_BLUE_GAIN	0x18
#define PO1030_GREEN_2_GAIN	0x19

#define PO1030_INTEGLINES_H	0x1a
#define PO1030_INTEGLINES_M	0x1b
#define PO1030_INTEGLINES_L	0x1c

#define PO1030_CONTROL1		0x1d
#define PO1030_CONTROL2		0x1e
#define PO1030_CONTROL3		0x1f
#define PO1030_CONTROL4		0x20

#define PO1030_PERIOD50_H	0x23
#define PO1030_PERIOD50_L	0x24
#define PO1030_PERIOD60_H	0x25
#define PO1030_PERIOD60_L	0x26
#define PO1030_REGCLK167	0x27
#define PO1030_FLICKER_DELTA50	0x28
#define PO1030_FLICKERDELTA60	0x29

#define PO1030_ADCOFFSET	0x2c

/* Gamma Correction Coeffs */
#define PO1030_GC0		0x2d
#define PO1030_GC1		0x2e
#define PO1030_GC2		0x2f
#define PO1030_GC3		0x30
#define PO1030_GC4		0x31
#define PO1030_GC5		0x32
#define PO1030_GC6		0x33
#define PO1030_GC7		0x34

/* Color Transform Matrix */
#define PO1030_CT0		0x35
#define PO1030_CT1		0x36
#define PO1030_CT2		0x37
#define PO1030_CT3		0x38
#define PO1030_CT4		0x39
#define PO1030_CT5		0x3a
#define PO1030_CT6		0x3b
#define PO1030_CT7		0x3c
#define PO1030_CT8		0x3d

#define PO1030_AUTOCTRL1	0x3e
#define PO1030_AUTOCTRL2	0x3f

#define PO1030_YTARGET		0x40
#define PO1030_GLOBALGAINMIN	0x41
#define PO1030_GLOBALGAINMAX	0x42

#define PO1030_AWB_RED_TUNING	0x47
#define PO1030_AWB_BLUE_TUNING	0x48

/* Output format control */
#define PO1030_OUTFORMCTRL1	0x5a
#define PO1030_OUTFORMCTRL2	0x5b
#define PO1030_OUTFORMCTRL3	0x5c
#define PO1030_OUTFORMCTRL4	0x5d
#define PO1030_OUTFORMCTRL5	0x5e

#define PO1030_EDGE_ENH_OFF	0x5f
#define PO1030_EGA		0x60

#define PO1030_Cb_U_GAIN	0x63
#define PO1030_Cr_V_GAIN	0x64

#define PO1030_YCONTRAST	0x74
#define PO1030_YSATURATION	0x75

#define PO1030_HFLIP		(1 << 7)
#define PO1030_VFLIP		(1 << 6)

#define PO1030_HREF_ENABLE	(1 << 6)

#define PO1030_RAW_RGB_BAYER	0x4

#define PO1030_FRAME_EQUAL	(1 << 3)
#define PO1030_AUTO_SUBSAMPLING (1 << 4)

#define PO1030_WEIGHT_WIN_2X	(1 << 3)

#define PO1030_SHUTTER_MODE	(1 << 6)
#define PO1030_AUTO_SUBSAMPLING	(1 << 4)
#define PO1030_FRAME_EQUAL	(1 << 3)

#define PO1030_SENSOR_RESET	(1 << 5)

#define PO1030_SUBSAMPLING	(1 << 6)

/*****************************************************************************/

#define PO1030_GLOBAL_GAIN_DEFAULT	0x12
#define PO1030_EXPOSURE_DEFAULT		0x0085
#define PO1030_BLUE_GAIN_DEFAULT	0x36
#define PO1030_RED_GAIN_DEFAULT		0x36
#define PO1030_GREEN_GAIN_DEFAULT	0x40

/*****************************************************************************/

/* Kernel module parameters */
extern int force_sensor;
extern bool dump_sensor;

int po1030_probe(struct sd *sd);
int po1030_init(struct sd *sd);
int po1030_init_controls(struct sd *sd);
int po1030_start(struct sd *sd);
void po1030_disconnect(struct sd *sd);

static const struct m5602_sensor po1030 = {
	.name = "PO1030",

	.i2c_slave_id = 0xdc,
	.i2c_regW = 1,

	.probe = po1030_probe,
	.init = po1030_init,
	.init_controls = po1030_init_controls,
	.start = po1030_start,
	.disconnect = po1030_disconnect,
};
#endif
