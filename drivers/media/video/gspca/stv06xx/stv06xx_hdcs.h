/*
 * Copyright (c) 2001 Jean-Fredric Clere, Nikolas Zimmermann, Georg Acher
 *		      Mark Cave-Ayland, Carlo E Prelz, Dick Streefland
 * Copyright (c) 2002, 2003 Tuukka Toivonen
 * Copyright (c) 2008 Erik Andrén
 * Copyright (c) 2008 Chia-I Wu
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

#ifndef STV06XX_HDCS_H_
#define STV06XX_HDCS_H_

#include "stv06xx_sensor.h"

#define HDCS_REG_CONFIG(sd)	(IS_1020(sd) ? HDCS20_CONFIG : HDCS00_CONFIG)
#define HDCS_REG_CONTROL(sd)	(IS_1020(sd) ? HDCS20_CONTROL : HDCS00_CONTROL)

#define HDCS_1X00_DEF_WIDTH	360
#define HDCS_1X00_DEF_HEIGHT	296

#define HDCS_1020_DEF_WIDTH	352
#define HDCS_1020_DEF_HEIGHT	292

#define HDCS_1020_BOTTOM_Y_SKIP	4

#define HDCS_CLK_FREQ_MHZ	25

#define HDCS_ADC_START_SIG_DUR	3

/* LSB bit of I2C or register address signifies write (0) or read (1) */
/* I2C Registers common for both HDCS-1000/1100 and HDCS-1020 */
/* Identifications Register */
#define HDCS_IDENT		(0x00 << 1)
/* Status Register */
#define HDCS_STATUS		(0x01 << 1)
/* Interrupt Mask Register */
#define HDCS_IMASK		(0x02 << 1)
/* Pad Control Register */
#define HDCS_PCTRL		(0x03 << 1)
/* Pad Drive Control Register */
#define HDCS_PDRV		(0x04 << 1)
/* Interface Control Register */
#define HDCS_ICTRL		(0x05 << 1)
/* Interface Timing Register */
#define HDCS_ITMG		(0x06 << 1)
/* Baud Fraction Register */
#define HDCS_BFRAC		(0x07 << 1)
/* Baud Rate Register */
#define HDCS_BRATE		(0x08 << 1)
/* ADC Control Register */
#define HDCS_ADCCTRL		(0x09 << 1)
/* First Window Row Register */
#define HDCS_FWROW		(0x0a << 1)
/* First Window Column Register */
#define HDCS_FWCOL		(0x0b << 1)
/* Last Window Row Register */
#define HDCS_LWROW		(0x0c << 1)
/* Last Window Column Register */
#define HDCS_LWCOL		(0x0d << 1)
/* Timing Control Register */
#define HDCS_TCTRL		(0x0e << 1)
/* PGA Gain Register: Even Row, Even Column */
#define HDCS_ERECPGA		(0x0f << 1)
/* PGA Gain Register: Even Row, Odd Column */
#define HDCS_EROCPGA		(0x10 << 1)
/* PGA Gain Register: Odd Row, Even Column */
#define HDCS_ORECPGA		(0x11 << 1)
/* PGA Gain Register: Odd Row, Odd Column */
#define HDCS_OROCPGA		(0x12 << 1)
/* Row Exposure Low Register */
#define HDCS_ROWEXPL		(0x13 << 1)
/* Row Exposure High Register */
#define HDCS_ROWEXPH		(0x14 << 1)

/* I2C Registers only for HDCS-1000/1100 */
/* Sub-Row Exposure Low Register */
#define HDCS00_SROWEXPL		(0x15 << 1)
/* Sub-Row Exposure High Register */
#define HDCS00_SROWEXPH		(0x16 << 1)
/* Configuration Register */
#define HDCS00_CONFIG		(0x17 << 1)
/* Control Register */
#define HDCS00_CONTROL		(0x18 << 1)

/* I2C Registers only for HDCS-1020 */
/* Sub-Row Exposure Register */
#define HDCS20_SROWEXP		(0x15 << 1)
/* Error Control Register */
#define HDCS20_ERROR		(0x16 << 1)
/* Interface Timing 2 Register */
#define HDCS20_ITMG2		(0x17 << 1)
/* Interface Control 2 Register	*/
#define HDCS20_ICTRL2		(0x18 << 1)
/* Horizontal Blank Register */
#define HDCS20_HBLANK		(0x19 << 1)
/* Vertical Blank Register */
#define HDCS20_VBLANK		(0x1a << 1)
/* Configuration Register */
#define HDCS20_CONFIG		(0x1b << 1)
/* Control Register */
#define HDCS20_CONTROL		(0x1c << 1)

#define HDCS_RUN_ENABLE		(1 << 2)
#define HDCS_SLEEP_MODE		(1 << 1)

#define HDCS_DEFAULT_EXPOSURE	48
#define HDCS_DEFAULT_GAIN	128

static int hdcs_probe_1x00(struct sd *sd);
static int hdcs_probe_1020(struct sd *sd);
static int hdcs_start(struct sd *sd);
static int hdcs_init(struct sd *sd);
static int hdcs_stop(struct sd *sd);
static int hdcs_dump(struct sd *sd);
static void hdcs_disconnect(struct sd *sd);

static int hdcs_get_exposure(struct gspca_dev *gspca_dev, __s32 *val);
static int hdcs_set_exposure(struct gspca_dev *gspca_dev, __s32 val);
static int hdcs_set_gain(struct gspca_dev *gspca_dev, __s32 val);
static int hdcs_get_gain(struct gspca_dev *gspca_dev, __s32 *val);

const struct stv06xx_sensor stv06xx_sensor_hdcs1x00 = {
	.name = "HP HDCS-1000/1100",
	.i2c_flush = 0,
	.i2c_addr = (0x55 << 1),
	.i2c_len = 1,

	.init = hdcs_init,
	.probe = hdcs_probe_1x00,
	.start = hdcs_start,
	.stop = hdcs_stop,
	.disconnect = hdcs_disconnect,
	.dump = hdcs_dump,
};

const struct stv06xx_sensor stv06xx_sensor_hdcs1020 = {
	.name = "HDCS-1020",
	.i2c_flush = 0,
	.i2c_addr = (0x55 << 1),
	.i2c_len = 1,

	.init = hdcs_init,
	.probe = hdcs_probe_1020,
	.start = hdcs_start,
	.stop = hdcs_stop,
	.dump = hdcs_dump,
};

static const u16 stv_bridge_init[][2] = {
	{STV_ISO_ENABLE, 0},
	{STV_REG23, 0},
	{STV_REG00, 0x1d},
	{STV_REG01, 0xb5},
	{STV_REG02, 0xa8},
	{STV_REG03, 0x95},
	{STV_REG04, 0x07},

	{STV_SCAN_RATE, 0x20},
	{STV_ISO_SIZE_L, 847},
	{STV_Y_CTRL, 0x01},
	{STV_X_CTRL, 0x0a}
};

static const u8 stv_sensor_init[][2] = {
	/* Clear status (writing 1 will clear the corresponding status bit) */
	{HDCS_STATUS, BIT(6) | BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(1)},
	/* Disable all interrupts */
	{HDCS_IMASK, 0x00},
	{HDCS_PCTRL, BIT(6) | BIT(5) | BIT(1) | BIT(0)},
	{HDCS_PDRV,  0x00},
	{HDCS_ICTRL, BIT(5)},
	{HDCS_ITMG,  BIT(4) | BIT(1)},
	/* ADC output resolution to 10 bits */
	{HDCS_ADCCTRL, 10}
};

#endif
