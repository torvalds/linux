/*
 * Copyright (c) 2004 Video54 Technologies, Inc.
 * Copyright (c) 2004-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Atheros rate control algorithm
 */

#include "core.h"
#include "../net/mac80211/rate.h"

static u32 tx_triglevel_max;

static struct ath_rate_table ar5416_11na_ratetable = {
	42,
	{
		{ TRUE, TRUE, WLAN_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, 12,
			0, 2, 1, 0, 0, 0, 0, 0 },
		{ TRUE,	TRUE, WLAN_PHY_OFDM, 9000, /* 9 Mb */
			7800,  0x0f, 0x00, 18,
			0, 3, 1, 1, 1, 1, 1, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 12000, /* 12 Mb */
			10000, 0x0a, 0x00, 24,
			2, 4, 2, 2, 2, 2, 2, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 18000, /* 18 Mb */
			13900, 0x0e, 0x00, 36,
			2, 6,  2, 3, 3, 3, 3, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 24000, /* 24 Mb */
			17300, 0x09, 0x00, 48,
			4, 10, 3, 4, 4, 4, 4, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 36000, /* 36 Mb */
			23000, 0x0d, 0x00, 72,
			4, 14, 3, 5, 5, 5, 5, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			4, 20, 3, 6, 6, 6, 6, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 54000, /* 54 Mb */
			29300, 0x0c, 0x00, 108,
			4, 23, 3, 7, 7, 7, 7, 0 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 6500, /* 6.5 Mb */
			6400, 0x80, 0x00, 0,
			0, 2, 3, 8, 24, 8, 24, 3216 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 13000, /* 13 Mb */
			12700, 0x81, 0x00, 1,
			2, 4, 3, 9, 25, 9, 25, 6434 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 19500, /* 19.5 Mb */
			18800, 0x82, 0x00, 2,
			2, 6, 3, 10, 26, 10, 26, 9650 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 26000, /* 26 Mb */
			25000, 0x83, 0x00, 3,
			4, 10, 3, 11, 27, 11, 27, 12868 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 39000, /* 39 Mb */
			36700, 0x84, 0x00, 4,
			4, 14, 3, 12, 28, 12, 28, 19304 },
		{ FALSE, TRUE_20, WLAN_PHY_HT_20_SS, 52000, /* 52 Mb */
			48100, 0x85, 0x00, 5,
			4, 20, 3, 13, 29, 13, 29, 25740 },
		{ FALSE, TRUE_20, WLAN_PHY_HT_20_SS, 58500, /* 58.5 Mb */
			53500, 0x86, 0x00, 6,
			4, 23, 3, 14, 30, 14, 30,  28956 },
		{ FALSE, TRUE_20, WLAN_PHY_HT_20_SS, 65000, /* 65 Mb */
			59000, 0x87, 0x00, 7,
			4, 25, 3, 15, 31, 15, 32, 32180 },
		{ FALSE, FALSE, WLAN_PHY_HT_20_DS, 13000, /* 13 Mb */
			12700, 0x88, 0x00,
			8, 0, 2, 3, 16, 33, 16, 33, 6430 },
		{ FALSE, FALSE, WLAN_PHY_HT_20_DS, 26000, /* 26 Mb */
			24800, 0x89, 0x00, 9,
			2, 4, 3, 17, 34, 17, 34, 12860 },
		{ FALSE, FALSE, WLAN_PHY_HT_20_DS, 39000, /* 39 Mb */
			36600, 0x8a, 0x00, 10,
			2, 6, 3, 18, 35, 18, 35, 19300 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 52000, /* 52 Mb */
			48100, 0x8b, 0x00, 11,
			4, 10, 3, 19, 36, 19, 36, 25736 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 78000, /* 78 Mb */
			69500, 0x8c, 0x00, 12,
			4, 14, 3, 20, 37, 20, 37, 38600 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 104000, /* 104 Mb */
			89500, 0x8d, 0x00, 13,
			4, 20, 3, 21, 38, 21, 38, 51472 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 117000, /* 117 Mb */
			98900, 0x8e, 0x00, 14,
			4, 23, 3, 22, 39, 22, 39, 57890 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 130000, /* 130 Mb */
			108300, 0x8f, 0x00, 15,
			4, 25, 3, 23, 40, 23, 41, 64320 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 13500, /* 13.5 Mb */
			13200, 0x80, 0x00, 0,
			0, 2, 3, 8, 24, 24, 24, 6684 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 27500, /* 27.0 Mb */
			25900, 0x81, 0x00, 1,
			2, 4, 3, 9, 25, 25, 25, 13368 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 40500, /* 40.5 Mb */
			38600, 0x82, 0x00, 2,
			2, 6, 3, 10, 26, 26, 26, 20052 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 54000, /* 54 Mb */
			49800, 0x83, 0x00, 3,
			4, 10, 3, 11, 27, 27, 27, 26738 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 81500, /* 81 Mb */
			72200, 0x84, 0x00, 4,
			4, 14, 3, 12, 28, 28, 28, 40104 },
		{ FALSE, TRUE_40, WLAN_PHY_HT_40_SS, 108000, /* 108 Mb */
			92900, 0x85, 0x00, 5,
			4, 20, 3, 13, 29, 29, 29, 53476 },
		{ FALSE, TRUE_40, WLAN_PHY_HT_40_SS, 121500, /* 121.5 Mb */
			102700, 0x86, 0x00, 6,
			4, 23, 3, 14, 30, 30, 30, 60156 },
		{ FALSE, TRUE_40, WLAN_PHY_HT_40_SS, 135000, /* 135 Mb */
			112000, 0x87, 0x00, 7,
			4, 25, 3, 15, 31, 32, 32, 66840 },
		{ FALSE, TRUE_40, WLAN_PHY_HT_40_SS_HGI, 150000, /* 150 Mb */
			122000, 0x87, 0x00, 7,
			4, 25, 3, 15, 31, 32, 32, 74200 },
		{ FALSE, FALSE, WLAN_PHY_HT_40_DS, 27000, /* 27 Mb */
			25800, 0x88, 0x00, 8,
			0, 2, 3, 16, 33, 33, 33, 13360 },
		{ FALSE, FALSE, WLAN_PHY_HT_40_DS, 54000, /* 54 Mb */
			49800, 0x89, 0x00, 9,
			2, 4, 3, 17, 34, 34, 34, 26720 },
		{ FALSE, FALSE, WLAN_PHY_HT_40_DS, 81000, /* 81 Mb */
			71900, 0x8a, 0x00, 10,
			2, 6, 3, 18, 35, 35, 35, 40080 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 108000, /* 108 Mb */
			92500, 0x8b, 0x00, 11,
			4, 10, 3, 19, 36, 36, 36, 53440 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 162000, /* 162 Mb */
			130300, 0x8c, 0x00, 12,
			4, 14, 3, 20, 37, 37, 37, 80160 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 216000, /* 216 Mb */
			162800, 0x8d, 0x00, 13,
			4, 20, 3, 21, 38, 38, 38, 106880 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 243000, /* 243 Mb */
			178200, 0x8e, 0x00, 14,
			4, 23, 3, 22, 39, 39, 39, 120240 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 270000, /* 270 Mb */
			192100, 0x8f, 0x00, 15,
			4, 25, 3, 23, 40, 41, 41, 133600 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS_HGI, 300000, /* 300 Mb */
			207000, 0x8f, 0x00, 15,
			4, 25, 3, 23, 40, 41, 41, 148400 },
	},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	WLAN_RC_HT_FLAG,  /* Phy rates allowed initially */
};

/* TRUE_ALL - valid for 20/40/Legacy,
 * TRUE - Legacy only,
 * TRUE_20 - HT 20 only,
 * TRUE_40 - HT 40 only */

/* 4ms frame limit not used for NG mode.  The values filled
 * for HT are the 64K max aggregate limit */

