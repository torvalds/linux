/*
    Driver for Zarlink MT312 QPSK Frontend

    Copyright (C) 2003 Andreas Oberritter <obi@linuxtv.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the

    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef _DVB_FRONTENDS_MT312_PRIV
#define _DVB_FRONTENDS_MT312_PRIV

enum mt312_reg_addr {
	QPSK_INT_H = 0,
	QPSK_INT_M = 1,
	QPSK_INT_L = 2,
	FEC_INT = 3,
	QPSK_STAT_H = 4,
	QPSK_STAT_L = 5,
	FEC_STATUS = 6,
	LNB_FREQ_H = 7,
	LNB_FREQ_L = 8,
	M_SNR_H = 9,
	M_SNR_L = 10,
	VIT_ERRCNT_H = 11,
	VIT_ERRCNT_M = 12,
	VIT_ERRCNT_L = 13,
	RS_BERCNT_H = 14,
	RS_BERCNT_M = 15,
	RS_BERCNT_L = 16,
	RS_UBC_H = 17,
	RS_UBC_L = 18,
	SIG_LEVEL = 19,
	GPP_CTRL = 20,
	RESET = 21,
	DISEQC_MODE = 22,
	SYM_RATE_H = 23,
	SYM_RATE_L = 24,
	VIT_MODE = 25,
	QPSK_CTRL = 26,
	GO = 27,
	IE_QPSK_H = 28,
	IE_QPSK_M = 29,
	IE_QPSK_L = 30,
	IE_FEC = 31,
	QPSK_STAT_EN = 32,
	FEC_STAT_EN = 33,
	SYS_CLK = 34,
	DISEQC_RATIO = 35,
	DISEQC_INSTR = 36,
	FR_LIM = 37,
	FR_OFF = 38,
	AGC_CTRL = 39,
	AGC_INIT = 40,
	AGC_REF = 41,
	AGC_MAX = 42,
	AGC_MIN = 43,
	AGC_LK_TH = 44,
	TS_AGC_LK_TH = 45,
	AGC_PWR_SET = 46,
	QPSK_MISC = 47,
	SNR_THS_LOW = 48,
	SNR_THS_HIGH = 49,
	TS_SW_RATE = 50,
	TS_SW_LIM_L = 51,
	TS_SW_LIM_H = 52,
	CS_SW_RATE_1 = 53,
	CS_SW_RATE_2 = 54,
	CS_SW_RATE_3 = 55,
	CS_SW_RATE_4 = 56,
	CS_SW_LIM = 57,
	TS_LPK = 58,
	TS_LPK_M = 59,
	TS_LPK_L = 60,
	CS_KPROP_H = 61,
	CS_KPROP_L = 62,
	CS_KINT_H = 63,
	CS_KINT_L = 64,
	QPSK_SCALE = 65,
	TLD_OUTCLK_TH = 66,
	TLD_INCLK_TH = 67,
	FLD_TH = 68,
	PLD_OUTLK3 = 69,
	PLD_OUTLK2 = 70,
	PLD_OUTLK1 = 71,
	PLD_OUTLK0 = 72,
	PLD_INLK3 = 73,
	PLD_INLK2 = 74,
	PLD_INLK1 = 75,
	PLD_INLK0 = 76,
	PLD_ACC_TIME = 77,
	SWEEP_PAR = 78,
	STARTUP_TIME = 79,
	LOSSLOCK_TH = 80,
	FEC_LOCK_TM = 81,
	LOSSLOCK_TM = 82,
	VIT_ERRPER_H = 83,
	VIT_ERRPER_M = 84,
	VIT_ERRPER_L = 85,
	HW_CTRL = 84,	/* ZL10313 only */
	MPEG_CTRL = 85,	/* ZL10313 only */
	VIT_SETUP = 86,
	VIT_REF0 = 87,
	VIT_REF1 = 88,
	VIT_REF2 = 89,
	VIT_REF3 = 90,
	VIT_REF4 = 91,
	VIT_REF5 = 92,
	VIT_REF6 = 93,
	VIT_MAXERR = 94,
	BA_SETUPT = 95,
	OP_CTRL = 96,
	FEC_SETUP = 97,
	PROG_SYNC = 98,
	AFC_SEAR_TH = 99,
	CSACC_DIF_TH = 100,
	QPSK_LK_CT = 101,
	QPSK_ST_CT = 102,
	MON_CTRL = 103,
	QPSK_RESET = 104,
	QPSK_TST_CT = 105,
	QPSK_TST_ST = 106,
	TEST_R = 107,
	AGC_H = 108,
	AGC_M = 109,
	AGC_L = 110,
	FREQ_ERR1_H = 111,
	FREQ_ERR1_M = 112,
	FREQ_ERR1_L = 113,
	FREQ_ERR2_H = 114,
	FREQ_ERR2_L = 115,
	SYM_RAT_OP_H = 116,
	SYM_RAT_OP_L = 117,
	DESEQC2_INT = 118,
	DISEQC2_STAT = 119,
	DISEQC2_FIFO = 120,
	DISEQC2_CTRL1 = 121,
	DISEQC2_CTRL2 = 122,
	MONITOR_H = 123,
	MONITOR_L = 124,
	TEST_MODE = 125,
	ID = 126,
	CONFIG = 127
};

enum mt312_model_id {
	ID_VP310 = 1,
	ID_MT312 = 3,
	ID_ZL10313 = 5,
};

#endif				/* DVB_FRONTENDS_MT312_PRIV */
