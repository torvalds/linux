/*
 * Copyright (c) 2004 Video54 Technologies, Inc.
 * Copyright (c) 2004-2009 Atheros Communications, Inc.
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

#include "ath9k.h"

static struct ath_rate_table ar5416_11na_ratetable = {
	42,
	{
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, 12,
			0, 2, 1, 0, 0, 0, 0, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800,  0x0f, 0x00, 18,
			0, 3, 1, 1, 1, 1, 1, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10000, 0x0a, 0x00, 24,
			2, 4, 2, 2, 2, 2, 2, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			13900, 0x0e, 0x00, 36,
			2, 6,  2, 3, 3, 3, 3, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17300, 0x09, 0x00, 48,
			4, 10, 3, 4, 4, 4, 4, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23000, 0x0d, 0x00, 72,
			4, 14, 3, 5, 5, 5, 5, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			4, 20, 3, 6, 6, 6, 6, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
			29300, 0x0c, 0x00, 108,
			4, 23, 3, 7, 7, 7, 7, 0 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 6500, /* 6.5 Mb */
			6400, 0x80, 0x00, 0,
			0, 2, 3, 8, 24, 8, 24, 3216 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 13000, /* 13 Mb */
			12700, 0x81, 0x00, 1,
			2, 4, 3, 9, 25, 9, 25, 6434 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 19500, /* 19.5 Mb */
			18800, 0x82, 0x00, 2,
			2, 6, 3, 10, 26, 10, 26, 9650 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 26000, /* 26 Mb */
			25000, 0x83, 0x00, 3,
			4, 10, 3, 11, 27, 11, 27, 12868 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 39000, /* 39 Mb */
			36700, 0x84, 0x00, 4,
			4, 14, 3, 12, 28, 12, 28, 19304 },
		{ INVALID, VALID_20, WLAN_RC_PHY_HT_20_SS, 52000, /* 52 Mb */
			48100, 0x85, 0x00, 5,
			4, 20, 3, 13, 29, 13, 29, 25740 },
		{ INVALID, VALID_20, WLAN_RC_PHY_HT_20_SS, 58500, /* 58.5 Mb */
			53500, 0x86, 0x00, 6,
			4, 23, 3, 14, 30, 14, 30,  28956 },
		{ INVALID, VALID_20, WLAN_RC_PHY_HT_20_SS, 65000, /* 65 Mb */
			59000, 0x87, 0x00, 7,
			4, 25, 3, 15, 31, 15, 32, 32180 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_20_DS, 13000, /* 13 Mb */
			12700, 0x88, 0x00,
			8, 0, 2, 3, 16, 33, 16, 33, 6430 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_20_DS, 26000, /* 26 Mb */
			24800, 0x89, 0x00, 9,
			2, 4, 3, 17, 34, 17, 34, 12860 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_20_DS, 39000, /* 39 Mb */
			36600, 0x8a, 0x00, 10,
			2, 6, 3, 18, 35, 18, 35, 19300 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 52000, /* 52 Mb */
			48100, 0x8b, 0x00, 11,
			4, 10, 3, 19, 36, 19, 36, 25736 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 78000, /* 78 Mb */
			69500, 0x8c, 0x00, 12,
			4, 14, 3, 20, 37, 20, 37, 38600 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 104000, /* 104 Mb */
			89500, 0x8d, 0x00, 13,
			4, 20, 3, 21, 38, 21, 38, 51472 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 117000, /* 117 Mb */
			98900, 0x8e, 0x00, 14,
			4, 23, 3, 22, 39, 22, 39, 57890 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 130000, /* 130 Mb */
			108300, 0x8f, 0x00, 15,
			4, 25, 3, 23, 40, 23, 41, 64320 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 13500, /* 13.5 Mb */
			13200, 0x80, 0x00, 0,
			0, 2, 3, 8, 24, 24, 24, 6684 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 27500, /* 27.0 Mb */
			25900, 0x81, 0x00, 1,
			2, 4, 3, 9, 25, 25, 25, 13368 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 40500, /* 40.5 Mb */
			38600, 0x82, 0x00, 2,
			2, 6, 3, 10, 26, 26, 26, 20052 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 54000, /* 54 Mb */
			49800, 0x83, 0x00, 3,
			4, 10, 3, 11, 27, 27, 27, 26738 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 81500, /* 81 Mb */
			72200, 0x84, 0x00, 4,
			4, 14, 3, 12, 28, 28, 28, 40104 },
		{ INVALID, VALID_40, WLAN_RC_PHY_HT_40_SS, 108000, /* 108 Mb */
			92900, 0x85, 0x00, 5,
			4, 20, 3, 13, 29, 29, 29, 53476 },
		{ INVALID, VALID_40, WLAN_RC_PHY_HT_40_SS, 121500, /* 121.5 Mb */
			102700, 0x86, 0x00, 6,
			4, 23, 3, 14, 30, 30, 30, 60156 },
		{ INVALID, VALID_40, WLAN_RC_PHY_HT_40_SS, 135000, /* 135 Mb */
			112000, 0x87, 0x00, 7,
			4, 25, 3, 15, 31, 32, 32, 66840 },
		{ INVALID, VALID_40, WLAN_RC_PHY_HT_40_SS_HGI, 150000, /* 150 Mb */
			122000, 0x87, 0x00, 7,
			4, 25, 3, 15, 31, 32, 32, 74200 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_40_DS, 27000, /* 27 Mb */
			25800, 0x88, 0x00, 8,
			0, 2, 3, 16, 33, 33, 33, 13360 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_40_DS, 54000, /* 54 Mb */
			49800, 0x89, 0x00, 9,
			2, 4, 3, 17, 34, 34, 34, 26720 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_40_DS, 81000, /* 81 Mb */
			71900, 0x8a, 0x00, 10,
			2, 6, 3, 18, 35, 35, 35, 40080 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 108000, /* 108 Mb */
			92500, 0x8b, 0x00, 11,
			4, 10, 3, 19, 36, 36, 36, 53440 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 162000, /* 162 Mb */
			130300, 0x8c, 0x00, 12,
			4, 14, 3, 20, 37, 37, 37, 80160 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 216000, /* 216 Mb */
			162800, 0x8d, 0x00, 13,
			4, 20, 3, 21, 38, 38, 38, 106880 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 243000, /* 243 Mb */
			178200, 0x8e, 0x00, 14,
			4, 23, 3, 22, 39, 39, 39, 120240 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 270000, /* 270 Mb */
			192100, 0x8f, 0x00, 15,
			4, 25, 3, 23, 40, 41, 41, 133600 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS_HGI, 300000, /* 300 Mb */
			207000, 0x8f, 0x00, 15,
			4, 25, 3, 23, 40, 41, 41, 148400 },
	},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	WLAN_RC_HT_FLAG,  /* Phy rates allowed initially */
};

/* 4ms frame limit not used for NG mode.  The values filled
 * for HT are the 64K max aggregate limit */