static struct ath_rate_table ar5416_11ng_ratetable = {
	46,
	{
		{ TRUE_ALL, TRUE_ALL, WLAN_PHY_CCK, 1000, /* 1 Mb */
			900, 0x1b, 0x00, 2,
			0, 0, 1, 0, 0, 0, 0, 0 },
		{ TRUE_ALL, TRUE_ALL, WLAN_PHY_CCK, 2000, /* 2 Mb */
			1900, 0x1a, 0x04, 4,
			1, 1, 1, 1, 1, 1, 1, 0 },
		{ TRUE_ALL, TRUE_ALL, WLAN_PHY_CCK, 5500, /* 5.5 Mb */
			4900, 0x19, 0x04, 11,
			2, 2, 2, 2, 2, 2, 2, 0 },
		{ TRUE_ALL, TRUE_ALL, WLAN_PHY_CCK, 11000, /* 11 Mb */
			8100, 0x18, 0x04, 22,
			3, 3, 2, 3, 3, 3, 3, 0 },
		{ FALSE, FALSE, WLAN_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, 12,
			4, 2, 1, 4, 4, 4, 4, 0 },
		{ FALSE, FALSE, WLAN_PHY_OFDM, 9000, /* 9 Mb */
			7800, 0x0f, 0x00, 18,
			4, 3, 1, 5, 5, 5, 5, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 12000, /* 12 Mb */
			10100, 0x0a, 0x00, 24,
			6, 4, 1, 6, 6, 6, 6, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 18000, /* 18 Mb */
			14100,  0x0e, 0x00, 36,
			6, 6, 2, 7, 7, 7, 7, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 24000, /* 24 Mb */
			17700, 0x09, 0x00, 48,
			8, 10, 3, 8, 8, 8, 8, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 36000, /* 36 Mb */
			23700, 0x0d, 0x00, 72,
			8, 14, 3, 9, 9, 9, 9, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			8, 20, 3, 10, 10, 10, 10, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 54000, /* 54 Mb */
			30900, 0x0c, 0x00, 108,
			8, 23, 3, 11, 11, 11, 11, 0 },
		{ FALSE, FALSE, WLAN_PHY_HT_20_SS, 6500, /* 6.5 Mb */
			6400, 0x80, 0x00, 0,
			4, 2, 3, 12, 28, 12, 28, 3216 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 13000, /* 13 Mb */
			12700, 0x81, 0x00, 1,
			6, 4, 3, 13, 29, 13, 29, 6434 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 19500, /* 19.5 Mb */
			18800, 0x82, 0x00, 2,
			6, 6, 3, 14, 30, 14, 30, 9650 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 26000, /* 26 Mb */
			25000, 0x83, 0x00, 3,
			8, 10, 3, 15, 31, 15, 31, 12868 },
		{ TRUE_20, TRUE_20, WLAN_PHY_HT_20_SS, 39000, /* 39 Mb */
			36700, 0x84, 0x00, 4,
			8, 14, 3, 16, 32, 16, 32, 19304 },
		{ FALSE, TRUE_20, WLAN_PHY_HT_20_SS, 52000, /* 52 Mb */
			48100, 0x85, 0x00, 5,
			8, 20, 3, 17, 33, 17, 33, 25740 },
		{ FALSE,  TRUE_20, WLAN_PHY_HT_20_SS, 58500, /* 58.5 Mb */
			53500, 0x86, 0x00, 6,
			8, 23, 3, 18, 34, 18, 34, 28956 },
		{ FALSE, TRUE_20, WLAN_PHY_HT_20_SS, 65000, /* 65 Mb */
			59000, 0x87, 0x00, 7,
			8, 25, 3, 19, 35, 19, 36, 32180 },
		{ FALSE, FALSE, WLAN_PHY_HT_20_DS, 13000, /* 13 Mb */
			12700, 0x88, 0x00, 8,
			4, 2, 3, 20, 37, 20, 37, 6430 },
		{ FALSE, FALSE, WLAN_PHY_HT_20_DS, 26000, /* 26 Mb */
			24800, 0x89, 0x00, 9,
			6, 4, 3, 21, 38, 21, 38, 12860 },
		{ FALSE, FALSE, WLAN_PHY_HT_20_DS, 39000, /* 39 Mb */
			36600, 0x8a, 0x00, 10,
			6, 6, 3, 22, 39, 22, 39, 19300 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 52000, /* 52 Mb */
			48100, 0x8b, 0x00, 11,
			8, 10, 3, 23, 40, 23, 40, 25736 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 78000, /* 78 Mb */
			69500, 0x8c, 0x00, 12,
			8, 14, 3, 24, 41, 24, 41, 38600 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 104000, /* 104 Mb */
			89500, 0x8d, 0x00, 13,
			8, 20, 3, 25, 42, 25, 42, 51472 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 117000, /* 117 Mb */
			98900, 0x8e, 0x00, 14,
			8, 23, 3, 26, 43, 26, 44, 57890 },
		{ TRUE_20, FALSE, WLAN_PHY_HT_20_DS, 130000, /* 130 Mb */
			108300, 0x8f, 0x00, 15,
			8, 25, 3, 27, 44, 27, 45, 64320 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 13500, /* 13.5 Mb */
			13200, 0x80, 0x00, 0,
			8, 2, 3, 12, 28, 28, 28, 6684 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 27500, /* 27.0 Mb */
			25900, 0x81, 0x00, 1,
			8, 4, 3, 13, 29, 29, 29, 13368 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 40500, /* 40.5 Mb */
			38600, 0x82, 0x00, 2,
			8, 6, 3, 14, 30, 30, 30, 20052 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 54000, /* 54 Mb */
			49800, 0x83, 0x00, 3,
			8, 10, 3, 15, 31, 31, 31, 26738 },
		{ TRUE_40, TRUE_40, WLAN_PHY_HT_40_SS, 81500, /* 81 Mb */
			72200, 0x84, 0x00, 4,
			8, 14, 3, 16, 32, 32, 32, 40104 },
		{ FALSE, TRUE_40, WLAN_PHY_HT_40_SS, 108000, /* 108 Mb */
			92900, 0x85, 0x00, 5,
			8, 20, 3, 17, 33, 33, 33, 53476 },
		{ FALSE,  TRUE_40, WLAN_PHY_HT_40_SS, 121500, /* 121.5 Mb */
			102700, 0x86, 0x00, 6,
			8, 23, 3, 18, 34, 34, 34, 60156 },
		{ FALSE, TRUE_40, WLAN_PHY_HT_40_SS, 135000, /* 135 Mb */
			112000, 0x87, 0x00, 7,
			8, 23, 3, 19, 35, 36, 36, 66840 },
		{ FALSE, TRUE_40, WLAN_PHY_HT_40_SS_HGI, 150000, /* 150 Mb */
			122000, 0x87, 0x00, 7,
			8, 25, 3, 19, 35, 36, 36, 74200 },
		{ FALSE, FALSE, WLAN_PHY_HT_40_DS, 27000, /* 27 Mb */
			25800, 0x88, 0x00, 8,
			8, 2, 3, 20, 37, 37, 37, 13360 },
		{ FALSE, FALSE, WLAN_PHY_HT_40_DS, 54000, /* 54 Mb */
			49800, 0x89, 0x00, 9,
			8, 4, 3, 21, 38, 38, 38, 26720 },
		{ FALSE, FALSE, WLAN_PHY_HT_40_DS, 81000, /* 81 Mb */
			71900, 0x8a, 0x00, 10,
			8, 6, 3, 22, 39, 39, 39, 40080 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 108000, /* 108 Mb */
			92500, 0x8b, 0x00, 11,
			8, 10, 3, 23, 40, 40, 40, 53440 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 162000, /* 162 Mb */
			130300, 0x8c, 0x00, 12,
			8, 14, 3, 24, 41, 41, 41, 80160 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 216000, /* 216 Mb */
			162800, 0x8d, 0x00, 13,
			8, 20, 3, 25, 42, 42, 42, 106880 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 243000, /* 243 Mb */
			178200, 0x8e, 0x00, 14,
			8, 23, 3, 26, 43, 43, 43, 120240 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS, 270000, /* 270 Mb */
			192100, 0x8f, 0x00, 15,
			8, 23, 3, 27, 44, 45, 45, 133600 },
		{ TRUE_40, FALSE, WLAN_PHY_HT_40_DS_HGI, 300000, /* 300 Mb */
			207000, 0x8f, 0x00, 15,
			8, 25, 3, 27, 44, 45, 45, 148400 },
		},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	WLAN_RC_HT_FLAG,  /* Phy rates allowed initially */
};

static struct ath_rate_table ar5416_11a_ratetable = {
	8,
	{
		{ TRUE, TRUE, WLAN_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, (0x80|12),
			0, 2, 1, 0, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 9000, /* 9 Mb */
			7800, 0x0f, 0x00, 18,
			0, 3, 1, 1, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 12000, /* 12 Mb */
			10000, 0x0a, 0x00, (0x80|24),
			2, 4, 2, 2, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 18000, /* 18 Mb */
			13900, 0x0e, 0x00, 36,
			2, 6, 2, 3, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 24000, /* 24 Mb */
			17300, 0x09, 0x00, (0x80|48),
			4, 10, 3, 4, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 36000, /* 36 Mb */
			23000, 0x0d, 0x00, 72,
			4, 14, 3, 5, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			4, 19, 3, 6, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 54000, /* 54 Mb */
			29300, 0x0c, 0x00, 108,
			4, 23, 3, 7, 0 },
	},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	0,   /* Phy rates allowed initially */
};

static struct ath_rate_table ar5416_11a_ratetable_Half = {
	8,
	{
		{ TRUE, TRUE, WLAN_PHY_OFDM, 3000, /* 6 Mb */
			2700, 0x0b, 0x00, (0x80|6),
			0, 2,  1, 0, 0},
		{ TRUE, TRUE,  WLAN_PHY_OFDM, 4500, /* 9 Mb */
			3900, 0x0f, 0x00, 9,
			0, 3, 1, 1, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 6000, /* 12 Mb */
			5000, 0x0a, 0x00, (0x80|12),
			2, 4, 2, 2, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 9000, /* 18 Mb */
			6950, 0x0e, 0x00, 18,
			2, 6, 2, 3, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 12000, /* 24 Mb */
			8650, 0x09, 0x00, (0x80|24),
			4, 10, 3, 4, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 18000, /* 36 Mb */
			11500, 0x0d, 0x00, 36,
			4, 14, 3, 5, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 24000, /* 48 Mb */
			13700, 0x08, 0x00, 48,
			4, 19, 3, 6, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 27000, /* 54 Mb */
			14650, 0x0c, 0x00, 54,
			4, 23, 3, 7, 0 },
	},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	0,   /* Phy rates allowed initially */
};

static struct ath_rate_table ar5416_11a_ratetable_Quarter = {
	8,
	{
		{ TRUE, TRUE, WLAN_PHY_OFDM, 1500, /* 6 Mb */
			1350, 0x0b, 0x00, (0x80|3),
			0, 2, 1, 0, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 2250, /* 9 Mb */
			1950, 0x0f, 0x00, 4,
			0, 3, 1, 1, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 3000, /* 12 Mb */
			2500, 0x0a, 0x00, (0x80|6),
			2, 4, 2, 2, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 4500, /* 18 Mb */
			3475, 0x0e, 0x00, 9,
			2, 6, 2, 3, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 6000, /* 25 Mb */
			4325, 0x09, 0x00, (0x80|12),
			4, 10, 3, 4, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 9000, /* 36 Mb */
			5750, 0x0d, 0x00, 18,
			4, 14, 3, 5, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 12000, /* 48 Mb */
			6850, 0x08, 0x00, 24,
			4, 19, 3, 6, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 13500, /* 54 Mb */
			7325, 0x0c, 0x00, 27,
			4, 23, 3, 7, 0 },
	},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	0,   /* Phy rates allowed initially */
};

static struct ath_rate_table ar5416_11g_ratetable = {
	12,
	{
		{ TRUE, TRUE, WLAN_PHY_CCK, 1000, /* 1 Mb */
			900, 0x1b, 0x00, 2,
			0, 0, 1, 0, 0 },
		{ TRUE, TRUE, WLAN_PHY_CCK, 2000, /* 2 Mb */
			1900, 0x1a, 0x04, 4,
			1, 1, 1, 1, 0 },
		{ TRUE, TRUE, WLAN_PHY_CCK, 5500, /* 5.5 Mb */
			4900, 0x19, 0x04, 11,
			2, 2, 2, 2, 0 },
		{ TRUE, TRUE, WLAN_PHY_CCK, 11000, /* 11 Mb */
			8100, 0x18, 0x04, 22,
			3, 3, 2, 3, 0 },
		{ FALSE, FALSE, WLAN_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, 12,
			4, 2, 1, 4, 0 },
		{ FALSE, FALSE, WLAN_PHY_OFDM, 9000, /* 9 Mb */
			7800, 0x0f, 0x00, 18,
			4, 3, 1, 5, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 12000, /* 12 Mb */
			10000, 0x0a, 0x00, 24,
			6, 4, 1, 6, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 18000, /* 18 Mb */
			13900, 0x0e, 0x00, 36,
			6, 6, 2, 7, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 24000, /* 24 Mb */
			17300, 0x09, 0x00, 48,
			8, 10, 3, 8, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 36000, /* 36 Mb */
			23000, 0x0d, 0x00, 72,
			8, 14, 3, 9, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			8, 19, 3, 10, 0 },
		{ TRUE, TRUE, WLAN_PHY_OFDM, 54000, /* 54 Mb */
			29300, 0x0c, 0x00, 108,
			8, 23, 3, 11, 0 },
	},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	0,   /* Phy rates allowed initially */
};

