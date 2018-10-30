/*
 * NXP TDA18250BHN silicon tuner driver
 *
 * Copyright (C) 2017 Olli Salonen <olli.salonen@iki.fi>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 */

#ifndef TDA18250_PRIV_H
#define TDA18250_PRIV_H

#include "tda18250.h"

#define R00_ID1		0x00	/* ID byte 1 */
#define R01_ID2		0x01	/* ID byte 2 */
#define R02_ID3		0x02	/* ID byte 3 */
#define R03_THERMO1	0x03	/* Thermo byte 1 */
#define R04_THERMO2	0x04	/* Thermo byte 2 */
#define R05_POWER1	0x05	/* Power byte 1 */
#define R06_POWER2	0x06	/* Power byte 2 */
#define R07_GPIO	0x07	/* GPIO */
#define R08_IRQ1	0x08	/* IRQ */
#define R09_IRQ2	0x09	/* IRQ */
#define R0A_IRQ3	0x0a	/* IRQ */
#define R0B_IRQ4	0x0b	/* IRQ */
#define R0C_AGC11	0x0c	/* AGC1 byte 1 */
#define R0D_AGC12	0x0d	/* AGC1 byte 2 */
#define R0E_AGC13	0x0e	/* AGC1 byte 3 */
#define R0F_AGC14	0x0f	/* AGC1 byte 4 */
#define R10_LT1		0x10	/* LT byte 1 */
#define R11_LT2		0x11	/* LT byte 2 */
#define R12_AGC21	0x12	/* AGC2 byte 1 */
#define R13_AGC22	0x13	/* AGC2 byte 2 */
#define R14_AGC23	0x14	/* AGC2 byte 3 */
#define R15_AGC24	0x15	/* AGC2 byte 4 */
#define R16_AGC25	0x16	/* AGC2 byte 5 */
#define R17_AGC31	0x17	/* AGC3 byte 1 */
#define R18_AGC32	0x18	/* AGC3 byte 2 */
#define R19_AGC33	0x19	/* AGC3 byte 3 */
#define R1A_AGCK	0x1a
#define R1B_GAIN1	0x1b
#define R1C_GAIN2	0x1c
#define R1D_GAIN3	0x1d
#define R1E_WI_FI	0x1e	/* Wireless Filter */
#define R1F_RF_BPF	0x1f	/* RF Band Pass Filter */
#define R20_IR_MIX	0x20	/* IR Mixer */
#define R21_IF_AGC	0x21
#define R22_IF1		0x22	/* IF byte 1 */
#define R23_IF2		0x23	/* IF byte 2 */
#define R24_IF3		0x24	/* IF byte 3 */
#define R25_REF		0x25	/* reference byte */
#define R26_IF		0x26	/* IF frequency */
#define R27_RF1		0x27	/* RF frequency byte 1 */
#define R28_RF2		0x28	/* RF frequency byte 2 */
#define R29_RF3		0x29	/* RF frequency byte 3 */
#define R2A_MSM1	0x2a
#define R2B_MSM2	0x2b
#define R2C_PS1		0x2c	/* power saving mode byte 1 */
#define R2D_PS2		0x2d	/* power saving mode byte 2 */
#define R2E_PS3		0x2e	/* power saving mode byte 3 */
#define R2F_RSSI1	0x2f
#define R30_RSSI2	0x30
#define R31_IRQ_CTRL	0x31
#define R32_DUMMY	0x32
#define R33_TEST	0x33
#define R34_MD1		0x34
#define R35_SD1		0x35
#define R36_SD2		0x36
#define R37_SD3		0x37
#define R38_SD4		0x38
#define R39_SD5		0x39
#define R3A_SD_TEST	0x3a
#define R3B_REGU	0x3b
#define R3C_RCCAL1	0x3c
#define R3D_RCCAL2	0x3d
#define R3E_IRCAL1	0x3e
#define R3F_IRCAL2	0x3f
#define R40_IRCAL3	0x40
#define R41_IRCAL4	0x41
#define R42_IRCAL5	0x42
#define R43_PD1		0x43	/* power down byte 1 */
#define R44_PD2		0x44	/* power down byte 2 */
#define R45_PD		0x45	/* power down */
#define R46_CPUMP	0x46	/* charge pump */
#define R47_LNAPOL	0x47	/* LNA polar casc */
#define R48_SMOOTH1	0x48	/* smooth test byte 1 */
#define R49_SMOOTH2	0x49	/* smooth test byte 2 */
#define R4A_SMOOTH3	0x4a	/* smooth test byte 3 */
#define R4B_XTALOSC1	0x4b
#define R4C_XTALOSC2	0x4c
#define R4D_XTALFLX1	0x4d
#define R4E_XTALFLX2	0x4e
#define R4F_XTALFLX3	0x4f
#define R50_XTALFLX4	0x50
#define R51_XTALFLX5	0x51
#define R52_IRLOOP0	0x52
#define R53_IRLOOP1	0x53
#define R54_IRLOOP2	0x54
#define R55_IRLOOP3	0x55
#define R56_IRLOOP4	0x56
#define R57_PLL_LOG	0x57
#define R58_AGC2_UP1	0x58
#define R59_AGC2_UP2	0x59
#define R5A_H3H5	0x5a
#define R5B_AGC_AUTO	0x5b
#define R5C_AGC_DEBUG	0x5c

#define TDA18250_NUM_REGS 93

#define TDA18250_POWER_STANDBY 0
#define TDA18250_POWER_NORMAL 1

#define TDA18250_IRQ_CAL     0x81
#define TDA18250_IRQ_HW_INIT 0x82
#define TDA18250_IRQ_TUNE    0x88

struct tda18250_dev {
	struct mutex i2c_mutex;
	struct dvb_frontend *fe;
	struct i2c_adapter *i2c;
	struct regmap *regmap;
	u8 xtal_freq;
	/* IF in kHz */
	u16 if_dvbt_6;
	u16 if_dvbt_7;
	u16 if_dvbt_8;
	u16 if_dvbc_6;
	u16 if_dvbc_8;
	u16 if_atsc;
	u16 if_frequency;
	bool slave;
	bool loopthrough;
	bool warm;
	u8 regs[TDA18250_NUM_REGS];
};

#endif