static struct ath_rate_table ar5416_11ng_ratetable = {
	46,
	{
		{ VALID_ALL, VALID_ALL, WLAN_RC_PHY_CCK, 1000, /* 1 Mb */
			900, 0x1b, 0x00, 2,
			0, 0, 1, 0, 0, 0, 0, 0 },
		{ VALID_ALL, VALID_ALL, WLAN_RC_PHY_CCK, 2000, /* 2 Mb */
			1900, 0x1a, 0x04, 4,
			1, 1, 1, 1, 1, 1, 1, 0 },
		{ VALID_ALL, VALID_ALL, WLAN_RC_PHY_CCK, 5500, /* 5.5 Mb */
			4900, 0x19, 0x04, 11,
			2, 2, 2, 2, 2, 2, 2, 0 },
		{ VALID_ALL, VALID_ALL, WLAN_RC_PHY_CCK, 11000, /* 11 Mb */
			8100, 0x18, 0x04, 22,
			3, 3, 2, 3, 3, 3, 3, 0 },
		{ INVALID, INVALID, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, 12,
			4, 2, 1, 4, 4, 4, 4, 0 },
		{ INVALID, INVALID, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800, 0x0f, 0x00, 18,
			4, 3, 1, 5, 5, 5, 5, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10100, 0x0a, 0x00, 24,
			6, 4, 1, 6, 6, 6, 6, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			14100,  0x0e, 0x00, 36,
			6, 6, 2, 7, 7, 7, 7, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17700, 0x09, 0x00, 48,
			8, 10, 3, 8, 8, 8, 8, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23700, 0x0d, 0x00, 72,
			8, 14, 3, 9, 9, 9, 9, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			8, 20, 3, 10, 10, 10, 10, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
			30900, 0x0c, 0x00, 108,
			8, 23, 3, 11, 11, 11, 11, 0 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_20_SS, 6500, /* 6.5 Mb */
			6400, 0x80, 0x00, 0,
			4, 2, 3, 12, 28, 12, 28, 3216 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 13000, /* 13 Mb */
			12700, 0x81, 0x00, 1,
			6, 4, 3, 13, 29, 13, 29, 6434 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 19500, /* 19.5 Mb */
			18800, 0x82, 0x00, 2,
			6, 6, 3, 14, 30, 14, 30, 9650 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 26000, /* 26 Mb */
			25000, 0x83, 0x00, 3,
			8, 10, 3, 15, 31, 15, 31, 12868 },
		{ VALID_20, VALID_20, WLAN_RC_PHY_HT_20_SS, 39000, /* 39 Mb */
			36700, 0x84, 0x00, 4,
			8, 14, 3, 16, 32, 16, 32, 19304 },
		{ INVALID, VALID_20, WLAN_RC_PHY_HT_20_SS, 52000, /* 52 Mb */
			48100, 0x85, 0x00, 5,
			8, 20, 3, 17, 33, 17, 33, 25740 },
		{ INVALID,  VALID_20, WLAN_RC_PHY_HT_20_SS, 58500, /* 58.5 Mb */
			53500, 0x86, 0x00, 6,
			8, 23, 3, 18, 34, 18, 34, 28956 },
		{ INVALID, VALID_20, WLAN_RC_PHY_HT_20_SS, 65000, /* 65 Mb */
			59000, 0x87, 0x00, 7,
			8, 25, 3, 19, 35, 19, 36, 32180 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_20_DS, 13000, /* 13 Mb */
			12700, 0x88, 0x00, 8,
			4, 2, 3, 20, 37, 20, 37, 6430 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_20_DS, 26000, /* 26 Mb */
			24800, 0x89, 0x00, 9,
			6, 4, 3, 21, 38, 21, 38, 12860 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_20_DS, 39000, /* 39 Mb */
			36600, 0x8a, 0x00, 10,
			6, 6, 3, 22, 39, 22, 39, 19300 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 52000, /* 52 Mb */
			48100, 0x8b, 0x00, 11,
			8, 10, 3, 23, 40, 23, 40, 25736 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 78000, /* 78 Mb */
			69500, 0x8c, 0x00, 12,
			8, 14, 3, 24, 41, 24, 41, 38600 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 104000, /* 104 Mb */
			89500, 0x8d, 0x00, 13,
			8, 20, 3, 25, 42, 25, 42, 51472 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 117000, /* 117 Mb */
			98900, 0x8e, 0x00, 14,
			8, 23, 3, 26, 43, 26, 44, 57890 },
		{ VALID_20, INVALID, WLAN_RC_PHY_HT_20_DS, 130000, /* 130 Mb */
			108300, 0x8f, 0x00, 15,
			8, 25, 3, 27, 44, 27, 45, 64320 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 13500, /* 13.5 Mb */
			13200, 0x80, 0x00, 0,
			8, 2, 3, 12, 28, 28, 28, 6684 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 27500, /* 27.0 Mb */
			25900, 0x81, 0x00, 1,
			8, 4, 3, 13, 29, 29, 29, 13368 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 40500, /* 40.5 Mb */
			38600, 0x82, 0x00, 2,
			8, 6, 3, 14, 30, 30, 30, 20052 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 54000, /* 54 Mb */
			49800, 0x83, 0x00, 3,
			8, 10, 3, 15, 31, 31, 31, 26738 },
		{ VALID_40, VALID_40, WLAN_RC_PHY_HT_40_SS, 81500, /* 81 Mb */
			72200, 0x84, 0x00, 4,
			8, 14, 3, 16, 32, 32, 32, 40104 },
		{ INVALID, VALID_40, WLAN_RC_PHY_HT_40_SS, 108000, /* 108 Mb */
			92900, 0x85, 0x00, 5,
			8, 20, 3, 17, 33, 33, 33, 53476 },
		{ INVALID,  VALID_40, WLAN_RC_PHY_HT_40_SS, 121500, /* 121.5 Mb */
			102700, 0x86, 0x00, 6,
			8, 23, 3, 18, 34, 34, 34, 60156 },
		{ INVALID, VALID_40, WLAN_RC_PHY_HT_40_SS, 135000, /* 135 Mb */
			112000, 0x87, 0x00, 7,
			8, 23, 3, 19, 35, 36, 36, 66840 },
		{ INVALID, VALID_40, WLAN_RC_PHY_HT_40_SS_HGI, 150000, /* 150 Mb */
			122000, 0x87, 0x00, 7,
			8, 25, 3, 19, 35, 36, 36, 74200 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_40_DS, 27000, /* 27 Mb */
			25800, 0x88, 0x00, 8,
			8, 2, 3, 20, 37, 37, 37, 13360 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_40_DS, 54000, /* 54 Mb */
			49800, 0x89, 0x00, 9,
			8, 4, 3, 21, 38, 38, 38, 26720 },
		{ INVALID, INVALID, WLAN_RC_PHY_HT_40_DS, 81000, /* 81 Mb */
			71900, 0x8a, 0x00, 10,
			8, 6, 3, 22, 39, 39, 39, 40080 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 108000, /* 108 Mb */
			92500, 0x8b, 0x00, 11,
			8, 10, 3, 23, 40, 40, 40, 53440 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 162000, /* 162 Mb */
			130300, 0x8c, 0x00, 12,
			8, 14, 3, 24, 41, 41, 41, 80160 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 216000, /* 216 Mb */
			162800, 0x8d, 0x00, 13,
			8, 20, 3, 25, 42, 42, 42, 106880 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 243000, /* 243 Mb */
			178200, 0x8e, 0x00, 14,
			8, 23, 3, 26, 43, 43, 43, 120240 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS, 270000, /* 270 Mb */
			192100, 0x8f, 0x00, 15,
			8, 23, 3, 27, 44, 45, 45, 133600 },
		{ VALID_40, INVALID, WLAN_RC_PHY_HT_40_DS_HGI, 300000, /* 300 Mb */
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
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, (0x80|12),
			0, 2, 1, 0, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800, 0x0f, 0x00, 18,
			0, 3, 1, 1, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10000, 0x0a, 0x00, (0x80|24),
			2, 4, 2, 2, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			13900, 0x0e, 0x00, 36,
			2, 6, 2, 3, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17300, 0x09, 0x00, (0x80|48),
			4, 10, 3, 4, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23000, 0x0d, 0x00, 72,
			4, 14, 3, 5, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			4, 19, 3, 6, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
			29300, 0x0c, 0x00, 108,
			4, 23, 3, 7, 0 },
	},
	50,  /* probe interval */
	50,  /* rssi reduce interval */
	0,   /* Phy rates allowed initially */
};

