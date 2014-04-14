/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/
#ifndef	__RTW_RF_H_
#define __RTW_RF_H_

#include <rtw_cmd.h>

#define OFDM_PHY		1
#define MIXED_PHY		2
#define CCK_PHY			3

#define NumRates		(13)

/*  slot time for 11g */
#define SHORT_SLOT_TIME			9
#define NON_SHORT_SLOT_TIME		20

/*  We now define the max channels in each channel plan. */
#define	MAX_CHANNEL_NUM_2G	14
#define	MAX_CHANNEL_NUM_5G	24
#define	MAX_CHANNEL_NUM		38/* 14+24 */

/* define NUM_REGULATORYS	21 */
#define NUM_REGULATORYS	1

/* Country codes */
#define USA		0x555320
#define EUROPE		0x1 /* temp, should be provided later */
#define JAPAN		0x2 /* temp, should be provided later */

struct	regulatory_class {
	u32	starting_freq;					/* MHz, */
	u8	channel_set[MAX_CHANNEL_NUM];
	u8	channel_cck_power[MAX_CHANNEL_NUM];/* dbm */
	u8	channel_ofdm_power[MAX_CHANNEL_NUM];/* dbm */
	u8	txpower_limit;					/* dbm */
	u8	channel_spacing;				/* MHz */
	u8	modem;
};

enum {
	cESS		= 0x0001,
	cIBSS		= 0x0002,
	cPollable	= 0x0004,
	cPollReq	= 0x0008,
	cPrivacy	= 0x0010,
	cShortPreamble	= 0x0020,
	cPBCC		= 0x0040,
	cChannelAgility	= 0x0080,
	cSpectrumMgnt	= 0x0100,
	cQos		= 0x0200,	/*  For HCCA, use with CF-Pollable and CF-PollReq */
	cShortSlotTime	= 0x0400,
	cAPSD		= 0x0800,
	cRM		= 0x1000,	/*  RRM (Radio Request Measurement) */
	cDSSS_OFDM	= 0x2000,
	cDelayedBA	= 0x4000,
	cImmediateBA	= 0x8000,
};

enum {
	PREAMBLE_LONG	= 1,
	PREAMBLE_AUTO	= 2,
	PREAMBLE_SHORT	= 3,
};

/*  Bandwidth Offset */
#define HAL_PRIME_CHNL_OFFSET_DONT_CARE	0
#define HAL_PRIME_CHNL_OFFSET_LOWER	1
#define HAL_PRIME_CHNL_OFFSET_UPPER	2

/*  Represent Channel Width in HT Capabilities */
enum ht_channel_width {
	HT_CHANNEL_WIDTH_20 = 0,
	HT_CHANNEL_WIDTH_40 = 1,
	HT_CHANNEL_WIDTH_80 = 2,
	HT_CHANNEL_WIDTH_160 = 3,
	HT_CHANNEL_WIDTH_10 = 4,
};

/*  */
/*  Represent Extention Channel Offset in HT Capabilities */
/*  This is available only in 40Mhz mode. */
/*  */
enum {
	HT_EXTCHNL_OFFSET_NO_EXT = 0,
	HT_EXTCHNL_OFFSET_UPPER = 1,
	HT_EXTCHNL_OFFSET_NO_DEF = 2,
	HT_EXTCHNL_OFFSET_LOWER = 3,
};

/* 2007/11/15 MH Define different RF type. */
enum {
	RF_1T2R = 0,
	RF_2T4R = 1,
	RF_2T2R = 2,
	RF_1T1R = 3,
	RF_2T2R_GREEN = 4,
	RF_819X_MAX_TYPE = 5,
};

#endif /* _RTL8711_RF_H_ */
