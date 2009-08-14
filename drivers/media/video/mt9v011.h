/*
 * mt9v011 -Micron 1/4-Inch VGA Digital Image Sensor
 *
 * Copyright (c) 2009 Mauro Carvalho Chehab (mchehab@redhat.com)
 * This code is placed under the terms of the GNU General Public License v2
 */

#ifndef MT9V011_H_
#define MT9V011_H_

#define R00_MT9V011_CHIP_VERSION	0x00
#define R01_MT9V011_ROWSTART		0x01
#define R02_MT9V011_COLSTART		0x02
#define R03_MT9V011_HEIGHT		0x03
#define R04_MT9V011_WIDTH		0x04
#define R05_MT9V011_HBLANK		0x05
#define R06_MT9V011_VBLANK		0x06
#define R07_MT9V011_OUT_CTRL		0x07
#define R09_MT9V011_SHUTTER_WIDTH	0x09
#define R0A_MT9V011_CLK_SPEED		0x0a
#define R0B_MT9V011_RESTART		0x0b
#define R0C_MT9V011_SHUTTER_DELAY	0x0c
#define R0D_MT9V011_RESET		0x0d
#define R1E_MT9V011_DIGITAL_ZOOM	0x1e
#define R20_MT9V011_READ_MODE		0x20
#define R2B_MT9V011_GREEN_1_GAIN	0x2b
#define R2C_MT9V011_BLUE_GAIN		0x2c
#define R2D_MT9V011_RED_GAIN		0x2d
#define R2E_MT9V011_GREEN_2_GAIN	0x2e
#define R35_MT9V011_GLOBAL_GAIN		0x35
#define RF1_MT9V011_CHIP_ENABLE		0xf1

#define MT9V011_VERSION			0x8243

#endif