static struct ath_rate_table ar5416_11b_ratetable = {
	4,
	{
		{ TRUE, TRUE, WLAN_PHY_CCK, 1000, /* 1 Mb */
			900, 0x1b,  0x00, (0x80|2),
			0, 0, 1, 0, 0 },
		{ TRUE, TRUE, WLAN_PHY_CCK, 2000, /* 2 Mb */
			1800, 0x1a, 0x04, (0x80|4),
			1, 1, 1, 1, 0 },
		{ TRUE, TRUE, WLAN_PHY_CCK, 5500, /* 5.5 Mb */
			4300, 0x19, 0x04, (0x80|11),
			1, 2, 2, 2, 0 },
		{ TRUE, TRUE, WLAN_PHY_CCK, 11000, /* 11 Mb */
			7100, 0x18, 0x04, (0x80|22),
			1, 4, 100, 3, 0 },
	},
	100, /* probe interval */
	100, /* rssi reduce interval */
	0,   /* Phy rates allowed initially */
};

static void ar5416_attach_ratetables(struct ath_rate_softc *sc)
{
	/*
	 * Attach rate tables.
	 */
	sc->hw_rate_table[ATH9K_MODE_11B] = &ar5416_11b_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11A] = &ar5416_11a_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11G] = &ar5416_11g_ratetable;

	sc->hw_rate_table[ATH9K_MODE_11NA_HT20] = &ar5416_11na_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NG_HT20] = &ar5416_11ng_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NA_HT40PLUS] =
		&ar5416_11na_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NA_HT40MINUS] =
		&ar5416_11na_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NG_HT40PLUS] =
		&ar5416_11ng_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NG_HT40MINUS] =
		&ar5416_11ng_ratetable;
}

static void ar5416_setquarter_ratetable(struct ath_rate_softc *sc)
{
	sc->hw_rate_table[ATH9K_MODE_11A] = &ar5416_11a_ratetable_Quarter;
	return;
}

static void ar5416_sethalf_ratetable(struct ath_rate_softc *sc)
{
	sc->hw_rate_table[ATH9K_MODE_11A] = &ar5416_11a_ratetable_Half;
	return;
}

static void ar5416_setfull_ratetable(struct ath_rate_softc *sc)
{
	sc->hw_rate_table[ATH9K_MODE_11A] = &ar5416_11a_ratetable;
	return;
}

/*
 * Return the median of three numbers
 */
static inline int8_t median(int8_t a, int8_t b, int8_t c)
{
	if (a >= b) {
		if (b >= c)
			return b;
		else if (a > c)
			return c;
		else
			return a;
	} else {
		if (a >= c)
			return a;
		else if (b >= c)
			return c;
		else
			return b;
	}
}

static void ath_rc_sort_validrates(const struct ath_rate_table *rate_table,
				   struct ath_tx_ratectrl *rate_ctrl)
{
	u8 i, j, idx, idx_next;

	for (i = rate_ctrl->max_valid_rate - 1; i > 0; i--) {
		for (j = 0; j <= i-1; j++) {
			idx = rate_ctrl->valid_rate_index[j];
			idx_next = rate_ctrl->valid_rate_index[j+1];

			if (rate_table->info[idx].ratekbps >
				rate_table->info[idx_next].ratekbps) {
				rate_ctrl->valid_rate_index[j] = idx_next;
				rate_ctrl->valid_rate_index[j+1] = idx;
			}
		}
	}
}

/* Access functions for valid_txrate_mask */

static void ath_rc_init_valid_txmask(struct ath_tx_ratectrl *rate_ctrl)
{
	u8 i;

	for (i = 0; i < rate_ctrl->rate_table_size; i++)
		rate_ctrl->valid_rate_index[i] = FALSE;
}

static inline void ath_rc_set_valid_txmask(struct ath_tx_ratectrl *rate_ctrl,
					   u8 index, int valid_tx_rate)
{
	ASSERT(index <= rate_ctrl->rate_table_size);
	rate_ctrl->valid_rate_index[index] = valid_tx_rate ? TRUE : FALSE;
}

static inline int ath_rc_isvalid_txmask(struct ath_tx_ratectrl *rate_ctrl,
					u8 index)
{
	ASSERT(index <= rate_ctrl->rate_table_size);
	return rate_ctrl->valid_rate_index[index];
}

/* Iterators for valid_txrate_mask */
static inline int
ath_rc_get_nextvalid_txrate(const struct ath_rate_table *rate_table,
			    struct ath_tx_ratectrl *rate_ctrl,
			    u8 cur_valid_txrate,
			    u8 *next_idx)
{
	u8 i;

	for (i = 0; i < rate_ctrl->max_valid_rate - 1; i++) {
		if (rate_ctrl->valid_rate_index[i] == cur_valid_txrate) {
			*next_idx = rate_ctrl->valid_rate_index[i+1];
			return TRUE;
		}
	}

	/* No more valid rates */
	*next_idx = 0;
	return FALSE;
}

/* Return true only for single stream */

static int ath_rc_valid_phyrate(u32 phy, u32 capflag, int ignore_cw)
{
	if (WLAN_RC_PHY_HT(phy) & !(capflag & WLAN_RC_HT_FLAG))
		return FALSE;
	if (WLAN_RC_PHY_DS(phy) && !(capflag & WLAN_RC_DS_FLAG))
		return FALSE;
	if (WLAN_RC_PHY_SGI(phy) && !(capflag & WLAN_RC_SGI_FLAG))
		return FALSE;
	if (!ignore_cw && WLAN_RC_PHY_HT(phy))
		if (WLAN_RC_PHY_40(phy) && !(capflag & WLAN_RC_40_FLAG))
			return FALSE;
		if (!WLAN_RC_PHY_40(phy) && (capflag & WLAN_RC_40_FLAG))
			return FALSE;
	return TRUE;
}

static inline int
ath_rc_get_nextlowervalid_txrate(const struct ath_rate_table *rate_table,
				 struct ath_tx_ratectrl *rate_ctrl,
				 u8 cur_valid_txrate, u8 *next_idx)
{
	int8_t i;

	for (i = 1; i < rate_ctrl->max_valid_rate ; i++) {
		if (rate_ctrl->valid_rate_index[i] == cur_valid_txrate) {
			*next_idx = rate_ctrl->valid_rate_index[i-1];
			return TRUE;
		}
	}
	return FALSE;
}

/*
 * Initialize the Valid Rate Index from valid entries in Rate Table
 */
static u8
ath_rc_sib_init_validrates(struct ath_rate_node *ath_rc_priv,
			   const struct ath_rate_table *rate_table,
			   u32 capflag)
{
	struct ath_tx_ratectrl *rate_ctrl;
	u8 i, hi = 0;
	u32 valid;

	rate_ctrl = (struct ath_tx_ratectrl *)(ath_rc_priv);
	for (i = 0; i < rate_table->rate_cnt; i++) {
		valid = (ath_rc_priv->single_stream ?
				rate_table->info[i].valid_single_stream :
				rate_table->info[i].valid);
		if (valid == TRUE) {
			u32 phy = rate_table->info[i].phy;
			u8 valid_rate_count = 0;

			if (!ath_rc_valid_phyrate(phy, capflag, FALSE))
				continue;

			valid_rate_count = rate_ctrl->valid_phy_ratecnt[phy];

			rate_ctrl->valid_phy_rateidx[phy][valid_rate_count] = i;
			rate_ctrl->valid_phy_ratecnt[phy] += 1;
			ath_rc_set_valid_txmask(rate_ctrl, i, TRUE);
			hi = A_MAX(hi, i);
		}
	}
	return hi;
}

/*
 * Initialize the Valid Rate Index from Rate Set
 */
static u8
ath_rc_sib_setvalid_rates(struct ath_rate_node *ath_rc_priv,
			  const struct ath_rate_table *rate_table,
			  struct ath_rateset *rateset,
			  u32 capflag)
{
	/* XXX: Clean me up and make identation friendly */
	u8 i, j, hi = 0;
	struct ath_tx_ratectrl *rate_ctrl =
		(struct ath_tx_ratectrl *)(ath_rc_priv);

	/* Use intersection of working rates and valid rates */
	for (i = 0; i < rateset->rs_nrates; i++) {
		for (j = 0; j < rate_table->rate_cnt; j++) {
			u32 phy = rate_table->info[j].phy;
			u32 valid = (ath_rc_priv->single_stream ?
				rate_table->info[j].valid_single_stream :
				rate_table->info[j].valid);

			/* We allow a rate only if its valid and the
			 * capflag matches one of the validity
			 * (TRUE/TRUE_20/TRUE_40) flags */

			/* XXX: catch the negative of this branch
			 * first and then continue */
			if (((rateset->rs_rates[i] & 0x7F) ==
				(rate_table->info[j].dot11rate & 0x7F)) &&
				((valid & WLAN_RC_CAP_MODE(capflag)) ==
				WLAN_RC_CAP_MODE(capflag)) &&
				!WLAN_RC_PHY_HT(phy)) {

				u8 valid_rate_count = 0;

				if (!ath_rc_valid_phyrate(phy, capflag, FALSE))
					continue;

				valid_rate_count =
					rate_ctrl->valid_phy_ratecnt[phy];

				rate_ctrl->valid_phy_rateidx[phy]
					[valid_rate_count] = j;
				rate_ctrl->valid_phy_ratecnt[phy] += 1;
				ath_rc_set_valid_txmask(rate_ctrl, j, TRUE);
				hi = A_MAX(hi, j);
			}
		}
	}
	return hi;
}

