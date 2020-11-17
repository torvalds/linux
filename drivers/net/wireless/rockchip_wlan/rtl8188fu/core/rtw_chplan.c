/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2018 Realtek Corporation.
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
 *****************************************************************************/
#define _RTW_CHPLAN_C_

#include <drv_types.h>

#define RTW_DOMAIN_MAP_VER	"40e"
#define RTW_COUNTRY_MAP_VER	"23"

#ifdef LEGACY_CHANNEL_PLAN_REF
/********************************************************
ChannelPlan definitions
*********************************************************/
static RT_CHANNEL_PLAN legacy_channel_plan[] = {
	/* 0x00, RTW_CHPLAN_FCC */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165}, 32},
	/* 0x01, RTW_CHPLAN_IC */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 31},
	/* 0x02, RTW_CHPLAN_ETSI */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 32},
	/* 0x03, RTW_CHPLAN_SPAIN */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x04, RTW_CHPLAN_FRANCE */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x05, RTW_CHPLAN_MKK */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x06, RTW_CHPLAN_MKK1 */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x07, RTW_CHPLAN_ISRAEL */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64}, 21},
	/* 0x08, RTW_CHPLAN_TELEC */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 36, 40, 44, 48, 52, 56, 60, 64}, 22},
	/* 0x09, RTW_CHPLAN_GLOBAL_DOAMIN */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, 14},
	/* 0x0A, RTW_CHPLAN_WORLD_WIDE_13 */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}, 13},
	/* 0x0B, RTW_CHPLAN_TAIWAN */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165}, 26},
	/* 0x0C, RTW_CHPLAN_CHINA */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 149, 153, 157, 161, 165}, 18},
	/* 0x0D, RTW_CHPLAN_SINGAPORE_INDIA_MEXICO */	{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165}, 24},
	/* 0x0E, RTW_CHPLAN_KOREA */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165}, 31},
	/* 0x0F, RTW_CHPLAN_TURKEY */					{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64}, 19},
	/* 0x10, RTW_CHPLAN_JAPAN */						{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140}, 32},
	/* 0x11, RTW_CHPLAN_FCC_NO_DFS */				{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 149, 153, 157, 161, 165}, 20},
	/* 0x12, RTW_CHPLAN_JAPAN_NO_DFS */				{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48}, 17},
	/* 0x13, RTW_CHPLAN_WORLD_WIDE_5G */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165}, 37},
	/* 0x14, RTW_CHPLAN_TAIWAN_NO_DFS */			{{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 56, 60, 64, 149, 153, 157, 161, 165}, 19},
};
#endif

enum rtw_rd_2g {
	RTW_RD_2G_NULL = 0,
	RTW_RD_2G_WORLD = 1,	/* Worldwird 13 */
	RTW_RD_2G_ETSI1 = 2,	/* Europe */
	RTW_RD_2G_FCC1 = 3,		/* US */
	RTW_RD_2G_MKK1 = 4,		/* Japan */
	RTW_RD_2G_ETSI2 = 5,	/* France */
	RTW_RD_2G_GLOBAL = 6,	/* Global domain */
	RTW_RD_2G_MKK2 = 7,		/* Japan */
	RTW_RD_2G_FCC2 = 8,		/* US */
	RTW_RD_2G_IC1 = 9,		/* Canada */
	RTW_RD_2G_WORLD1 = 10,	/* Worldwide 11 */
	RTW_RD_2G_KCC1 = 11,	/* Korea */
	RTW_RD_2G_IC2 = 12,		/* Canada */

	RTW_RD_2G_MAX,
};

enum rtw_rd_5g {
	RTW_RD_5G_NULL = 0,		/*	*/
	RTW_RD_5G_ETSI1 = 1,	/* Europe */
	RTW_RD_5G_ETSI2 = 2,	/* Australia, New Zealand */
	RTW_RD_5G_ETSI3 = 3,	/* Russia */
	RTW_RD_5G_FCC1 = 4,		/* US */
	RTW_RD_5G_FCC2 = 5,		/* FCC w/o DFS Channels */
	RTW_RD_5G_FCC3 = 6,		/* Bolivia, Chile, El Salvador, Venezuela */
	RTW_RD_5G_FCC4 = 7,		/* Venezuela */
	RTW_RD_5G_FCC5 = 8,		/* China */
	RTW_RD_5G_FCC6 = 9,		/*	*/
	RTW_RD_5G_FCC7 = 10,	/* US(w/o Weather radar) */
	RTW_RD_5G_IC1 = 11,		/* Canada(w/o Weather radar) */
	RTW_RD_5G_KCC1 = 12,	/* Korea */
	RTW_RD_5G_MKK1 = 13,	/* Japan */
	RTW_RD_5G_MKK2 = 14,	/* Japan (W52, W53) */
	RTW_RD_5G_MKK3 = 15,	/* Japan (W56) */
	RTW_RD_5G_NCC1 = 16,	/* Taiwan, (w/o Weather radar) */
	RTW_RD_5G_NCC2 = 17,	/* Taiwan, Band2, Band4 */
	RTW_RD_5G_NCC3 = 18,	/* Taiwan w/o DFS, Band4 only */
	RTW_RD_5G_ETSI4 = 19,	/* Europe w/o DFS, Band1 only */
	RTW_RD_5G_ETSI5 = 20,	/* Australia, New Zealand(w/o Weather radar) */
	RTW_RD_5G_FCC8 = 21,	/* Latin America */
	RTW_RD_5G_ETSI6 = 22,	/* Israel, Bahrain, Egypt, India, China, Malaysia */
	RTW_RD_5G_ETSI7 = 23,	/* China */
	RTW_RD_5G_ETSI8 = 24,	/* Jordan */
	RTW_RD_5G_ETSI9 = 25,	/* Lebanon */
	RTW_RD_5G_ETSI10 = 26,	/* Qatar */
	RTW_RD_5G_ETSI11 = 27,	/* Russia */
	RTW_RD_5G_NCC4 = 28,	/* Taiwan, (w/o Weather radar) */
	RTW_RD_5G_ETSI12 = 29,	/* Indonesia */
	RTW_RD_5G_FCC9 = 30,	/* (w/o Weather radar) */
	RTW_RD_5G_ETSI13 = 31,	/* (w/o Weather radar) */
	RTW_RD_5G_FCC10 = 32,	/* Argentina(w/o Weather radar) */
	RTW_RD_5G_MKK4 = 33,	/* Japan (W52) */
	RTW_RD_5G_ETSI14 = 34,	/* Russia */
	RTW_RD_5G_FCC11 = 35,	/* US(include CH144) */
	RTW_RD_5G_ETSI15 = 36,	/* Malaysia */
	RTW_RD_5G_MKK5 = 37,	/* Japan */
	RTW_RD_5G_ETSI16 = 38,	/* Europe */
	RTW_RD_5G_ETSI17 = 39,	/* Europe */
	RTW_RD_5G_FCC12 = 40,	/* FCC */
	RTW_RD_5G_FCC13 = 41,	/* FCC */
	RTW_RD_5G_FCC14 = 42,	/* FCC w/o Weather radar(w/o 5600~5650MHz) */
	RTW_RD_5G_FCC15 = 43,	/* FCC w/o Band3 */
	RTW_RD_5G_FCC16 = 44,	/* FCC w/o Band3 */
	RTW_RD_5G_ETSI18 = 45,	/* ETSI w/o DFS Band2&3 */
	RTW_RD_5G_ETSI19 = 46,	/* Europe */
	RTW_RD_5G_FCC17 = 47,	/* FCC w/o Weather radar(w/o 5600~5650MHz) */
	RTW_RD_5G_ETSI20 = 48,	/* Europe */
	RTW_RD_5G_IC2 = 49,		/* Canada(w/o Weather radar), include ch144 */
	RTW_RD_5G_ETSI21 = 50,	/* Australia, New Zealand(w/o Weather radar) */
	RTW_RD_5G_FCC18 = 51,	/*  */
	RTW_RD_5G_WORLD = 52,	/* Worldwide */
	RTW_RD_5G_CHILE1 = 53,	/* Chile */
	RTW_RD_5G_ACMA1 = 54,	/* Australia, New Zealand (w/o Weather radar) (w/o Ch120~Ch128) */
	RTW_RD_5G_WORLD1 = 55,	/* 5G Worldwide Band1&2 */
	RTW_RD_5G_CHILE2 = 56,	/* Chile (Band2,Band3) */
	RTW_RD_5G_KCC2 = 57,	/* Korea (New standard) */
	RTW_RD_5G_KCC3 = 58,	/* Korea (2018 Dec 05 New standard, include ch144) */
	RTW_RD_5G_MKK6 = 59,	/* Japan */
	RTW_RD_5G_MKK7 = 60,	/* Japan */
	RTW_RD_5G_MKK8 = 61,	/* Japan */

	/* === Below are driver defined for legacy channel plan compatible, DON'T assign index ==== */
	RTW_RD_5G_OLD_FCC1,
	RTW_RD_5G_OLD_NCC1,
	RTW_RD_5G_OLD_KCC1,

	RTW_RD_5G_MAX,
};

struct ch_list_t {
	u8 *len_ch;
};

