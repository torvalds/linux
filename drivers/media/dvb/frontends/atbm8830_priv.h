/*
 *    Support for AltoBeam GB20600 (a.k.a DMB-TH) demodulator
 *    ATBM8830, ATBM8831
 *
 *    Copyright (C) 2009 David T.L. Wong <davidtlwong@gmail.com>
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
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __ATBM8830_PRIV_H
#define __ATBM8830_PRIV_H

struct atbm_state {
	struct i2c_adapter *i2c;
	/* configuration settings */
	const struct atbm8830_config *config;
	struct dvb_frontend frontend;
};

#define REG_CHIP_ID	0x0000
#define REG_TUNER_BASEBAND	0x0001
#define REG_DEMOD_RUN	0x0004
#define REG_DSP_RESET	0x0005
#define REG_RAM_RESET	0x0006
#define REG_ADC_RESET	0x0007
#define REG_TSPORT_RESET	0x0008
#define REG_BLKERR_POL	0x000C
#define REG_I2C_GATE	0x0103
#define REG_TS_SAMPLE_EDGE	0x0301
#define REG_TS_PKT_LEN_204	0x0302
#define REG_TS_PKT_LEN_AUTO	0x0303
#define REG_TS_SERIAL	0x0305
#define REG_TS_CLK_FREERUN	0x0306
#define REG_TS_VALID_MODE	0x0307
#define REG_TS_CLK_MODE	0x030B /* 1 for serial, 0 for parallel */

#define REG_TS_ERRBIT_USE	0x030C
#define REG_LOCK_STATUS	0x030D
#define REG_ADC_CONFIG	0x0602
#define REG_CARRIER_OFFSET	0x0827 /* 0x0827-0x0829 little endian */
#define REG_DETECTED_PN_MODE	0x082D
#define REG_READ_LATCH	0x084D
#define REG_IF_FREQ	0x0A00 /* 0x0A00-0x0A02 little endian */
#define REG_OSC_CLK	0x0A03 /* 0x0A03-0x0A05 little endian */
#define REG_BYPASS_CCI	0x0A06
#define REG_ANALOG_LUMA_DETECTED	0x0A25
#define REG_ANALOG_AUDIO_DETECTED	0x0A26
#define REG_ANALOG_CHROMA_DETECTED	0x0A39
#define REG_FRAME_ERR_CNT	0x0B04
#define REG_USE_EXT_ADC	0x0C00
#define REG_SWAP_I_Q	0x0C01
#define REG_TPS_MANUAL	0x0D01
#define REG_TPS_CONFIG	0x0D02
#define REG_BYPASS_DEINTERLEAVER	0x0E00
#define REG_AGC_TARGET	0x1003 /* 0x1003-0x1005 little endian */
#define REG_AGC_MIN	0x1020
#define REG_AGC_MAX	0x1023
#define REG_AGC_LOCK	0x1027
#define REG_AGC_PWM_VAL	0x1028 /* 0x1028-0x1029 little endian */
#define REG_AGC_HOLD_LOOP	0x1031

#endif

