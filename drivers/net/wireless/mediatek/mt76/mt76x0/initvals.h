/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
 * Copyright (C) 2018 Stanislaw Gruszka <stf_xl@wp.pl>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __MT76X0U_INITVALS_H
#define __MT76X0U_INITVALS_H

#include "phy.h"

static const struct mt76_reg_pair common_mac_reg_table[] = {
#if 1
	{MT_BCN_OFFSET(0),			0xf8f0e8e0}, /* 0x3800(e0), 0x3A00(e8), 0x3C00(f0), 0x3E00(f8), 512B for each beacon */
	{MT_BCN_OFFSET(1),			0x6f77d0c8}, /* 0x3200(c8), 0x3400(d0), 0x1DC0(77), 0x1BC0(6f), 512B for each beacon */
#endif

	{MT_LEGACY_BASIC_RATE,		0x0000013f}, /*  Basic rate set bitmap*/
	{MT_HT_BASIC_RATE,		0x00008003}, /* Basic HT rate set , 20M, MCS=3, MM. Format is the same as in TXWI.*/
	{MT_MAC_SYS_CTRL,		0x00}, /* 0x1004, , default Disable RX*/
	{MT_RX_FILTR_CFG,		0x17f97}, /*0x1400  , RX filter control,  */
	{MT_BKOFF_SLOT_CFG,	0x209}, /* default set short slot time, CC_DELAY_TIME should be 2	 */
	/*{TX_SW_CFG0,		0x40a06},  Gary,2006-08-23 */
	{MT_TX_SW_CFG0,		0x0}, 		/* Gary,2008-05-21 for CWC test */
	{MT_TX_SW_CFG1,		0x80606}, /* Gary,2006-08-23 */
	{MT_TX_LINK_CFG,		0x1020},		/* Gary,2006-08-23 */
	/*{TX_TIMEOUT_CFG,	0x00182090},	 CCK has some problem. So increase timieout value. 2006-10-09 MArvek RT*/
	{MT_TX_TIMEOUT_CFG,	0x000a2090},	/* CCK has some problem. So increase timieout value. 2006-10-09 MArvek RT , Modify for 2860E ,2007-08-01*/
	{MT_MAX_LEN_CFG,		0xa0fff | 0x00001000},	/* 0x3018, MAX frame length. Max PSDU = 16kbytes.*/
	{MT_LED_CFG,		0x7f031e46}, /* Gary, 2006-08-23*/

	{MT_PBF_TX_MAX_PCNT,		0x1fbf1f1f /*0xbfbf3f1f*/},
	{MT_PBF_RX_MAX_PCNT,		0x9f},

	/*{TX_RTY_CFG,			0x6bb80408},	 Jan, 2006/11/16*/
/* WMM_ACM_SUPPORT */
/*	{TX_RTY_CFG,			0x6bb80101},	 sample*/
	{MT_TX_RETRY_CFG,			0x47d01f0f},	/* Jan, 2006/11/16, Set TxWI->ACK =0 in Probe Rsp Modify for 2860E ,2007-08-03*/

	{MT_AUTO_RSP_CFG,			0x00000013},	/* Initial Auto_Responder, because QA will turn off Auto-Responder*/
	{MT_CCK_PROT_CFG,			0x05740003 /*0x01740003*/},	/* Initial Auto_Responder, because QA will turn off Auto-Responder. And RTS threshold is enabled. */
	{MT_OFDM_PROT_CFG,			0x05740003 /*0x01740003*/},	/* Initial Auto_Responder, because QA will turn off Auto-Responder. And RTS threshold is enabled. */
	{MT_PBF_CFG, 				0xf40006}, 		/* Only enable Queue 2*/
	{MT_MM40_PROT_CFG,			0x3F44084},		/* Initial Auto_Responder, because QA will turn off Auto-Responder*/
	{MT_WPDMA_GLO_CFG,			0x00000030},
	{MT_GF20_PROT_CFG,			0x01744004},    /* set 19:18 --> Short NAV for MIMO PS*/
	{MT_GF40_PROT_CFG,			0x03F44084},
	{MT_MM20_PROT_CFG,			0x01744004},
	{MT_TXOP_CTRL_CFG,			0x0000583f, /*0x0000243f*/ /*0x000024bf*/},	/*Extension channel backoff.*/
	{MT_TX_RTS_CFG,			0x00092b20},

	{MT_EXP_ACK_TIME,			0x002400ca},	/* default value */
	{MT_TXOP_HLDR_ET, 			0x00000002},

	/* Jerry comments 2008/01/16: we use SIFS = 10us in CCK defaultly, but it seems that 10us
		is too small for INTEL 2200bg card, so in MBSS mode, the delta time between beacon0
		and beacon1 is SIFS (10us), so if INTEL 2200bg card connects to BSS0, the ping
		will always lost. So we change the SIFS of CCK from 10us to 16us. */
	{MT_XIFS_TIME_CFG,			0x33a41010},
	{MT_PWR_PIN_CFG,			0x00000000},
};