#define CH_LIST_ENT(_len, arg...) \
	{.len_ch = (u8[_len + 1]) {_len, ##arg}, }

#define CH_LIST_LEN(_ch_list) (_ch_list.len_ch[0])
#define CH_LIST_CH(_ch_list, _i) (_ch_list.len_ch[_i + 1])

struct chplan_ent_t {
	u8 rd_2g;
#ifdef CONFIG_IEEE80211_BAND_5GHZ
	u8 rd_5g;
#endif
	u8 regd; /* value of REGULATION_TXPWR_LMT */
};

#ifdef CONFIG_IEEE80211_BAND_5GHZ
#define CHPLAN_ENT(i2g, i5g, regd) {i2g, i5g, regd}
#else
#define CHPLAN_ENT(i2g, i5g, regd) {i2g, regd}
#endif

static struct ch_list_t RTW_ChannelPlan2G[] = {
	/* 0, RTW_RD_2G_NULL */		CH_LIST_ENT(0),
	/* 1, RTW_RD_2G_WORLD */	CH_LIST_ENT(13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 2, RTW_RD_2G_ETSI1 */		CH_LIST_ENT(13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 3, RTW_RD_2G_FCC1 */		CH_LIST_ENT(11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
	/* 4, RTW_RD_2G_MKK1 */		CH_LIST_ENT(14, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14),
	/* 5, RTW_RD_2G_ETSI2 */		CH_LIST_ENT(4, 10, 11, 12, 13),
	/* 6, RTW_RD_2G_GLOBAL */	CH_LIST_ENT(14, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14),
	/* 7, RTW_RD_2G_MKK2 */		CH_LIST_ENT(13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 8, RTW_RD_2G_FCC2 */		CH_LIST_ENT(13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 9, RTW_RD_2G_IC1 */		CH_LIST_ENT(13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 10, RTW_RD_2G_WORLD1 */	CH_LIST_ENT(11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
	/* 11, RTW_RD_2G_KCC1 */	CH_LIST_ENT(13, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13),
	/* 12, RTW_RD_2G_IC2 */		CH_LIST_ENT(11, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11),
};

#ifdef CONFIG_IEEE80211_BAND_5GHZ
static struct ch_list_t RTW_ChannelPlan5G[] = {
	/* 0, RTW_RD_5G_NULL */		CH_LIST_ENT(0),
	/* 1, RTW_RD_5G_ETSI1 */		CH_LIST_ENT(19, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140),
	/* 2, RTW_RD_5G_ETSI2 */		CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 3, RTW_RD_5G_ETSI3 */		CH_LIST_ENT(22, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 149, 153, 157, 161, 165),
	/* 4, RTW_RD_5G_FCC1 */		CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 5, RTW_RD_5G_FCC2 */		CH_LIST_ENT(9, 36, 40, 44, 48, 149, 153, 157, 161, 165),
	/* 6, RTW_RD_5G_FCC3 */		CH_LIST_ENT(13, 36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165),
	/* 7, RTW_RD_5G_FCC4 */		CH_LIST_ENT(12, 36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161),
	/* 8, RTW_RD_5G_FCC5 */		CH_LIST_ENT(5, 149, 153, 157, 161, 165),
	/* 9, RTW_RD_5G_FCC6 */		CH_LIST_ENT(8, 36, 40, 44, 48, 52, 56, 60, 64),
	/* 10, RTW_RD_5G_FCC7 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 11, RTW_RD_5G_IC1 */		CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 12, RTW_RD_5G_KCC1 */	CH_LIST_ENT(19, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161),
	/* 13, RTW_RD_5G_MKK1 */	CH_LIST_ENT(19, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140),
	/* 14, RTW_RD_5G_MKK2 */	CH_LIST_ENT(8, 36, 40, 44, 48, 52, 56, 60, 64),
	/* 15, RTW_RD_5G_MKK3 */	CH_LIST_ENT(11, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140),
	/* 16, RTW_RD_5G_NCC1 */	CH_LIST_ENT(16, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 17, RTW_RD_5G_NCC2 */	CH_LIST_ENT(8, 56, 60, 64, 149, 153, 157, 161, 165),
	/* 18, RTW_RD_5G_NCC3 */	CH_LIST_ENT(5, 149, 153, 157, 161, 165),
	/* 19, RTW_RD_5G_ETSI4 */	CH_LIST_ENT(4, 36, 40, 44, 48),
	/* 20, RTW_RD_5G_ETSI5 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 21, RTW_RD_5G_FCC8 */	CH_LIST_ENT(4, 149, 153, 157, 161),
	/* 22, RTW_RD_5G_ETSI6 */	CH_LIST_ENT(8, 36, 40, 44, 48, 52, 56, 60, 64),
	/* 23, RTW_RD_5G_ETSI7 */	CH_LIST_ENT(13, 36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165),
	/* 24, RTW_RD_5G_ETSI8 */	CH_LIST_ENT(9, 36, 40, 44, 48, 149, 153, 157, 161, 165),
	/* 25, RTW_RD_5G_ETSI9 */	CH_LIST_ENT(11, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140),
	/* 26, RTW_RD_5G_ETSI10 */	CH_LIST_ENT(5, 149, 153, 157, 161, 165),
	/* 27, RTW_RD_5G_ETSI11 */	CH_LIST_ENT(16, 36, 40, 44, 48, 52, 56, 60, 64, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 28, RTW_RD_5G_NCC4 */	CH_LIST_ENT(17, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 29, RTW_RD_5G_ETSI12 */	CH_LIST_ENT(4, 149, 153, 157, 161),
	/* 30, RTW_RD_5G_FCC9 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 31, RTW_RD_5G_ETSI13 */	CH_LIST_ENT(16, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140),
	/* 32, RTW_RD_5G_FCC10 */	CH_LIST_ENT(20, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161),
	/* 33, RTW_RD_5G_MKK4 */	CH_LIST_ENT(4, 36, 40, 44, 48),
	/* 34, RTW_RD_5G_ETSI14 */	CH_LIST_ENT(11, 36, 40, 44, 48, 52, 56, 60, 64, 132, 136, 140),
	/* 35, RTW_RD_5G_FCC11 */	CH_LIST_ENT(25, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165),
	/* 36, RTW_RD_5G_ETSI15 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 149, 153, 157, 161, 165),
	/* 37, RTW_RD_5G_MKK5 */	CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 38, RTW_RD_5G_ETSI16 */	CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 39, RTW_RD_5G_ETSI17 */	CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 40, RTW_RD_5G_FCC12*/	CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 41, RTW_RD_5G_FCC13 */	CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 42, RTW_RD_5G_FCC14 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 43, RTW_RD_5G_FCC15 */	CH_LIST_ENT(13, 36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165),
	/* 44, RTW_RD_5G_FCC16 */	CH_LIST_ENT(13, 36, 40, 44, 48, 52, 56, 60, 64, 149, 153, 157, 161, 165),
	/* 45, RTW_RD_5G_ETSI18 */	CH_LIST_ENT(9, 36, 40, 44, 48, 149, 153, 157, 161, 165),
	/* 46, RTW_RD_5G_ETSI19 */	CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 47, RTW_RD_5G_FCC17 */	CH_LIST_ENT(16, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140),
	/* 48, RTW_RD_5G_ETSI20 */	CH_LIST_ENT(9, 52, 56, 60, 64, 149, 153, 157, 161, 165),
	/* 49, RTW_RD_5G_IC2 */		CH_LIST_ENT(22, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 144, 149, 153, 157, 161, 165),
	/* 50, RTW_RD_5G_ETSI21 */	CH_LIST_ENT(13, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 51, RTW_RD_5G_FCC18 */	CH_LIST_ENT(8, 100, 104, 108, 112, 116, 132, 136, 140),
	/* 52, RTW_RD_5G_WORLD */	CH_LIST_ENT(25, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165),
	/* 53, RTW_RD_5G_CHILE1 */	CH_LIST_ENT(25, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165),
	/* 54, RTW_RD_5G_ACMA1 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 55, RTW_RD_5G_WORLD1 */	CH_LIST_ENT(8, 36, 40, 44, 48, 52, 56, 60, 64),
	/* 56, RTW_RD_5G_CHILE2 */	CH_LIST_ENT(16, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144),
	/* 57, RTW_RD_5G_KCC2 */	CH_LIST_ENT(24, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 58, RTW_RD_5G_KCC3 */	CH_LIST_ENT(25, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165),
	/* 59, RTW_RD_5G_MKK6 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 149, 153, 157, 161, 165),
	/* 60, RTW_RD_5G_MKK7 */	CH_LIST_ENT(21, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 132, 136, 140, 149, 153, 157, 161, 165),
	/* 61, RTW_RD_5G_MKK8 */	CH_LIST_ENT(23, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 136, 140, 149, 153, 157, 161, 165),

	/* === Below are driver defined for legacy channel plan compatible, NO static index assigned ==== */
	/* RTW_RD_5G_OLD_FCC1 */	CH_LIST_ENT(20, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165),
	/* RTW_RD_5G_OLD_NCC1 */	CH_LIST_ENT(15, 56, 60, 64, 100, 104, 108, 112, 116, 136, 140, 149, 153, 157, 161, 165),
	/* RTW_RD_5G_OLD_KCC1 */	CH_LIST_ENT(20, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 149, 153, 157, 161, 165),
};
#endif /* CONFIG_IEEE80211_BAND_5GHZ */

static struct chplan_ent_t RTW_ChannelPlanMap[RTW_CHPLAN_MAX] = {
	/* ===== 0x00 ~ 0x1F, legacy channel plan ===== */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_KCC1,		TXPWR_LMT_FCC),		/* 0x00, RTW_CHPLAN_FCC */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_OLD_FCC1,	TXPWR_LMT_FCC),		/* 0x01, RTW_CHPLAN_IC */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI1,	TXPWR_LMT_ETSI),	/* 0x02, RTW_CHPLAN_ETSI */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_NULL,		TXPWR_LMT_ETSI),	/* 0x03, RTW_CHPLAN_SPAIN */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_NULL,		TXPWR_LMT_ETSI),	/* 0x04, RTW_CHPLAN_FRANCE */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_NULL,		TXPWR_LMT_MKK),		/* 0x05, RTW_CHPLAN_MKK */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_NULL,		TXPWR_LMT_MKK),		/* 0x06, RTW_CHPLAN_MKK1 */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_FCC6,		TXPWR_LMT_ETSI),	/* 0x07, RTW_CHPLAN_ISRAEL */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_FCC6,		TXPWR_LMT_MKK),		/* 0x08, RTW_CHPLAN_TELEC */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x09, RTW_CHPLAN_GLOBAL_DOAMIN */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x0A, RTW_CHPLAN_WORLD_WIDE_13 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_OLD_NCC1,	TXPWR_LMT_FCC),		/* 0x0B, RTW_CHPLAN_TAIWAN */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_FCC5,		TXPWR_LMT_ETSI),	/* 0x0C, RTW_CHPLAN_CHINA */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC3,		TXPWR_LMT_WW),		/* 0x0D, RTW_CHPLAN_SINGAPORE_INDIA_MEXICO */ /* ETSI:Singapore, India. FCC:Mexico => WW */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_OLD_KCC1,	TXPWR_LMT_ETSI),	/* 0x0E, RTW_CHPLAN_KOREA */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC6,		TXPWR_LMT_ETSI),	/* 0x0F, RTW_CHPLAN_TURKEY */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI1,	TXPWR_LMT_MKK),		/* 0x10, RTW_CHPLAN_JAPAN */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC2,		TXPWR_LMT_FCC),		/* 0x11, RTW_CHPLAN_FCC_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_FCC7,		TXPWR_LMT_MKK),		/* 0x12, RTW_CHPLAN_JAPAN_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC1,		TXPWR_LMT_WW),		/* 0x13, RTW_CHPLAN_WORLD_WIDE_5G */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_NCC2,		TXPWR_LMT_FCC),		/* 0x14, RTW_CHPLAN_TAIWAN_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC7,		TXPWR_LMT_ETSI),	/* 0x15, RTW_CHPLAN_ETSI_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_NCC1,		TXPWR_LMT_ETSI),	/* 0x16, RTW_CHPLAN_KOREA_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_FCC7,		TXPWR_LMT_MKK),		/* 0x17, RTW_CHPLAN_JAPAN_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_FCC5,		TXPWR_LMT_ETSI),	/* 0x18, RTW_CHPLAN_PAKISTAN_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC5,		TXPWR_LMT_FCC),		/* 0x19, RTW_CHPLAN_TAIWAN2_NO_DFS */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x1A, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x1B, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x1C, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x1D, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x1E, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_FCC1,		TXPWR_LMT_WW),		/* 0x1F, RTW_CHPLAN_WORLD_WIDE_ONLY_5G */

	/* ===== 0x20 ~ 0x7F, new channel plan ===== */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x20, RTW_CHPLAN_WORLD_NULL */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_NULL,		TXPWR_LMT_ETSI),	/* 0x21, RTW_CHPLAN_ETSI1_NULL */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_NULL,		TXPWR_LMT_FCC),		/* 0x22, RTW_CHPLAN_FCC1_NULL */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_NULL,		TXPWR_LMT_MKK),		/* 0x23, RTW_CHPLAN_MKK1_NULL */
	CHPLAN_ENT(RTW_RD_2G_ETSI2,		RTW_RD_5G_NULL,		TXPWR_LMT_ETSI),	/* 0x24, RTW_CHPLAN_ETSI2_NULL */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC1,		TXPWR_LMT_FCC),		/* 0x25, RTW_CHPLAN_FCC1_FCC1 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI1,	TXPWR_LMT_ETSI),	/* 0x26, RTW_CHPLAN_WORLD_ETSI1 */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_MKK1,		TXPWR_LMT_MKK),		/* 0x27, RTW_CHPLAN_MKK1_MKK1 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_KCC1,		TXPWR_LMT_KCC),		/* 0x28, RTW_CHPLAN_WORLD_KCC1 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC2,		TXPWR_LMT_FCC),		/* 0x29, RTW_CHPLAN_WORLD_FCC2 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_NULL,		TXPWR_LMT_FCC),		/* 0x2A, RTW_CHPLAN_FCC2_NULL */
	CHPLAN_ENT(RTW_RD_2G_IC1,		RTW_RD_5G_IC2,		TXPWR_LMT_IC),		/* 0x2B, RTW_CHPLAN_IC1_IC2 */
	CHPLAN_ENT(RTW_RD_2G_MKK2,		RTW_RD_5G_NULL,		TXPWR_LMT_MKK),		/* 0x2C, RTW_CHPLAN_MKK2_NULL */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_CHILE1,	TXPWR_LMT_CHILE),	/* 0x2D, RTW_CHPLAN_WORLD_CHILE1 */
	CHPLAN_ENT(RTW_RD_2G_WORLD1,	RTW_RD_5G_WORLD1,	TXPWR_LMT_WW),		/* 0x2E, RTW_CHPLAN_WORLD1_WORLD1 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_CHILE2,	TXPWR_LMT_CHILE),	/* 0x2F, RTW_CHPLAN_WORLD_CHILE2 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC3,		TXPWR_LMT_FCC),		/* 0x30, RTW_CHPLAN_WORLD_FCC3 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC4,		TXPWR_LMT_FCC),		/* 0x31, RTW_CHPLAN_WORLD_FCC4 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC5,		TXPWR_LMT_FCC),		/* 0x32, RTW_CHPLAN_WORLD_FCC5 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC6,		TXPWR_LMT_FCC),		/* 0x33, RTW_CHPLAN_WORLD_FCC6 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC7,		TXPWR_LMT_FCC),		/* 0x34, RTW_CHPLAN_FCC1_FCC7 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI2,	TXPWR_LMT_ETSI),	/* 0x35, RTW_CHPLAN_WORLD_ETSI2 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI3,	TXPWR_LMT_ETSI),	/* 0x36, RTW_CHPLAN_WORLD_ETSI3 */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_MKK2,		TXPWR_LMT_MKK),		/* 0x37, RTW_CHPLAN_MKK1_MKK2 */
	CHPLAN_ENT(RTW_RD_2G_MKK1,		RTW_RD_5G_MKK3,		TXPWR_LMT_MKK),		/* 0x38, RTW_CHPLAN_MKK1_MKK3 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_NCC1,		TXPWR_LMT_FCC),		/* 0x39, RTW_CHPLAN_FCC1_NCC1 */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI1,	TXPWR_LMT_ETSI),	/* 0x3A, RTW_CHPLAN_ETSI1_ETSI1 */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ACMA1,	TXPWR_LMT_ACMA),	/* 0x3B, RTW_CHPLAN_ETSI1_ACMA1 */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI6,	TXPWR_LMT_ETSI),	/* 0x3C, RTW_CHPLAN_ETSI1_ETSI6 */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI12,	TXPWR_LMT_ETSI),	/* 0x3D, RTW_CHPLAN_ETSI1_ETSI12 */
	CHPLAN_ENT(RTW_RD_2G_KCC1,		RTW_RD_5G_KCC2,		TXPWR_LMT_KCC),		/* 0x3E, RTW_CHPLAN_KCC1_KCC2 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC11,	TXPWR_LMT_FCC),		/* 0x3F, RTW_CHPLAN_FCC1_FCC11*/
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_NCC2,		TXPWR_LMT_FCC),		/* 0x40, RTW_CHPLAN_FCC1_NCC2 */
	CHPLAN_ENT(RTW_RD_2G_GLOBAL,	RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x41, RTW_CHPLAN_GLOBAL_NULL */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI4,	TXPWR_LMT_ETSI),	/* 0x42, RTW_CHPLAN_ETSI1_ETSI4 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC2,		TXPWR_LMT_FCC),		/* 0x43, RTW_CHPLAN_FCC1_FCC2 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_NCC3,		TXPWR_LMT_FCC),		/* 0x44, RTW_CHPLAN_FCC1_NCC3 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ACMA1,	TXPWR_LMT_ACMA),	/* 0x45, RTW_CHPLAN_WORLD_ACMA1 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC8,		TXPWR_LMT_FCC),		/* 0x46, RTW_CHPLAN_FCC1_FCC8 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI6,	TXPWR_LMT_ETSI),	/* 0x47, RTW_CHPLAN_WORLD_ETSI6 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI7,	TXPWR_LMT_ETSI),	/* 0x48, RTW_CHPLAN_WORLD_ETSI7 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI8,	TXPWR_LMT_ETSI),	/* 0x49, RTW_CHPLAN_WORLD_ETSI8 */
	CHPLAN_ENT(RTW_RD_2G_IC2,		RTW_RD_5G_IC2,		TXPWR_LMT_IC),		/* 0x4A, RTW_CHPLAN_IC2_IC2 */
	CHPLAN_ENT(RTW_RD_2G_KCC1,		RTW_RD_5G_KCC3,		TXPWR_LMT_KCC),		/* 0x4B, RTW_CHPLAN_KCC1_KCC3 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC15,	TXPWR_LMT_FCC),		/* 0x4C, RTW_CHPLAN_FCC1_FCC15*/
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x4D, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x4E, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x4F, */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI9,	TXPWR_LMT_ETSI),	/* 0x50, RTW_CHPLAN_WORLD_ETSI9 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI10,	TXPWR_LMT_ETSI),	/* 0x51, RTW_CHPLAN_WORLD_ETSI10 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI11,	TXPWR_LMT_ETSI),	/* 0x52, RTW_CHPLAN_WORLD_ETSI11 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_NCC4,		TXPWR_LMT_FCC),		/* 0x53, RTW_CHPLAN_FCC1_NCC4 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI12,	TXPWR_LMT_ETSI),	/* 0x54, RTW_CHPLAN_WORLD_ETSI12 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC9,		TXPWR_LMT_FCC),		/* 0x55, RTW_CHPLAN_FCC1_FCC9 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI13,	TXPWR_LMT_ETSI),	/* 0x56, RTW_CHPLAN_WORLD_ETSI13 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC10,	TXPWR_LMT_FCC),		/* 0x57, RTW_CHPLAN_FCC1_FCC10 */
	CHPLAN_ENT(RTW_RD_2G_MKK2,		RTW_RD_5G_MKK4,		TXPWR_LMT_MKK),		/* 0x58, RTW_CHPLAN_MKK2_MKK4 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI14,	TXPWR_LMT_ETSI),	/* 0x59, RTW_CHPLAN_WORLD_ETSI14 */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x5A, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x5B, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x5C, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x5D, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x5E, */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_NULL,		TXPWR_LMT_WW),		/* 0x5F, */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC5,		TXPWR_LMT_FCC),		/* 0x60, RTW_CHPLAN_FCC1_FCC5 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_FCC7,		TXPWR_LMT_FCC),		/* 0x61, RTW_CHPLAN_FCC2_FCC7 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_FCC1,		TXPWR_LMT_FCC),		/* 0x62, RTW_CHPLAN_FCC2_FCC1 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI15,	TXPWR_LMT_ETSI),	/* 0x63, RTW_CHPLAN_WORLD_ETSI15 */
	CHPLAN_ENT(RTW_RD_2G_MKK2,		RTW_RD_5G_MKK5,		TXPWR_LMT_MKK),		/* 0x64, RTW_CHPLAN_MKK2_MKK5 */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI16,	TXPWR_LMT_ETSI),	/* 0x65, RTW_CHPLAN_ETSI1_ETSI16 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC14,	TXPWR_LMT_FCC),		/* 0x66, RTW_CHPLAN_FCC1_FCC14 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC12,	TXPWR_LMT_FCC),		/* 0x67, RTW_CHPLAN_FCC1_FCC12 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_FCC14,	TXPWR_LMT_FCC),		/* 0x68, RTW_CHPLAN_FCC2_FCC14 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_FCC12,	TXPWR_LMT_FCC),		/* 0x69, RTW_CHPLAN_FCC2_FCC12 */
	CHPLAN_ENT(RTW_RD_2G_ETSI1,		RTW_RD_5G_ETSI17,	TXPWR_LMT_ETSI),	/* 0x6A, RTW_CHPLAN_ETSI1_ETSI17 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC16,	TXPWR_LMT_FCC),		/* 0x6B, RTW_CHPLAN_WORLD_FCC16 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC13,	TXPWR_LMT_FCC),		/* 0x6C, RTW_CHPLAN_WORLD_FCC13 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_FCC15,	TXPWR_LMT_FCC),		/* 0x6D, RTW_CHPLAN_FCC2_FCC15 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC12,	TXPWR_LMT_FCC),		/* 0x6E, RTW_CHPLAN_WORLD_FCC12 */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_ETSI8,	TXPWR_LMT_ETSI),	/* 0x6F, RTW_CHPLAN_NULL_ETSI8 */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_ETSI18,	TXPWR_LMT_ETSI),	/* 0x70, RTW_CHPLAN_NULL_ETSI18 */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_ETSI17,	TXPWR_LMT_ETSI),	/* 0x71, RTW_CHPLAN_NULL_ETSI17 */
	CHPLAN_ENT(RTW_RD_2G_NULL,		RTW_RD_5G_ETSI19,	TXPWR_LMT_ETSI),	/* 0x72, RTW_CHPLAN_NULL_ETSI19 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC7,		TXPWR_LMT_FCC),		/* 0x73, RTW_CHPLAN_WORLD_FCC7 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_FCC17,	TXPWR_LMT_FCC),		/* 0x74, RTW_CHPLAN_FCC2_FCC17 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI20,	TXPWR_LMT_ETSI),	/* 0x75, RTW_CHPLAN_WORLD_ETSI20 */
	CHPLAN_ENT(RTW_RD_2G_FCC2,		RTW_RD_5G_FCC11,	TXPWR_LMT_FCC),		/* 0x76, RTW_CHPLAN_FCC2_FCC11 */
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_ETSI21,	TXPWR_LMT_ETSI),	/* 0x77, RTW_CHPLAN_WORLD_ETSI21 */
	CHPLAN_ENT(RTW_RD_2G_FCC1,		RTW_RD_5G_FCC18,	TXPWR_LMT_FCC),		/* 0x78, RTW_CHPLAN_FCC1_FCC18 */
	CHPLAN_ENT(RTW_RD_2G_MKK2,		RTW_RD_5G_MKK1,		TXPWR_LMT_MKK),		/* 0x79, RTW_CHPLAN_MKK2_MKK1 */
};