static u8
ath_rc_sib_setvalid_htrates(struct ath_rate_node *ath_rc_priv,
			    const struct ath_rate_table *rate_table,
			    u8 *mcs_set, u32 capflag)
{
	u8 i, j, hi = 0;
	struct ath_tx_ratectrl *rate_ctrl =
		(struct ath_tx_ratectrl *)(ath_rc_priv);

	/* Use intersection of working rates and valid rates */
	for (i = 0; i <  ((struct ath_rateset *)mcs_set)->rs_nrates; i++) {
		for (j = 0; j < rate_table->rate_cnt; j++) {
			u32 phy = rate_table->info[j].phy;
			u32 valid = (ath_rc_priv->single_stream ?
				rate_table->info[j].valid_single_stream :
				rate_table->info[j].valid);

			if (((((struct ath_rateset *)
				mcs_set)->rs_rates[i] & 0x7F) !=
				(rate_table->info[j].dot11rate & 0x7F)) ||
					!WLAN_RC_PHY_HT(phy) ||
					!WLAN_RC_PHY_HT_VALID(valid, capflag))
				continue;

			if (!ath_rc_valid_phyrate(phy, capflag, FALSE))
				continue;

			rate_ctrl->valid_phy_rateidx[phy]
				[rate_ctrl->valid_phy_ratecnt[phy]] = j;
			rate_ctrl->valid_phy_ratecnt[phy] += 1;
			ath_rc_set_valid_txmask(rate_ctrl, j, TRUE);
			hi = A_MAX(hi, j);
		}
	}
	return hi;
}

/*
 * Attach to a device instance.  Setup the public definition
 * of how much per-node space we need and setup the private
 * phy tables that have rate control parameters.
 */
struct ath_rate_softc *ath_rate_attach(struct ath_hal *ah)
{
	struct ath_rate_softc *asc;

	/* we are only in user context so we can sleep for memory */
	asc = kzalloc(sizeof(struct ath_rate_softc), GFP_KERNEL);
	if (asc == NULL)
		return NULL;

	ar5416_attach_ratetables(asc);

	/* Save Maximum TX Trigger Level (used for 11n) */
	tx_triglevel_max = ah->ah_caps.tx_triglevel_max;
	/*  return alias for ath_rate_softc * */
	return asc;
}

static struct ath_rate_node *ath_rate_node_alloc(struct ath_vap *avp,
						 struct ath_rate_softc *rsc,
						 gfp_t gfp)
{
	struct ath_rate_node *anode;

	anode = kzalloc(sizeof(struct ath_rate_node), gfp);
	if (anode == NULL)
		return NULL;

	anode->avp = avp;
	anode->asc = rsc;
	avp->rc_node = anode;

	return anode;
}

static void ath_rate_node_free(struct ath_rate_node *anode)
{
	if (anode != NULL)
		kfree(anode);
}

void ath_rate_detach(struct ath_rate_softc *asc)
{
	if (asc != NULL)
		kfree(asc);
}

u8 ath_rate_findrateix(struct ath_softc *sc,
			     u8 dot11rate)
{
	const struct ath_rate_table *ratetable;
	struct ath_rate_softc *rsc = sc->sc_rc;
	int i;

	ratetable = rsc->hw_rate_table[sc->sc_curmode];

	if (WARN_ON(!ratetable))
		return 0;

	for (i = 0; i < ratetable->rate_cnt; i++) {
		if ((ratetable->info[i].dot11rate & 0x7f) == (dot11rate & 0x7f))
			return i;
	}

	return 0;
}

/*
 * Update rate-control state on a device state change.  When
 * operating as a station this includes associate/reassociate
 * with an AP.  Otherwise this gets called, for example, when
 * the we transition to run state when operating as an AP.
 */
void ath_rate_newstate(struct ath_softc *sc, struct ath_vap *avp)
{
	struct ath_rate_softc *asc = sc->sc_rc;

	/* For half and quarter rate channles use different
	 * rate tables
	 */
	if (sc->sc_curchan.channelFlags & CHANNEL_HALF)
		ar5416_sethalf_ratetable(asc);
	else if (sc->sc_curchan.channelFlags & CHANNEL_QUARTER)
		ar5416_setquarter_ratetable(asc);
	else /* full rate */
		ar5416_setfull_ratetable(asc);

	if (avp->av_config.av_fixed_rateset != IEEE80211_FIXED_RATE_NONE) {
		asc->fixedrix =
			sc->sc_rixmap[avp->av_config.av_fixed_rateset & 0xff];
		/* NB: check the fixed rate exists */
		if (asc->fixedrix == 0xff)
			asc->fixedrix = IEEE80211_FIXED_RATE_NONE;
	} else {
		asc->fixedrix = IEEE80211_FIXED_RATE_NONE;
	}
}

static u8 ath_rc_ratefind_ht(struct ath_softc *sc,
				   struct ath_rate_node *ath_rc_priv,
				   const struct ath_rate_table *rate_table,
				   int probe_allowed, int *is_probing,
				   int is_retry)
{
	u32 dt, best_thruput, this_thruput, now_msec;
	u8 rate, next_rate, best_rate, maxindex, minindex;
	int8_t  rssi_last, rssi_reduce = 0, index = 0;
	struct ath_tx_ratectrl  *rate_ctrl = NULL;

	rate_ctrl = (struct ath_tx_ratectrl *)(ath_rc_priv ?
					       (ath_rc_priv) : NULL);

	*is_probing = FALSE;

	rssi_last = median(rate_ctrl->rssi_last,
			   rate_ctrl->rssi_last_prev,
			   rate_ctrl->rssi_last_prev2);

	/*
	 * Age (reduce) last ack rssi based on how old it is.
	 * The bizarre numbers are so the delta is 160msec,
	 * meaning we divide by 16.
	 *   0msec   <= dt <= 25msec:   don't derate
	 *   25msec  <= dt <= 185msec:  derate linearly from 0 to 10dB
	 *   185msec <= dt:             derate by 10dB
	 */

	now_msec = jiffies_to_msecs(jiffies);
	dt = now_msec - rate_ctrl->rssi_time;

	if (dt >= 185)
		rssi_reduce = 10;
	else if (dt >= 25)
		rssi_reduce = (u8)((dt - 25) >> 4);

	/* Now reduce rssi_last by rssi_reduce */
	if (rssi_last < rssi_reduce)
		rssi_last = 0;
	else
		rssi_last -= rssi_reduce;

	/*
	 * Now look up the rate in the rssi table and return it.
	 * If no rates match then we return 0 (lowest rate)
	 */

	best_thruput = 0;
	maxindex = rate_ctrl->max_valid_rate-1;

	minindex = 0;
	best_rate = minindex;

	/*
	 * Try the higher rate first. It will reduce memory moving time
	 * if we have very good channel characteristics.
	 */
	for (index = maxindex; index >= minindex ; index--) {
		u8 per_thres;

		rate = rate_ctrl->valid_rate_index[index];
		if (rate > rate_ctrl->rate_max_phy)
			continue;

		/*
		 * For TCP the average collision rate is around 11%,
		 * so we ignore PERs less than this.  This is to
		 * prevent the rate we are currently using (whose
		 * PER might be in the 10-15 range because of TCP
		 * collisions) looking worse than the next lower
		 * rate whose PER has decayed close to 0.  If we
		 * used to next lower rate, its PER would grow to
		 * 10-15 and we would be worse off then staying
		 * at the current rate.
		 */
		per_thres = rate_ctrl->state[rate].per;
		if (per_thres < 12)
			per_thres = 12;

		this_thruput = rate_table->info[rate].user_ratekbps *
			(100 - per_thres);

		if (best_thruput <= this_thruput) {
			best_thruput = this_thruput;
			best_rate    = rate;
		}
	}

	rate = best_rate;

	/* if we are retrying for more than half the number
	 * of max retries, use the min rate for the next retry
	 */
	if (is_retry)
		rate = rate_ctrl->valid_rate_index[minindex];

	rate_ctrl->rssi_last_lookup = rssi_last;

	/*
	 * Must check the actual rate (ratekbps) to account for
	 * non-monoticity of 11g's rate table
	 */

	if (rate >= rate_ctrl->rate_max_phy && probe_allowed) {
		rate = rate_ctrl->rate_max_phy;

		/* Probe the next allowed phy state */
		/* FIXME:XXXX Check to make sure ratMax is checked properly */
		if (ath_rc_get_nextvalid_txrate(rate_table,
						rate_ctrl, rate, &next_rate) &&
		    (now_msec - rate_ctrl->probe_time >
		     rate_table->probe_interval) &&
		    (rate_ctrl->hw_maxretry_pktcnt >= 1)) {
			rate = next_rate;
			rate_ctrl->probe_rate = rate;
			rate_ctrl->probe_time = now_msec;
			rate_ctrl->hw_maxretry_pktcnt = 0;
			*is_probing = TRUE;
		}
	}

	/*
	 * Make sure rate is not higher than the allowed maximum.
	 * We should also enforce the min, but I suspect the min is
	 * normally 1 rather than 0 because of the rate 9 vs 6 issue
	 * in the old code.
	 */
	if (rate > (rate_ctrl->rate_table_size - 1))
		rate = rate_ctrl->rate_table_size - 1;

	ASSERT((rate_table->info[rate].valid && !ath_rc_priv->single_stream) ||
		(rate_table->info[rate].valid_single_stream &&
			ath_rc_priv->single_stream));

	return rate;
}

static void ath_rc_rate_set_series(const struct ath_rate_table *rate_table ,
				   struct ath_rc_series *series,
				   u8 tries,
				   u8 rix,
				   int rtsctsenable)
{
	series->tries = tries;
	series->flags = (rtsctsenable ? ATH_RC_RTSCTS_FLAG : 0) |
		(WLAN_RC_PHY_DS(rate_table->info[rix].phy) ?
		 ATH_RC_DS_FLAG : 0) |
		(WLAN_RC_PHY_40(rate_table->info[rix].phy) ?
		 ATH_RC_CW40_FLAG : 0) |
		(WLAN_RC_PHY_SGI(rate_table->info[rix].phy) ?
		 ATH_RC_SGI_FLAG : 0);

	series->rix = rate_table->info[rix].base_index;
	series->max_4ms_framelen = rate_table->info[rix].max_4ms_framelen;
}

