/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Driver for Zarlink DVB-T ZL10353 demodulator
 *
 *  Copyright (C) 2006, 2007 Christopher Pascoe <c.pascoe@itee.uq.edu.au>
 */

#ifndef _ZL10353_PRIV_
#define _ZL10353_PRIV_

#define ID_ZL10353	0x14 /* Zarlink ZL10353 */
#define ID_CE6230	0x18 /* Intel CE6230 */
#define ID_CE6231	0x19 /* Intel CE6231 */

#define msb(x) (((x) >> 8) & 0xff)
#define lsb(x) ((x) & 0xff)

enum zl10353_reg_addr {
	INTERRUPT_0        = 0x00,
	INTERRUPT_1        = 0x01,
	INTERRUPT_2        = 0x02,
	INTERRUPT_3        = 0x03,
	INTERRUPT_4        = 0x04,
	INTERRUPT_5        = 0x05,
	STATUS_6           = 0x06,
	STATUS_7           = 0x07,
	STATUS_8           = 0x08,
	STATUS_9           = 0x09,
	AGC_GAIN_1         = 0x0A,
	AGC_GAIN_0         = 0x0B,
	SNR                = 0x10,
	RS_ERR_CNT_2       = 0x11,
	RS_ERR_CNT_1       = 0x12,
	RS_ERR_CNT_0       = 0x13,
	RS_UBC_1           = 0x14,
	RS_UBC_0           = 0x15,
	TPS_RECEIVED_1     = 0x1D,
	TPS_RECEIVED_0     = 0x1E,
	TPS_CURRENT_1      = 0x1F,
	TPS_CURRENT_0      = 0x20,
	CLOCK_CTL_0        = 0x51,
	CLOCK_CTL_1        = 0x52,
	PLL_0              = 0x53,
	PLL_1              = 0x54,
	RESET              = 0x55,
	AGC_TARGET         = 0x56,
	MCLK_RATIO         = 0x5C,
	ACQ_CTL            = 0x5E,
	TRL_NOMINAL_RATE_1 = 0x65,
	TRL_NOMINAL_RATE_0 = 0x66,
	INPUT_FREQ_1       = 0x6C,
	INPUT_FREQ_0       = 0x6D,
	TPS_GIVEN_1        = 0x6E,
	TPS_GIVEN_0        = 0x6F,
	TUNER_GO           = 0x70,
	FSM_GO             = 0x71,
	CHIP_ID            = 0x7F,
	CHAN_STEP_1        = 0xE4,
	CHAN_STEP_0        = 0xE5,
	OFDM_LOCK_TIME     = 0xE7,
	FEC_LOCK_TIME      = 0xE8,
	ACQ_DELAY          = 0xE9,
};

#endif                          /* _ZL10353_PRIV_ */