static struct chplan_ent_t RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE =
	CHPLAN_ENT(RTW_RD_2G_WORLD,		RTW_RD_5G_FCC1,		TXPWR_LMT_FCC);		/* 0x7F, Realtek Define */

u8 rtw_chplan_get_default_regd(u8 id)
{
	u8 regd;

	if (id == RTW_CHPLAN_REALTEK_DEFINE)
		regd = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.regd;
	else
		regd = RTW_ChannelPlanMap[id].regd;

	return regd;
}

bool rtw_chplan_is_empty(u8 id)
{
	struct chplan_ent_t *chplan_map;

	if (id == RTW_CHPLAN_REALTEK_DEFINE)
		chplan_map = &RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE;
	else
		chplan_map = &RTW_ChannelPlanMap[id];

	if (chplan_map->rd_2g == RTW_RD_2G_NULL
		#ifdef CONFIG_IEEE80211_BAND_5GHZ
		&& chplan_map->rd_5g == RTW_RD_5G_NULL
		#endif
	)
		return _TRUE;

	return _FALSE;
}

bool rtw_regsty_is_excl_chs(struct registry_priv *regsty, u8 ch)
{
	int i;

	for (i = 0; i < MAX_CHANNEL_NUM; i++) {
		if (regsty->excl_chs[i] == 0)
			break;
		if (regsty->excl_chs[i] == ch)
			return _TRUE;
	}
	return _FALSE;
}