static u8 ath_rc_rate_getidx(struct ath_softc *sc,
				   struct ath_rate_node *ath_rc_priv,
				   const struct ath_rate_table *rate_table,
				   u8 rix, u16 stepdown,
				   u16 min_rate)
{
	u32 j;
	u8 nextindex;
	struct ath_tx_ratectrl *rate_ctrl =
		(struct ath_tx_ratectrl *)(ath_rc_priv);

	if (min_rate) {
		for (j = RATE_TABLE_SIZE; j > 0; j--) {
			if (ath_rc_get_nextlowervalid_txrate(rate_table,
						rate_ctrl, rix, &nextindex))
				rix = nextindex;
			else
				break;
		}
	} else {
		for (j = stepdown; j > 0; j--) {
			if (ath_rc_get_nextlowervalid_txrate(rate_table,
						rate_ctrl, rix, &nextindex))
				rix = nextindex;
			else
				break;
		}
	}
	return rix;
}

static void ath_rc_ratefind(struct ath_softc *sc,
			    struct ath_rate_node *ath_rc_priv,
			    int num_tries, int num_rates, unsigned int rcflag,
			    struct ath_rc_series series[], int *is_probe,
			    int is_retry)
{
	u8 try_per_rate = 0, i = 0, rix, nrix;
	struct ath_rate_softc  *asc = (struct ath_rate_softc *)sc->sc_rc;
	struct ath_rate_table *rate_table;

	rate_table =
		(struct ath_rate_table *)asc->hw_rate_table[sc->sc_curmode];
	rix = ath_rc_ratefind_ht(sc, ath_rc_priv, rate_table,
				(rcflag & ATH_RC_PROBE_ALLOWED) ? 1 : 0,
				is_probe, is_retry);
	nrix = rix;

	if ((rcflag & ATH_RC_PROBE_ALLOWED) && (*is_probe)) {
		/* set one try for probe rates. For the
		 * probes don't enable rts */
		ath_rc_rate_set_series(rate_table,
			&series[i++], 1, nrix, FALSE);

		try_per_rate = (num_tries/num_rates);
		/* Get the next tried/allowed rate. No RTS for the next series
		 * after the probe rate
		 */
		nrix = ath_rc_rate_getidx(sc,
			ath_rc_priv, rate_table, nrix, 1, FALSE);
		ath_rc_rate_set_series(rate_table,
			&series[i++], try_per_rate, nrix, 0);
	} else {
		try_per_rate = (num_tries/num_rates);
		/* Set the choosen rate. No RTS for first series entry. */
		ath_rc_rate_set_series(rate_table,
			&series[i++], try_per_rate, nrix, FALSE);
	}

	/* Fill in the other rates for multirate retry */
	for ( ; i < num_rates; i++) {
		u8 try_num;
		u8 min_rate;

		try_num = ((i + 1) == num_rates) ?
			num_tries - (try_per_rate * i) : try_per_rate ;
		min_rate = (((i + 1) == num_rates) &&
			(rcflag & ATH_RC_MINRATE_LASTRATE)) ? 1 : 0;

		nrix = ath_rc_rate_getidx(sc, ath_rc_priv,
			rate_table, nrix, 1, min_rate);
		/* All other rates in the series have RTS enabled */
		ath_rc_rate_set_series(rate_table,
			&series[i], try_num, nrix, TRUE);
	}

	/*
	 * NB:Change rate series to enable aggregation when operating
	 * at lower MCS rates. When first rate in series is MCS2
	 * in HT40 @ 2.4GHz, series should look like:
	 *
	 * {MCS2, MCS1, MCS0, MCS0}.
	 *
	 * When first rate in series is MCS3 in HT20 @ 2.4GHz, series should
	 * look like:
	 *
	 * {MCS3, MCS2, MCS1, MCS1}
	 *
	 * So, set fourth rate in series to be same as third one for
	 * above conditions.
	 */
	if ((sc->sc_curmode == ATH9K_MODE_11NG_HT20) ||
			(sc->sc_curmode == ATH9K_MODE_11NG_HT40PLUS) ||
			(sc->sc_curmode == ATH9K_MODE_11NG_HT40MINUS)) {
		u8  dot11rate = rate_table->info[rix].dot11rate;
		u8 phy = rate_table->info[rix].phy;
		if (i == 4 &&
		    ((dot11rate == 2 && phy == WLAN_RC_PHY_HT_40_SS) ||
		    (dot11rate == 3 && phy == WLAN_RC_PHY_HT_20_SS))) {
			series[3].rix = series[2].rix;
			series[3].flags = series[2].flags;
			series[3].max_4ms_framelen = series[2].max_4ms_framelen;
		}
	}
}

/*
 * Return the Tx rate series.
 */
void ath_rate_findrate(struct ath_softc *sc,
		       struct ath_rate_node *ath_rc_priv,
		       int num_tries,
		       int num_rates,
		       unsigned int rcflag,
		       struct ath_rc_series series[],
		       int *is_probe,
		       int is_retry)
{
	struct ath_vap *avp = ath_rc_priv->avp;

	DPRINTF(sc, ATH_DBG_RATE, "%s", __func__);
	if (!num_rates || !num_tries)
		return;

	if (avp->av_config.av_fixed_rateset == IEEE80211_FIXED_RATE_NONE) {
		ath_rc_ratefind(sc, ath_rc_priv, num_tries, num_rates,
				rcflag, series, is_probe, is_retry);
	} else {
		/* Fixed rate */
		int idx;
		u8 flags;
		u32 rix;
		struct ath_rate_softc *asc = ath_rc_priv->asc;
		struct ath_rate_table *rate_table;

		rate_table = (struct ath_rate_table *)
			asc->hw_rate_table[sc->sc_curmode];

		for (idx = 0; idx < 4; idx++) {
			unsigned int    mcs;
			u8 series_rix = 0;

			series[idx].tries =
				IEEE80211_RATE_IDX_ENTRY(
					avp->av_config.av_fixed_retryset, idx);

			mcs = IEEE80211_RATE_IDX_ENTRY(
				avp->av_config.av_fixed_rateset, idx);

			if (idx == 3 && (mcs & 0xf0) == 0x70)
				mcs = (mcs & ~0xf0)|0x80;

			if (!(mcs & 0x80))
				flags = 0;
			else
				flags = ((ath_rc_priv->ht_cap &
						WLAN_RC_DS_FLAG) ?
						ATH_RC_DS_FLAG : 0) |
					((ath_rc_priv->ht_cap &
						WLAN_RC_40_FLAG) ?
						ATH_RC_CW40_FLAG : 0) |
					((ath_rc_priv->ht_cap &
						WLAN_RC_SGI_FLAG) ?
					((ath_rc_priv->ht_cap &
						WLAN_RC_40_FLAG) ?
						ATH_RC_SGI_FLAG : 0) : 0);

			series[idx].rix = sc->sc_rixmap[mcs];
			series_rix  = series[idx].rix;

			/* XXX: Give me some cleanup love */
			if ((flags & ATH_RC_CW40_FLAG) &&
				(flags & ATH_RC_SGI_FLAG))
				rix = rate_table->info[series_rix].ht_index;
			else if (flags & ATH_RC_SGI_FLAG)
				rix = rate_table->info[series_rix].sgi_index;
			else if (flags & ATH_RC_CW40_FLAG)
				rix = rate_table->info[series_rix].cw40index;
			else
				rix = rate_table->info[series_rix].base_index;
			series[idx].max_4ms_framelen =
				rate_table->info[rix].max_4ms_framelen;
			series[idx].flags = flags;
		}
	}
}

