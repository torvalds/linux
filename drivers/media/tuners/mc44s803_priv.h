/*
 *  Driver for Freescale MC44S803 Low Power CMOS Broadband Tuner
 *
 *  Copyright (c) 2009 Jochen Friedrich <jochen@scram.de>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#ifndef MC44S803_PRIV_H
#define MC44S803_PRIV_H

/* This driver is based on the information available in the datasheet
   http://www.freescale.com/files/rf_if/doc/data_sheet/MC44S803.pdf

   SPI or I2C Address : 0xc0-0xc6

   Reg.No | Function
   -------------------------------------------
       00 | Power Down
       01 | Reference Oszillator
       02 | Reference Dividers
       03 | Mixer and Reference Buffer
       04 | Reset/Serial Out
       05 | LO 1
       06 | LO 2
       07 | Circuit Adjust
       08 | Test
       09 | Digital Tune
       0A | LNA AGC
       0B | Data Register Address
       0C | Regulator Test
       0D | VCO Test
       0E | LNA Gain/Input Power
       0F | ID Bits

*/

#define MC44S803_OSC 26000000	/* 26 MHz */
#define MC44S803_IF1 1086000000 /* 1086 MHz */
#define MC44S803_IF2 36125000	/* 36.125 MHz */

#define MC44S803_REG_POWER	0
#define MC44S803_REG_REFOSC	1
#define MC44S803_REG_REFDIV	2
#define MC44S803_REG_MIXER	3
#define MC44S803_REG_RESET	4
#define MC44S803_REG_LO1	5
#define MC44S803_REG_LO2	6
#define MC44S803_REG_CIRCADJ	7
#define MC44S803_REG_TEST	8
#define MC44S803_REG_DIGTUNE	9
#define MC44S803_REG_LNAAGC	0x0A
#define MC44S803_REG_DATAREG	0x0B
#define MC44S803_REG_REGTEST	0x0C
#define MC44S803_REG_VCOTEST	0x0D
#define MC44S803_REG_LNAGAIN	0x0E
#define MC44S803_REG_ID		0x0F

/* Register definitions */
#define MC44S803_ADDR		0x0F
#define MC44S803_ADDR_S		0
/* REG_POWER */
#define MC44S803_POWER		0xFFFFF0
#define MC44S803_POWER_S	4
/* REG_REFOSC */
#define MC44S803_REFOSC		0x1FF0
#define MC44S803_REFOSC_S	4
#define MC44S803_OSCSEL		0x2000
#define MC44S803_OSCSEL_S	13
/* REG_REFDIV */
#define MC44S803_R2		0x1FF0
#define MC44S803_R2_S		4
#define MC44S803_REFBUF_EN	0x2000
#define MC44S803_REFBUF_EN_S	13
#define MC44S803_R1		0x7C000
#define MC44S803_R1_S		14
/* REG_MIXER */
#define MC44S803_R3		0x70
#define MC44S803_R3_S		4
#define MC44S803_MUX3		0x80
#define MC44S803_MUX3_S		7
#define MC44S803_MUX4		0x100
#define MC44S803_MUX4_S		8
#define MC44S803_OSC_SCR	0x200
#define MC44S803_OSC_SCR_S	9
#define MC44S803_TRI_STATE	0x400
#define MC44S803_TRI_STATE_S	10
#define MC44S803_BUF_GAIN	0x800
#define MC44S803_BUF_GAIN_S	11
#define MC44S803_BUF_IO		0x1000
#define MC44S803_BUF_IO_S	12
#define MC44S803_MIXER_RES	0xFE000
#define MC44S803_MIXER_RES_S	13
/* REG_RESET */
#define MC44S803_RS		0x10
#define MC44S803_RS_S		4
#define MC44S803_SO		0x20
#define MC44S803_SO_S		5
/* REG_LO1 */
#define MC44S803_LO1		0xFFF0
#define MC44S803_LO1_S		4
/* REG_LO2 */
#define MC44S803_LO2		0x7FFF0
#define MC44S803_LO2_S		4
/* REG_CIRCADJ */
#define MC44S803_G1		0x20
#define MC44S803_G1_S		5
#define MC44S803_G3		0x80
#define MC44S803_G3_S		7
#define MC44S803_CIRCADJ_RES	0x300
#define MC44S803_CIRCADJ_RES_S	8
#define MC44S803_G6		0x400
#define MC44S803_G6_S		10
#define MC44S803_G7		0x800
#define MC44S803_G7_S		11
#define MC44S803_S1		0x1000
#define MC44S803_S1_S		12
#define MC44S803_LP		0x7E000
#define MC44S803_LP_S		13
#define MC44S803_CLRF		0x80000
#define MC44S803_CLRF_S		19
#define MC44S803_CLIF		0x100000
#define MC44S803_CLIF_S		20
/* REG_TEST */
/* REG_DIGTUNE */
#define MC44S803_DA		0xF0
#define MC44S803_DA_S		4
#define MC44S803_XOD		0x300
#define MC44S803_XOD_S		8
#define MC44S803_RST		0x10000
#define MC44S803_RST_S		16
#define MC44S803_LO_REF		0x1FFF00
#define MC44S803_LO_REF_S	8
#define MC44S803_AT		0x200000
#define MC44S803_AT_S		21
#define MC44S803_MT		0x400000
#define MC44S803_MT_S		22
/* REG_LNAAGC */
#define MC44S803_G		0x3F0
#define MC44S803_G_S		4
#define MC44S803_AT1		0x400
#define MC44S803_AT1_S		10
#define MC44S803_AT2		0x800
#define MC44S803_AT2_S		11
#define MC44S803_HL_GR_EN	0x8000
#define MC44S803_HL_GR_EN_S	15
#define MC44S803_AGC_AN_DIG	0x10000
#define MC44S803_AGC_AN_DIG_S	16
#define MC44S803_ATTEN_EN	0x20000
#define MC44S803_ATTEN_EN_S	17
#define MC44S803_AGC_READ_EN	0x40000
#define MC44S803_AGC_READ_EN_S	18
#define MC44S803_LNA0		0x80000
#define MC44S803_LNA0_S		19
#define MC44S803_AGC_SEL	0x100000
#define MC44S803_AGC_SEL_S	20
#define MC44S803_AT0		0x200000
#define MC44S803_AT0_S		21
#define MC44S803_B		0xC00000
#define MC44S803_B_S		22
/* REG_DATAREG */
#define MC44S803_D		0xF0
#define MC44S803_D_S		4
/* REG_REGTEST */
/* REG_VCOTEST */
/* REG_LNAGAIN */
#define MC44S803_IF_PWR		0x700
#define MC44S803_IF_PWR_S	8
#define MC44S803_RF_PWR		0x3800
#define MC44S803_RF_PWR_S	11
#define MC44S803_LNA_GAIN	0xFC000
#define MC44S803_LNA_GAIN_S	14
/* REG_ID */
#define MC44S803_ID		0x3E00
#define MC44S803_ID_S		9

/* Some macros to read/write fields */

/* First shift, then mask */
#define MC44S803_REG_SM(_val, _reg)					\
	(((_val) << _reg##_S) & (_reg))

/* First mask, then shift */
#define MC44S803_REG_MS(_val, _reg)					\
	(((_val) & (_reg)) >> _reg##_S)

struct mc44s803_priv {
	struct mc44s803_config *cfg;
	struct i2c_adapter *i2c;
	struct dvb_frontend *fe;

	u32 frequency;
};

#endif