inline static u8 rtw_rd_5g_band1_passive(u8 rtw_rd_5g)
{
	u8 passive = 0;

	switch (rtw_rd_5g) {
	case RTW_RD_5G_FCC13:
	case RTW_RD_5G_FCC16:
	case RTW_RD_5G_ETSI18:
	case RTW_RD_5G_ETSI19:
	case RTW_RD_5G_WORLD:
	case RTW_RD_5G_WORLD1:
	case RTW_RD_5G_MKK6:
	case RTW_RD_5G_MKK7:
		passive = 1;
	};

	return passive;
}

inline static u8 rtw_rd_5g_band4_passive(u8 rtw_rd_5g)
{
	u8 passive = 0;

	switch (rtw_rd_5g) {
	case RTW_RD_5G_MKK5:
	case RTW_RD_5G_ETSI16:
	case RTW_RD_5G_ETSI18:
	case RTW_RD_5G_ETSI19:
	case RTW_RD_5G_WORLD:
	case RTW_RD_5G_MKK8:
		passive = 1;
	};

	return passive;
}

u8 init_channel_set(_adapter *padapter, u8 ChannelPlan, RT_CHANNEL_INFO *channel_set)
{
	struct registry_priv *regsty = adapter_to_regsty(padapter);
	u8	index, chanset_size = 0;
	u8	b5GBand = _FALSE, b2_4GBand = _FALSE;
	u8	rd_2g = 0, rd_5g = 0;
#ifdef CONFIG_DFS_MASTER
	int i;
#endif

	if (!rtw_is_channel_plan_valid(ChannelPlan)) {
		RTW_ERR("ChannelPlan ID 0x%02X error !!!!!\n", ChannelPlan);
		return chanset_size;
	}

	_rtw_memset(channel_set, 0, sizeof(RT_CHANNEL_INFO) * MAX_CHANNEL_NUM);

	if (IsSupported24G(regsty->wireless_mode) && hal_chk_band_cap(padapter, BAND_CAP_2G))
		b2_4GBand = _TRUE;

	if (is_supported_5g(regsty->wireless_mode) && hal_chk_band_cap(padapter, BAND_CAP_5G))
		b5GBand = _TRUE;

	if (b2_4GBand == _FALSE && b5GBand == _FALSE) {
		RTW_WARN("HW band_cap has no intersection with SW wireless_mode setting\n");
		return chanset_size;
	}

	if (b2_4GBand) {
		if (ChannelPlan == RTW_CHPLAN_REALTEK_DEFINE)
			rd_2g = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.rd_2g;
		else
			rd_2g = RTW_ChannelPlanMap[ChannelPlan].rd_2g;

		for (index = 0; index < CH_LIST_LEN(RTW_ChannelPlan2G[rd_2g]); index++) {
			if (rtw_regsty_is_excl_chs(regsty, CH_LIST_CH(RTW_ChannelPlan2G[rd_2g], index)) == _TRUE)
				continue;

			if (chanset_size >= MAX_CHANNEL_NUM) {
				RTW_WARN("chset size can't exceed MAX_CHANNEL_NUM(%u)\n", MAX_CHANNEL_NUM);
				break;
			}

			channel_set[chanset_size].ChannelNum = CH_LIST_CH(RTW_ChannelPlan2G[rd_2g], index);

			if (ChannelPlan == RTW_CHPLAN_GLOBAL_DOAMIN
				|| rd_2g == RTW_RD_2G_GLOBAL
			) {
				/* Channel 1~11 is active, and 12~14 is passive */
				if (channel_set[chanset_size].ChannelNum >= 1 && channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else if ((channel_set[chanset_size].ChannelNum  >= 12 && channel_set[chanset_size].ChannelNum  <= 14))
					channel_set[chanset_size].ScanType  = SCAN_PASSIVE;
			} else if (ChannelPlan == RTW_CHPLAN_WORLD_WIDE_13
				|| ChannelPlan == RTW_CHPLAN_WORLD_WIDE_5G
				|| rd_2g == RTW_RD_2G_WORLD
			) {
				/* channel 12~13, passive scan */
				if (channel_set[chanset_size].ChannelNum <= 11)
					channel_set[chanset_size].ScanType = SCAN_ACTIVE;
				else
					channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			} else
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;

			chanset_size++;
		}
	}

#ifdef CONFIG_IEEE80211_BAND_5GHZ
	if (b5GBand) {
		if (ChannelPlan == RTW_CHPLAN_REALTEK_DEFINE)
			rd_5g = RTW_CHANNEL_PLAN_MAP_REALTEK_DEFINE.rd_5g;
		else
			rd_5g = RTW_ChannelPlanMap[ChannelPlan].rd_5g;

		for (index = 0; index < CH_LIST_LEN(RTW_ChannelPlan5G[rd_5g]); index++) {
			if (rtw_regsty_is_excl_chs(regsty, CH_LIST_CH(RTW_ChannelPlan5G[rd_5g], index)) == _TRUE)
				continue;
			#ifndef CONFIG_DFS
			if (rtw_is_dfs_ch(CH_LIST_CH(RTW_ChannelPlan5G[rd_5g], index)))
				continue;
			#endif

			if (chanset_size >= MAX_CHANNEL_NUM) {
				RTW_WARN("chset size can't exceed MAX_CHANNEL_NUM(%u)\n", MAX_CHANNEL_NUM);
				break;
			}

			channel_set[chanset_size].ChannelNum = CH_LIST_CH(RTW_ChannelPlan5G[rd_5g], index);

			if ((ChannelPlan == RTW_CHPLAN_WORLD_WIDE_5G) /* all channels passive */
				|| (rtw_is_5g_band1(channel_set[chanset_size].ChannelNum)
					&& rtw_rd_5g_band1_passive(rd_5g)) /* band1 passive */
				|| (rtw_is_5g_band4(channel_set[chanset_size].ChannelNum)
					&& rtw_rd_5g_band4_passive(rd_5g)) /* band4 passive */
				|| (rtw_is_dfs_ch(channel_set[chanset_size].ChannelNum)) /* DFS channel(band2, 3) passive */
			)
				channel_set[chanset_size].ScanType = SCAN_PASSIVE;
			else
				channel_set[chanset_size].ScanType = SCAN_ACTIVE;

			chanset_size++;
		}
	}

	#ifdef CONFIG_DFS_MASTER
	for (i = 0; i < chanset_size; i++)
		channel_set[i].non_ocp_end_time = rtw_get_current_time();
	#endif
#endif /* CONFIG_IEEE80211_BAND_5GHZ */

	if (chanset_size)
		RTW_INFO(FUNC_ADPT_FMT" ChannelPlan ID:0x%02x, ch num:%d\n"
			, FUNC_ADPT_ARG(padapter), ChannelPlan, chanset_size);
	else
		RTW_WARN(FUNC_ADPT_FMT" ChannelPlan ID:0x%02x, final chset has no channel\n"
			, FUNC_ADPT_ARG(padapter), ChannelPlan);

	return chanset_size;
}

#ifdef CONFIG_80211AC_VHT
#define COUNTRY_CHPLAN_ASSIGN_EN_11AC(_val) , .en_11ac = (_val)
#else
#define COUNTRY_CHPLAN_ASSIGN_EN_11AC(_val)
#endif

#if RTW_DEF_MODULE_REGULATORY_CERT
#define COUNTRY_CHPLAN_ASSIGN_DEF_MODULE_FLAGS(_val) , .def_module_flags = (_val)
#else
#define COUNTRY_CHPLAN_ASSIGN_DEF_MODULE_FLAGS(_val)
#endif

/* has def_module_flags specified, used by common map and HAL dfference map */
#define COUNTRY_CHPLAN_ENT(_alpha2, _chplan, _en_11ac, _def_module_flags) \
	{.alpha2 = (_alpha2), .chplan = (_chplan) \
		COUNTRY_CHPLAN_ASSIGN_EN_11AC(_en_11ac) \
		COUNTRY_CHPLAN_ASSIGN_DEF_MODULE_FLAGS(_def_module_flags) \
	}

#ifdef CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP

#include "../platform/custom_country_chplan.h"

#elif RTW_DEF_MODULE_REGULATORY_CERT

/* leave def_module_flags empty, def_module_flags check is done on country_chplan_map */
#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8821AE_HMC_M2) /* 2013 certify */
static const struct country_chplan RTL8821AE_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x34, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("CL", 0x30, 1, 0), /* Chile */
	COUNTRY_CHPLAN_ENT("CN", 0x51, 1, 0), /* China */
	COUNTRY_CHPLAN_ENT("CO", 0x34, 1, 0), /* Colombia */
	COUNTRY_CHPLAN_ENT("CR", 0x34, 1, 0), /* Costa Rica */
	COUNTRY_CHPLAN_ENT("DO", 0x34, 1, 0), /* Dominican Republic */
	COUNTRY_CHPLAN_ENT("EC", 0x34, 1, 0), /* Ecuador */
	COUNTRY_CHPLAN_ENT("GT", 0x34, 1, 0), /* Guatemala */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0), /* Mexico */
	COUNTRY_CHPLAN_ENT("MY", 0x47, 1, 0), /* Malaysia */
	COUNTRY_CHPLAN_ENT("NI", 0x34, 1, 0), /* Nicaragua */
	COUNTRY_CHPLAN_ENT("PA", 0x34, 1, 0), /* Panama */
	COUNTRY_CHPLAN_ENT("PE", 0x34, 1, 0), /* Peru */
	COUNTRY_CHPLAN_ENT("PR", 0x34, 1, 0), /* Puerto Rico */
	COUNTRY_CHPLAN_ENT("PY", 0x34, 1, 0), /* Paraguay */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 0, 0), /* Ukraine */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8821AU) /* 2014 certify */
