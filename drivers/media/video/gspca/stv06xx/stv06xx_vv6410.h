/*
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * P/N 861037:      Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0010: Sensor HDCS1000        ASIC STV0600
 * P/N 861050-0020: Sensor Photobit PB100  ASIC STV0600-1 - QuickCam Express
 * P/N 861055:      Sensor ST VV6410       ASIC STV0610   - LEGO cam
 * P/N 861075-0040: Sensor HDCS1000        ASIC
 * P/N 961179-0700: Sensor ST VV6410       ASIC STV0602   - Dexxa WebCam USB
 * P/N 861040-0000: Sensor ST VV6410       ASIC STV0610   - QuickCam Web
 */

#ifndef STV06XX_VV6410_H_
#define STV06XX_VV6410_H_

#include "stv06xx_sensor.h"

#define VV6410_COLS			416
#define VV6410_ROWS			320

/* Status registers */
/* Chip identification number including revision indicator */
#define VV6410_DEVICEH			0x00
#define VV6410_DEVICEL			0x01

/* User can determine whether timed I2C data
   has been consumed by interrogating flag states */
#define VV6410_STATUS0			0x02

/* Current line counter value */
#define VV6410_LINECOUNTH		0x03
#define VV6410_LINECOUNTL		0x04

/* End x coordinate of image size */
#define VV6410_XENDH			0x05
#define VV6410_XENDL			0x06

/* End y coordinate of image size */
#define VV6410_YENDH			0x07
#define VV6410_YENDL			0x08

/* This is the average pixel value returned from the
   dark line offset cancellation algorithm */
#define VV6410_DARKAVGH			0x09
#define VV6410_DARKAVGL			0x0a

/* This is the average pixel value returned from the
   black line offset cancellation algorithm  */
#define VV6410_BLACKAVGH		0x0b
#define VV6410_BLACKAVGL		0x0c

/* Flags to indicate whether the x or y image coordinates have been clipped */
#define VV6410_STATUS1			0x0d

/* Setup registers */

/* Low-power/sleep modes & video timing */
#define VV6410_SETUP0			0x10

/* Various parameters */
#define VV6410_SETUP1			0x11

/* Contains pixel counter reset value used by external sync */
#define VV6410_SYNCVALUE		0x12

/* Frame grabbing modes (FST, LST and QCK) */
#define VV6410_FGMODES			0x14

/* FST and QCK mapping modes. */
#define VV6410_PINMAPPING		0x15

/* Data resolution */
#define VV6410_DATAFORMAT		0x16

/* Output coding formats */
#define VV6410_OPFORMAT			0x17

/* Various mode select bits */
#define VV6410_MODESELECT		0x18

/* Exposure registers */
/* Fine exposure. */
#define VV6410_FINEH			0x20
#define VV6410_FINEL			0x21

/* Coarse exposure */
#define VV6410_COARSEH			0x22
#define VV6410_COARSEL			0x23

/* Analog gain setting */
#define VV6410_ANALOGGAIN		0x24

/* Clock division */
#define VV6410_CLKDIV			0x25

/* Dark line offset cancellation value */
#define VV6410_DARKOFFSETH		0x2c
#define VV6410_DARKOFFSETL		0x2d

/* Dark line offset cancellation enable */
#define VV6410_DARKOFFSETSETUP		0x2e

/* Video timing registers */
/* Line Length (Pixel Clocks) */
#define VV6410_LINELENGTHH		0x52
#define VV6410_LINELENGTHL		0x53

/* X-co-ordinate of top left corner of region of interest (x-offset) */
#define VV6410_XOFFSETH			0x57
#define VV6410_XOFFSETL			0x58

/* Y-coordinate of top left corner of region of interest (y-offset) */
#define VV6410_YOFFSETH			0x59
#define VV6410_YOFFSETL			0x5a

/* Field length (Lines) */
#define VV6410_FIELDLENGTHH		0x61
#define VV6410_FIELDLENGTHL		0x62

/* System registers */
/* Black offset cancellation default value */
#define VV6410_BLACKOFFSETH		0x70
#define VV6410_BLACKOFFSETL		0x71

/* Black offset cancellation setup */
#define VV6410_BLACKOFFSETSETUP		0x72

/* Analog Control Register 0 */
#define VV6410_CR0			0x75

/* Analog Control Register 1 */
#define VV6410_CR1			0x76

/* ADC Setup Register */
#define VV6410_AS0			0x77

