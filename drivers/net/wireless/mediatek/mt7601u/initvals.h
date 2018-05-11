/*
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 * Copyright (C) 2015 Jakub Kicinski <kubakici@wp.pl>
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

#ifndef __MT7601U_INITVALS_H
#define __MT7601U_INITVALS_H

static const struct mt76_reg_pair bbp_common_vals[] = {
	{  65,	0x2c },
	{  66,	0x38 },
	{  68,	0x0b },
	{  69,	0x12 },
	{  70,	0x0a },
	{  73,	0x10 },
	{  81,	0x37 },
	{  82,	0x62 },
	{  83,	0x6a },
	{  84,	0x99 },
	{  86,	0x00 },
	{  91,	0x04 },
	{  92,	0x00 },
	{ 103,	0x00 },
	{ 105,	0x05 },
	{ 106,	0x35 },
};

static const struct mt76_reg_pair bbp_chip_vals[] = {
	{   1, 0x04 },	{   4, 0x40 },	{  20, 0x06 },	{  31, 0x08 },
	/* CCK Tx Control */
	{ 178, 0xff },
	/* AGC/Sync controls */
	{  66, 0x14 },	{  68, 0x8b },	{  69, 0x12 },	{  70, 0x09 },
	{  73, 0x11 },	{  75, 0x60 },	{  76, 0x44 },	{  84, 0x9a },
	{  86, 0x38 },	{  91, 0x07 },	{  92, 0x02 },
	/* Rx Path Controls */
	{  99, 0x50 },	{ 101, 0x00 },	{ 103, 0xc0 },	{ 104, 0x92 },
	{ 105, 0x3c },	{ 106, 0x03 },	{ 128, 0x12 },
	/* Change RXWI content: Gain Report */
	{ 142, 0x04 },	{ 143, 0x37 },
	/* Change RXWI content: Antenna Report */
	{ 142, 0x03 },	{ 143, 0x99 },
	/* Calibration Index Register */
	/* CCK Receiver Control */
	{ 160, 0xeb },	{ 161, 0xc4 },	{ 162, 0x77 },	{ 163, 0xf9 },
	{ 164, 0x88 },	{ 165, 0x80 },	{ 166, 0xff },	{ 167, 0xe4 },
	/* Added AGC controls - these AGC/GLRT registers are accessed
	 * through R195 and R196.
	 */
	{ 195, 0x00 },	{ 196, 0x00 },
	{ 195, 0x01 },	{ 196, 0x04 },
	{ 195, 0x02 },	{ 196, 0x20 },
	{ 195, 0x03 },	{ 196, 0x0a },
	{ 195, 0x06 },	{ 196, 0x16 },
	{ 195, 0x07 },	{ 196, 0x05 },
	{ 195, 0x08 },	{ 196, 0x37 },
	{ 195, 0x0a },	{ 196, 0x15 },
	{ 195, 0x0b },	{ 196, 0x17 },
	{ 195, 0x0c },	{ 196, 0x06 },
	{ 195, 0x0d },	{ 196, 0x09 },
	{ 195, 0x0e },	{ 196, 0x05 },
	{ 195, 0x0f },	{ 196, 0x09 },
	{ 195, 0x10 },	{ 196, 0x20 },
	{ 195, 0x20 },	{ 196, 0x17 },
	{ 195, 0x21 },	{ 196, 0x06 },
	{ 195, 0x22 },	{ 196, 0x09 },
	{ 195, 0x23 },	{ 196, 0x17 },
	{ 195, 0x24 },	{ 196, 0x06 },
	{ 195, 0x25 },	{ 196, 0x09 },
	{ 195, 0x26 },	{ 196, 0x17 },
	{ 195, 0x27 },	{ 196, 0x06 },
	{ 195, 0x28 },	{ 196, 0x09 },
	{ 195, 0x29 },	{ 196, 0x05 },
	{ 195, 0x2a },	{ 196, 0x09 },
	{ 195, 0x80 },	{ 196, 0x8b },
	{ 195, 0x81 },	{ 196, 0x12 },
	{ 195, 0x82 },	{ 196, 0x09 },
	{ 195, 0x83 },	{ 196, 0x17 },
	{ 195, 0x84 },	{ 196, 0x11 },
	{ 195, 0x85 },	{ 196, 0x00 },
	{ 195, 0x86 },	{ 196, 0x00 },
	{ 195, 0x87 },	{ 196, 0x18 },
	{ 195, 0x88 },	{ 196, 0x60 },
	{ 195, 0x89 },	{ 196, 0x44 },
	{ 195, 0x8a },	{ 196, 0x8b },
	{ 195, 0x8b },	{ 196, 0x8b },
	{ 195, 0x8c },	{ 196, 0x8b },
	{ 195, 0x8d },	{ 196, 0x8b },
	{ 195, 0x8e },	{ 196, 0x09 },
	{ 195, 0x8f },	{ 196, 0x09 },
	{ 195, 0x90 },	{ 196, 0x09 },
	{ 195, 0x91 },	{ 196, 0x09 },
	{ 195, 0x92 },	{ 196, 0x11 },
	{ 195, 0x93 },	{ 196, 0x11 },
	{ 195, 0x94 },	{ 196, 0x11 },
	{ 195, 0x95 },	{ 196, 0x11 },
	/* PPAD */
	{  47, 0x80 },	{  60, 0x80 },	{ 150, 0xd2 },	{ 151, 0x32 },
	{ 152, 0x23 },	{ 153, 0x41 },	{ 154, 0x00 },	{ 155, 0x4f },
	{ 253, 0x7e },	{ 195, 0x30 },	{ 196, 0x32 },	{ 195, 0x31 },
	{ 196, 0x23 },	{ 195, 0x32 },	{ 196, 0x45 },	{ 195, 0x35 },
	{ 196, 0x4a },	{ 195, 0x36 },	{ 196, 0x5a },	{ 195, 0x37 },
	{ 196, 0x5a },
};