static const struct country_chplan RTL8821AU_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x34, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("RU", 0x59, 0, 0), /* Russia(fac/gost), Kaliningrad */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 0, 0), /* Ukraine */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8812AENF_NGFF) /* 2014 certify */
static const struct country_chplan RTL8812AENF_NGFF_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8812AEBT_HMC) /* 2013 certify */
static const struct country_chplan RTL8812AEBT_HMC_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x34, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("RU", 0x59, 0, 0), /* Russia(fac/gost), Kaliningrad */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 0, 0), /* Ukraine */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8188EE_HMC_M2) /* 2012 certify */
static const struct country_chplan RTL8188EE_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("AW", 0x34, 1, 0), /* Aruba */
	COUNTRY_CHPLAN_ENT("BB", 0x34, 1, 0), /* Barbados */
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("CO", 0x34, 1, 0), /* Colombia */
	COUNTRY_CHPLAN_ENT("CR", 0x34, 1, 0), /* Costa Rica */
	COUNTRY_CHPLAN_ENT("DO", 0x34, 1, 0), /* Dominican Republic */
	COUNTRY_CHPLAN_ENT("EC", 0x34, 1, 0), /* Ecuador */
	COUNTRY_CHPLAN_ENT("GT", 0x34, 1, 0), /* Guatemala */
	COUNTRY_CHPLAN_ENT("HT", 0x34, 1, 0), /* Haiti */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0), /* Mexico */
	COUNTRY_CHPLAN_ENT("NI", 0x34, 1, 0), /* Nicaragua */
	COUNTRY_CHPLAN_ENT("PA", 0x34, 1, 0), /* Panama */
	COUNTRY_CHPLAN_ENT("PE", 0x34, 1, 0), /* Peru */
	COUNTRY_CHPLAN_ENT("PR", 0x34, 1, 0), /* Puerto Rico */
	COUNTRY_CHPLAN_ENT("PY", 0x34, 1, 0), /* Paraguay */
	COUNTRY_CHPLAN_ENT("SC", 0x34, 1, 0), /* Seychelles */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
	COUNTRY_CHPLAN_ENT("VC", 0x34, 1, 0), /* Saint Vincent and the Grenadines */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8723BE_HMC_M2) /* 2013 certify */
static const struct country_chplan RTL8723BE_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("AW", 0x34, 1, 0), /* Aruba */
	COUNTRY_CHPLAN_ENT("BS", 0x34, 1, 0), /* Bahamas */
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("CO", 0x34, 1, 0), /* Colombia */
	COUNTRY_CHPLAN_ENT("CR", 0x34, 1, 0), /* Costa Rica */
	COUNTRY_CHPLAN_ENT("DO", 0x34, 1, 0), /* Dominican Republic */
	COUNTRY_CHPLAN_ENT("EC", 0x34, 1, 0), /* Ecuador */
	COUNTRY_CHPLAN_ENT("GT", 0x34, 1, 0), /* Guatemala */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0), /* Mexico */
	COUNTRY_CHPLAN_ENT("NI", 0x34, 1, 0), /* Nicaragua */
	COUNTRY_CHPLAN_ENT("PA", 0x34, 1, 0), /* Panama */
	COUNTRY_CHPLAN_ENT("PE", 0x34, 1, 0), /* Peru */
	COUNTRY_CHPLAN_ENT("PR", 0x34, 1, 0), /* Puerto Rico */
	COUNTRY_CHPLAN_ENT("PY", 0x34, 1, 0), /* Paraguay */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8723BS_NGFF1216) /* 2014 certify */
static const struct country_chplan RTL8723BS_NGFF1216_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("BB", 0x34, 1, 0), /* Barbados */
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("CO", 0x34, 1, 0), /* Colombia */
	COUNTRY_CHPLAN_ENT("CR", 0x34, 1, 0), /* Costa Rica */
	COUNTRY_CHPLAN_ENT("DO", 0x34, 1, 0), /* Dominican Republic */
	COUNTRY_CHPLAN_ENT("EC", 0x34, 1, 0), /* Ecuador */
	COUNTRY_CHPLAN_ENT("GT", 0x34, 1, 0), /* Guatemala */
	COUNTRY_CHPLAN_ENT("HT", 0x34, 1, 0), /* Haiti */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0), /* Mexico */
	COUNTRY_CHPLAN_ENT("NI", 0x34, 1, 0), /* Nicaragua */
	COUNTRY_CHPLAN_ENT("PA", 0x34, 1, 0), /* Panama */
	COUNTRY_CHPLAN_ENT("PE", 0x34, 1, 0), /* Peru */
	COUNTRY_CHPLAN_ENT("PR", 0x34, 1, 0), /* Puerto Rico */
	COUNTRY_CHPLAN_ENT("PY", 0x34, 1, 0), /* Paraguay */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8192EEBT_HMC_M2) /* 2013 certify */
static const struct country_chplan RTL8192EEBT_HMC_M2_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("AW", 0x34, 1, 0), /* Aruba */
	COUNTRY_CHPLAN_ENT("CA", 0x20, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("CO", 0x34, 1, 0), /* Colombia */
	COUNTRY_CHPLAN_ENT("CR", 0x34, 1, 0), /* Costa Rica */
	COUNTRY_CHPLAN_ENT("DO", 0x34, 1, 0), /* Dominican Republic */
	COUNTRY_CHPLAN_ENT("EC", 0x34, 1, 0), /* Ecuador */
	COUNTRY_CHPLAN_ENT("GT", 0x34, 1, 0), /* Guatemala */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0), /* Mexico */
	COUNTRY_CHPLAN_ENT("NI", 0x34, 1, 0), /* Nicaragua */
	COUNTRY_CHPLAN_ENT("PA", 0x34, 1, 0), /* Panama */
	COUNTRY_CHPLAN_ENT("PE", 0x34, 1, 0), /* Peru */
	COUNTRY_CHPLAN_ENT("PR", 0x34, 1, 0), /* Puerto Rico */
	COUNTRY_CHPLAN_ENT("PY", 0x34, 1, 0), /* Paraguay */
	COUNTRY_CHPLAN_ENT("SC", 0x34, 1, 0), /* Seychelles */
	COUNTRY_CHPLAN_ENT("ST", 0x34, 1, 0), /* Sao Tome and Principe */
	COUNTRY_CHPLAN_ENT("TW", 0x39, 1, 0), /* Taiwan */
	COUNTRY_CHPLAN_ENT("US", 0x34, 1, 0), /* United States of America (USA) */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8723DE_NGFF1630) /* 2016 certify */
static const struct country_chplan RTL8723DE_NGFF1630_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("CA", 0x2A, 1, 0), /* Canada */
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
	COUNTRY_CHPLAN_ENT("MX", 0x34, 1, 0), /* Mexico */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8822BE) /* 2016 certify */
static const struct country_chplan RTL8822BE_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
};
#endif

#if (RTW_DEF_MODULE_REGULATORY_CERT & RTW_MODULE_RTL8821CE) /* 2016 certify */
static const struct country_chplan RTL8821CE_country_chplan_exc_map[] = {
	COUNTRY_CHPLAN_ENT("KR", 0x28, 1, 0), /* South Korea */
};
#endif

/**
 * rtw_def_module_get_chplan_from_country -
 * @country_code: string of country code
 * @return:
 * Return NULL for case referring to common map
 */
static const struct country_chplan *rtw_def_module_get_chplan_from_country(const char *country_code)
{
	const struct country_chplan *ent = NULL;
	const struct country_chplan *hal_map = NULL;
	u16 hal_map_sz = 0;
	int i;

	/* TODO: runtime selection for multi driver */
#if (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8821AE_HMC_M2)
	hal_map = RTL8821AE_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8821AE_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8821AU)
	hal_map = RTL8821AU_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8821AU_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8812AENF_NGFF)
	hal_map = RTL8812AENF_NGFF_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8812AENF_NGFF_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8812AEBT_HMC)
	hal_map = RTL8812AEBT_HMC_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8812AEBT_HMC_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8188EE_HMC_M2)
	hal_map = RTL8188EE_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8188EE_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8723BE_HMC_M2)
	hal_map = RTL8723BE_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8723BE_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8723BS_NGFF1216)
	hal_map = RTL8723BS_NGFF1216_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8723BS_NGFF1216_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8192EEBT_HMC_M2)
	hal_map = RTL8192EEBT_HMC_M2_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8192EEBT_HMC_M2_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8723DE_NGFF1630)
	hal_map = RTL8723DE_NGFF1630_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8723DE_NGFF1630_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8822BE)
	hal_map = RTL8822BE_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8822BE_country_chplan_exc_map) / sizeof(struct country_chplan);
#elif (RTW_DEF_MODULE_REGULATORY_CERT == RTW_MODULE_RTL8821CE)
	hal_map = RTL8821CE_country_chplan_exc_map;
	hal_map_sz = sizeof(RTL8821CE_country_chplan_exc_map) / sizeof(struct country_chplan);