static const struct mt76_reg_pair mt76x0_mac_reg_table[] = {
	/* {MT_IOCFG_6,		0xA0040080 }, */
	{MT_PBF_SYS_CTRL,	0x00080c00 },
	{MT_PBF_CFG,		0x77723c1f },
	{MT_FCE_PSE_CTRL,	0x00000001 },

	{MT_AMPDU_MAX_LEN_20M1S,	0xBAA99887 },

	/* Delay bb_tx_pe for proper tx_mcs_pwr update */
	{MT_TX_SW_CFG0,		0x00000601 },

	/* Set rf_tx_pe deassert time to 1us by Chee's comment @MT7650_CR_setting_1018.xlsx */
	{MT_TX_SW_CFG1,		0x00040000 },
	{MT_TX_SW_CFG2,		0x00000000 },

	/* disable Tx info report */
	{0xa44,		0x0000000 },

	{MT_HEADER_TRANS_CTRL_REG, 0x0},
	{MT_TSO_CTRL,		0x0},

	/* BB_PA_MODE_CFG0(0x1214) Keep default value @20120903 */
	{MT_BB_PA_MODE_CFG1,	0x00500055},

	/* RF_PA_MODE_CFG0(0x121C) Keep default value @20120903 */
	{MT_RF_PA_MODE_CFG1,	0x00500055},

	{MT_TX_ALC_CFG_0,	0x2F2F000C},
	{MT_TX0_BB_GAIN_ATTEN,  0x00000000}, /* set BBP atten gain = 0 */

	{MT_TX_PWR_CFG_0, 0x3A3A3A3A},
	{MT_TX_PWR_CFG_1, 0x3A3A3A3A},
	{MT_TX_PWR_CFG_2, 0x3A3A3A3A},
	{MT_TX_PWR_CFG_3, 0x3A3A3A3A},
	{MT_TX_PWR_CFG_4, 0x3A3A3A3A},
	{MT_TX_PWR_CFG_7, 0x3A3A3A3A},
	{MT_TX_PWR_CFG_8, 0x3A},
	{MT_TX_PWR_CFG_9, 0x3A},
	/* Enable Tx length > 4095 byte */
	{0x150C,		0x00000002},

	/* Disable bt_abort_tx_en(0x1238[21] = 0) which is not used at MT7650 */
	{0x1238, 		0x001700C8},
	/* PMU_OCLEVEL<5:1> from default <5'b10010> to <5'b11011> for normal driver */
	/* {MT_LDO_CTRL_0,		0x00A647B6}, */

	/* Default LDO_DIG supply 1.26V, change to 1.2V */
	{MT_LDO_CTRL_1,		0x6B006464 },
/*
	{MT_HT_BASIC_RATE,	0x00004003 },
	{MT_HT_CTRL_CFG,	0x000001FF },
*/
};


static const struct mt76_reg_pair mt76x0_bbp_init_tab[] = {
	{MT_BBP(CORE, 1), 0x00000002},
	{MT_BBP(CORE, 4), 0x00000000},
	{MT_BBP(CORE, 24), 0x00000000},
	{MT_BBP(CORE, 32), 0x4003000a},
	{MT_BBP(CORE, 42), 0x00000000},
	{MT_BBP(CORE, 44), 0x00000000},

	{MT_BBP(IBI, 11), 0x00000080},

	/*
		0x2300[5] Default Antenna:
		0 for WIFI main antenna
		1 for WIFI aux  antenna

	*/
	{MT_BBP(AGC, 0), 0x00021400},
	{MT_BBP(AGC, 1), 0x00000003},
	{MT_BBP(AGC, 2), 0x003A6464},
	{MT_BBP(AGC, 15), 0x88A28CB8},
	{MT_BBP(AGC, 22), 0x00001E21},
	{MT_BBP(AGC, 23), 0x0000272C},
	{MT_BBP(AGC, 24), 0x00002F3A},
	{MT_BBP(AGC, 25), 0x8000005A},
	{MT_BBP(AGC, 26), 0x007C2005},
	{MT_BBP(AGC, 34), 0x000A0C0C},
	{MT_BBP(AGC, 37), 0x2121262C},
	{MT_BBP(AGC, 41), 0x38383E45},
	{MT_BBP(AGC, 57), 0x00001010},
	{MT_BBP(AGC, 59), 0xBAA20E96},
	{MT_BBP(AGC, 63), 0x00000001},

	{MT_BBP(TXC, 0), 0x00280403},
	{MT_BBP(TXC, 1), 0x00000000},

	{MT_BBP(RXC, 1), 0x00000012},
	{MT_BBP(RXC, 2), 0x00000011},
	{MT_BBP(RXC, 3), 0x00000005},
	{MT_BBP(RXC, 4), 0x00000000},
	{MT_BBP(RXC, 5), 0xF977C4EC},
	{MT_BBP(RXC, 7), 0x00000090},

	{MT_BBP(TXO, 8), 0x00000000},

	{MT_BBP(TXBE, 0), 0x00000000},
	{MT_BBP(TXBE, 4), 0x00000004},
	{MT_BBP(TXBE, 6), 0x00000000},
	{MT_BBP(TXBE, 8), 0x00000014},
	{MT_BBP(TXBE, 9), 0x20000000},
	{MT_BBP(TXBE, 10), 0x00000000},
	{MT_BBP(TXBE, 12), 0x00000000},
	{MT_BBP(TXBE, 13), 0x00000000},
	{MT_BBP(TXBE, 14), 0x00000000},
	{MT_BBP(TXBE, 15), 0x00000000},
	{MT_BBP(TXBE, 16), 0x00000000},
	{MT_BBP(TXBE, 17), 0x00000000},

	{MT_BBP(RXFE, 1), 0x00008800}, /* Add for E3 */
	{MT_BBP(RXFE, 3), 0x00000000},
	{MT_BBP(RXFE, 4), 0x00000000},

	{MT_BBP(RXO, 13), 0x00000092},
	{MT_BBP(RXO, 14), 0x00060612},
	{MT_BBP(RXO, 15), 0xC8321B18},
	{MT_BBP(RXO, 16), 0x0000001E},
	{MT_BBP(RXO, 17), 0x00000000},
	{MT_BBP(RXO, 18), 0xCC00A993},
	{MT_BBP(RXO, 19), 0xB9CB9CB9},
	{MT_BBP(RXO, 20), 0x26c00057},
	{MT_BBP(RXO, 21), 0x00000001},
	{MT_BBP(RXO, 24), 0x00000006},
};