static void ath_rc_update_ht(struct ath_softc *sc,
			     struct ath_rate_node *ath_rc_priv,
			     struct ath_tx_info_priv *info_priv,
			     int tx_rate, int xretries, int retries)
{
	struct ath_tx_ratectrl *rate_ctrl;
	u32 now_msec = jiffies_to_msecs(jiffies);
	int state_change = FALSE, rate, count;
	u8 last_per;
	struct ath_rate_softc  *asc = (struct ath_rate_softc *)sc->sc_rc;
	struct ath_rate_table *rate_table =
		(struct ath_rate_table *)asc->hw_rate_table[sc->sc_curmode];

	static u32 nretry_to_per_lookup[10] = {
		100 * 0 / 1,
		100 * 1 / 4,
		100 * 1 / 2,
		100 * 3 / 4,
		100 * 4 / 5,
		100 * 5 / 6,
		100 * 6 / 7,
		100 * 7 / 8,
		100 * 8 / 9,
		100 * 9 / 10
	};

	if (!ath_rc_priv)
		return;

	rate_ctrl = (struct ath_tx_ratectrl *)(ath_rc_priv);

	ASSERT(tx_rate >= 0);
	if (tx_rate < 0)
		return;

	/* To compensate for some imbalance between ctrl and ext. channel */

	if (WLAN_RC_PHY_40(rate_table->info[tx_rate].phy))
		info_priv->tx.ts_rssi =
			info_priv->tx.ts_rssi < 3 ? 0 :
			info_priv->tx.ts_rssi - 3;

	last_per = rate_ctrl->state[tx_rate].per;

	if (xretries) {
		/* Update the PER. */
		if (xretries == 1) {
			rate_ctrl->state[tx_rate].per += 30;
			if (rate_ctrl->state[tx_rate].per > 100)
				rate_ctrl->state[tx_rate].per = 100;
		} else {
			/* xretries == 2 */
			count = sizeof(nretry_to_per_lookup) /
					sizeof(nretry_to_per_lookup[0]);
			if (retries >= count)
				retries = count - 1;
			/* new_PER = 7/8*old_PER + 1/8*(currentPER) */
			rate_ctrl->state[tx_rate].per =
				(u8)(rate_ctrl->state[tx_rate].per -
				(rate_ctrl->state[tx_rate].per >> 3) +
				((100) >> 3));
		}

		/* xretries == 1 or 2 */

		if (rate_ctrl->probe_rate == tx_rate)
			rate_ctrl->probe_rate = 0;

	} else {	/* xretries == 0 */
		/* Update the PER. */
		/* Make sure it doesn't index out of array's bounds. */
		count = sizeof(nretry_to_per_lookup) /
			sizeof(nretry_to_per_lookup[0]);
		if (retries >= count)
			retries = count - 1;
		if (info_priv->n_bad_frames) {
			/* new_PER = 7/8*old_PER + 1/8*(currentPER)  */
			/*
			 * Assuming that n_frames is not 0.  The current PER
			 * from the retries is 100 * retries / (retries+1),
			 * since the first retries attempts failed, and the
			 * next one worked.  For the one that worked,
			 * n_bad_frames subframes out of n_frames wored,
			 * so the PER for that part is
			 * 100 * n_bad_frames / n_frames, and it contributes
			 * 100 * n_bad_frames / (n_frames * (retries+1)) to
			 * the above PER.  The expression below is a
			 * simplified version of the sum of these two terms.
			 */
			if (info_priv->n_frames > 0)
				rate_ctrl->state[tx_rate].per
				      = (u8)
					(rate_ctrl->state[tx_rate].per -
					(rate_ctrl->state[tx_rate].per >> 3) +
					((100*(retries*info_priv->n_frames +
					info_priv->n_bad_frames) /
					(info_priv->n_frames *
						(retries+1))) >> 3));
		} else {
			/* new_PER = 7/8*old_PER + 1/8*(currentPER) */

			rate_ctrl->state[tx_rate].per = (u8)
				(rate_ctrl->state[tx_rate].per -
				(rate_ctrl->state[tx_rate].per >> 3) +
				(nretry_to_per_lookup[retries] >> 3));
		}

		rate_ctrl->rssi_last_prev2 = rate_ctrl->rssi_last_prev;
		rate_ctrl->rssi_last_prev  = rate_ctrl->rssi_last;
		rate_ctrl->rssi_last = info_priv->tx.ts_rssi;
		rate_ctrl->rssi_time = now_msec;

		/*
		 * If we got at most one retry then increase the max rate if
		 * this was a probe.  Otherwise, ignore the probe.
		 */

		if (rate_ctrl->probe_rate && rate_ctrl->probe_rate == tx_rate) {
			if (retries > 0 || 2 * info_priv->n_bad_frames >
				info_priv->n_frames) {
				/*
				 * Since we probed with just a single attempt,
				 * any retries means the probe failed.  Also,
				 * if the attempt worked, but more than half
				 * the subframes were bad then also consider
				 * the probe a failure.
				 */
				rate_ctrl->probe_rate = 0;
			} else {
				u8 probe_rate = 0;

				rate_ctrl->rate_max_phy = rate_ctrl->probe_rate;
				probe_rate = rate_ctrl->probe_rate;

				if (rate_ctrl->state[probe_rate].per > 30)
					rate_ctrl->state[probe_rate].per = 20;

				rate_ctrl->probe_rate = 0;

				/*
				 * Since this probe succeeded, we allow the next
				 * probe twice as soon.  This allows the maxRate
				 * to move up faster if the probes are
				 * succesful.
				 */
				rate_ctrl->probe_time = now_msec -
					rate_table->probe_interval / 2;
			}
		}

		if (retries > 0) {
			/*
			 * Don't update anything.  We don't know if
			 * this was because of collisions or poor signal.
			 *
			 * Later: if rssi_ack is close to
			 * rate_ctrl->state[txRate].rssi_thres and we see lots
			 * of retries, then we could increase
			 * rate_ctrl->state[txRate].rssi_thres.
			 */
			rate_ctrl->hw_maxretry_pktcnt = 0;
		} else {
			/*
			 * It worked with no retries. First ignore bogus (small)
			 * rssi_ack values.
			 */
			if (tx_rate == rate_ctrl->rate_max_phy &&
					rate_ctrl->hw_maxretry_pktcnt < 255) {
				rate_ctrl->hw_maxretry_pktcnt++;
			}

			if (info_priv->tx.ts_rssi >=
				rate_table->info[tx_rate].rssi_ack_validmin) {
				/* Average the rssi */
				if (tx_rate != rate_ctrl->rssi_sum_rate) {
					rate_ctrl->rssi_sum_rate = tx_rate;
					rate_ctrl->rssi_sum =
						rate_ctrl->rssi_sum_cnt = 0;
				}

				rate_ctrl->rssi_sum += info_priv->tx.ts_rssi;
				rate_ctrl->rssi_sum_cnt++;

				if (rate_ctrl->rssi_sum_cnt > 4) {
					int32_t rssi_ackAvg =
						(rate_ctrl->rssi_sum + 2) / 4;
					int8_t rssi_thres =
						rate_ctrl->state[tx_rate].
						rssi_thres;
					int8_t rssi_ack_vmin =
						rate_table->info[tx_rate].
						rssi_ack_validmin;

					rate_ctrl->rssi_sum =
						rate_ctrl->rssi_sum_cnt = 0;

					/* Now reduce the current
					 * rssi threshold. */
					if ((rssi_ackAvg < rssi_thres + 2) &&
						(rssi_thres > rssi_ack_vmin)) {
						rate_ctrl->state[tx_rate].
							rssi_thres--;
					}

					state_change = TRUE;
				}
			}
		}
	}

	/* For all cases */

	/*
	 * If this rate looks bad (high PER) then stop using it for
	 * a while (except if we are probing).
	 */
	if (rate_ctrl->state[tx_rate].per >= 55 && tx_rate > 0 &&
			rate_table->info[tx_rate].ratekbps <=
			rate_table->info[rate_ctrl->rate_max_phy].ratekbps) {
		ath_rc_get_nextlowervalid_txrate(rate_table, rate_ctrl,
				(u8) tx_rate, &rate_ctrl->rate_max_phy);

		/* Don't probe for a little while. */
		rate_ctrl->probe_time = now_msec;
	}

	if (state_change) {
		/*
		 * Make sure the rates above this have higher rssi thresholds.
		 * (Note:  Monotonicity is kept within the OFDM rates and
		 *         within the CCK rates. However, no adjustment is
		 *         made to keep the rssi thresholds monotonically
		 *         increasing between the CCK and OFDM rates.)
		 */
		for (rate = tx_rate; rate <
				rate_ctrl->rate_table_size - 1; rate++) {
			if (rate_table->info[rate+1].phy !=
				rate_table->info[tx_rate].phy)
				break;

			if (rate_ctrl->state[rate].rssi_thres +
				rate_table->info[rate].rssi_ack_deltamin >
					rate_ctrl->state[rate+1].rssi_thres) {
				rate_ctrl->state[rate+1].rssi_thres =
					rate_ctrl->state[rate].
						rssi_thres +
					rate_table->info[rate].
						rssi_ack_deltamin;
			}
		}

		/* Make sure the rates below this have lower rssi thresholds. */
		for (rate = tx_rate - 1; rate >= 0; rate--) {
			if (rate_table->info[rate].phy !=
				rate_table->info[tx_rate].phy)
				break;

			if (rate_ctrl->state[rate].rssi_thres +
				rate_table->info[rate].rssi_ack_deltamin >
					rate_ctrl->state[rate+1].rssi_thres) {
				if (rate_ctrl->state[rate+1].rssi_thres <
					rate_table->info[rate].
					rssi_ack_deltamin)
					rate_ctrl->state[rate].rssi_thres = 0;
				else {
					rate_ctrl->state[rate].rssi_thres =
						rate_ctrl->state[rate+1].
							rssi_thres -
							rate_table->info[rate].
							rssi_ack_deltamin;
				}

				if (rate_ctrl->state[rate].rssi_thres <
					rate_table->info[rate].
						rssi_ack_validmin) {
					rate_ctrl->state[rate].rssi_thres =
						rate_table->info[rate].
							rssi_ack_validmin;
				}
			}
		}
	}

	/* Make sure the rates below this have lower PER */
	/* Monotonicity is kept only for rates below the current rate. */
	if (rate_ctrl->state[tx_rate].per < last_per) {
		for (rate = tx_rate - 1; rate >= 0; rate--) {
			if (rate_table->info[rate].phy !=
				rate_table->info[tx_rate].phy)
				break;

			if (rate_ctrl->state[rate].per >
					rate_ctrl->state[rate+1].per) {
				rate_ctrl->state[rate].per =
					rate_ctrl->state[rate+1].per;
			}
		}
	}

	/* Maintain monotonicity for rates above the current rate */
	for (rate = tx_rate; rate < rate_ctrl->rate_table_size - 1; rate++) {
		if (rate_ctrl->state[rate+1].per < rate_ctrl->state[rate].per)
			rate_ctrl->state[rate+1].per =
				rate_ctrl->state[rate].per;
	}

	/* Every so often, we reduce the thresholds and
	 * PER (different for CCK and OFDM). */
	if (now_msec - rate_ctrl->rssi_down_time >=
		rate_table->rssi_reduce_interval) {

		for (rate = 0; rate < rate_ctrl->rate_table_size; rate++) {
			if (rate_ctrl->state[rate].rssi_thres >
				rate_table->info[rate].rssi_ack_validmin)
				rate_ctrl->state[rate].rssi_thres -= 1;
		}
		rate_ctrl->rssi_down_time = now_msec;
	}

	/* Every so often, we reduce the thresholds
	 * and PER (different for CCK and OFDM). */
	if (now_msec - rate_ctrl->per_down_time >=
		rate_table->rssi_reduce_interval) {
		for (rate = 0; rate < rate_ctrl->rate_table_size; rate++) {
			rate_ctrl->state[rate].per =
				7 * rate_ctrl->state[rate].per / 8;
		}

		rate_ctrl->per_down_time = now_msec;
	}
}

/*
 * This routine is called in rate control callback tx_status() to give
 * the status of previous frames.
 */