static struct ath_rate_table ar5416_11g_ratetable = {
	12,
	{
		{ VALID, VALID, WLAN_RC_PHY_CCK, 1000, /* 1 Mb */
			900, 0x1b, 0x00, 2,
			0, 0, 1, 0, 0 },
		{ VALID, VALID, WLAN_RC_PHY_CCK, 2000, /* 2 Mb */
			1900, 0x1a, 0x04, 4,
			1, 1, 1, 1, 0 },
		{ VALID, VALID, WLAN_RC_PHY_CCK, 5500, /* 5.5 Mb */
			4900, 0x19, 0x04, 11,
			2, 2, 2, 2, 0 },
		{ VALID, VALID, WLAN_RC_PHY_CCK, 11000, /* 11 Mb */
			8100, 0x18, 0x04, 22,
			3, 3, 2, 3, 0 },
		{ INVALID, INVALID, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0x0b, 0x00, 12,
			4, 2, 1, 4, 0 },
		{ INVALID, INVALID, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800, 0x0f, 0x00, 18,
			4, 3, 1, 5, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10000, 0x0a, 0x00, 24,
			6, 4, 1, 6, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			13900, 0x0e, 0x00, 36,
			6, 6, 2, 7, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17300, 0x09, 0x00, 48,
			8, 10, 3, 8, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23000, 0x0d, 0x00, 72,
			8, 14, 3, 9, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 0x08, 0x00, 96,
			8, 19, 3, 10, 0 },
		{ VALID, VALID, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
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
		{ VALID, VALID, WLAN_RC_PHY_CCK, 1000, /* 1 Mb */
			900, 0x1b,  0x00, (0x80|2),
			0, 0, 1, 0, 0 },
		{ VALID, VALID, WLAN_RC_PHY_CCK, 2000, /* 2 Mb */
			1800, 0x1a, 0x04, (0x80|4),
			1, 1, 1, 1, 0 },
		{ VALID, VALID, WLAN_RC_PHY_CCK, 5500, /* 5.5 Mb */
			4300, 0x19, 0x04, (0x80|11),
			1, 2, 2, 2, 0 },
		{ VALID, VALID, WLAN_RC_PHY_CCK, 11000, /* 11 Mb */
			7100, 0x18, 0x04, (0x80|22),
			1, 4, 100, 3, 0 },
	},
	100, /* probe interval */
	100, /* rssi reduce interval */
	0,   /* Phy rates allowed initially */
};

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

static void ath_rc_sort_validrates(struct ath_rate_table *rate_table,
				   struct ath_rate_priv *ath_rc_priv)
{
	u8 i, j, idx, idx_next;