#endif

	if (hal_map == NULL || hal_map_sz == 0)
		goto exit;

	for (i = 0; i < hal_map_sz; i++) {
		if (strncmp(country_code, hal_map[i].alpha2, 2) == 0) {
			ent = &hal_map[i];
			break;
		}
	}

exit:
	return ent;
}
#endif /* CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP or RTW_DEF_MODULE_REGULATORY_CERT */

static const struct country_chplan country_chplan_map[] = {
	COUNTRY_CHPLAN_ENT("AD", 0x26, 1, 0x000), /* Andorra */
	COUNTRY_CHPLAN_ENT("AE", 0x35, 1, 0x7FB), /* United Arab Emirates */
	COUNTRY_CHPLAN_ENT("AF", 0x42, 1, 0x000), /* Afghanistan */
	COUNTRY_CHPLAN_ENT("AG", 0x76, 1, 0x000), /* Antigua & Barbuda */
	COUNTRY_CHPLAN_ENT("AI", 0x26, 1, 0x000), /* Anguilla(UK) */
	COUNTRY_CHPLAN_ENT("AL", 0x26, 1, 0x7F1), /* Albania */
	COUNTRY_CHPLAN_ENT("AM", 0x26, 1, 0x6B0), /* Armenia */
	COUNTRY_CHPLAN_ENT("AN", 0x76, 1, 0x7F1), /* Netherlands Antilles */
	COUNTRY_CHPLAN_ENT("AO", 0x47, 1, 0x6E0), /* Angola */
	COUNTRY_CHPLAN_ENT("AQ", 0x26, 1, 0x000), /* Antarctica */
	COUNTRY_CHPLAN_ENT("AR", 0x61, 1, 0x7F3), /* Argentina */
	COUNTRY_CHPLAN_ENT("AS", 0x76, 1, 0x000), /* American Samoa */
	COUNTRY_CHPLAN_ENT("AT", 0x26, 1, 0x7FB), /* Austria */
	COUNTRY_CHPLAN_ENT("AU", 0x45, 1, 0x7FB), /* Australia */
	COUNTRY_CHPLAN_ENT("AW", 0x76, 1, 0x0B0), /* Aruba */
	COUNTRY_CHPLAN_ENT("AZ", 0x26, 1, 0x7F1), /* Azerbaijan */
	COUNTRY_CHPLAN_ENT("BA", 0x26, 1, 0x7F1), /* Bosnia & Herzegovina */
	COUNTRY_CHPLAN_ENT("BB", 0x76, 1, 0x650), /* Barbados */
	COUNTRY_CHPLAN_ENT("BD", 0x26, 1, 0x7F1), /* Bangladesh */
	COUNTRY_CHPLAN_ENT("BE", 0x26, 1, 0x7FB), /* Belgium */
	COUNTRY_CHPLAN_ENT("BF", 0x26, 1, 0x6B0), /* Burkina Faso */
	COUNTRY_CHPLAN_ENT("BG", 0x26, 1, 0x7F1), /* Bulgaria */
	COUNTRY_CHPLAN_ENT("BH", 0x48, 1, 0x7F1), /* Bahrain */
	COUNTRY_CHPLAN_ENT("BI", 0x26, 1, 0x6B0), /* Burundi */
	COUNTRY_CHPLAN_ENT("BJ", 0x26, 1, 0x6B0), /* Benin */
	COUNTRY_CHPLAN_ENT("BM", 0x76, 1, 0x600), /* Bermuda (UK) */
	COUNTRY_CHPLAN_ENT("BN", 0x47, 1, 0x610), /* Brunei */
	COUNTRY_CHPLAN_ENT("BO", 0x73, 1, 0x7F1), /* Bolivia */
	COUNTRY_CHPLAN_ENT("BR", 0x62, 1, 0x7F1), /* Brazil */
	COUNTRY_CHPLAN_ENT("BS", 0x76, 1, 0x620), /* Bahamas */
	COUNTRY_CHPLAN_ENT("BT", 0x26, 1, 0x000), /* Bhutan */
	COUNTRY_CHPLAN_ENT("BV", 0x26, 1, 0x000), /* Bouvet Island (Norway) */
	COUNTRY_CHPLAN_ENT("BW", 0x35, 1, 0x6F1), /* Botswana */
	COUNTRY_CHPLAN_ENT("BY", 0x26, 1, 0x7F1), /* Belarus */
	COUNTRY_CHPLAN_ENT("BZ", 0x76, 1, 0x000), /* Belize */
	COUNTRY_CHPLAN_ENT("CA", 0x2B, 1, 0x7FB), /* Canada */
	COUNTRY_CHPLAN_ENT("CC", 0x26, 1, 0x000), /* Cocos (Keeling) Islands (Australia) */
	COUNTRY_CHPLAN_ENT("CD", 0x26, 1, 0x6B0), /* Congo, Republic of the */
	COUNTRY_CHPLAN_ENT("CF", 0x26, 1, 0x6B0), /* Central African Republic */
	COUNTRY_CHPLAN_ENT("CG", 0x26, 1, 0x6B0), /* Congo, Democratic Republic of the. Zaire */
	COUNTRY_CHPLAN_ENT("CH", 0x26, 1, 0x7FB), /* Switzerland */
	COUNTRY_CHPLAN_ENT("CI", 0x42, 1, 0x7F1), /* Cote d'Ivoire */
	COUNTRY_CHPLAN_ENT("CK", 0x26, 1, 0x000), /* Cook Islands */
	COUNTRY_CHPLAN_ENT("CL", 0x2D, 1, 0x7F1), /* Chile */
	COUNTRY_CHPLAN_ENT("CM", 0x26, 1, 0x6B0), /* Cameroon */
	COUNTRY_CHPLAN_ENT("CN", 0x48, 1, 0x7FB), /* China */
	COUNTRY_CHPLAN_ENT("CO", 0x76, 1, 0x7F1), /* Colombia */
	COUNTRY_CHPLAN_ENT("CR", 0x76, 1, 0x7F1), /* Costa Rica */
	COUNTRY_CHPLAN_ENT("CV", 0x26, 1, 0x6B0), /* Cape Verde */
	COUNTRY_CHPLAN_ENT("CX", 0x45, 1, 0x000), /* Christmas Island (Australia) */
	COUNTRY_CHPLAN_ENT("CY", 0x26, 1, 0x7FB), /* Cyprus */
	COUNTRY_CHPLAN_ENT("CZ", 0x26, 1, 0x7FB), /* Czech Republic */
	COUNTRY_CHPLAN_ENT("DE", 0x26, 1, 0x7FB), /* Germany */
	COUNTRY_CHPLAN_ENT("DJ", 0x26, 1, 0x680), /* Djibouti */
	COUNTRY_CHPLAN_ENT("DK", 0x26, 1, 0x7FB), /* Denmark */
	COUNTRY_CHPLAN_ENT("DM", 0x76, 1, 0x000), /* Dominica */
	COUNTRY_CHPLAN_ENT("DO", 0x76, 1, 0x7F1), /* Dominican Republic */
	COUNTRY_CHPLAN_ENT("DZ", 0x26, 1, 0x7F1), /* Algeria */
	COUNTRY_CHPLAN_ENT("EC", 0x76, 1, 0x7F1), /* Ecuador */
	COUNTRY_CHPLAN_ENT("EE", 0x26, 1, 0x7FB), /* Estonia */
	COUNTRY_CHPLAN_ENT("EG", 0x47, 1, 0x7F1), /* Egypt */
	COUNTRY_CHPLAN_ENT("EH", 0x47, 1, 0x680), /* Western Sahara */
	COUNTRY_CHPLAN_ENT("ER", 0x26, 1, 0x000), /* Eritrea */
	COUNTRY_CHPLAN_ENT("ES", 0x26, 1, 0x7FB), /* Spain, Canary Islands, Ceuta, Melilla */
	COUNTRY_CHPLAN_ENT("ET", 0x26, 1, 0x4B0), /* Ethiopia */
	COUNTRY_CHPLAN_ENT("FI", 0x26, 1, 0x7FB), /* Finland */
	COUNTRY_CHPLAN_ENT("FJ", 0x76, 1, 0x600), /* Fiji */
	COUNTRY_CHPLAN_ENT("FK", 0x26, 1, 0x000), /* Falkland Islands (Islas Malvinas) (UK) */
	COUNTRY_CHPLAN_ENT("FM", 0x76, 1, 0x000), /* Micronesia, Federated States of (USA) */
	COUNTRY_CHPLAN_ENT("FO", 0x26, 1, 0x000), /* Faroe Islands (Denmark) */
	COUNTRY_CHPLAN_ENT("FR", 0x26, 1, 0x7FB), /* France */
	COUNTRY_CHPLAN_ENT("GA", 0x26, 1, 0x6B0), /* Gabon */
	COUNTRY_CHPLAN_ENT("GB", 0x26, 1, 0x7FB), /* Great Britain (United Kingdom; England) */
	COUNTRY_CHPLAN_ENT("GD", 0x76, 1, 0x0B0), /* Grenada */
	COUNTRY_CHPLAN_ENT("GE", 0x26, 1, 0x600), /* Georgia */
	COUNTRY_CHPLAN_ENT("GF", 0x26, 1, 0x080), /* French Guiana */
	COUNTRY_CHPLAN_ENT("GG", 0x26, 1, 0x000), /* Guernsey (UK) */
	COUNTRY_CHPLAN_ENT("GH", 0x26, 1, 0x7F1), /* Ghana */
	COUNTRY_CHPLAN_ENT("GI", 0x26, 1, 0x600), /* Gibraltar (UK) */
	COUNTRY_CHPLAN_ENT("GL", 0x26, 1, 0x600), /* Greenland (Denmark) */
	COUNTRY_CHPLAN_ENT("GM", 0x26, 1, 0x6B0), /* Gambia */
	COUNTRY_CHPLAN_ENT("GN", 0x26, 1, 0x610), /* Guinea */
	COUNTRY_CHPLAN_ENT("GP", 0x26, 1, 0x600), /* Guadeloupe (France) */
	COUNTRY_CHPLAN_ENT("GQ", 0x26, 1, 0x6B0), /* Equatorial Guinea */
	COUNTRY_CHPLAN_ENT("GR", 0x26, 1, 0x7FB), /* Greece */
	COUNTRY_CHPLAN_ENT("GS", 0x26, 1, 0x000), /* South Georgia and the Sandwich Islands (UK) */
	COUNTRY_CHPLAN_ENT("GT", 0x61, 1, 0x7F1), /* Guatemala */
	COUNTRY_CHPLAN_ENT("GU", 0x76, 1, 0x600), /* Guam (USA) */
	COUNTRY_CHPLAN_ENT("GW", 0x26, 1, 0x6B0), /* Guinea-Bissau */
	COUNTRY_CHPLAN_ENT("GY", 0x44, 1, 0x000), /* Guyana */
	COUNTRY_CHPLAN_ENT("HK", 0x35, 1, 0x7FB), /* Hong Kong */
	COUNTRY_CHPLAN_ENT("HM", 0x45, 1, 0x000), /* Heard and McDonald Islands (Australia) */
	COUNTRY_CHPLAN_ENT("HN", 0x32, 1, 0x7F1), /* Honduras */
	COUNTRY_CHPLAN_ENT("HR", 0x26, 1, 0x7F9), /* Croatia */
	COUNTRY_CHPLAN_ENT("HT", 0x76, 1, 0x650), /* Haiti */
	COUNTRY_CHPLAN_ENT("HU", 0x26, 1, 0x7FB), /* Hungary */
	COUNTRY_CHPLAN_ENT("ID", 0x3D, 0, 0x7F3), /* Indonesia */
	COUNTRY_CHPLAN_ENT("IE", 0x26, 1, 0x7FB), /* Ireland */
	COUNTRY_CHPLAN_ENT("IL", 0x47, 1, 0x7F1), /* Israel */
	COUNTRY_CHPLAN_ENT("IM", 0x26, 1, 0x000), /* Isle of Man (UK) */
	COUNTRY_CHPLAN_ENT("IN", 0x48, 1, 0x7F1), /* India */
	COUNTRY_CHPLAN_ENT("IO", 0x26, 1, 0x000), /* British Indian Ocean Territory (UK) */
	COUNTRY_CHPLAN_ENT("IQ", 0x26, 1, 0x000), /* Iraq */
	COUNTRY_CHPLAN_ENT("IR", 0x26, 0, 0x000), /* Iran */
	COUNTRY_CHPLAN_ENT("IS", 0x26, 1, 0x7FB), /* Iceland */
	COUNTRY_CHPLAN_ENT("IT", 0x26, 1, 0x7FB), /* Italy */
	COUNTRY_CHPLAN_ENT("JE", 0x26, 1, 0x000), /* Jersey (UK) */
	COUNTRY_CHPLAN_ENT("JM", 0x32, 1, 0x7F1), /* Jamaica */
	COUNTRY_CHPLAN_ENT("JO", 0x49, 1, 0x7FB), /* Jordan */
	COUNTRY_CHPLAN_ENT("JP", 0x27, 1, 0x7FF), /* Japan- Telec */
	COUNTRY_CHPLAN_ENT("KE", 0x47, 1, 0x7F9), /* Kenya */
	COUNTRY_CHPLAN_ENT("KG", 0x26, 1, 0x7F1), /* Kyrgyzstan */
	COUNTRY_CHPLAN_ENT("KH", 0x26, 1, 0x7F1), /* Cambodia */
	COUNTRY_CHPLAN_ENT("KI", 0x26, 1, 0x000), /* Kiribati */
	COUNTRY_CHPLAN_ENT("KM", 0x26, 1, 0x000), /* Comoros */
	COUNTRY_CHPLAN_ENT("KN", 0x76, 1, 0x000), /* Saint Kitts and Nevis */
	COUNTRY_CHPLAN_ENT("KR", 0x4B, 1, 0x7FB), /* South Korea */
	COUNTRY_CHPLAN_ENT("KW", 0x47, 1, 0x7FB), /* Kuwait */
	COUNTRY_CHPLAN_ENT("KY", 0x76, 1, 0x000), /* Cayman Islands (UK) */
	COUNTRY_CHPLAN_ENT("KZ", 0x26, 1, 0x700), /* Kazakhstan */
	COUNTRY_CHPLAN_ENT("LA", 0x26, 1, 0x000), /* Laos */
	COUNTRY_CHPLAN_ENT("LB", 0x26, 1, 0x7F1), /* Lebanon */
	COUNTRY_CHPLAN_ENT("LC", 0x76, 1, 0x000), /* Saint Lucia */
	COUNTRY_CHPLAN_ENT("LI", 0x26, 1, 0x7FB), /* Liechtenstein */
	COUNTRY_CHPLAN_ENT("LK", 0x26, 1, 0x7F1), /* Sri Lanka */
	COUNTRY_CHPLAN_ENT("LR", 0x26, 1, 0x6B0), /* Liberia */
	COUNTRY_CHPLAN_ENT("LS", 0x26, 1, 0x7F1), /* Lesotho */
	COUNTRY_CHPLAN_ENT("LT", 0x26, 1, 0x7FB), /* Lithuania */
	COUNTRY_CHPLAN_ENT("LU", 0x26, 1, 0x7FB), /* Luxembourg */
	COUNTRY_CHPLAN_ENT("LV", 0x26, 1, 0x7FB), /* Latvia */
	COUNTRY_CHPLAN_ENT("LY", 0x26, 1, 0x000), /* Libya */
	COUNTRY_CHPLAN_ENT("MA", 0x47, 1, 0x7F1), /* Morocco */
	COUNTRY_CHPLAN_ENT("MC", 0x26, 1, 0x7FB), /* Monaco */
	COUNTRY_CHPLAN_ENT("MD", 0x26, 1, 0x7F1), /* Moldova */
	COUNTRY_CHPLAN_ENT("ME", 0x26, 1, 0x7F1), /* Montenegro */
	COUNTRY_CHPLAN_ENT("MF", 0x76, 1, 0x000), /* Saint Martin */
	COUNTRY_CHPLAN_ENT("MG", 0x26, 1, 0x620), /* Madagascar */
	COUNTRY_CHPLAN_ENT("MH", 0x76, 1, 0x000), /* Marshall Islands (USA) */
	COUNTRY_CHPLAN_ENT("MK", 0x26, 1, 0x7F1), /* Republic of Macedonia (FYROM) */
	COUNTRY_CHPLAN_ENT("ML", 0x26, 1, 0x6B0), /* Mali */
	COUNTRY_CHPLAN_ENT("MM", 0x26, 1, 0x000), /* Burma (Myanmar) */
	COUNTRY_CHPLAN_ENT("MN", 0x26, 1, 0x000), /* Mongolia */
	COUNTRY_CHPLAN_ENT("MO", 0x35, 1, 0x600), /* Macau */
	COUNTRY_CHPLAN_ENT("MP", 0x76, 1, 0x000), /* Northern Mariana Islands (USA) */
	COUNTRY_CHPLAN_ENT("MQ", 0x26, 1, 0x640), /* Martinique (France) */
	COUNTRY_CHPLAN_ENT("MR", 0x26, 1, 0x6A0), /* Mauritania */
	COUNTRY_CHPLAN_ENT("MS", 0x26, 1, 0x000), /* Montserrat (UK) */
	COUNTRY_CHPLAN_ENT("MT", 0x26, 1, 0x7FB), /* Malta */
	COUNTRY_CHPLAN_ENT("MU", 0x26, 1, 0x6B0), /* Mauritius */
	COUNTRY_CHPLAN_ENT("MV", 0x47, 1, 0x000), /* Maldives */
	COUNTRY_CHPLAN_ENT("MW", 0x26, 1, 0x6B0), /* Malawi */
	COUNTRY_CHPLAN_ENT("MX", 0x61, 1, 0x7F1), /* Mexico */
	COUNTRY_CHPLAN_ENT("MY", 0x63, 1, 0x7F1), /* Malaysia */
	COUNTRY_CHPLAN_ENT("MZ", 0x26, 1, 0x7F1), /* Mozambique */
	COUNTRY_CHPLAN_ENT("NA", 0x26, 1, 0x700), /* Namibia */
	COUNTRY_CHPLAN_ENT("NC", 0x26, 1, 0x000), /* New Caledonia */
	COUNTRY_CHPLAN_ENT("NE", 0x26, 1, 0x6B0), /* Niger */
	COUNTRY_CHPLAN_ENT("NF", 0x45, 1, 0x000), /* Norfolk Island (Australia) */
	COUNTRY_CHPLAN_ENT("NG", 0x75, 1, 0x7F9), /* Nigeria */
	COUNTRY_CHPLAN_ENT("NI", 0x76, 1, 0x7F1), /* Nicaragua */
	COUNTRY_CHPLAN_ENT("NL", 0x26, 1, 0x7FB), /* Netherlands */
	COUNTRY_CHPLAN_ENT("NO", 0x26, 1, 0x7FB), /* Norway */
	COUNTRY_CHPLAN_ENT("NP", 0x48, 1, 0x6F0), /* Nepal */
	COUNTRY_CHPLAN_ENT("NR", 0x26, 1, 0x000), /* Nauru */
	COUNTRY_CHPLAN_ENT("NU", 0x45, 1, 0x000), /* Niue */
	COUNTRY_CHPLAN_ENT("NZ", 0x45, 1, 0x7FB), /* New Zealand */
	COUNTRY_CHPLAN_ENT("OM", 0x26, 1, 0x7F9), /* Oman */
	COUNTRY_CHPLAN_ENT("PA", 0x76, 1, 0x7F1), /* Panama */
	COUNTRY_CHPLAN_ENT("PE", 0x76, 1, 0x7F1), /* Peru */
	COUNTRY_CHPLAN_ENT("PF", 0x26, 1, 0x000), /* French Polynesia (France) */
	COUNTRY_CHPLAN_ENT("PG", 0x35, 1, 0x7F1), /* Papua New Guinea */
	COUNTRY_CHPLAN_ENT("PH", 0x35, 1, 0x7F1), /* Philippines */
	COUNTRY_CHPLAN_ENT("PK", 0x51, 1, 0x7F1), /* Pakistan */
	COUNTRY_CHPLAN_ENT("PL", 0x26, 1, 0x7FB), /* Poland */
	COUNTRY_CHPLAN_ENT("PM", 0x26, 1, 0x000), /* Saint Pierre and Miquelon (France) */
	COUNTRY_CHPLAN_ENT("PR", 0x76, 1, 0x7F1), /* Puerto Rico */
	COUNTRY_CHPLAN_ENT("PT", 0x26, 1, 0x7FB), /* Portugal */
	COUNTRY_CHPLAN_ENT("PW", 0x76, 1, 0x000), /* Palau */
	COUNTRY_CHPLAN_ENT("PY", 0x76, 1, 0x7F1), /* Paraguay */
	COUNTRY_CHPLAN_ENT("QA", 0x35, 1, 0x7F9), /* Qatar */
	COUNTRY_CHPLAN_ENT("RE", 0x26, 1, 0x000), /* Reunion (France) */
	COUNTRY_CHPLAN_ENT("RO", 0x26, 1, 0x7F1), /* Romania */
	COUNTRY_CHPLAN_ENT("RS", 0x26, 1, 0x7F1), /* Serbia, Kosovo */
	COUNTRY_CHPLAN_ENT("RU", 0x59, 1, 0x7FB), /* Russia(fac/gost), Kaliningrad */
	COUNTRY_CHPLAN_ENT("RW", 0x26, 1, 0x0B0), /* Rwanda */
	COUNTRY_CHPLAN_ENT("SA", 0x35, 1, 0x7FB), /* Saudi Arabia */
	COUNTRY_CHPLAN_ENT("SB", 0x26, 1, 0x000), /* Solomon Islands */
	COUNTRY_CHPLAN_ENT("SC", 0x76, 1, 0x690), /* Seychelles */
	COUNTRY_CHPLAN_ENT("SE", 0x26, 1, 0x7FB), /* Sweden */
	COUNTRY_CHPLAN_ENT("SG", 0x35, 1, 0x7FB), /* Singapore */
	COUNTRY_CHPLAN_ENT("SH", 0x26, 1, 0x000), /* Saint Helena (UK) */
	COUNTRY_CHPLAN_ENT("SI", 0x26, 1, 0x7FB), /* Slovenia */
	COUNTRY_CHPLAN_ENT("SJ", 0x26, 1, 0x000), /* Svalbard (Norway) */
	COUNTRY_CHPLAN_ENT("SK", 0x26, 1, 0x7FB), /* Slovakia */
	COUNTRY_CHPLAN_ENT("SL", 0x26, 1, 0x6B0), /* Sierra Leone */
	COUNTRY_CHPLAN_ENT("SM", 0x26, 1, 0x000), /* San Marino */
	COUNTRY_CHPLAN_ENT("SN", 0x26, 1, 0x7F1), /* Senegal */
	COUNTRY_CHPLAN_ENT("SO", 0x26, 1, 0x000), /* Somalia */
	COUNTRY_CHPLAN_ENT("SR", 0x74, 1, 0x000), /* Suriname */
	COUNTRY_CHPLAN_ENT("ST", 0x76, 1, 0x680), /* Sao Tome and Principe */
	COUNTRY_CHPLAN_ENT("SV", 0x30, 1, 0x7F1), /* El Salvador */
	COUNTRY_CHPLAN_ENT("SX", 0x76, 1, 0x000), /* Sint Marteen */
	COUNTRY_CHPLAN_ENT("SZ", 0x26, 1, 0x020), /* Swaziland */
	COUNTRY_CHPLAN_ENT("TC", 0x26, 1, 0x000), /* Turks and Caicos Islands (UK) */
	COUNTRY_CHPLAN_ENT("TD", 0x26, 1, 0x6B0), /* Chad */
	COUNTRY_CHPLAN_ENT("TF", 0x26, 1, 0x680), /* French Southern and Antarctic Lands (FR Southern Territories) */
	COUNTRY_CHPLAN_ENT("TG", 0x26, 1, 0x6B0), /* Togo */
	COUNTRY_CHPLAN_ENT("TH", 0x35, 1, 0x7F1), /* Thailand */
	COUNTRY_CHPLAN_ENT("TJ", 0x26, 1, 0x640), /* Tajikistan */
	COUNTRY_CHPLAN_ENT("TK", 0x45, 1, 0x000), /* Tokelau */
	COUNTRY_CHPLAN_ENT("TM", 0x26, 1, 0x000), /* Turkmenistan */
	COUNTRY_CHPLAN_ENT("TN", 0x47, 1, 0x7F1), /* Tunisia */
	COUNTRY_CHPLAN_ENT("TO", 0x26, 1, 0x000), /* Tonga */
	COUNTRY_CHPLAN_ENT("TR", 0x26, 1, 0x7F1), /* Turkey, Northern Cyprus */
	COUNTRY_CHPLAN_ENT("TT", 0x76, 1, 0x3F1), /* Trinidad & Tobago */
	COUNTRY_CHPLAN_ENT("TV", 0x21, 0, 0x000), /* Tuvalu */
	COUNTRY_CHPLAN_ENT("TW", 0x76, 1, 0x7FF), /* Taiwan */
	COUNTRY_CHPLAN_ENT("TZ", 0x26, 1, 0x6F0), /* Tanzania */
	COUNTRY_CHPLAN_ENT("UA", 0x36, 1, 0x7FB), /* Ukraine */
	COUNTRY_CHPLAN_ENT("UG", 0x26, 1, 0x6F1), /* Uganda */
	COUNTRY_CHPLAN_ENT("US", 0x76, 1, 0x7FF), /* United States of America (USA) */
	COUNTRY_CHPLAN_ENT("UY", 0x30, 1, 0x7F1), /* Uruguay */
	COUNTRY_CHPLAN_ENT("UZ", 0x47, 1, 0x6F0), /* Uzbekistan */
	COUNTRY_CHPLAN_ENT("VA", 0x26, 1, 0x000), /* Holy See (Vatican City) */
	COUNTRY_CHPLAN_ENT("VC", 0x76, 1, 0x010), /* Saint Vincent and the Grenadines */
	COUNTRY_CHPLAN_ENT("VE", 0x30, 1, 0x7F1), /* Venezuela */
	COUNTRY_CHPLAN_ENT("VG", 0x76, 1, 0x000), /* British Virgin Islands (UK) */
	COUNTRY_CHPLAN_ENT("VI", 0x76, 1, 0x000), /* United States Virgin Islands (USA) */
	COUNTRY_CHPLAN_ENT("VN", 0x35, 1, 0x7F1), /* Vietnam */
	COUNTRY_CHPLAN_ENT("VU", 0x26, 1, 0x000), /* Vanuatu */
	COUNTRY_CHPLAN_ENT("WF", 0x26, 1, 0x000), /* Wallis and Futuna (France) */
	COUNTRY_CHPLAN_ENT("WS", 0x76, 1, 0x000), /* Samoa */
	COUNTRY_CHPLAN_ENT("YE", 0x26, 1, 0x040), /* Yemen */
	COUNTRY_CHPLAN_ENT("YT", 0x26, 1, 0x680), /* Mayotte (France) */
	COUNTRY_CHPLAN_ENT("ZA", 0x35, 1, 0x7F1), /* South Africa */
	COUNTRY_CHPLAN_ENT("ZM", 0x26, 1, 0x6B0), /* Zambia */
	COUNTRY_CHPLAN_ENT("ZW", 0x26, 1, 0x7F1), /* Zimbabwe */
};