static void ath_rc_update(struct ath_softc *sc,
			  struct ath_rate_node *ath_rc_priv,
			  struct ath_tx_info_priv *info_priv, int final_ts_idx,
			  int xretries, int long_retry)
{
	struct ath_rate_softc  *asc = (struct ath_rate_softc *)sc->sc_rc;
	struct ath_rate_table *rate_table;
	struct ath_tx_ratectrl *rate_ctrl;
	struct ath_rc_series rcs[4];
	u8 flags;
	u32 series = 0, rix;

	memcpy(rcs, info_priv->rcs, 4 * sizeof(rcs[0]));
	rate_table = (struct ath_rate_table *)
		asc->hw_rate_table[sc->sc_curmode];
	rate_ctrl = (struct ath_tx_ratectrl *)(ath_rc_priv);
	ASSERT(rcs[0].tries != 0);

	/*
	 * If the first rate is not the final index, there
	 * are intermediate rate failures to be processed.
	 */
	if (final_ts_idx != 0) {
		/* Process intermediate rates that failed.*/
		for (series = 0; series < final_ts_idx ; series++) {
			if (rcs[series].tries != 0) {
				flags = rcs[series].flags;
				/* If HT40 and we have switched mode from
				 * 40 to 20 => don't update */
				if ((flags & ATH_RC_CW40_FLAG) &&
					(rate_ctrl->rc_phy_mode !=
					(flags & ATH_RC_CW40_FLAG)))
					return;
				if ((flags & ATH_RC_CW40_FLAG) &&
					(flags & ATH_RC_SGI_FLAG))
					rix = rate_table->info[
						rcs[series].rix].ht_index;
				else if (flags & ATH_RC_SGI_FLAG)
					rix = rate_table->info[
						rcs[series].rix].sgi_index;
				else if (flags & ATH_RC_CW40_FLAG)
					rix = rate_table->info[
						rcs[series].rix].cw40index;
				else
					rix = rate_table->info[
						rcs[series].rix].base_index;
				ath_rc_update_ht(sc, ath_rc_priv,
						info_priv, rix,
						xretries ? 1 : 2,
						rcs[series].tries);
			}
		}
	} else {
		/*
		 * Handle the special case of MIMO PS burst, where the second
		 * aggregate is sent out with only one rate and one try.
		 * Treating it as an excessive retry penalizes the rate
		 * inordinately.
		 */
		if (rcs[0].tries == 1 && xretries == 1)
			xretries = 2;
	}

	flags = rcs[series].flags;
	/* If HT40 and we have switched mode from 40 to 20 => don't update */
	if ((flags & ATH_RC_CW40_FLAG) &&
		(rate_ctrl->rc_phy_mode != (flags & ATH_RC_CW40_FLAG)))
		return;

	if ((flags & ATH_RC_CW40_FLAG) && (flags & ATH_RC_SGI_FLAG))
		rix = rate_table->info[rcs[series].rix].ht_index;
	else if (flags & ATH_RC_SGI_FLAG)
		rix = rate_table->info[rcs[series].rix].sgi_index;
	else if (flags & ATH_RC_CW40_FLAG)
		rix = rate_table->info[rcs[series].rix].cw40index;
	else
		rix = rate_table->info[rcs[series].rix].base_index;

	ath_rc_update_ht(sc, ath_rc_priv, info_priv, rix,
		xretries, long_retry);
}


/*
 * Process a tx descriptor for a completed transmit (success or failure).
 */
static void ath_rate_tx_complete(struct ath_softc *sc,
				 struct ath_node *an,
				 struct ath_rate_node *rc_priv,
				 struct ath_tx_info_priv *info_priv)
{
	int final_ts_idx = info_priv->tx.ts_rateindex;
	int tx_status = 0, is_underrun = 0;
	struct ath_vap *avp;

	avp = rc_priv->avp;
	if ((avp->av_config.av_fixed_rateset != IEEE80211_FIXED_RATE_NONE)
			|| info_priv->tx.ts_status & ATH9K_TXERR_FILT)
		return;

	if (info_priv->tx.ts_rssi > 0) {
		ATH_RSSI_LPF(an->an_chainmask_sel.tx_avgrssi,
				info_priv->tx.ts_rssi);
	}

	/*
	 * If underrun error is seen assume it as an excessive retry only
	 * if prefetch trigger level have reached the max (0x3f for 5416)
	 * Adjust the long retry as if the frame was tried ATH_11N_TXMAXTRY
	 * times. This affects how ratectrl updates PER for the failed rate.
	 */
	if (info_priv->tx.ts_flags &
		(ATH9K_TX_DATA_UNDERRUN | ATH9K_TX_DELIM_UNDERRUN) &&
		((sc->sc_ah->ah_txTrigLevel) >= tx_triglevel_max)) {
		tx_status = 1;
		is_underrun = 1;
	}

	if ((info_priv->tx.ts_status & ATH9K_TXERR_XRETRY) ||
			(info_priv->tx.ts_status & ATH9K_TXERR_FIFO))
		tx_status = 1;

	ath_rc_update(sc, rc_priv, info_priv, final_ts_idx, tx_status,
		      (is_underrun) ? ATH_11N_TXMAXTRY :
		      info_priv->tx.ts_longretry);
}


/*
 *  Update the SIB's rate control information
 *
 *  This should be called when the supported rates change
 *  (e.g. SME operation, wireless mode change)
 *
 *  It will determine which rates are valid for use.
 */
static void ath_rc_sib_update(struct ath_softc *sc,
			      struct ath_rate_node *ath_rc_priv,
			      u32 capflag, int keep_state,
			      struct ath_rateset *negotiated_rates,
			      struct ath_rateset *negotiated_htrates)
{
	struct ath_rate_table *rate_table = NULL;
	struct ath_rate_softc *asc = (struct ath_rate_softc *)sc->sc_rc;
	struct ath_rateset *rateset = negotiated_rates;
	u8 *ht_mcs = (u8 *)negotiated_htrates;
	struct ath_tx_ratectrl *rate_ctrl  = (struct ath_tx_ratectrl *)
		(ath_rc_priv);
	u8 i, j, k, hi = 0, hthi = 0;

	rate_table = (struct ath_rate_table *)
		asc->hw_rate_table[sc->sc_curmode];

	/* Initial rate table size. Will change depending
	 * on the working rate set */
	rate_ctrl->rate_table_size = MAX_TX_RATE_TBL;

	/* Initialize thresholds according to the global rate table */
	for (i = 0 ; (i < rate_ctrl->rate_table_size) && (!keep_state); i++) {
		rate_ctrl->state[i].rssi_thres =
			rate_table->info[i].rssi_ack_validmin;
		rate_ctrl->state[i].per = 0;
	}

	/* Determine the valid rates */
	ath_rc_init_valid_txmask(rate_ctrl);

	for (i = 0; i < WLAN_RC_PHY_MAX; i++) {
		for (j = 0; j < MAX_TX_RATE_PHY; j++)
			rate_ctrl->valid_phy_rateidx[i][j] = 0;
		rate_ctrl->valid_phy_ratecnt[i] = 0;
	}
	rate_ctrl->rc_phy_mode = (capflag & WLAN_RC_40_FLAG);

	/* Set stream capability */
	ath_rc_priv->single_stream = (capflag & WLAN_RC_DS_FLAG) ? 0 : 1;

	if (!rateset->rs_nrates) {
		/* No working rate, just initialize valid rates */
		hi = ath_rc_sib_init_validrates(ath_rc_priv, rate_table,
						capflag);
	} else {
		/* Use intersection of working rates and valid rates */
		hi = ath_rc_sib_setvalid_rates(ath_rc_priv, rate_table,
					       rateset, capflag);
		if (capflag & WLAN_RC_HT_FLAG) {
			hthi = ath_rc_sib_setvalid_htrates(ath_rc_priv,
							   rate_table,
							   ht_mcs,
							   capflag);
		}
		hi = A_MAX(hi, hthi);
	}

	rate_ctrl->rate_table_size = hi + 1;
	rate_ctrl->rate_max_phy = 0;
	ASSERT(rate_ctrl->rate_table_size <= MAX_TX_RATE_TBL);

	for (i = 0, k = 0; i < WLAN_RC_PHY_MAX; i++) {
		for (j = 0; j < rate_ctrl->valid_phy_ratecnt[i]; j++) {
			rate_ctrl->valid_rate_index[k++] =
				rate_ctrl->valid_phy_rateidx[i][j];
		}

		if (!ath_rc_valid_phyrate(i, rate_table->initial_ratemax, TRUE)
		    || !rate_ctrl->valid_phy_ratecnt[i])
			continue;

		rate_ctrl->rate_max_phy = rate_ctrl->valid_phy_rateidx[i][j-1];
	}
	ASSERT(rate_ctrl->rate_table_size <= MAX_TX_RATE_TBL);
	ASSERT(k <= MAX_TX_RATE_TBL);

	rate_ctrl->max_valid_rate = k;
	/*
	 * Some third party vendors don't send the supported rate series in
	 * order. So sorting to make sure its in order, otherwise our RateFind
	 * Algo will select wrong rates
	 */
	ath_rc_sort_validrates(rate_table, rate_ctrl);
	rate_ctrl->rate_max_phy = rate_ctrl->valid_rate_index[k-4];
}

/*
 * Update rate-control state on station associate/reassociate.
 */
static int ath_rate_newassoc(struct ath_softc *sc,
			     struct ath_rate_node *ath_rc_priv,
			     unsigned int capflag,
			     struct ath_rateset *negotiated_rates,
			     struct ath_rateset *negotiated_htrates)
{


	ath_rc_priv->ht_cap =
		((capflag & ATH_RC_DS_FLAG) ? WLAN_RC_DS_FLAG : 0) |
		((capflag & ATH_RC_SGI_FLAG) ? WLAN_RC_SGI_FLAG : 0) |
		((capflag & ATH_RC_HT_FLAG)  ? WLAN_RC_HT_FLAG : 0) |
		((capflag & ATH_RC_CW40_FLAG) ? WLAN_RC_40_FLAG : 0);

	ath_rc_sib_update(sc, ath_rc_priv, ath_rc_priv->ht_cap, 0,
			  negotiated_rates, negotiated_htrates);

	return 0;
}

/*
 *  This routine is called to initialize the rate control parameters
 *  in the SIB. It is called initially during system initialization
 *  or when a station is associated with the AP.
 */
static void ath_rc_sib_init(struct ath_rate_node *ath_rc_priv)
{
	struct ath_tx_ratectrl *rate_ctrl;

	rate_ctrl = (struct ath_tx_ratectrl *)(ath_rc_priv);
	rate_ctrl->rssi_down_time = jiffies_to_msecs(jiffies);
}


static void ath_setup_rates(struct ieee80211_local *local, struct sta_info *sta)

{
	struct ieee80211_supported_band *sband;
	struct ieee80211_hw *hw = local_to_hw(local);
	struct ath_softc *sc = hw->priv;
	struct ath_rate_node *rc_priv = sta->rate_ctrl_priv;
	int i, j = 0;

	DPRINTF(sc, ATH_DBG_RATE, "%s", __func__);
	sband =  local->hw.wiphy->bands[local->hw.conf.channel->band];
	for (i = 0; i < sband->n_bitrates; i++) {
		if (sta->supp_rates[local->hw.conf.channel->band] & BIT(i)) {
			rc_priv->neg_rates.rs_rates[j]
				= (sband->bitrates[i].bitrate * 2) / 10;
			j++;
		}
	}
	rc_priv->neg_rates.rs_nrates = j;
}