	for (i = ath_rc_priv->max_valid_rate - 1; i > 0; i--) {
		for (j = 0; j <= i-1; j++) {
			idx = ath_rc_priv->valid_rate_index[j];
			idx_next = ath_rc_priv->valid_rate_index[j+1];

			if (rate_table->info[idx].ratekbps >
				rate_table->info[idx_next].ratekbps) {
				ath_rc_priv->valid_rate_index[j] = idx_next;
				ath_rc_priv->valid_rate_index[j+1] = idx;
			}
		}
	}
}

static void ath_rc_init_valid_txmask(struct ath_rate_priv *ath_rc_priv)
{
	u8 i;

	for (i = 0; i < ath_rc_priv->rate_table_size; i++)
		ath_rc_priv->valid_rate_index[i] = 0;
}

static inline void ath_rc_set_valid_txmask(struct ath_rate_priv *ath_rc_priv,
					   u8 index, int valid_tx_rate)
{
	ASSERT(index <= ath_rc_priv->rate_table_size);
	ath_rc_priv->valid_rate_index[index] = valid_tx_rate ? 1 : 0;
}

static inline int ath_rc_isvalid_txmask(struct ath_rate_priv *ath_rc_priv,
					u8 index)
{
	ASSERT(index <= ath_rc_priv->rate_table_size);
	return ath_rc_priv->valid_rate_index[index];
}

static inline int ath_rc_get_nextvalid_txrate(struct ath_rate_table *rate_table,
					      struct ath_rate_priv *ath_rc_priv,
					      u8 cur_valid_txrate,
					      u8 *next_idx)
{
	u8 i;

	for (i = 0; i < ath_rc_priv->max_valid_rate - 1; i++) {
		if (ath_rc_priv->valid_rate_index[i] == cur_valid_txrate) {
			*next_idx = ath_rc_priv->valid_rate_index[i+1];
			return 1;
		}
	}

	/* No more valid rates */
	*next_idx = 0;

	return 0;
}

/* Return true only for single stream */

static int ath_rc_valid_phyrate(u32 phy, u32 capflag, int ignore_cw)
{
	if (WLAN_RC_PHY_HT(phy) && !(capflag & WLAN_RC_HT_FLAG))
		return 0;
	if (WLAN_RC_PHY_DS(phy) && !(capflag & WLAN_RC_DS_FLAG))
		return 0;
	if (WLAN_RC_PHY_SGI(phy) && !(capflag & WLAN_RC_SGI_FLAG))
		return 0;
	if (!ignore_cw && WLAN_RC_PHY_HT(phy))
		if (WLAN_RC_PHY_40(phy) && !(capflag & WLAN_RC_40_FLAG))
			return 0;
		if (!WLAN_RC_PHY_40(phy) && (capflag & WLAN_RC_40_FLAG))
			return 0;
	return 1;
}

static inline int
ath_rc_get_nextlowervalid_txrate(struct ath_rate_table *rate_table,
				 struct ath_rate_priv *ath_rc_priv,
				 u8 cur_valid_txrate, u8 *next_idx)
{
	int8_t i;

	for (i = 1; i < ath_rc_priv->max_valid_rate ; i++) {
		if (ath_rc_priv->valid_rate_index[i] == cur_valid_txrate) {
			*next_idx = ath_rc_priv->valid_rate_index[i-1];
			return 1;
		}
	}

	return 0;
}

static u8 ath_rc_init_validrates(struct ath_rate_priv *ath_rc_priv,
				 struct ath_rate_table *rate_table,
				 u32 capflag)
{
	u8 i, hi = 0;
	u32 valid;

	for (i = 0; i < rate_table->rate_cnt; i++) {
		valid = (ath_rc_priv->single_stream ?
			 rate_table->info[i].valid_single_stream :
			 rate_table->info[i].valid);
		if (valid == 1) {
			u32 phy = rate_table->info[i].phy;
			u8 valid_rate_count = 0;

			if (!ath_rc_valid_phyrate(phy, capflag, 0))
				continue;

			valid_rate_count = ath_rc_priv->valid_phy_ratecnt[phy];

			ath_rc_priv->valid_phy_rateidx[phy][valid_rate_count] = i;
			ath_rc_priv->valid_phy_ratecnt[phy] += 1;
			ath_rc_set_valid_txmask(ath_rc_priv, i, 1);
			hi = A_MAX(hi, i);
		}
	}

	return hi;
}

static u8 ath_rc_setvalid_rates(struct ath_rate_priv *ath_rc_priv,
				struct ath_rate_table *rate_table,
				struct ath_rateset *rateset,
				u32 capflag)
{
	u8 i, j, hi = 0;

	/* Use intersection of working rates and valid rates */
	for (i = 0; i < rateset->rs_nrates; i++) {
		for (j = 0; j < rate_table->rate_cnt; j++) {
			u32 phy = rate_table->info[j].phy;
			u32 valid = (ath_rc_priv->single_stream ?
				rate_table->info[j].valid_single_stream :
				rate_table->info[j].valid);
			u8 rate = rateset->rs_rates[i];
			u8 dot11rate = rate_table->info[j].dot11rate;

			/* We allow a rate only if its valid and the
			 * capflag matches one of the validity
			 * (VALID/VALID_20/VALID_40) flags */

			if (((rate & 0x7F) == (dot11rate & 0x7F)) &&
			    ((valid & WLAN_RC_CAP_MODE(capflag)) ==
			     WLAN_RC_CAP_MODE(capflag)) &&
			    !WLAN_RC_PHY_HT(phy)) {
				u8 valid_rate_count = 0;

				if (!ath_rc_valid_phyrate(phy, capflag, 0))
					continue;

				valid_rate_count =
					ath_rc_priv->valid_phy_ratecnt[phy];

				ath_rc_priv->valid_phy_rateidx[phy]
					[valid_rate_count] = j;
				ath_rc_priv->valid_phy_ratecnt[phy] += 1;
				ath_rc_set_valid_txmask(ath_rc_priv, j, 1);
				hi = A_MAX(hi, j);
			}
		}
	}

	return hi;
}

static u8 ath_rc_setvalid_htrates(struct ath_rate_priv *ath_rc_priv,
				  struct ath_rate_table *rate_table,
				  u8 *mcs_set, u32 capflag)
{
	struct ath_rateset *rateset = (struct ath_rateset *)mcs_set;

	u8 i, j, hi = 0;

	/* Use intersection of working rates and valid rates */
	for (i = 0; i < rateset->rs_nrates; i++) {
		for (j = 0; j < rate_table->rate_cnt; j++) {
			u32 phy = rate_table->info[j].phy;
			u32 valid = (ath_rc_priv->single_stream ?
				     rate_table->info[j].valid_single_stream :
				     rate_table->info[j].valid);
			u8 rate = rateset->rs_rates[i];
			u8 dot11rate = rate_table->info[j].dot11rate;

			if (((rate & 0x7F) != (dot11rate & 0x7F)) ||
			    !WLAN_RC_PHY_HT(phy) ||
			    !WLAN_RC_PHY_HT_VALID(valid, capflag))
				continue;

			if (!ath_rc_valid_phyrate(phy, capflag, 0))
				continue;

			ath_rc_priv->valid_phy_rateidx[phy]
				[ath_rc_priv->valid_phy_ratecnt[phy]] = j;
			ath_rc_priv->valid_phy_ratecnt[phy] += 1;
			ath_rc_set_valid_txmask(ath_rc_priv, j, 1);
			hi = A_MAX(hi, j);
		}
	}

	return hi;
}

static u8 ath_rc_ratefind_ht(struct ath_softc *sc,
			     struct ath_rate_priv *ath_rc_priv,
			     struct ath_rate_table *rate_table,
			     int *is_probing)
{
	u32 dt, best_thruput, this_thruput, now_msec;
	u8 rate, next_rate, best_rate, maxindex, minindex;
	int8_t  rssi_last, rssi_reduce = 0, index = 0;

	*is_probing = 0;

	rssi_last = median(ath_rc_priv->rssi_last,
			   ath_rc_priv->rssi_last_prev,
			   ath_rc_priv->rssi_last_prev2);

	/*
	 * Age (reduce) last ack rssi based on how old it is.
	 * The bizarre numbers are so the delta is 160msec,
	 * meaning we divide by 16.
	 *   0msec   <= dt <= 25msec:   don't derate
	 *   25msec  <= dt <= 185msec:  derate linearly from 0 to 10dB
	 *   185msec <= dt:             derate by 10dB
	 */

	now_msec = jiffies_to_msecs(jiffies);
	dt = now_msec - ath_rc_priv->rssi_time;

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
	maxindex = ath_rc_priv->max_valid_rate-1;

	minindex = 0;
	best_rate = minindex;

	/*
	 * Try the higher rate first. It will reduce memory moving time
	 * if we have very good channel characteristics.
	 */
	for (index = maxindex; index >= minindex ; index--) {
		u8 per_thres;

		rate = ath_rc_priv->valid_rate_index[index];
		if (rate > ath_rc_priv->rate_max_phy)
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
		per_thres = ath_rc_priv->state[rate].per;
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
	ath_rc_priv->rssi_last_lookup = rssi_last;

	/*
	 * Must check the actual rate (ratekbps) to account for
	 * non-monoticity of 11g's rate table
	 */

	if (rate >= ath_rc_priv->rate_max_phy) {
		rate = ath_rc_priv->rate_max_phy;

		/* Probe the next allowed phy state */
		if (ath_rc_get_nextvalid_txrate(rate_table,
					ath_rc_priv, rate, &next_rate) &&
		    (now_msec - ath_rc_priv->probe_time >
		     rate_table->probe_interval) &&
		    (ath_rc_priv->hw_maxretry_pktcnt >= 1)) {
			rate = next_rate;
			ath_rc_priv->probe_rate = rate;
			ath_rc_priv->probe_time = now_msec;
			ath_rc_priv->hw_maxretry_pktcnt = 0;
			*is_probing = 1;
		}
	}

	if (rate > (ath_rc_priv->rate_table_size - 1))
		rate = ath_rc_priv->rate_table_size - 1;

	ASSERT((rate_table->info[rate].valid && !ath_rc_priv->single_stream) ||
	       (rate_table->info[rate].valid_single_stream &&
		ath_rc_priv->single_stream));

	return rate;
}

static void ath_rc_rate_set_series(struct ath_rate_table *rate_table,
				   struct ieee80211_tx_rate *rate,
				   struct ieee80211_tx_rate_control *txrc,
				   u8 tries, u8 rix, int rtsctsenable)
{
	rate->count = tries;
	rate->idx = rix;

	if (txrc->short_preamble)
		rate->flags |= IEEE80211_TX_RC_USE_SHORT_PREAMBLE;
	if (txrc->rts || rtsctsenable)
		rate->flags |= IEEE80211_TX_RC_USE_RTS_CTS;
	if (WLAN_RC_PHY_40(rate_table->info[rix].phy))
		rate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
	if (WLAN_RC_PHY_SGI(rate_table->info[rix].phy))
		rate->flags |= IEEE80211_TX_RC_SHORT_GI;
	if (WLAN_RC_PHY_HT(rate_table->info[rix].phy))
		rate->flags |= IEEE80211_TX_RC_MCS;
}

static void ath_rc_rate_set_rtscts(struct ath_softc *sc,
				   struct ath_rate_table *rate_table,
				   struct ieee80211_tx_info *tx_info)
{
	struct ieee80211_tx_rate *rates = tx_info->control.rates;
	int i = 0, rix = 0, cix, enable_g_protection = 0;

	/* get the cix for the lowest valid rix */
	for (i = 3; i >= 0; i--) {
		if (rates[i].count && (rates[i].idx >= 0)) {
			rix = rates[i].idx;
			break;
		}
	}
	cix = rate_table->info[rix].ctrl_rate;

	/* All protection frames are transmited at 2Mb/s for 802.11g,
	 * otherwise we transmit them at 1Mb/s */
	if (sc->hw->conf.channel->band == IEEE80211_BAND_2GHZ &&
	    !conf_is_ht(&sc->hw->conf))
		enable_g_protection = 1;

	/*
	 * If 802.11g protection is enabled, determine whether to use RTS/CTS or
	 * just CTS.  Note that this is only done for OFDM/HT unicast frames.
	 */
	if ((sc->sc_flags & SC_OP_PROTECT_ENABLE) &&
	    !(tx_info->flags & IEEE80211_TX_CTL_NO_ACK) &&
	    (rate_table->info[rix].phy == WLAN_RC_PHY_OFDM ||
	     WLAN_RC_PHY_HT(rate_table->info[rix].phy))) {
		rates[0].flags |= IEEE80211_TX_RC_USE_CTS_PROTECT;
		cix = rate_table->info[enable_g_protection].ctrl_rate;
	}

	tx_info->control.rts_cts_rate_idx = cix;
}

static u8 ath_rc_rate_getidx(struct ath_softc *sc,
			     struct ath_rate_priv *ath_rc_priv,
			     struct ath_rate_table *rate_table,
			     u8 rix, u16 stepdown,
			     u16 min_rate)
{
	u32 j;
	u8 nextindex;

	if (min_rate) {
		for (j = RATE_TABLE_SIZE; j > 0; j--) {
			if (ath_rc_get_nextlowervalid_txrate(rate_table,
						ath_rc_priv, rix, &nextindex))
				rix = nextindex;
			else
				break;
		}
	} else {
		for (j = stepdown; j > 0; j--) {
			if (ath_rc_get_nextlowervalid_txrate(rate_table,
						ath_rc_priv, rix, &nextindex))
				rix = nextindex;
			else
				break;
		}
	}
	return rix;
}

static void ath_rc_ratefind(struct ath_softc *sc,
			    struct ath_rate_priv *ath_rc_priv,
			    struct ieee80211_tx_rate_control *txrc)
{
	struct ath_rate_table *rate_table;
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rates = tx_info->control.rates;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;
	u8 try_per_rate = 0, i = 0, rix, nrix;
	int is_probe = 0;

	rate_table = sc->cur_rate_table;
	rix = ath_rc_ratefind_ht(sc, ath_rc_priv, rate_table, &is_probe);
	nrix = rix;

	if (is_probe) {
		/* set one try for probe rates. For the
		 * probes don't enable rts */
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       1, nrix, 0);

		try_per_rate = (ATH_11N_TXMAXTRY/4);
		/* Get the next tried/allowed rate. No RTS for the next series
		 * after the probe rate
		 */
		nrix = ath_rc_rate_getidx(sc, ath_rc_priv,
					  rate_table, nrix, 1, 0);
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       try_per_rate, nrix, 0);

		tx_info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;
	} else {
		try_per_rate = (ATH_11N_TXMAXTRY/4);
		/* Set the choosen rate. No RTS for first series entry. */
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       try_per_rate, nrix, 0);
	}

	/* Fill in the other rates for multirate retry */
	for ( ; i < 4; i++) {
		u8 try_num;
		u8 min_rate;

		try_num = ((i + 1) == 4) ?
			ATH_11N_TXMAXTRY - (try_per_rate * i) : try_per_rate ;
		min_rate = (((i + 1) == 4) && 0);

		nrix = ath_rc_rate_getidx(sc, ath_rc_priv,
					  rate_table, nrix, 1, min_rate);
		/* All other rates in the series have RTS enabled */
		ath_rc_rate_set_series(rate_table, &rates[i], txrc,
				       try_num, nrix, 1);
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
	if ((sc->hw->conf.channel->band == IEEE80211_BAND_2GHZ) &&
	    (conf_is_ht(&sc->hw->conf))) {
		u8 dot11rate = rate_table->info[rix].dot11rate;
		u8 phy = rate_table->info[rix].phy;
		if (i == 4 &&
		    ((dot11rate == 2 && phy == WLAN_RC_PHY_HT_40_SS) ||
		     (dot11rate == 3 && phy == WLAN_RC_PHY_HT_20_SS))) {
			rates[3].idx = rates[2].idx;
			rates[3].flags = rates[2].flags;
		}
	}

	/*
	 * Force hardware to use computed duration for next
	 * fragment by disabling multi-rate retry, which
	 * updates duration based on the multi-rate duration table.
	 *
	 * FIXME: Fix duration
	 */
	if (!(tx_info->flags & IEEE80211_TX_CTL_NO_ACK) &&
	    (ieee80211_has_morefrags(fc) ||
	     (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG))) {
		rates[1].count = rates[2].count = rates[3].count = 0;
		rates[1].idx = rates[2].idx = rates[3].idx = 0;
		rates[0].count = ATH_TXMAXTRY;
	}

	/* Setup RTS/CTS */
	ath_rc_rate_set_rtscts(sc, rate_table, tx_info);
}

