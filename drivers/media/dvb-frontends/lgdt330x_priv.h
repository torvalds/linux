/*
 *    Support for LGDT3302 and LGDT3303 - VSB/QAM
 *
 *    Copyright (C) 2005 Wilson Michaels <wilsonmichaels@earthlink.net>
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
 */

#ifndef _LGDT330X_PRIV_
#define _LGDT330X_PRIV_

/* i2c control register addresses */
enum I2C_REG {
	TOP_CONTROL= 0x00,
	IRQ_MASK= 0x01,
	IRQ_STATUS= 0x02,
	VSB_CARRIER_FREQ0= 0x16,
	VSB_CARRIER_FREQ1= 0x17,
	VSB_CARRIER_FREQ2= 0x18,
	VSB_CARRIER_FREQ3= 0x19,
	CARRIER_MSEQAM1= 0x1a,
	CARRIER_MSEQAM2= 0x1b,
	CARRIER_LOCK= 0x1c,
	TIMING_RECOVERY= 0x1d,
	AGC_DELAY0= 0x2a,
	AGC_DELAY1= 0x2b,
	AGC_DELAY2= 0x2c,
	AGC_RF_BANDWIDTH0= 0x2d,
	AGC_RF_BANDWIDTH1= 0x2e,
	AGC_RF_BANDWIDTH2= 0x2f,
	AGC_LOOP_BANDWIDTH0= 0x30,
	AGC_LOOP_BANDWIDTH1= 0x31,
	AGC_FUNC_CTRL1= 0x32,
	AGC_FUNC_CTRL2= 0x33,
	AGC_FUNC_CTRL3= 0x34,
	AGC_RFIF_ACC0= 0x39,
	AGC_RFIF_ACC1= 0x3a,
	AGC_RFIF_ACC2= 0x3b,
	AGC_STATUS= 0x3f,
	SYNC_STATUS_VSB= 0x43,
	DEMUX_CONTROL= 0x66,
	LGDT3302_EQPH_ERR0= 0x47,
	LGDT3302_EQ_ERR1= 0x48,
	LGDT3302_EQ_ERR2= 0x49,
	LGDT3302_PH_ERR1= 0x4a,
	LGDT3302_PH_ERR2= 0x4b,
	LGDT3302_PACKET_ERR_COUNTER1= 0x6a,
	LGDT3302_PACKET_ERR_COUNTER2= 0x6b,
	LGDT3303_EQPH_ERR0= 0x6e,
	LGDT3303_EQ_ERR1= 0x6f,
	LGDT3303_EQ_ERR2= 0x70,
	LGDT3303_PH_ERR1= 0x71,
	LGDT3303_PH_ERR2= 0x72,
	LGDT3303_PACKET_ERR_COUNTER1= 0x8b,
	LGDT3303_PACKET_ERR_COUNTER2= 0x8c,
};

#endif /* _LGDT330X_PRIV_ */