void ath_rc_node_update(struct ieee80211_hw *hw, struct ath_rate_node *rc_priv)
{
	struct ath_softc *sc = hw->priv;
	u32 capflag = 0;

	if (hw->conf.ht_conf.ht_supported) {
		capflag |= ATH_RC_HT_FLAG | ATH_RC_DS_FLAG;
		if (sc->sc_ht_info.tx_chan_width == ATH9K_HT_MACMODE_2040)
			capflag |= ATH_RC_CW40_FLAG;
	}

	ath_rate_newassoc(sc, rc_priv, capflag,
			  &rc_priv->neg_rates,
			  &rc_priv->neg_ht_rates);

}

/* Rate Control callbacks */
static void ath_tx_status(void *priv, struct net_device *dev,
			  struct sk_buff *skb)
{
	struct ath_softc *sc = priv;
	struct ath_tx_info_priv *tx_info_priv;
	struct ath_node *an;
	struct sta_info *sta;
	struct ieee80211_local *local;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr;
	__le16 fc;

	local = hw_to_local(sc->hw);
	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->driver_data[0];

	spin_lock_bh(&sc->node_lock);
	an = ath_node_find(sc, hdr->addr1);
	spin_unlock_bh(&sc->node_lock);

	sta = sta_info_get(local, hdr->addr1);
	if (!an || !sta || !ieee80211_is_data(fc)) {
		if (tx_info->driver_data[0] != NULL) {
			kfree(tx_info->driver_data[0]);
			tx_info->driver_data[0] = NULL;
		}
		return;
	}
	if (tx_info->driver_data[0] != NULL) {
		ath_rate_tx_complete(sc, an, sta->rate_ctrl_priv, tx_info_priv);
		kfree(tx_info->driver_data[0]);
		tx_info->driver_data[0] = NULL;
	}
}

static void ath_tx_aggr_resp(struct ath_softc *sc,
			     struct sta_info *sta,
			     struct ath_node *an,
			     u8 tidno)
{
	struct ieee80211_hw *hw = sc->hw;
	struct ieee80211_local *local;
	struct ath_atx_tid *txtid;
	struct ieee80211_supported_band *sband;
	u16 buffersize = 0;
	int state;
	DECLARE_MAC_BUF(mac);

	if (!sc->sc_txaggr)
		return;

	txtid = ATH_AN_2_TID(an, tidno);
	if (!txtid->paused)
		return;

	local = hw_to_local(sc->hw);
	sband = hw->wiphy->bands[hw->conf.channel->band];
	buffersize = IEEE80211_MIN_AMPDU_BUF <<
		sband->ht_info.ampdu_factor; /* FIXME */
	state = sta->ampdu_mlme.tid_state_tx[tidno];

	if (state & HT_ADDBA_RECEIVED_MSK) {
		txtid->addba_exchangecomplete = 1;
		txtid->addba_exchangeinprogress = 0;
		txtid->baw_size = buffersize;

		DPRINTF(sc, ATH_DBG_AGGR,
			"%s: Resuming tid, buffersize: %d\n",
			__func__,
			buffersize);

		ath_tx_resume_tid(sc, txtid);
	}
}

static void ath_get_rate(void *priv, struct net_device *dev,
			 struct ieee80211_supported_band *sband,
			 struct sk_buff *skb,
			 struct rate_selection *sel)
{
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_local *local = wdev_priv(dev->ieee80211_ptr);
	struct sta_info *sta;
	struct ath_softc *sc = (struct ath_softc *)priv;
	struct ieee80211_hw *hw = sc->hw;
	struct ath_tx_info_priv *tx_info_priv;
	struct ath_rate_node *ath_rc_priv;
	struct ath_node *an;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	int is_probe, chk, ret;
	s8 lowest_idx;
	__le16 fc = hdr->frame_control;
	u8 *qc, tid;
	DECLARE_MAC_BUF(mac);

	DPRINTF(sc, ATH_DBG_RATE, "%s\n", __func__);

	/* allocate driver private area of tx_info */
	tx_info->driver_data[0] = kzalloc(sizeof(*tx_info_priv), GFP_ATOMIC);
	ASSERT(tx_info->driver_data[0] != NULL);
	tx_info_priv = (struct ath_tx_info_priv *)tx_info->driver_data[0];

	sta = sta_info_get(local, hdr->addr1);
	lowest_idx = rate_lowest_index(local, sband, sta);
	tx_info_priv->min_rate = (sband->bitrates[lowest_idx].bitrate * 2) / 10;
	/* lowest rate for management and multicast/broadcast frames */
	if (!ieee80211_is_data(fc) ||
			is_multicast_ether_addr(hdr->addr1) || !sta) {
		sel->rate_idx = lowest_idx;
		return;
	}

	ath_rc_priv = sta->rate_ctrl_priv;

	/* Find tx rate for unicast frames */
	ath_rate_findrate(sc, ath_rc_priv,
			  ATH_11N_TXMAXTRY, 4,
			  ATH_RC_PROBE_ALLOWED,
			  tx_info_priv->rcs,
			  &is_probe,
			  false);
	if (is_probe)
		sel->probe_idx = ((struct ath_tx_ratectrl *)
			sta->rate_ctrl_priv)->probe_rate;

	/* Ratecontrol sometimes returns invalid rate index */
	if (tx_info_priv->rcs[0].rix != 0xff)
		ath_rc_priv->prev_data_rix = tx_info_priv->rcs[0].rix;
	else
		tx_info_priv->rcs[0].rix = ath_rc_priv->prev_data_rix;

	sel->rate_idx = tx_info_priv->rcs[0].rix;

	/* Check if aggregation has to be enabled for this tid */

	if (hw->conf.ht_conf.ht_supported) {
		if (ieee80211_is_data_qos(fc)) {
			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;

			spin_lock_bh(&sc->node_lock);
			an = ath_node_find(sc, hdr->addr1);
			spin_unlock_bh(&sc->node_lock);

			if (!an) {
				DPRINTF(sc, ATH_DBG_AGGR,
					"%s: Node not found to "
					"init/chk TX aggr\n", __func__);
				return;
			}

			chk = ath_tx_aggr_check(sc, an, tid);
			if (chk == AGGR_REQUIRED) {
				ret = ieee80211_start_tx_ba_session(hw,
					hdr->addr1, tid);
				if (ret)
					DPRINTF(sc, ATH_DBG_AGGR,
						"%s: Unable to start tx "
						"aggr for: %s\n",
						__func__,
						print_mac(mac, hdr->addr1));
				else
					DPRINTF(sc, ATH_DBG_AGGR,
						"%s: Started tx aggr for: %s\n",
						__func__,
						print_mac(mac, hdr->addr1));
			} else if (chk == AGGR_EXCHANGE_PROGRESS)
				ath_tx_aggr_resp(sc, sta, an, tid);
		}
	}
}

static void ath_rate_init(void *priv, void *priv_sta,
			  struct ieee80211_local *local,
			  struct sta_info *sta)
{
	struct ieee80211_supported_band *sband;
	struct ieee80211_hw *hw = local_to_hw(local);
	struct ieee80211_conf *conf = &local->hw.conf;
	struct ath_softc *sc = hw->priv;
	int i, j = 0;

	DPRINTF(sc, ATH_DBG_RATE, "%s\n", __func__);

	sband = local->hw.wiphy->bands[local->hw.conf.channel->band];
	sta->txrate_idx = rate_lowest_index(local, sband, sta);

	ath_setup_rates(local, sta);
	if (conf->flags & IEEE80211_CONF_SUPPORT_HT_MODE) {
		for (i = 0; i < MCS_SET_SIZE; i++) {
			if (conf->ht_conf.supp_mcs_set[i/8] & (1<<(i%8)))
				((struct ath_rate_node *)
				priv_sta)->neg_ht_rates.rs_rates[j++] = i;
			if (j == ATH_RATE_MAX)
				break;
		}
		((struct ath_rate_node *)priv_sta)->neg_ht_rates.rs_nrates = j;
	}
	ath_rc_node_update(hw, priv_sta);
}

static void ath_rate_clear(void *priv)
{
	return;
}

static void *ath_rate_alloc(struct ieee80211_local *local)
{
	struct ieee80211_hw *hw = local_to_hw(local);
	struct ath_softc *sc = hw->priv;

	DPRINTF(sc, ATH_DBG_RATE, "%s", __func__);
	return local->hw.priv;
}

static void ath_rate_free(void *priv)
{
	return;
}

static void *ath_rate_alloc_sta(void *priv, gfp_t gfp)
{
	struct ath_softc *sc = priv;
	struct ath_vap *avp = sc->sc_vaps[0];
	struct ath_rate_node *rate_priv;

	DPRINTF(sc, ATH_DBG_RATE, "%s", __func__);
	rate_priv = ath_rate_node_alloc(avp, sc->sc_rc, gfp);
	if (!rate_priv) {
		DPRINTF(sc, ATH_DBG_FATAL, "%s:Unable to allocate"
				"private rate control structure", __func__);
		return NULL;
	}
	ath_rc_sib_init(rate_priv);
	return rate_priv;
}

static void ath_rate_free_sta(void *priv, void *priv_sta)
{
	struct ath_rate_node *rate_priv = priv_sta;
	struct ath_softc *sc = priv;

	DPRINTF(sc, ATH_DBG_RATE, "%s", __func__);
	ath_rate_node_free(rate_priv);
}

static struct rate_control_ops ath_rate_ops = {
	.module = NULL,
	.name = "ath9k_rate_control",
	.tx_status = ath_tx_status,
	.get_rate = ath_get_rate,
	.rate_init = ath_rate_init,
	.clear = ath_rate_clear,
	.alloc = ath_rate_alloc,
	.free = ath_rate_free,
	.alloc_sta = ath_rate_alloc_sta,
	.free_sta = ath_rate_free_sta
};

int ath_rate_control_register(void)
{
	return ieee80211_rate_control_register(&ath_rate_ops);
}

void ath_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&ath_rate_ops);
}