/*
* rtw_get_chplan_from_country -
* @country_code: string of country code
*
* Return pointer of struct country_chplan entry or NULL when unsupported country_code is given
*/
const struct country_chplan *rtw_get_chplan_from_country(const char *country_code)
{
#if RTW_DEF_MODULE_REGULATORY_CERT
	const struct country_chplan *exc_ent = NULL;
#endif
	const struct country_chplan *ent = NULL;
	const struct country_chplan *map = NULL;
	u16 map_sz = 0;
	char code[2];
	int i;

	code[0] = alpha_to_upper(country_code[0]);
	code[1] = alpha_to_upper(country_code[1]);

#ifdef CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP
	map = CUSTOMIZED_country_chplan_map;
	map_sz = sizeof(CUSTOMIZED_country_chplan_map) / sizeof(struct country_chplan);
#else
	#if RTW_DEF_MODULE_REGULATORY_CERT
	exc_ent = rtw_def_module_get_chplan_from_country(code);
	#endif
	map = country_chplan_map;
	map_sz = sizeof(country_chplan_map) / sizeof(struct country_chplan);
#endif

	for (i = 0; i < map_sz; i++) {
		if (strncmp(code, map[i].alpha2, 2) == 0) {
			ent = &map[i];
			break;
		}
	}

	#if RTW_DEF_MODULE_REGULATORY_CERT
	if (!ent || !(COUNTRY_CHPLAN_DEF_MODULE_FALGS(ent) & RTW_DEF_MODULE_REGULATORY_CERT))
		exc_ent = ent = NULL;
	if (exc_ent)
		ent = exc_ent;
	#endif

	return ent;
}