static const struct mt76_reg_pair mac_common_vals[] = {
	{ MT_LEGACY_BASIC_RATE,		0x0000013f },
	{ MT_HT_BASIC_RATE,		0x00008003 },
	{ MT_MAC_SYS_CTRL,		0x00000000 },
	{ MT_RX_FILTR_CFG,		0x00017f97 },
	{ MT_BKOFF_SLOT_CFG,		0x00000209 },
	{ MT_TX_SW_CFG0,		0x00000000 },
	{ MT_TX_SW_CFG1,		0x00080606 },
	{ MT_TX_LINK_CFG,		0x00001020 },
	{ MT_TX_TIMEOUT_CFG,		0x000a2090 },
	{ MT_MAX_LEN_CFG,		0x00003fff },
	{ MT_PBF_TX_MAX_PCNT,		0x1fbf1f1f },
	{ MT_PBF_RX_MAX_PCNT,		0x0000009f },
	{ MT_TX_RETRY_CFG,		0x47d01f0f },
	{ MT_AUTO_RSP_CFG,		0x00000013 },
	{ MT_CCK_PROT_CFG,		0x05740003 },
	{ MT_OFDM_PROT_CFG,		0x05740003 },
	{ MT_MM40_PROT_CFG,		0x03f44084 },
	{ MT_GF20_PROT_CFG,		0x01744004 },
	{ MT_GF40_PROT_CFG,		0x03f44084 },
	{ MT_MM20_PROT_CFG,		0x01744004 },
	{ MT_TXOP_CTRL_CFG,		0x0000583f },
	{ MT_TX_RTS_CFG,		0x01092b20 },
	{ MT_EXP_ACK_TIME,		0x002400ca },
	{ MT_TXOP_HLDR_ET,		0x00000002 },
	{ MT_XIFS_TIME_CFG,		0x33a41010 },
	{ MT_PWR_PIN_CFG,		0x00000000 },
	{ MT_PN_PAD_MODE,		0x00000001 },
};

static const struct mt76_reg_pair mac_chip_vals[] = {
	{ MT_TSO_CTRL,			0x00006050 },
	{ MT_BCN_OFFSET(0),		0x18100800 },
	{ MT_BCN_OFFSET(1),		0x38302820 },
	{ MT_PBF_SYS_CTRL,		0x00080c00 },
	{ MT_PBF_CFG,			0x7f723c1f },
	{ MT_FCE_PSE_CTRL,		0x00000001 },
	{ MT_PAUSE_ENABLE_CONTROL1,	0x00000000 },
	{ MT_TX0_RF_GAIN_CORR,		0x003b0005 },
	{ MT_TX0_RF_GAIN_ATTEN,		0x00006900 },
	{ MT_TX0_BB_GAIN_ATTEN,		0x00000400 },
	{ MT_TX_ALC_VGA3,		0x00060006 },
	{ MT_TX_SW_CFG0,		0x00000402 },
	{ MT_TX_SW_CFG1,		0x00000000 },
	{ MT_TX_SW_CFG2,		0x00000000 },
	{ MT_HEADER_TRANS_CTRL_REG,	0x00000000 },
	{ MT_FCE_CSO,			0x0000030f },
	{ MT_FCE_PARAMETERS,		0x00256f0f },
};

#endif