static const struct mt76x0_bbp_switch_item mt76x0_bbp_switch_tab[] = {
	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 8), 0x0E344EF0}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 8), 0x122C54F2}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 14), 0x310F2E39}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 14), 0x310F2A3F}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 32), 0x00003230}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 32), 0x0000181C}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 33), 0x00003240}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 33), 0x00003218}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 35), 0x11112016}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 35), 0x11112016}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(RXO, 28), 0x0000008A}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(RXO, 28), 0x0000008A}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 4), 0x1FEDA049}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 4), 0x1FECA054}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 6), 0x00000045}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 6), 0x0000000A}},

	{RF_G_BAND | RF_BW_20,				{MT_BBP(AGC, 12), 0x05052879}},
	{RF_G_BAND | RF_BW_40,				{MT_BBP(AGC, 12), 0x050528F9}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 12), 0x050528F9}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 13), 0x35050004}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 13), 0x2C3A0406}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 27), 0x000000E1}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 27), 0x000000EC}},

	{RF_G_BAND | RF_BW_20,				{MT_BBP(AGC, 28), 0x00060806}},
	{RF_G_BAND | RF_BW_40,				{MT_BBP(AGC, 28), 0x00050806}},
	{RF_A_BAND | RF_BW_40,				{MT_BBP(AGC, 28), 0x00060801}},
	{RF_A_BAND | RF_BW_20 | RF_BW_80,		{MT_BBP(AGC, 28), 0x00060806}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 31), 0x00000F23}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 31), 0x00000F13}},

	{RF_G_BAND | RF_BW_20,				{MT_BBP(AGC, 39), 0x2A2A3036}},
	{RF_G_BAND | RF_BW_40,				{MT_BBP(AGC, 39), 0x2A2A2C36}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 39), 0x2A2A3036}},
	{RF_A_BAND | RF_BW_80,				{MT_BBP(AGC, 39), 0x2A2A2A36}},

	{RF_G_BAND | RF_BW_20,				{MT_BBP(AGC, 43), 0x27273438}},
	{RF_G_BAND | RF_BW_40,				{MT_BBP(AGC, 43), 0x27272D38}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 43), 0x27272B30}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 51), 0x17171C1C}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 51), 0xFFFFFFFF}},

	{RF_G_BAND | RF_BW_20,				{MT_BBP(AGC, 53), 0x26262A2F}},
	{RF_G_BAND | RF_BW_40,				{MT_BBP(AGC, 53), 0x2626322F}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 53), 0xFFFFFFFF}},

	{RF_G_BAND | RF_BW_20,				{MT_BBP(AGC, 55), 0x40404E58}},
	{RF_G_BAND | RF_BW_40,				{MT_BBP(AGC, 55), 0x40405858}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 55), 0xFFFFFFFF}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(AGC, 58), 0x00001010}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(AGC, 58), 0x00000000}},

	{RF_G_BAND | RF_BW_20 | RF_BW_40,		{MT_BBP(RXFE, 0), 0x3D5000E0}},
	{RF_A_BAND | RF_BW_20 | RF_BW_40 | RF_BW_80,	{MT_BBP(RXFE, 0), 0x895000E0}},
};

static const struct mt76_reg_pair mt76x0_dcoc_tab[] = {
	{MT_BBP(CAL, 47), 0x000010F0 },
	{MT_BBP(CAL, 48), 0x00008080 },
	{MT_BBP(CAL, 49), 0x00000F07 },
	{MT_BBP(CAL, 50), 0x00000040 },
	{MT_BBP(CAL, 51), 0x00000404 },
	{MT_BBP(CAL, 52), 0x00080803 },
	{MT_BBP(CAL, 53), 0x00000704 },
	{MT_BBP(CAL, 54), 0x00002828 },
	{MT_BBP(CAL, 55), 0x00005050 },
};

#endif
