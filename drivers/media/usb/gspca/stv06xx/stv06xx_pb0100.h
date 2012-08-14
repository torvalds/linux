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

#ifndef STV06XX_PB0100_H_
#define STV06XX_PB0100_H_

#include "stv06xx_sensor.h"

/* mode priv field flags */
#define PB0100_CROP_TO_VGA	0x01
#define PB0100_SUBSAMPLE	0x02

/* I2C Registers */
#define PB_IDENT		0x00	/* Chip Version */
#define PB_RSTART		0x01	/* Row Window Start */
#define PB_CSTART		0x02	/* Column Window Start */
#define PB_RWSIZE		0x03	/* Row Window Size */
#define PB_CWSIZE		0x04	/* Column  Window Size */
#define PB_CFILLIN		0x05	/* Column Fill-In */
#define PB_VBL			0x06	/* Vertical Blank Count */
#define PB_CONTROL		0x07	/* Control Mode */
#define PB_FINTTIME		0x08	/* Integration Time/Frame Unit Count */
#define PB_RINTTIME		0x09	/* Integration Time/Row Unit Count */
#define PB_ROWSPEED		0x0a	/* Row Speed Control */
#define PB_ABORTFRAME		0x0b	/* Abort Frame */
#define PB_R12			0x0c	/* Reserved */
#define PB_RESET		0x0d	/* Reset */
#define PB_EXPGAIN		0x0e	/* Exposure Gain Command */
#define PB_R15			0x0f	/* Expose0 */
#define PB_R16			0x10	/* Expose1 */
#define PB_R17			0x11	/* Expose2 */
#define PB_R18			0x12	/* Low0_DAC */
#define PB_R19			0x13	/* Low1_DAC */
#define PB_R20			0x14	/* Low2_DAC */
#define PB_R21			0x15	/* Threshold11 */
#define PB_R22			0x16	/* Threshold0x */
#define PB_UPDATEINT		0x17	/* Update Interval */
#define PB_R24			0x18	/* High_DAC */
#define PB_R25			0x19	/* Trans0H */
#define PB_R26			0x1a	/* Trans1L */
#define PB_R27			0x1b	/* Trans1H */
#define PB_R28			0x1c	/* Trans2L */
#define PB_R29			0x1d	/* Reserved */
#define PB_R30			0x1e	/* Reserved */
#define PB_R31			0x1f	/* Wait to Read */
#define PB_PREADCTRL		0x20	/* Pixel Read Control Mode */
#define PB_R33			0x21	/* IREF_VLN */
#define PB_R34			0x22	/* IREF_VLP */
#define PB_R35			0x23	/* IREF_VLN_INTEG */
#define PB_R36			0x24	/* IREF_MASTER */
#define PB_R37			0x25	/* IDACP */
#define PB_R38			0x26	/* IDACN */
#define PB_R39			0x27	/* DAC_Control_Reg */
#define PB_R40			0x28	/* VCL */
#define PB_R41			0x29	/* IREF_VLN_ADCIN */
#define PB_R42			0x2a	/* Reserved */
#define PB_G1GAIN		0x2b	/* Green 1 Gain */
#define PB_BGAIN		0x2c	/* Blue Gain */
#define PB_RGAIN		0x2d	/* Red Gain */
#define PB_G2GAIN		0x2e	/* Green 2 Gain */
#define PB_R47			0x2f	/* Dark Row Address */
#define PB_R48			0x30	/* Dark Row Options */
#define PB_R49			0x31	/* Reserved */
#define PB_R50			0x32	/* Image Test Data */
#define PB_ADCMAXGAIN		0x33	/* Maximum Gain */
#define PB_ADCMINGAIN		0x34	/* Minimum Gain */
#define PB_ADCGLOBALGAIN	0x35	/* Global Gain */
#define PB_R54			0x36	/* Maximum Frame */
#define PB_R55			0x37	/* Minimum Frame */
#define PB_R56			0x38	/* Reserved */
#define PB_VOFFSET		0x39	/* VOFFSET */
#define PB_R58			0x3a	/* Snap-Shot Sequence Trigger */
#define PB_ADCGAINH		0x3b	/* VREF_HI */
#define PB_ADCGAINL		0x3c	/* VREF_LO */
#define PB_R61			0x3d	/* Reserved */
#define PB_R62			0x3e	/* Reserved */
#define PB_R63			0x3f	/* Reserved */
#define PB_R64			0x40	/* Red/Blue Gain */
#define PB_R65			0x41	/* Green 2/Green 1 Gain */
#define PB_R66			0x42	/* VREF_HI/LO */
#define PB_R67			0x43	/* Integration Time/Row Unit Count */
#define PB_R240			0xf0	/* ADC Test */
#define PB_R241			0xf1    /* Chip Enable */
#define PB_R242			0xf2	/* Reserved */

static int pb0100_probe(struct sd *sd);
static int pb0100_start(struct sd *sd);
static int pb0100_init(struct sd *sd);
static int pb0100_init_controls(struct sd *sd);
static int pb0100_stop(struct sd *sd);
static int pb0100_dump(struct sd *sd);

/* V4L2 controls supported by the driver */
static int pb0100_set_gain(struct gspca_dev *gspca_dev, __s32 val);
static int pb0100_set_red_balance(struct gspca_dev *gspca_dev, __s32 val);
static int pb0100_set_blue_balance(struct gspca_dev *gspca_dev, __s32 val);
static int pb0100_set_exposure(struct gspca_dev *gspca_dev, __s32 val);
static int pb0100_set_autogain(struct gspca_dev *gspca_dev, __s32 val);
static int pb0100_set_autogain_target(struct gspca_dev *gspca_dev, __s32 val);

const struct stv06xx_sensor stv06xx_sensor_pb0100 = {
	.name = "PB-0100",
	.i2c_flush = 1,
	.i2c_addr = 0xba,
	.i2c_len = 2,

	.min_packet_size = { 635, 847 },
	.max_packet_size = { 847, 923 },

	.init = pb0100_init,
	.init_controls = pb0100_init_controls,
	.probe = pb0100_probe,
	.start = pb0100_start,
	.stop = pb0100_stop,
	.dump = pb0100_dump,
};

#endif