static bool ath_rc_update_per(struct ath_softc *sc,
			      struct ath_rate_table *rate_table,
			      struct ath_rate_priv *ath_rc_priv,
			      struct ath_tx_info_priv *tx_info_priv,
			      int tx_rate, int xretries, int retries,
			      u32 now_msec)
{
	bool state_change = false;
	int count;
	u8 last_per;
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

	last_per = ath_rc_priv->state[tx_rate].per;

	if (xretries) {
		if (xretries == 1) {
			ath_rc_priv->state[tx_rate].per += 30;
			if (ath_rc_priv->state[tx_rate].per > 100)
				ath_rc_priv->state[tx_rate].per = 100;
		} else {
			/* xretries == 2 */
			count = ARRAY_SIZE(nretry_to_per_lookup);
			if (retries >= count)
				retries = count - 1;

			/* new_PER = 7/8*old_PER + 1/8*(currentPER) */
			ath_rc_priv->state[tx_rate].per =
				(u8)(last_per - (last_per >> 3) + (100 >> 3));
		}

		/* xretries == 1 or 2 */

		if (ath_rc_priv->probe_rate == tx_rate)
			ath_rc_priv->probe_rate = 0;

	} else { /* xretries == 0 */
		count = ARRAY_SIZE(nretry_to_per_lookup);
		if (retries >= count)
			retries = count - 1;

		if (tx_info_priv->n_bad_frames) {
			/* new_PER = 7/8*old_PER + 1/8*(currentPER)
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
			if (tx_info_priv->n_frames > 0) {
				int n_frames, n_bad_frames;
				u8 cur_per, new_per;

				n_bad_frames = retries * tx_info_priv->n_frames +
					tx_info_priv->n_bad_frames;
				n_frames = tx_info_priv->n_frames * (retries + 1);
				cur_per = (100 * n_bad_frames / n_frames) >> 3;
				new_per = (u8)(last_per - (last_per >> 3) + cur_per);
				ath_rc_priv->state[tx_rate].per = new_per;
			}
		} else {
			ath_rc_priv->state[tx_rate].per =
				(u8)(last_per - (last_per >> 3) +
				     (nretry_to_per_lookup[retries] >> 3));
		}

		ath_rc_priv->rssi_last_prev2 = ath_rc_priv->rssi_last_prev;
		ath_rc_priv->rssi_last_prev  = ath_rc_priv->rssi_last;
		ath_rc_priv->rssi_last = tx_info_priv->tx.ts_rssi;
		ath_rc_priv->rssi_time = now_msec;

		/*
		 * If we got at most one retry then increase the max rate if
		 * this was a probe.  Otherwise, ignore the probe.
		 */
		if (ath_rc_priv->probe_rate && ath_rc_priv->probe_rate == tx_rate) {
			if (retries > 0 || 2 * tx_info_priv->n_bad_frames >
				tx_info_priv->n_frames) {
				/*
				 * Since we probed with just a single attempt,
				 * any retries means the probe failed.  Also,
				 * if the attempt worked, but more than half
				 * the subframes were bad then also consider
				 * the probe a failure.
				 */
				ath_rc_priv->probe_rate = 0;
			} else {
				u8 probe_rate = 0;

				ath_rc_priv->rate_max_phy =
					ath_rc_priv->probe_rate;
				probe_rate = ath_rc_priv->probe_rate;

				if (ath_rc_priv->state[probe_rate].per > 30)
					ath_rc_priv->state[probe_rate].per = 20;

				ath_rc_priv->probe_rate = 0;

				/*
				 * Since this probe succeeded, we allow the next
				 * probe twice as soon.  This allows the maxRate
				 * to move up faster if the probes are
				 * succesful.
				 */
				ath_rc_priv->probe_time =
					now_msec - rate_table->probe_interval / 2;
			}
		}

		if (retries > 0) {
			/*
			 * Don't update anything.  We don't know if
			 * this was because of collisions or poor signal.
			 *
			 * Later: if rssi_ack is close to
			 * ath_rc_priv->state[txRate].rssi_thres and we see lots
			 * of retries, then we could increase
			 * ath_rc_priv->state[txRate].rssi_thres.
			 */
			ath_rc_priv->hw_maxretry_pktcnt = 0;
		} else {
			int32_t rssi_ackAvg;
			int8_t rssi_thres;
			int8_t rssi_ack_vmin;

			/*
			 * It worked with no retries. First ignore bogus (small)
			 * rssi_ack values.
			 */
			if (tx_rate == ath_rc_priv->rate_max_phy &&
			    ath_rc_priv->hw_maxretry_pktcnt < 255) {
				ath_rc_priv->hw_maxretry_pktcnt++;
			}

			if (tx_info_priv->tx.ts_rssi <
			    rate_table->info[tx_rate].rssi_ack_validmin)
				goto exit;

			/* Average the rssi */
			if (tx_rate != ath_rc_priv->rssi_sum_rate) {
				ath_rc_priv->rssi_sum_rate = tx_rate;
				ath_rc_priv->rssi_sum =
					ath_rc_priv->rssi_sum_cnt = 0;
			}

			ath_rc_priv->rssi_sum += tx_info_priv->tx.ts_rssi;
			ath_rc_priv->rssi_sum_cnt++;

			if (ath_rc_priv->rssi_sum_cnt < 4)
				goto exit;

			rssi_ackAvg =
				(ath_rc_priv->rssi_sum + 2) / 4;
			rssi_thres =
				ath_rc_priv->state[tx_rate].rssi_thres;
			rssi_ack_vmin =
				rate_table->info[tx_rate].rssi_ack_validmin;

			ath_rc_priv->rssi_sum =
				ath_rc_priv->rssi_sum_cnt = 0;

			/* Now reduce the current rssi threshold */
			if ((rssi_ackAvg < rssi_thres + 2) &&
			    (rssi_thres > rssi_ack_vmin)) {
				ath_rc_priv->state[tx_rate].rssi_thres--;
			}

			state_change = true;
		}
	}
exit:
	return state_change;
}

/* Update PER, RSSI and whatever else that the code thinks it is doing.
   If you can make sense of all this, you really need to go out more. */

static void ath_rc_update_ht(struct ath_softc *sc,
			     struct ath_rate_priv *ath_rc_priv,
			     struct ath_tx_info_priv *tx_info_priv,
			     int tx_rate, int xretries, int retries)
{
#define CHK_RSSI(rate)					\
	((ath_rc_priv->state[(rate)].rssi_thres +	\
	  rate_table->info[(rate)].rssi_ack_deltamin) > \
	 ath_rc_priv->state[(rate)+1].rssi_thres)

	u32 now_msec = jiffies_to_msecs(jiffies);
	int rate;
	u8 last_per;
	bool state_change = false;
	struct ath_rate_table *rate_table = sc->cur_rate_table;
	int size = ath_rc_priv->rate_table_size;

	if ((tx_rate < 0) || (tx_rate > rate_table->rate_cnt))
		return;

	/* To compensate for some imbalance between ctrl and ext. channel */

	if (WLAN_RC_PHY_40(rate_table->info[tx_rate].phy))
		tx_info_priv->tx.ts_rssi =
			tx_info_priv->tx.ts_rssi < 3 ? 0 :
			tx_info_priv->tx.ts_rssi - 3;

	last_per = ath_rc_priv->state[tx_rate].per;

	/* Update PER first */
	state_change = ath_rc_update_per(sc, rate_table, ath_rc_priv,
					 tx_info_priv, tx_rate, xretries,
					 retries, now_msec);

	/*
	 * If this rate looks bad (high PER) then stop using it for
	 * a while (except if we are probing).
	 */
	if (ath_rc_priv->state[tx_rate].per >= 55 && tx_rate > 0 &&
	    rate_table->info[tx_rate].ratekbps <=
	    rate_table->info[ath_rc_priv->rate_max_phy].ratekbps) {
		ath_rc_get_nextlowervalid_txrate(rate_table, ath_rc_priv,
				 (u8)tx_rate, &ath_rc_priv->rate_max_phy);

		/* Don't probe for a little while. */
		ath_rc_priv->probe_time = now_msec;
	}

	if (state_change) {
		/*
		 * Make sure the rates above this have higher rssi thresholds.
		 * (Note:  Monotonicity is kept within the OFDM rates and
		 *         within the CCK rates. However, no adjustment is
		 *         made to keep the rssi thresholds monotonically
		 *         increasing between the CCK and OFDM rates.)
		 */
		for (rate = tx_rate; rate < size - 1; rate++) {
			if (rate_table->info[rate+1].phy !=
			    rate_table->info[tx_rate].phy)
				break;

			if (CHK_RSSI(rate)) {
				ath_rc_priv->state[rate+1].rssi_thres =
					ath_rc_priv->state[rate].rssi_thres +
					rate_table->info[rate].rssi_ack_deltamin;
			}
		}

		/* Make sure the rates below this have lower rssi thresholds. */
		for (rate = tx_rate - 1; rate >= 0; rate--) {
			if (rate_table->info[rate].phy !=
			    rate_table->info[tx_rate].phy)
				break;

			if (CHK_RSSI(rate)) {
				if (ath_rc_priv->state[rate+1].rssi_thres <
				    rate_table->info[rate].rssi_ack_deltamin)
					ath_rc_priv->state[rate].rssi_thres = 0;
				else {
					ath_rc_priv->state[rate].rssi_thres =
					ath_rc_priv->state[rate+1].rssi_thres -
					rate_table->info[rate].rssi_ack_deltamin;
				}

				if (ath_rc_priv->state[rate].rssi_thres <
				    rate_table->info[rate].rssi_ack_validmin) {
					ath_rc_priv->state[rate].rssi_thres =
					rate_table->info[rate].rssi_ack_validmin;
				}
			}
		}
	}

	/* Make sure the rates below this have lower PER */
	/* Monotonicity is kept only for rates below the current rate. */
	if (ath_rc_priv->state[tx_rate].per < last_per) {
		for (rate = tx_rate - 1; rate >= 0; rate--) {
			if (rate_table->info[rate].phy !=
			    rate_table->info[tx_rate].phy)
				break;

			if (ath_rc_priv->state[rate].per >
			    ath_rc_priv->state[rate+1].per) {
				ath_rc_priv->state[rate].per =
					ath_rc_priv->state[rate+1].per;
			}
		}
	}

	/* Maintain monotonicity for rates above the current rate */
	for (rate = tx_rate; rate < size - 1; rate++) {
		if (ath_rc_priv->state[rate+1].per <
		    ath_rc_priv->state[rate].per)
			ath_rc_priv->state[rate+1].per =
				ath_rc_priv->state[rate].per;
	}

	/* Every so often, we reduce the thresholds and
	 * PER (different for CCK and OFDM). */
	if (now_msec - ath_rc_priv->rssi_down_time >=
	    rate_table->rssi_reduce_interval) {

		for (rate = 0; rate < size; rate++) {
			if (ath_rc_priv->state[rate].rssi_thres >
			    rate_table->info[rate].rssi_ack_validmin)
				ath_rc_priv->state[rate].rssi_thres -= 1;
		}
		ath_rc_priv->rssi_down_time = now_msec;
	}

	/* Every so often, we reduce the thresholds
	 * and PER (different for CCK and OFDM). */
	if (now_msec - ath_rc_priv->per_down_time >=
	    rate_table->rssi_reduce_interval) {
		for (rate = 0; rate < size; rate++) {
			ath_rc_priv->state[rate].per =
				7 * ath_rc_priv->state[rate].per / 8;
		}

		ath_rc_priv->per_down_time = now_msec;
	}

	ath_debug_stat_retries(sc, tx_rate, xretries, retries,
			       ath_rc_priv->state[tx_rate].per);

#undef CHK_RSSI
}

static int ath_rc_get_rateindex(struct ath_rate_table *rate_table,
				struct ieee80211_tx_rate *rate)
{
	int rix;

	if ((rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
	    (rate->flags & IEEE80211_TX_RC_SHORT_GI))
		rix = rate_table->info[rate->idx].ht_index;
	else if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rix = rate_table->info[rate->idx].sgi_index;
	else if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		rix = rate_table->info[rate->idx].cw40index;
	else
		rix = rate_table->info[rate->idx].base_index;

	return rix;
}

static void ath_rc_tx_status(struct ath_softc *sc,
			     struct ath_rate_priv *ath_rc_priv,
			     struct ieee80211_tx_info *tx_info,
			     int final_ts_idx, int xretries, int long_retry)
{
	struct ath_tx_info_priv *tx_info_priv = ATH_TX_INFO_PRIV(tx_info);
	struct ath_rate_table *rate_table;
	struct ieee80211_tx_rate *rates = tx_info->status.rates;
	u8 flags;
	u32 i = 0, rix;

	rate_table = sc->cur_rate_table;

	/*
	 * If the first rate is not the final index, there
	 * are intermediate rate failures to be processed.
	 */
	if (final_ts_idx != 0) {
		/* Process intermediate rates that failed.*/
		for (i = 0; i < final_ts_idx ; i++) {
			if (rates[i].count != 0 && (rates[i].idx >= 0)) {
				flags = rates[i].flags;

				/* If HT40 and we have switched mode from
				 * 40 to 20 => don't update */

				if ((flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
				    (ath_rc_priv->rc_phy_mode != WLAN_RC_40_FLAG))
					return;

				rix = ath_rc_get_rateindex(rate_table, &rates[i]);
				ath_rc_update_ht(sc, ath_rc_priv,
						tx_info_priv, rix,
						xretries ? 1 : 2,
						rates[i].count);
			}
		}
	} else {
		/*
		 * Handle the special case of MIMO PS burst, where the second
		 * aggregate is sent out with only one rate and one try.
		 * Treating it as an excessive retry penalizes the rate
		 * inordinately.
		 */
		if (rates[0].count == 1 && xretries == 1)
			xretries = 2;
	}

	flags = rates[i].flags;

	/* If HT40 and we have switched mode from 40 to 20 => don't update */
	if ((flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
	    (ath_rc_priv->rc_phy_mode != WLAN_RC_40_FLAG)) {
		return;
	}

	rix = ath_rc_get_rateindex(rate_table, &rates[i]);
	ath_rc_update_ht(sc, ath_rc_priv, tx_info_priv, rix,
			 xretries, long_retry);
}

static struct ath_rate_table *ath_choose_rate_table(struct ath_softc *sc,
						    enum ieee80211_band band,
						    bool is_ht, bool is_cw_40)
{
	int mode = 0;

	switch(band) {
	case IEEE80211_BAND_2GHZ:
		mode = ATH9K_MODE_11G;
		if (is_ht)
			mode = ATH9K_MODE_11NG_HT20;
		if (is_cw_40)
			mode = ATH9K_MODE_11NG_HT40PLUS;
		break;
	case IEEE80211_BAND_5GHZ:
		mode = ATH9K_MODE_11A;
		if (is_ht)
			mode = ATH9K_MODE_11NA_HT20;
		if (is_cw_40)
			mode = ATH9K_MODE_11NA_HT40PLUS;
		break;
	default:
		DPRINTF(sc, ATH_DBG_CONFIG, "Invalid band\n");
		return NULL;
	}

	BUG_ON(mode >= ATH9K_MODE_MAX);

	DPRINTF(sc, ATH_DBG_CONFIG, "Choosing rate table for mode: %d\n", mode);
	return sc->hw_rate_table[mode];
}

static void ath_rc_init(struct ath_softc *sc,
			struct ath_rate_priv *ath_rc_priv,
			struct ieee80211_supported_band *sband,
			struct ieee80211_sta *sta,
			struct ath_rate_table *rate_table)
{
	struct ath_rateset *rateset = &ath_rc_priv->neg_rates;
	u8 *ht_mcs = (u8 *)&ath_rc_priv->neg_ht_rates;
	u8 i, j, k, hi = 0, hthi = 0;

	if (!rate_table) {
		DPRINTF(sc, ATH_DBG_FATAL, "Rate table not initialized\n");
		return;
	}

	/* Initial rate table size. Will change depending
	 * on the working rate set */
	ath_rc_priv->rate_table_size = RATE_TABLE_SIZE;

	/* Initialize thresholds according to the global rate table */
	for (i = 0 ; i < ath_rc_priv->rate_table_size; i++) {
		ath_rc_priv->state[i].rssi_thres =
			rate_table->info[i].rssi_ack_validmin;
		ath_rc_priv->state[i].per = 0;
	}

	/* Determine the valid rates */
	ath_rc_init_valid_txmask(ath_rc_priv);

	for (i = 0; i < WLAN_RC_PHY_MAX; i++) {
		for (j = 0; j < MAX_TX_RATE_PHY; j++)
			ath_rc_priv->valid_phy_rateidx[i][j] = 0;
		ath_rc_priv->valid_phy_ratecnt[i] = 0;
	}
	ath_rc_priv->rc_phy_mode = ath_rc_priv->ht_cap & WLAN_RC_40_FLAG;

	/* Set stream capability */
	ath_rc_priv->single_stream = (ath_rc_priv->ht_cap & WLAN_RC_DS_FLAG) ? 0 : 1;

	if (!rateset->rs_nrates) {
		/* No working rate, just initialize valid rates */
		hi = ath_rc_init_validrates(ath_rc_priv, rate_table,
					    ath_rc_priv->ht_cap);
	} else {
		/* Use intersection of working rates and valid rates */
		hi = ath_rc_setvalid_rates(ath_rc_priv, rate_table,
					   rateset, ath_rc_priv->ht_cap);
		if (ath_rc_priv->ht_cap & WLAN_RC_HT_FLAG) {
			hthi = ath_rc_setvalid_htrates(ath_rc_priv,
						       rate_table,
						       ht_mcs,
						       ath_rc_priv->ht_cap);
		}
		hi = A_MAX(hi, hthi);
	}

	ath_rc_priv->rate_table_size = hi + 1;
	ath_rc_priv->rate_max_phy = 0;
	ASSERT(ath_rc_priv->rate_table_size <= RATE_TABLE_SIZE);

	for (i = 0, k = 0; i < WLAN_RC_PHY_MAX; i++) {
		for (j = 0; j < ath_rc_priv->valid_phy_ratecnt[i]; j++) {
			ath_rc_priv->valid_rate_index[k++] =
				ath_rc_priv->valid_phy_rateidx[i][j];
		}

		if (!ath_rc_valid_phyrate(i, rate_table->initial_ratemax, 1)
		    || !ath_rc_priv->valid_phy_ratecnt[i])
			continue;

		ath_rc_priv->rate_max_phy = ath_rc_priv->valid_phy_rateidx[i][j-1];
	}
	ASSERT(ath_rc_priv->rate_table_size <= RATE_TABLE_SIZE);
	ASSERT(k <= RATE_TABLE_SIZE);

	ath_rc_priv->max_valid_rate = k;
	ath_rc_sort_validrates(rate_table, ath_rc_priv);
	ath_rc_priv->rate_max_phy = ath_rc_priv->valid_rate_index[k-4];
	sc->cur_rate_table = rate_table;

	DPRINTF(sc, ATH_DBG_CONFIG, "RC Initialized with capabilities: 0x%x\n",
		ath_rc_priv->ht_cap);
}

static u8 ath_rc_build_ht_caps(struct ath_softc *sc, struct ieee80211_sta *sta,
			       bool is_cw40, bool is_sgi40)
{
	u8 caps = 0;

	if (sta->ht_cap.ht_supported) {
		caps = WLAN_RC_HT_FLAG;
		if (sc->sc_ah->caps.tx_chainmask != 1 &&
		    ath9k_hw_getcapability(sc->sc_ah, ATH9K_CAP_DS, 0, NULL)) {
			if (sta->ht_cap.mcs.rx_mask[1])
				caps |= WLAN_RC_DS_FLAG;
		}
		if (is_cw40)
			caps |= WLAN_RC_40_FLAG;
		if (is_sgi40)
			caps |= WLAN_RC_SGI_FLAG;
	}

	return caps;
}

/***********************************/
/* mac80211 Rate Control callbacks */
/***********************************/

static void ath_tx_status(void *priv, struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta,
			  struct sk_buff *skb)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	struct ath_tx_info_priv *tx_info_priv = NULL;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr;
	int final_ts_idx, tx_status = 0, is_underrun = 0;
	__le16 fc;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;
	tx_info_priv = ATH_TX_INFO_PRIV(tx_info);
	final_ts_idx = tx_info_priv->tx.ts_rateindex;

	if (!priv_sta || !ieee80211_is_data(fc) ||
	    !tx_info_priv->update_rc)
		goto exit;

	if (tx_info_priv->tx.ts_status & ATH9K_TXERR_FILT)
		goto exit;

	/*
	 * If underrun error is seen assume it as an excessive retry only
	 * if prefetch trigger level have reached the max (0x3f for 5416)
	 * Adjust the long retry as if the frame was tried ATH_11N_TXMAXTRY
	 * times. This affects how ratectrl updates PER for the failed rate.
	 */
	if (tx_info_priv->tx.ts_flags &
	    (ATH9K_TX_DATA_UNDERRUN | ATH9K_TX_DELIM_UNDERRUN) &&
	    ((sc->sc_ah->tx_trig_level) >= ath_rc_priv->tx_triglevel_max)) {
		tx_status = 1;
		is_underrun = 1;
	}

	if ((tx_info_priv->tx.ts_status & ATH9K_TXERR_XRETRY) ||
	    (tx_info_priv->tx.ts_status & ATH9K_TXERR_FIFO))
		tx_status = 1;

	ath_rc_tx_status(sc, ath_rc_priv, tx_info, final_ts_idx, tx_status,
			 (is_underrun) ? ATH_11N_TXMAXTRY :
			 tx_info_priv->tx.ts_longretry);

	/* Check if aggregation has to be enabled for this tid */
	if (conf_is_ht(&sc->hw->conf) &&
	    !(skb->protocol == cpu_to_be16(ETH_P_PAE))) {
		if (ieee80211_is_data_qos(fc)) {
			u8 *qc, tid;
			struct ath_node *an;

			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;
			an = (struct ath_node *)sta->drv_priv;

			if(ath_tx_aggr_check(sc, an, tid))
				ieee80211_start_tx_ba_session(sc->hw, hdr->addr1, tid);
		}
	}

	ath_debug_stat_rc(sc, skb);
exit:
	kfree(tx_info_priv);
}

static void ath_get_rate(void *priv, struct ieee80211_sta *sta, void *priv_sta,
			 struct ieee80211_tx_rate_control *txrc)
{
	struct ieee80211_supported_band *sband = txrc->sband;
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	__le16 fc = hdr->frame_control;

	/* lowest rate for management and multicast/broadcast frames */
	if (!ieee80211_is_data(fc) || is_multicast_ether_addr(hdr->addr1) ||
	    !sta) {
		tx_info->control.rates[0].idx = rate_lowest_index(sband, sta);
		tx_info->control.rates[0].count =
			is_multicast_ether_addr(hdr->addr1) ? 1 : ATH_MGT_TXMAXTRY;
		return;
	}

	/* Find tx rate for unicast frames */
	ath_rc_ratefind(sc, ath_rc_priv, txrc);
}

static void ath_rate_init(void *priv, struct ieee80211_supported_band *sband,
                          struct ieee80211_sta *sta, void *priv_sta)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	struct ath_rate_table *rate_table = NULL;
	bool is_cw40, is_sgi40;
	int i, j = 0;

	for (i = 0; i < sband->n_bitrates; i++) {
		if (sta->supp_rates[sband->band] & BIT(i)) {
			ath_rc_priv->neg_rates.rs_rates[j]
				= (sband->bitrates[i].bitrate * 2) / 10;
			j++;
		}
	}
	ath_rc_priv->neg_rates.rs_nrates = j;

	if (sta->ht_cap.ht_supported) {
		for (i = 0, j = 0; i < 77; i++) {
			if (sta->ht_cap.mcs.rx_mask[i/8] & (1<<(i%8)))
				ath_rc_priv->neg_ht_rates.rs_rates[j++] = i;
			if (j == ATH_RATE_MAX)
				break;
		}
		ath_rc_priv->neg_ht_rates.rs_nrates = j;
	}

	is_cw40 = sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40;
	is_sgi40 = sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40;

	/* Choose rate table first */

	if ((sc->sc_ah->opmode == NL80211_IFTYPE_STATION) ||
	    (sc->sc_ah->opmode == NL80211_IFTYPE_MESH_POINT) ||
	    (sc->sc_ah->opmode == NL80211_IFTYPE_ADHOC)) {
		rate_table = ath_choose_rate_table(sc, sband->band,
						   sta->ht_cap.ht_supported,
						   is_cw40);
	} else if (sc->sc_ah->opmode == NL80211_IFTYPE_AP) {
		/* cur_rate_table would be set on init through config() */
		rate_table = sc->cur_rate_table;
	}

	ath_rc_priv->ht_cap = ath_rc_build_ht_caps(sc, sta, is_cw40, is_sgi40);
	ath_rc_init(sc, priv_sta, sband, sta, rate_table);
}

static void ath_rate_update(void *priv, struct ieee80211_supported_band *sband,
			    struct ieee80211_sta *sta, void *priv_sta,
			    u32 changed)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	struct ath_rate_table *rate_table = NULL;
	bool oper_cw40 = false, oper_sgi40;
	bool local_cw40 = (ath_rc_priv->ht_cap & WLAN_RC_40_FLAG) ?
		true : false;
	bool local_sgi40 = (ath_rc_priv->ht_cap & WLAN_RC_SGI_FLAG) ?
		true : false;

	/* FIXME: Handle AP mode later when we support CWM */

	if (changed & IEEE80211_RC_HT_CHANGED) {
		if (sc->sc_ah->opmode != NL80211_IFTYPE_STATION)
			return;

		if (sc->hw->conf.channel_type == NL80211_CHAN_HT40MINUS ||
		    sc->hw->conf.channel_type == NL80211_CHAN_HT40PLUS)
			oper_cw40 = true;

		oper_sgi40 = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
			true : false;

		if ((local_cw40 != oper_cw40) || (local_sgi40 != oper_sgi40)) {
			rate_table = ath_choose_rate_table(sc, sband->band,
						   sta->ht_cap.ht_supported,
						   oper_cw40);
			ath_rc_priv->ht_cap = ath_rc_build_ht_caps(sc, sta,
						   oper_cw40, oper_sgi40);
			ath_rc_init(sc, priv_sta, sband, sta, rate_table);

			DPRINTF(sc, ATH_DBG_CONFIG,
				"Operating HT Bandwidth changed to: %d\n",
				sc->hw->conf.channel_type);
		}
	}
}

static void *ath_rate_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	struct ath_wiphy *aphy = hw->priv;
	return aphy->sc;
}