void dump_country_chplan(void *sel, const struct country_chplan *ent)
{
	RTW_PRINT_SEL(sel, "\"%c%c\", 0x%02X%s\n"
		, ent->alpha2[0], ent->alpha2[1], ent->chplan
		, COUNTRY_CHPLAN_EN_11AC(ent) ? " ac" : ""
	);
}

void dump_country_chplan_map(void *sel)
{
	const struct country_chplan *ent;
	u8 code[2];

#if RTW_DEF_MODULE_REGULATORY_CERT
	RTW_PRINT_SEL(sel, "RTW_DEF_MODULE_REGULATORY_CERT:0x%x\n", RTW_DEF_MODULE_REGULATORY_CERT);
#endif
#ifdef CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP
	RTW_PRINT_SEL(sel, "CONFIG_CUSTOMIZED_COUNTRY_CHPLAN_MAP\n");
#endif

	for (code[0] = 'A'; code[0] <= 'Z'; code[0]++) {
		for (code[1] = 'A'; code[1] <= 'Z'; code[1]++) {
			ent = rtw_get_chplan_from_country(code);
			if (!ent)
				continue;

			dump_country_chplan(sel, ent);
		}
	}
}

void dump_chplan_id_list(void *sel)
{
	u8 first = 1;
	int i;

	for (i = 0; i < RTW_CHPLAN_MAX; i++) {
		if (!rtw_is_channel_plan_valid(i))
			continue;

		if (first) {
			RTW_PRINT_SEL(sel, "0x%02X ", i);
			first = 0;
		} else
			_RTW_PRINT_SEL(sel, "0x%02X ", i);
	}

	_RTW_PRINT_SEL(sel, "0x7F\n");
}

void dump_chplan_test(void *sel)
{
	int i, j;

	/* check invalid channel */
	for (i = 0; i < RTW_RD_2G_MAX; i++) {
		for (j = 0; j < CH_LIST_LEN(RTW_ChannelPlan2G[i]); j++) {
			if (rtw_ch2freq(CH_LIST_CH(RTW_ChannelPlan2G[i], j)) == 0)
				RTW_PRINT_SEL(sel, "invalid ch:%u at (%d,%d)\n", CH_LIST_CH(RTW_ChannelPlan2G[i], j), i, j);
		}
	}

#ifdef CONFIG_IEEE80211_BAND_5GHZ
	for (i = 0; i < RTW_RD_5G_MAX; i++) {
		for (j = 0; j < CH_LIST_LEN(RTW_ChannelPlan5G[i]); j++) {
			if (rtw_ch2freq(CH_LIST_CH(RTW_ChannelPlan5G[i], j)) == 0)
				RTW_PRINT_SEL(sel, "invalid ch:%u at (%d,%d)\n", CH_LIST_CH(RTW_ChannelPlan5G[i], j), i, j);
		}
	}
#endif
}

void dump_chplan_ver(void *sel)
{
	RTW_PRINT_SEL(sel, "%s-%s\n", RTW_DOMAIN_MAP_VER, RTW_COUNTRY_MAP_VER);
}