/* Analog Test Register */
#define VV6410_AT0			0x78

/* Audio Amplifier Setup Register */
#define VV6410_AT1			0x79

#define VV6410_HFLIP 			(1 << 3)
#define VV6410_VFLIP 			(1 << 4)

#define VV6410_LOW_POWER_MODE		(1 << 0)
#define VV6410_SOFT_RESET		(1 << 2)
#define VV6410_PAL_25_FPS		(0 << 3)

#define VV6410_CLK_DIV_2		(1 << 1)

#define VV6410_FINE_EXPOSURE		320
#define VV6410_COARSE_EXPOSURE		192
#define VV6410_DEFAULT_GAIN		5

#define VV6410_SUBSAMPLE		0x01
#define VV6410_CROP_TO_QVGA		0x02

#define VV6410_CIF_LINELENGTH		415

static int vv6410_probe(struct sd *sd);
static int vv6410_start(struct sd *sd);
static int vv6410_init(struct sd *sd);
static int vv6410_stop(struct sd *sd);
static int vv6410_dump(struct sd *sd);
static void vv6410_disconnect(struct sd *sd);

/* V4L2 controls supported by the driver */
static int vv6410_get_hflip(struct gspca_dev *gspca_dev, __s32 *val);
static int vv6410_set_hflip(struct gspca_dev *gspca_dev, __s32 val);
static int vv6410_get_vflip(struct gspca_dev *gspca_dev, __s32 *val);
static int vv6410_set_vflip(struct gspca_dev *gspca_dev, __s32 val);
static int vv6410_get_analog_gain(struct gspca_dev *gspca_dev, __s32 *val);
static int vv6410_set_analog_gain(struct gspca_dev *gspca_dev, __s32 val);
static int vv6410_get_exposure(struct gspca_dev *gspca_dev, __s32 *val);
static int vv6410_set_exposure(struct gspca_dev *gspca_dev, __s32 val);

const struct stv06xx_sensor stv06xx_sensor_vv6410 = {
	.name = "ST VV6410",
	.i2c_flush = 5,
	.i2c_addr = 0x20,
	.i2c_len = 1,
	.init = vv6410_init,
	.probe = vv6410_probe,
	.start = vv6410_start,
	.stop = vv6410_stop,
	.dump = vv6410_dump,
	.disconnect = vv6410_disconnect,
};

/* If NULL, only single value to write, stored in len */
struct stv_init {
	const u8 *data;
	u16 start;
	u8 len;
};

static const u8 x1500[] = {	/* 0x1500 - 0x150f */
	0x0b, 0xa7, 0xb7, 0x00, 0x00
};

static const u8 x1536[] = {	/* 0x1536 - 0x153b */
	0x02, 0x00, 0x60, 0x01, 0x20, 0x01
};

static const u8 x15c1[] = {	/* 0x15c1 - 0x15c2 */
	0xff, 0x03 /* Output word 0x03ff = 1023 (ISO size) */
};

static const struct stv_init stv_bridge_init[] = {
	/* This reg is written twice. Some kind of reset? */
	{NULL,  0x1620, 0x80},
	{NULL,  0x1620, 0x00},
	{NULL,  0x1423, 0x04},
	{x1500, 0x1500, ARRAY_SIZE(x1500)},
	{x1536, 0x1536, ARRAY_SIZE(x1536)},
	{x15c1, 0x15c1, ARRAY_SIZE(x15c1)}
};

static const u8 vv6410_sensor_init[][2] = {
	/* Setup registers */
	{VV6410_SETUP0,		VV6410_SOFT_RESET},
	{VV6410_SETUP0,		VV6410_LOW_POWER_MODE},
	/* Use shuffled read-out mode */
	{VV6410_SETUP1,		BIT(6)},
	/* All modes to 1 */
	{VV6410_FGMODES,	BIT(6) | BIT(4) | BIT(2) | BIT(0)},
	{VV6410_PINMAPPING,	0x00},
	/* Pre-clock generator divide off */
	{VV6410_DATAFORMAT,	BIT(7) | BIT(0)},

	{VV6410_CLKDIV,		VV6410_CLK_DIV_2},

	/* System registers */
	/* Enable voltage doubler */
	{VV6410_AS0,		BIT(6) | BIT(4) | BIT(3) | BIT(2) | BIT(1)},
	{VV6410_AT0,		0x00},
	/* Power up audio, differential */
	{VV6410_AT1,		BIT(4)|BIT(0)},
};

#endif