static void ath_rate_free(void *priv)
{
	return;
}

static void *ath_rate_alloc_sta(void *priv, struct ieee80211_sta *sta, gfp_t gfp)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *rate_priv;

	rate_priv = kzalloc(sizeof(struct ath_rate_priv), gfp);
	if (!rate_priv) {
		DPRINTF(sc, ATH_DBG_FATAL,
			"Unable to allocate private rc structure\n");
		return NULL;
	}

	rate_priv->rssi_down_time = jiffies_to_msecs(jiffies);
	rate_priv->tx_triglevel_max = sc->sc_ah->caps.tx_triglevel_max;

	return rate_priv;
}

static void ath_rate_free_sta(void *priv, struct ieee80211_sta *sta,
			      void *priv_sta)
{
	struct ath_rate_priv *rate_priv = priv_sta;
	kfree(rate_priv);
}

static struct rate_control_ops ath_rate_ops = {
	.module = NULL,
	.name = "ath9k_rate_control",
	.tx_status = ath_tx_status,
	.get_rate = ath_get_rate,
	.rate_init = ath_rate_init,
	.rate_update = ath_rate_update,
	.alloc = ath_rate_alloc,
	.free = ath_rate_free,
	.alloc_sta = ath_rate_alloc_sta,
	.free_sta = ath_rate_free_sta,
};

void ath_rate_attach(struct ath_softc *sc)
{
	sc->hw_rate_table[ATH9K_MODE_11B] =
		&ar5416_11b_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11A] =
		&ar5416_11a_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11G] =
		&ar5416_11g_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NA_HT20] =
		&ar5416_11na_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NG_HT20] =
		&ar5416_11ng_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NA_HT40PLUS] =
		&ar5416_11na_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NA_HT40MINUS] =
		&ar5416_11na_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NG_HT40PLUS] =
		&ar5416_11ng_ratetable;
	sc->hw_rate_table[ATH9K_MODE_11NG_HT40MINUS] =
		&ar5416_11ng_ratetable;
}

int ath_rate_control_register(void)
{
	return ieee80211_rate_control_register(&ath_rate_ops);
}

void ath_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&ath_rate_ops);
}
