/*
 * Copyright (c) 2004 Video54 Technologies, Inc.
 * Copyright (c) 2004-2011 Atheros Communications, Inc.
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

#include <linux/slab.h>

#include "ath9k.h"

static const struct ath_rate_table ar5416_11na_ratetable = {
	68,
	8, /* MCS start */
	{
		[0] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 6000,
			5400, 0, 12, 0, 0, 0, 0 }, /* 6 Mb */
		[1] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 9000,
			7800,  1, 18, 0, 1, 1, 1 }, /* 9 Mb */
		[2] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 12000,
			10000, 2, 24, 2, 2, 2, 2 }, /* 12 Mb */
		[3] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 18000,
			13900, 3, 36, 2, 3, 3, 3 }, /* 18 Mb */
		[4] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 24000,
			17300, 4, 48, 4, 4, 4, 4 }, /* 24 Mb */
		[5] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 36000,
			23000, 5, 72, 4, 5, 5, 5 }, /* 36 Mb */
		[6] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 48000,
			27400, 6, 96, 4, 6, 6, 6 }, /* 48 Mb */
		[7] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 54000,
			29300, 7, 108, 4, 7, 7, 7 }, /* 54 Mb */
		[8] = { RC_HT_SDT_2040, WLAN_RC_PHY_HT_20_SS, 6500,
			6400, 0, 0, 0, 38, 8, 38 }, /* 6.5 Mb */
		[9] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 13000,
			12700, 1, 1, 2, 39, 9, 39 }, /* 13 Mb */
		[10] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 19500,
			18800, 2, 2, 2, 40, 10, 40 }, /* 19.5 Mb */
		[11] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 26000,
			25000, 3, 3, 4, 41, 11, 41 }, /* 26 Mb */
		[12] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 39000,
			36700, 4, 4, 4, 42, 12, 42 }, /* 39 Mb */
		[13] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 52000,
			48100, 5, 5, 4, 43, 13, 43 }, /* 52 Mb */
		[14] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 58500,
			53500, 6, 6, 4, 44, 14, 44 }, /* 58.5 Mb */
		[15] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 65000,
			59000, 7, 7, 4, 45, 16, 46 }, /* 65 Mb */
		[16] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS_HGI, 72200,
			65400, 7, 7, 4, 45, 16, 46 }, /* 75 Mb */
		[17] = { RC_INVALID, WLAN_RC_PHY_HT_20_DS, 13000,
			12700, 8, 8, 0, 47, 17, 47 }, /* 13 Mb */
		[18] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 26000,
			24800, 9, 9, 2, 48, 18, 48 }, /* 26 Mb */
		[19] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 39000,
			36600, 10, 10, 2, 49, 19, 49 }, /* 39 Mb */
		[20] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 52000,
			48100, 11, 11, 4, 50, 20, 50 }, /* 52 Mb */
		[21] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 78000,
			69500, 12, 12, 4, 51, 21, 51 }, /* 78 Mb */
		[22] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 104000,
			89500, 13, 13, 4, 52, 22, 52 }, /* 104 Mb */
		[23] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 117000,
			98900, 14, 14, 4, 53, 23, 53 }, /* 117 Mb */
		[24] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 130000,
			108300, 15, 15, 4, 54, 25, 55 }, /* 130 Mb */
		[25] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS_HGI, 144400,
			120000, 15, 15, 4, 54, 25, 55 }, /* 144.4 Mb */
		[26] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 19500,
			17400, 16, 16, 0, 56, 26, 56 }, /* 19.5 Mb */
		[27] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 39000,
			35100, 17, 17, 2, 57, 27, 57 }, /* 39 Mb */
		[28] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 58500,
			52600, 18, 18, 2, 58, 28, 58 }, /* 58.5 Mb */
		[29] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 78000,
			70400, 19, 19, 4, 59, 29, 59 }, /* 78 Mb */
		[30] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 117000,
			104900, 20, 20, 4, 60, 31, 61 }, /* 117 Mb */
		[31] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS_HGI, 130000,
			115800, 20, 20, 4, 60, 31, 61 }, /* 130 Mb*/
		[32] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 156000,
			137200, 21, 21, 4, 62, 33, 63 }, /* 156 Mb */
		[33] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 173300,
			151100, 21, 21, 4, 62, 33, 63 }, /* 173.3 Mb */
		[34] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 175500,
			152800, 22, 22, 4, 64, 35, 65 }, /* 175.5 Mb */
		[35] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 195000,
			168400, 22, 22, 4, 64, 35, 65 }, /* 195 Mb*/
		[36] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 195000,
			168400, 23, 23, 4, 66, 37, 67 }, /* 195 Mb */
		[37] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 216700,
			185000, 23, 23, 4, 66, 37, 67 }, /* 216.7 Mb */
		[38] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 13500,
			13200, 0, 0, 0, 38, 38, 38 }, /* 13.5 Mb*/
		[39] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 27500,
			25900, 1, 1, 2, 39, 39, 39 }, /* 27.0 Mb*/
		[40] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 40500,
			38600, 2, 2, 2, 40, 40, 40 }, /* 40.5 Mb*/
		[41] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 54000,
			49800, 3, 3, 4, 41, 41, 41 }, /* 54 Mb */
		[42] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 81500,
			72200, 4, 4, 4, 42, 42, 42 }, /* 81 Mb */
		[43] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 108000,
			92900, 5, 5, 4, 43, 43, 43 }, /* 108 Mb */
		[44] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 121500,
			102700, 6, 6, 4, 44, 44, 44 }, /* 121.5 Mb*/
		[45] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 135000,
			112000, 7, 7, 4, 45, 46, 46 }, /* 135 Mb */
		[46] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS_HGI, 150000,
			122000, 7, 7, 4, 45, 46, 46 }, /* 150 Mb */
		[47] = { RC_INVALID, WLAN_RC_PHY_HT_40_DS, 27000,
			25800, 8, 8, 0, 47, 47, 47 }, /* 27 Mb */
		[48] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 54000,
			49800, 9, 9, 2, 48, 48, 48 }, /* 54 Mb */
		[49] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 81000,
			71900, 10, 10, 2, 49, 49, 49 }, /* 81 Mb */
		[50] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 108000,
			92500, 11, 11, 4, 50, 50, 50 }, /* 108 Mb */
		[51] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 162000,
			130300, 12, 12, 4, 51, 51, 51 }, /* 162 Mb */
		[52] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 216000,
			162800, 13, 13, 4, 52, 52, 52 }, /* 216 Mb */
		[53] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 243000,
			178200, 14, 14, 4, 53, 53, 53 }, /* 243 Mb */
		[54] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 270000,
			192100, 15, 15, 4, 54, 55, 55 }, /* 270 Mb */
		[55] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS_HGI, 300000,
			207000, 15, 15, 4, 54, 55, 55 }, /* 300 Mb */
		[56] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 40500,
			36100, 16, 16, 0, 56, 56, 56 }, /* 40.5 Mb */
		[57] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 81000,
			72900, 17, 17, 2, 57, 57, 57 }, /* 81 Mb */
		[58] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 121500,
			108300, 18, 18, 2, 58, 58, 58 }, /* 121.5 Mb */
		[59] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 162000,
			142000, 19, 19, 4, 59, 59, 59 }, /*  162 Mb */
		[60] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 243000,
			205100, 20, 20, 4, 60, 61, 61 }, /*  243 Mb */
		[61] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS_HGI, 270000,
			224700, 20, 20, 4, 60, 61, 61 }, /*  270 Mb */
		[62] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 324000,
			263100, 21, 21, 4, 62, 63, 63 }, /*  324 Mb */
		[63] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 360000,
			288000, 21, 21, 4, 62, 63, 63 }, /*  360 Mb */
		[64] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 364500,
			290700, 22, 22, 4, 64, 65, 65 }, /* 364.5 Mb */
		[65] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 405000,
			317200, 22, 22, 4, 64, 65, 65 }, /* 405 Mb */
		[66] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 405000,
			317200, 23, 23, 4, 66, 67, 67 }, /* 405 Mb */
		[67] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 450000,
			346400, 23, 23, 4, 66, 67, 67 }, /* 450 Mb */
	},
	50,  /* probe interval */
	WLAN_RC_HT_FLAG,  /* Phy rates allowed initially */
};

/* 4ms frame limit not used for NG mode.  The values filled
 * for HT are the 64K max aggregate limit */

static const struct ath_rate_table ar5416_11ng_ratetable = {
	72,
	12, /* MCS start */
	{
		[0] = { RC_ALL, WLAN_RC_PHY_CCK, 1000,
			900, 0, 2, 0, 0, 0, 0 }, /* 1 Mb */
		[1] = { RC_ALL, WLAN_RC_PHY_CCK, 2000,
			1900, 1, 4, 1, 1, 1, 1 }, /* 2 Mb */
		[2] = { RC_ALL, WLAN_RC_PHY_CCK, 5500,
			4900, 2, 11, 2, 2, 2, 2 }, /* 5.5 Mb */
		[3] = { RC_ALL, WLAN_RC_PHY_CCK, 11000,
			8100, 3, 22, 3, 3, 3, 3 }, /* 11 Mb */
		[4] = { RC_INVALID, WLAN_RC_PHY_OFDM, 6000,
			5400, 4, 12, 4, 4, 4, 4 }, /* 6 Mb */
		[5] = { RC_INVALID, WLAN_RC_PHY_OFDM, 9000,
			7800, 5, 18, 4, 5, 5, 5 }, /* 9 Mb */
		[6] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 12000,
			10100, 6, 24, 6, 6, 6, 6 }, /* 12 Mb */
		[7] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 18000,
			14100, 7, 36, 6, 7, 7, 7 }, /* 18 Mb */
		[8] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 24000,
			17700, 8, 48, 8, 8, 8, 8 }, /* 24 Mb */
		[9] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 36000,
			23700, 9, 72, 8, 9, 9, 9 }, /* 36 Mb */
		[10] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 48000,
			27400, 10, 96, 8, 10, 10, 10 }, /* 48 Mb */
		[11] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 54000,
			30900, 11, 108, 8, 11, 11, 11 }, /* 54 Mb */
		[12] = { RC_INVALID, WLAN_RC_PHY_HT_20_SS, 6500,
			6400, 0, 0, 4, 42, 12, 42 }, /* 6.5 Mb */
		[13] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 13000,
			12700, 1, 1, 6, 43, 13, 43 }, /* 13 Mb */
		[14] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 19500,
			18800, 2, 2, 6, 44, 14, 44 }, /* 19.5 Mb*/
		[15] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 26000,
			25000, 3, 3, 8, 45, 15, 45 }, /* 26 Mb */
		[16] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 39000,
			36700, 4, 4, 8, 46, 16, 46 }, /* 39 Mb */
		[17] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 52000,
			48100, 5, 5, 8, 47, 17, 47 }, /* 52 Mb */
		[18] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 58500,
			53500, 6, 6, 8, 48, 18, 48 }, /* 58.5 Mb */
		[19] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 65000,
			59000, 7, 7, 8, 49, 20, 50 }, /* 65 Mb */
		[20] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS_HGI, 72200,
			65400, 7, 7, 8, 49, 20, 50 }, /* 65 Mb*/
		[21] = { RC_INVALID, WLAN_RC_PHY_HT_20_DS, 13000,
			12700, 8, 8, 4, 51, 21, 51 }, /* 13 Mb */
		[22] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 26000,
			24800, 9, 9, 6, 52, 22, 52 }, /* 26 Mb */
		[23] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 39000,
			36600, 10, 10, 6, 53, 23, 53 }, /* 39 Mb */
		[24] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 52000,
			48100, 11, 11, 8, 54, 24, 54 }, /* 52 Mb */
		[25] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 78000,
			69500, 12, 12, 8, 55, 25, 55 }, /* 78 Mb */
		[26] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 104000,
			89500, 13, 13, 8, 56, 26, 56 }, /* 104 Mb */
		[27] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 117000,
			98900, 14, 14, 8, 57, 27, 57 }, /* 117 Mb */
		[28] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 130000,
			108300, 15, 15, 8, 58, 29, 59 }, /* 130 Mb */
		[29] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS_HGI, 144400,
			120000, 15, 15, 8, 58, 29, 59 }, /* 144.4 Mb */
		[30] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 19500,
			17400, 16, 16, 4, 60, 30, 60 }, /* 19.5 Mb */
		[31] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 39000,
			35100, 17, 17, 6, 61, 31, 61 }, /* 39 Mb */
		[32] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 58500,
			52600, 18, 18, 6, 62, 32, 62 }, /* 58.5 Mb */
		[33] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 78000,
			70400, 19, 19, 8, 63, 33, 63 }, /* 78 Mb */
		[34] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 117000,
			104900, 20, 20, 8, 64, 35, 65 }, /* 117 Mb */
		[35] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS_HGI, 130000,
			115800, 20, 20, 8, 64, 35, 65 }, /* 130 Mb */
		[36] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 156000,
			137200, 21, 21, 8, 66, 37, 67 }, /* 156 Mb */
		[37] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 173300,
			151100, 21, 21, 8, 66, 37, 67 }, /* 173.3 Mb */
		[38] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 175500,
			152800, 22, 22, 8, 68, 39, 69 }, /* 175.5 Mb */
		[39] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 195000,
			168400, 22, 22, 8, 68, 39, 69 }, /* 195 Mb */
		[40] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 195000,
			168400, 23, 23, 8, 70, 41, 71 }, /* 195 Mb */
		[41] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 216700,
			185000, 23, 23, 8, 70, 41, 71 }, /* 216.7 Mb */
		[42] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 13500,
			13200, 0, 0, 8, 42, 42, 42 }, /* 13.5 Mb */
		[43] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 27500,
			25900, 1, 1, 8, 43, 43, 43 }, /* 27.0 Mb */
		[44] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 40500,
			38600, 2, 2, 8, 44, 44, 44 }, /* 40.5 Mb */
		[45] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 54000,
			49800, 3, 3, 8, 45, 45, 45 }, /* 54 Mb */
		[46] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 81500,
			72200, 4, 4, 8, 46, 46, 46 }, /* 81 Mb */
		[47] = { RC_HT_S_40 , WLAN_RC_PHY_HT_40_SS, 108000,
			92900, 5, 5, 8, 47, 47, 47 }, /* 108 Mb */
		[48] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 121500,
			102700, 6, 6, 8, 48, 48, 48 }, /* 121.5 Mb */
		[49] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 135000,
			112000, 7, 7, 8, 49, 50, 50 }, /* 135 Mb */
		[50] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS_HGI, 150000,
			122000, 7, 7, 8, 49, 50, 50 }, /* 150 Mb */
		[51] = { RC_INVALID, WLAN_RC_PHY_HT_40_DS, 27000,
			25800, 8, 8, 8, 51, 51, 51 }, /* 27 Mb */
		[52] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 54000,
			49800, 9, 9, 8, 52, 52, 52 }, /* 54 Mb */
		[53] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 81000,
			71900, 10, 10, 8, 53, 53, 53 }, /* 81 Mb */
		[54] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 108000,
			92500, 11, 11, 8, 54, 54, 54 }, /* 108 Mb */
		[55] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 162000,
			130300, 12, 12, 8, 55, 55, 55 }, /* 162 Mb */
		[56] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 216000,
			162800, 13, 13, 8, 56, 56, 56 }, /* 216 Mb */
		[57] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 243000,
			178200, 14, 14, 8, 57, 57, 57 }, /* 243 Mb */
		[58] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 270000,
			192100, 15, 15, 8, 58, 59, 59 }, /* 270 Mb */
		[59] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS_HGI, 300000,
			207000, 15, 15, 8, 58, 59, 59 }, /* 300 Mb */
		[60] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 40500,
			36100, 16, 16, 8, 60, 60, 60 }, /* 40.5 Mb */
		[61] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 81000,
			72900, 17, 17, 8, 61, 61, 61 }, /* 81 Mb */
		[62] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 121500,
			108300, 18, 18, 8, 62, 62, 62 }, /* 121.5 Mb */
		[63] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 162000,
			142000, 19, 19, 8, 63, 63, 63 }, /* 162 Mb */
		[64] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 243000,
			205100, 20, 20, 8, 64, 65, 65 }, /* 243 Mb */
		[65] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS_HGI, 270000,
			224700, 20, 20, 8, 64, 65, 65 }, /* 270 Mb */
		[66] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 324000,
			263100, 21, 21, 8, 66, 67, 67 }, /* 324 Mb */
		[67] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 360000,
			288000, 21, 21, 8, 66, 67, 67 }, /* 360 Mb */
		[68] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 364500,
			290700, 22, 22, 8, 68, 69, 69 }, /* 364.5 Mb */
		[69] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 405000,
			317200, 22, 22, 8, 68, 69, 69 }, /* 405 Mb */
		[70] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 405000,
			317200, 23, 23, 8, 70, 71, 71 }, /* 405 Mb */
		[71] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 450000,
			346400, 23, 23, 8, 70, 71, 71 }, /* 450 Mb */
	},
	50,  /* probe interval */
	WLAN_RC_HT_FLAG,  /* Phy rates allowed initially */
};

static const struct ath_rate_table ar5416_11a_ratetable = {
	8,
	0,
	{
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0, 12, 0},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800,  1, 18, 0},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10000, 2, 24, 2},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			13900, 3, 36, 2},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17300, 4, 48, 4},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23000, 5, 72, 4},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 6, 96, 4},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
			29300, 7, 108, 4},
	},
	50,  /* probe interval */
	0,   /* Phy rates allowed initially */
};

static const struct ath_rate_table ar5416_11g_ratetable = {
	12,
	0,
	{
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 1000, /* 1 Mb */
			900, 0, 2, 0},
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 2000, /* 2 Mb */
			1900, 1, 4, 1},
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 5500, /* 5.5 Mb */
			4900, 2, 11, 2},
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 11000, /* 11 Mb */
			8100, 3, 22, 3},
		{ RC_INVALID, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 4, 12, 4},
		{ RC_INVALID, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800, 5, 18, 4},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10000, 6, 24, 6},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			13900, 7, 36, 6},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17300, 8, 48, 8},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23000, 9, 72, 8},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 10, 96, 8},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
			29300, 11, 108, 8},
	},
	50,  /* probe interval */
	0,   /* Phy rates allowed initially */
};

static int ath_rc_get_rateindex(const struct ath_rate_table *rate_table,
				struct ieee80211_tx_rate *rate)
{
	int rix = 0, i = 0;
	static const int mcs_rix_off[] = { 7, 15, 20, 21, 22, 23 };

	if (!(rate->flags & IEEE80211_TX_RC_MCS))
		return rate->idx;

	while (i < ARRAY_SIZE(mcs_rix_off) && rate->idx > mcs_rix_off[i]) {
		rix++; i++;
	}

	rix += rate->idx + rate_table->mcs_start;

	if ((rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
	    (rate->flags & IEEE80211_TX_RC_SHORT_GI))
		rix = rate_table->info[rix].ht_index;
	else if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rix = rate_table->info[rix].sgi_index;
	else if (rate->flags & IEEE80211_TX_RC_40_MHZ_WIDTH)
		rix = rate_table->info[rix].cw40index;

	return rix;
}

static void ath_rc_sort_validrates(const struct ath_rate_table *rate_table,
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

static void ath_rc_init_valid_rate_idx(struct ath_rate_priv *ath_rc_priv)
{
	u8 i;

	for (i = 0; i < ath_rc_priv->rate_table_size; i++)
		ath_rc_priv->valid_rate_index[i] = 0;
}

static inline void ath_rc_set_valid_rate_idx(struct ath_rate_priv *ath_rc_priv,
					   u8 index, int valid_tx_rate)
{
	BUG_ON(index > ath_rc_priv->rate_table_size);
	ath_rc_priv->valid_rate_index[index] = !!valid_tx_rate;
}

static inline
int ath_rc_get_nextvalid_txrate(const struct ath_rate_table *rate_table,
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
	if (WLAN_RC_PHY_TS(phy) && !(capflag & WLAN_RC_TS_FLAG))
		return 0;
	if (WLAN_RC_PHY_SGI(phy) && !(capflag & WLAN_RC_SGI_FLAG))
		return 0;
	if (!ignore_cw && WLAN_RC_PHY_HT(phy))
		if (WLAN_RC_PHY_40(phy) && !(capflag & WLAN_RC_40_FLAG))
			return 0;
	return 1;
}

static inline int
ath_rc_get_lower_rix(const struct ath_rate_table *rate_table,
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
				 const struct ath_rate_table *rate_table,
				 u32 capflag)
{
	u8 i, hi = 0;

	for (i = 0; i < rate_table->rate_cnt; i++) {
		if (rate_table->info[i].rate_flags & RC_LEGACY) {
			u32 phy = rate_table->info[i].phy;
			u8 valid_rate_count = 0;

			if (!ath_rc_valid_phyrate(phy, capflag, 0))
				continue;

			valid_rate_count = ath_rc_priv->valid_phy_ratecnt[phy];

			ath_rc_priv->valid_phy_rateidx[phy][valid_rate_count] = i;
			ath_rc_priv->valid_phy_ratecnt[phy] += 1;
			ath_rc_set_valid_rate_idx(ath_rc_priv, i, 1);
			hi = i;
		}
	}

	return hi;
}

static u8 ath_rc_setvalid_rates(struct ath_rate_priv *ath_rc_priv,
				const struct ath_rate_table *rate_table,
				struct ath_rateset *rateset,
				u32 capflag)
{
	u8 i, j, hi = 0;

	/* Use intersection of working rates and valid rates */
	for (i = 0; i < rateset->rs_nrates; i++) {
		for (j = 0; j < rate_table->rate_cnt; j++) {
			u32 phy = rate_table->info[j].phy;
			u16 rate_flags = rate_table->info[j].rate_flags;
			u8 rate = rateset->rs_rates[i];
			u8 dot11rate = rate_table->info[j].dot11rate;

			/* We allow a rate only if its valid and the
			 * capflag matches one of the validity
			 * (VALID/VALID_20/VALID_40) flags */

			if ((rate == dot11rate) &&
			    (rate_flags & WLAN_RC_CAP_MODE(capflag)) ==
			    WLAN_RC_CAP_MODE(capflag) &&
			    (rate_flags & WLAN_RC_CAP_STREAM(capflag)) &&
			    !WLAN_RC_PHY_HT(phy)) {
				u8 valid_rate_count = 0;

				if (!ath_rc_valid_phyrate(phy, capflag, 0))
					continue;

				valid_rate_count =
					ath_rc_priv->valid_phy_ratecnt[phy];

				ath_rc_priv->valid_phy_rateidx[phy]
					[valid_rate_count] = j;
				ath_rc_priv->valid_phy_ratecnt[phy] += 1;
				ath_rc_set_valid_rate_idx(ath_rc_priv, j, 1);
				hi = max(hi, j);
			}
		}
	}

	return hi;
}

static u8 ath_rc_setvalid_htrates(struct ath_rate_priv *ath_rc_priv,
				  const struct ath_rate_table *rate_table,
				  u8 *mcs_set, u32 capflag)
{
	struct ath_rateset *rateset = (struct ath_rateset *)mcs_set;

	u8 i, j, hi = 0;

	/* Use intersection of working rates and valid rates */
	for (i = 0; i < rateset->rs_nrates; i++) {
		for (j = 0; j < rate_table->rate_cnt; j++) {
			u32 phy = rate_table->info[j].phy;
			u16 rate_flags = rate_table->info[j].rate_flags;
			u8 rate = rateset->rs_rates[i];
			u8 dot11rate = rate_table->info[j].dot11rate;

			if ((rate != dot11rate) || !WLAN_RC_PHY_HT(phy) ||
			    !(rate_flags & WLAN_RC_CAP_STREAM(capflag)) ||
			    !WLAN_RC_PHY_HT_VALID(rate_flags, capflag))
				continue;

			if (!ath_rc_valid_phyrate(phy, capflag, 0))
				continue;

			ath_rc_priv->valid_phy_rateidx[phy]
				[ath_rc_priv->valid_phy_ratecnt[phy]] = j;
			ath_rc_priv->valid_phy_ratecnt[phy] += 1;
			ath_rc_set_valid_rate_idx(ath_rc_priv, j, 1);
			hi = max(hi, j);
		}
	}

	return hi;
}

/* Finds the highest rate index we can use */
static u8 ath_rc_get_highest_rix(struct ath_softc *sc,
			         struct ath_rate_priv *ath_rc_priv,
				 const struct ath_rate_table *rate_table,
				 int *is_probing,
				 bool legacy)
{
	u32 best_thruput, this_thruput, now_msec;
	u8 rate, next_rate, best_rate, maxindex, minindex;
	int8_t index = 0;

	now_msec = jiffies_to_msecs(jiffies);
	*is_probing = 0;
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
		if (legacy && !(rate_table->info[rate].rate_flags & RC_LEGACY))
			continue;
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
		per_thres = ath_rc_priv->per[rate];
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

	if (RC_TS_ONLY(rate_table->info[rate].rate_flags) &&
	    (ath_rc_priv->ht_cap & WLAN_RC_TS_FLAG))
		return rate;

	if (RC_DS_OR_LATER(rate_table->info[rate].rate_flags) &&
	    (ath_rc_priv->ht_cap & (WLAN_RC_DS_FLAG | WLAN_RC_TS_FLAG)))
		return rate;

	if (RC_SS_OR_LEGACY(rate_table->info[rate].rate_flags))
		return rate;

	/* This should not happen */
	WARN_ON(1);

	rate = ath_rc_priv->valid_rate_index[0];

	return rate;
}

static void ath_rc_rate_set_series(const struct ath_rate_table *rate_table,
				   struct ieee80211_tx_rate *rate,
				   struct ieee80211_tx_rate_control *txrc,
				   u8 tries, u8 rix, int rtsctsenable)
{
	rate->count = tries;
	rate->idx = rate_table->info[rix].ratecode;

	if (txrc->short_preamble)
		rate->flags |= IEEE80211_TX_RC_USE_SHORT_PREAMBLE;
	if (txrc->rts || rtsctsenable)
		rate->flags |= IEEE80211_TX_RC_USE_RTS_CTS;

	if (WLAN_RC_PHY_HT(rate_table->info[rix].phy)) {
		rate->flags |= IEEE80211_TX_RC_MCS;
		if (WLAN_RC_PHY_40(rate_table->info[rix].phy) &&
		    conf_is_ht40(&txrc->hw->conf))
			rate->flags |= IEEE80211_TX_RC_40_MHZ_WIDTH;
		if (WLAN_RC_PHY_SGI(rate_table->info[rix].phy))
			rate->flags |= IEEE80211_TX_RC_SHORT_GI;
	}
}

static void ath_rc_rate_set_rtscts(struct ath_softc *sc,
				   const struct ath_rate_table *rate_table,
				   struct ieee80211_tx_info *tx_info)
{
	struct ieee80211_tx_rate *rates = tx_info->control.rates;
	int i = 0, rix = 0, cix, enable_g_protection = 0;

	/* get the cix for the lowest valid rix */
	for (i = 3; i >= 0; i--) {
		if (rates[i].count && (rates[i].idx >= 0)) {
			rix = ath_rc_get_rateindex(rate_table, &rates[i]);
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
	    (rate_table->info[rix].phy == WLAN_RC_PHY_OFDM ||
	     WLAN_RC_PHY_HT(rate_table->info[rix].phy))) {
		rates[0].flags |= IEEE80211_TX_RC_USE_CTS_PROTECT;
		cix = rate_table->info[enable_g_protection].ctrl_rate;
	}

	tx_info->control.rts_cts_rate_idx = cix;
}

static void ath_get_rate(void *priv, struct ieee80211_sta *sta, void *priv_sta,
			 struct ieee80211_tx_rate_control *txrc)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	const struct ath_rate_table *rate_table;
	struct sk_buff *skb = txrc->skb;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rates = tx_info->control.rates;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;
	u8 try_per_rate, i = 0, rix, high_rix;
	int is_probe = 0;

	if (rate_control_send_low(sta, priv_sta, txrc))
		return;

	/*
	 * For Multi Rate Retry we use a different number of
	 * retry attempt counts. This ends up looking like this:
	 *
	 * MRR[0] = 4
	 * MRR[1] = 4
	 * MRR[2] = 4
	 * MRR[3] = 8
	 *
	 */
	try_per_rate = 4;

	rate_table = ath_rc_priv->rate_table;
	rix = ath_rc_get_highest_rix(sc, ath_rc_priv, rate_table,
				     &is_probe, false);
	high_rix = rix;

	/*
	 * If we're in HT mode and both us and our peer supports LDPC.
	 * We don't need to check our own device's capabilities as our own
	 * ht capabilities would have already been intersected with our peer's.
	 */
	if (conf_is_ht(&sc->hw->conf) &&
	    (sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING))
		tx_info->flags |= IEEE80211_TX_CTL_LDPC;

	if (conf_is_ht(&sc->hw->conf) &&
	    (sta->ht_cap.cap & IEEE80211_HT_CAP_TX_STBC))
		tx_info->flags |= (1 << IEEE80211_TX_CTL_STBC_SHIFT);

	if (is_probe) {
		/* set one try for probe rates. For the
		 * probes don't enable rts */
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       1, rix, 0);

		/* Get the next tried/allowed rate. No RTS for the next series
		 * after the probe rate
		 */
		ath_rc_get_lower_rix(rate_table, ath_rc_priv, rix, &rix);
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       try_per_rate, rix, 0);

		tx_info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;
	} else {
		/* Set the chosen rate. No RTS for first series entry. */
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       try_per_rate, rix, 0);
	}

	/* Fill in the other rates for multirate retry */
	for ( ; i < 3; i++) {

		ath_rc_get_lower_rix(rate_table, ath_rc_priv, rix, &rix);
		/* All other rates in the series have RTS enabled */
		ath_rc_rate_set_series(rate_table, &rates[i], txrc,
				       try_per_rate, rix, 1);
	}

	/* Use twice the number of tries for the last MRR segment. */
	try_per_rate = 8;

	/*
	 * Use a legacy rate as last retry to ensure that the frame
	 * is tried in both MCS and legacy rates.
	 */
	if ((rates[2].flags & IEEE80211_TX_RC_MCS) &&
	    (!(tx_info->flags & IEEE80211_TX_CTL_AMPDU) ||
	    (ath_rc_priv->per[high_rix] > 45)))
		rix = ath_rc_get_highest_rix(sc, ath_rc_priv, rate_table,
				&is_probe, true);
	else
		ath_rc_get_lower_rix(rate_table, ath_rc_priv, rix, &rix);

	/* All other rates in the series have RTS enabled */
	ath_rc_rate_set_series(rate_table, &rates[i], txrc,
			       try_per_rate, rix, 1);
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
	if (ieee80211_has_morefrags(fc) ||
	    (le16_to_cpu(hdr->seq_ctrl) & IEEE80211_SCTL_FRAG)) {
		rates[1].count = rates[2].count = rates[3].count = 0;
		rates[1].idx = rates[2].idx = rates[3].idx = 0;
		rates[0].count = ATH_TXMAXTRY;
	}

	/* Setup RTS/CTS */
	ath_rc_rate_set_rtscts(sc, rate_table, tx_info);
}

static void ath_rc_update_per(struct ath_softc *sc,
			      const struct ath_rate_table *rate_table,
			      struct ath_rate_priv *ath_rc_priv,
				  struct ieee80211_tx_info *tx_info,
			      int tx_rate, int xretries, int retries,
			      u32 now_msec)
{
	int count, n_bad_frames;
	u8 last_per;
	static const u32 nretry_to_per_lookup[10] = {
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

	last_per = ath_rc_priv->per[tx_rate];
	n_bad_frames = tx_info->status.ampdu_len - tx_info->status.ampdu_ack_len;

	if (xretries) {
		if (xretries == 1) {
			ath_rc_priv->per[tx_rate] += 30;
			if (ath_rc_priv->per[tx_rate] > 100)
				ath_rc_priv->per[tx_rate] = 100;
		} else {
			/* xretries == 2 */
			count = ARRAY_SIZE(nretry_to_per_lookup);
			if (retries >= count)
				retries = count - 1;

			/* new_PER = 7/8*old_PER + 1/8*(currentPER) */
			ath_rc_priv->per[tx_rate] =
				(u8)(last_per - (last_per >> 3) + (100 >> 3));
		}

		/* xretries == 1 or 2 */

		if (ath_rc_priv->probe_rate == tx_rate)
			ath_rc_priv->probe_rate = 0;

	} else { /* xretries == 0 */
		count = ARRAY_SIZE(nretry_to_per_lookup);
		if (retries >= count)
			retries = count - 1;

		if (n_bad_frames) {
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
			if (tx_info->status.ampdu_len > 0) {
				int n_frames, n_bad_tries;
				u8 cur_per, new_per;

				n_bad_tries = retries * tx_info->status.ampdu_len +
					n_bad_frames;
				n_frames = tx_info->status.ampdu_len * (retries + 1);
				cur_per = (100 * n_bad_tries / n_frames) >> 3;
				new_per = (u8)(last_per - (last_per >> 3) + cur_per);
				ath_rc_priv->per[tx_rate] = new_per;
			}
		} else {
			ath_rc_priv->per[tx_rate] =
				(u8)(last_per - (last_per >> 3) +
				     (nretry_to_per_lookup[retries] >> 3));
		}


		/*
		 * If we got at most one retry then increase the max rate if
		 * this was a probe.  Otherwise, ignore the probe.
		 */
		if (ath_rc_priv->probe_rate && ath_rc_priv->probe_rate == tx_rate) {
			if (retries > 0 || 2 * n_bad_frames > tx_info->status.ampdu_len) {
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

				if (ath_rc_priv->per[probe_rate] > 30)
					ath_rc_priv->per[probe_rate] = 20;

				ath_rc_priv->probe_rate = 0;

				/*
				 * Since this probe succeeded, we allow the next
				 * probe twice as soon.  This allows the maxRate
				 * to move up faster if the probes are
				 * successful.
				 */
				ath_rc_priv->probe_time =
					now_msec - rate_table->probe_interval / 2;
			}
		}

		if (retries > 0) {
			/*
			 * Don't update anything.  We don't know if
			 * this was because of collisions or poor signal.
			 */
			ath_rc_priv->hw_maxretry_pktcnt = 0;
		} else {
			/*
			 * It worked with no retries. First ignore bogus (small)
			 * rssi_ack values.
			 */
			if (tx_rate == ath_rc_priv->rate_max_phy &&
			    ath_rc_priv->hw_maxretry_pktcnt < 255) {
				ath_rc_priv->hw_maxretry_pktcnt++;
			}

		}
	}
}

static void ath_debug_stat_retries(struct ath_rate_priv *rc, int rix,
				   int xretries, int retries, u8 per)
{
	struct ath_rc_stats *stats = &rc->rcstats[rix];

	stats->xretries += xretries;
	stats->retries += retries;
	stats->per = per;
}

/* Update PER, RSSI and whatever else that the code thinks it is doing.
   If you can make sense of all this, you really need to go out more. */

static void ath_rc_update_ht(struct ath_softc *sc,
			     struct ath_rate_priv *ath_rc_priv,
			     struct ieee80211_tx_info *tx_info,
			     int tx_rate, int xretries, int retries)
{
	u32 now_msec = jiffies_to_msecs(jiffies);
	int rate;
	u8 last_per;
	const struct ath_rate_table *rate_table = ath_rc_priv->rate_table;
	int size = ath_rc_priv->rate_table_size;

	if ((tx_rate < 0) || (tx_rate > rate_table->rate_cnt))
		return;

	last_per = ath_rc_priv->per[tx_rate];

	/* Update PER first */
	ath_rc_update_per(sc, rate_table, ath_rc_priv,
			  tx_info, tx_rate, xretries,
			  retries, now_msec);

	/*
	 * If this rate looks bad (high PER) then stop using it for
	 * a while (except if we are probing).
	 */
	if (ath_rc_priv->per[tx_rate] >= 55 && tx_rate > 0 &&
	    rate_table->info[tx_rate].ratekbps <=
	    rate_table->info[ath_rc_priv->rate_max_phy].ratekbps) {
		ath_rc_get_lower_rix(rate_table, ath_rc_priv,
				     (u8)tx_rate, &ath_rc_priv->rate_max_phy);

		/* Don't probe for a little while. */
		ath_rc_priv->probe_time = now_msec;
	}

	/* Make sure the rates below this have lower PER */
	/* Monotonicity is kept only for rates below the current rate. */
	if (ath_rc_priv->per[tx_rate] < last_per) {
		for (rate = tx_rate - 1; rate >= 0; rate--) {

			if (ath_rc_priv->per[rate] >
			    ath_rc_priv->per[rate+1]) {
				ath_rc_priv->per[rate] =
					ath_rc_priv->per[rate+1];
			}
		}
	}

	/* Maintain monotonicity for rates above the current rate */
	for (rate = tx_rate; rate < size - 1; rate++) {
		if (ath_rc_priv->per[rate+1] <
		    ath_rc_priv->per[rate])
			ath_rc_priv->per[rate+1] =
				ath_rc_priv->per[rate];
	}

	/* Every so often, we reduce the thresholds
	 * and PER (different for CCK and OFDM). */
	if (now_msec - ath_rc_priv->per_down_time >=
	    rate_table->probe_interval) {
		for (rate = 0; rate < size; rate++) {
			ath_rc_priv->per[rate] =
				7 * ath_rc_priv->per[rate] / 8;
		}

		ath_rc_priv->per_down_time = now_msec;
	}

	ath_debug_stat_retries(ath_rc_priv, tx_rate, xretries, retries,
			       ath_rc_priv->per[tx_rate]);

}


static void ath_rc_tx_status(struct ath_softc *sc,
			     struct ath_rate_priv *ath_rc_priv,
			     struct ieee80211_tx_info *tx_info,
			     int final_ts_idx, int xretries, int long_retry)
{
	const struct ath_rate_table *rate_table;
	struct ieee80211_tx_rate *rates = tx_info->status.rates;
	u8 flags;
	u32 i = 0, rix;

	rate_table = ath_rc_priv->rate_table;

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
				    !(ath_rc_priv->ht_cap & WLAN_RC_40_FLAG))
					return;

				rix = ath_rc_get_rateindex(rate_table, &rates[i]);
				ath_rc_update_ht(sc, ath_rc_priv, tx_info,
						rix, xretries ? 1 : 2,
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
	    !(ath_rc_priv->ht_cap & WLAN_RC_40_FLAG))
		return;

	rix = ath_rc_get_rateindex(rate_table, &rates[i]);
	ath_rc_update_ht(sc, ath_rc_priv, tx_info, rix, xretries, long_retry);
}

static const
struct ath_rate_table *ath_choose_rate_table(struct ath_softc *sc,
					     enum ieee80211_band band,
					     bool is_ht)
{
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);

	switch(band) {
	case IEEE80211_BAND_2GHZ:
		if (is_ht)
			return &ar5416_11ng_ratetable;
		return &ar5416_11g_ratetable;
	case IEEE80211_BAND_5GHZ:
		if (is_ht)
			return &ar5416_11na_ratetable;
		return &ar5416_11a_ratetable;
	default:
		ath_dbg(common, ATH_DBG_CONFIG, "Invalid band\n");
		return NULL;
	}
}

static void ath_rc_init(struct ath_softc *sc,
			struct ath_rate_priv *ath_rc_priv,
			struct ieee80211_supported_band *sband,
			struct ieee80211_sta *sta,
			const struct ath_rate_table *rate_table)
{
	struct ath_rateset *rateset = &ath_rc_priv->neg_rates;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	u8 *ht_mcs = (u8 *)&ath_rc_priv->neg_ht_rates;
	u8 i, j, k, hi = 0, hthi = 0;

	/* Initial rate table size. Will change depending
	 * on the working rate set */
	ath_rc_priv->rate_table_size = RATE_TABLE_SIZE;

	/* Initialize thresholds according to the global rate table */
	for (i = 0 ; i < ath_rc_priv->rate_table_size; i++) {
		ath_rc_priv->per[i] = 0;
	}

	/* Determine the valid rates */
	ath_rc_init_valid_rate_idx(ath_rc_priv);

	for (i = 0; i < WLAN_RC_PHY_MAX; i++) {
		for (j = 0; j < MAX_TX_RATE_PHY; j++)
			ath_rc_priv->valid_phy_rateidx[i][j] = 0;
		ath_rc_priv->valid_phy_ratecnt[i] = 0;
	}

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
		hi = max(hi, hthi);
	}

	ath_rc_priv->rate_table_size = hi + 1;
	ath_rc_priv->rate_max_phy = 0;
	BUG_ON(ath_rc_priv->rate_table_size > RATE_TABLE_SIZE);

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
	BUG_ON(ath_rc_priv->rate_table_size > RATE_TABLE_SIZE);
	BUG_ON(k > RATE_TABLE_SIZE);

	ath_rc_priv->max_valid_rate = k;
	ath_rc_sort_validrates(rate_table, ath_rc_priv);
	ath_rc_priv->rate_max_phy = ath_rc_priv->valid_rate_index[k-4];
	ath_rc_priv->rate_table = rate_table;

	ath_dbg(common, ATH_DBG_CONFIG,
		"RC Initialized with capabilities: 0x%x\n",
		ath_rc_priv->ht_cap);
}

static u8 ath_rc_build_ht_caps(struct ath_softc *sc, struct ieee80211_sta *sta,
			       bool is_cw40, bool is_sgi)
{
	u8 caps = 0;

	if (sta->ht_cap.ht_supported) {
		caps = WLAN_RC_HT_FLAG;
		if (sta->ht_cap.mcs.rx_mask[1] && sta->ht_cap.mcs.rx_mask[2])
			caps |= WLAN_RC_TS_FLAG | WLAN_RC_DS_FLAG;
		else if (sta->ht_cap.mcs.rx_mask[1])
			caps |= WLAN_RC_DS_FLAG;
		if (is_cw40)
			caps |= WLAN_RC_40_FLAG;
		if (is_sgi)
			caps |= WLAN_RC_SGI_FLAG;
	}

	return caps;
}

static bool ath_tx_aggr_check(struct ath_softc *sc, struct ath_node *an,
			      u8 tidno)
{
	struct ath_atx_tid *txtid;

	if (!(sc->sc_flags & SC_OP_TXAGGR))
		return false;

	txtid = ATH_AN_2_TID(an, tidno);

	if (!(txtid->state & (AGGR_ADDBA_COMPLETE | AGGR_ADDBA_PROGRESS)))
			return true;
	return false;
}


/***********************************/
/* mac80211 Rate Control callbacks */
/***********************************/

static void ath_debug_stat_rc(struct ath_rate_priv *rc, int final_rate)
{
	struct ath_rc_stats *stats;

	stats = &rc->rcstats[final_rate];
	stats->success++;
}


static void ath_tx_status(void *priv, struct ieee80211_supported_band *sband,
			  struct ieee80211_sta *sta, void *priv_sta,
			  struct sk_buff *skb)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr;
	int final_ts_idx = 0, tx_status = 0;
	int long_retry = 0;
	__le16 fc;
	int i;

	hdr = (struct ieee80211_hdr *)skb->data;
	fc = hdr->frame_control;
	for (i = 0; i < sc->hw->max_rates; i++) {
		struct ieee80211_tx_rate *rate = &tx_info->status.rates[i];
		if (!rate->count)
			break;

		final_ts_idx = i;
		long_retry = rate->count - 1;
	}

	if (!priv_sta || !ieee80211_is_data(fc))
		return;

	/* This packet was aggregated but doesn't carry status info */
	if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    !(tx_info->flags & IEEE80211_TX_STAT_AMPDU))
		return;

	if (tx_info->flags & IEEE80211_TX_STAT_TX_FILTERED)
		return;

	if (!(tx_info->flags & IEEE80211_TX_STAT_ACK))
		tx_status = 1;

	ath_rc_tx_status(sc, ath_rc_priv, tx_info, final_ts_idx, tx_status,
			 long_retry);

	/* Check if aggregation has to be enabled for this tid */
	if (conf_is_ht(&sc->hw->conf) &&
	    !(skb->protocol == cpu_to_be16(ETH_P_PAE))) {
		if (ieee80211_is_data_qos(fc) &&
		    skb_get_queue_mapping(skb) != IEEE80211_AC_VO) {
			u8 *qc, tid;
			struct ath_node *an;

			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;
			an = (struct ath_node *)sta->drv_priv;

			if(ath_tx_aggr_check(sc, an, tid))
				ieee80211_start_tx_ba_session(sta, tid, 0);
		}
	}

	ath_debug_stat_rc(ath_rc_priv,
		ath_rc_get_rateindex(ath_rc_priv->rate_table,
			&tx_info->status.rates[final_ts_idx]));
}

static void ath_rate_init(void *priv, struct ieee80211_supported_band *sband,
                          struct ieee80211_sta *sta, void *priv_sta)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	const struct ath_rate_table *rate_table;
	bool is_cw40, is_sgi = false;
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

	is_cw40 = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_SUP_WIDTH_20_40);

	if (is_cw40)
		is_sgi = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40);
	else if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_SGI_20)
		is_sgi = !!(sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20);

	/* Choose rate table first */

	rate_table = ath_choose_rate_table(sc, sband->band,
	                      sta->ht_cap.ht_supported);

	ath_rc_priv->ht_cap = ath_rc_build_ht_caps(sc, sta, is_cw40, is_sgi);
	ath_rc_init(sc, priv_sta, sband, sta, rate_table);
}

static void ath_rate_update(void *priv, struct ieee80211_supported_band *sband,
			    struct ieee80211_sta *sta, void *priv_sta,
			    u32 changed, enum nl80211_channel_type oper_chan_type)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	const struct ath_rate_table *rate_table = NULL;
	bool oper_cw40 = false, oper_sgi;
	bool local_cw40 = !!(ath_rc_priv->ht_cap & WLAN_RC_40_FLAG);
	bool local_sgi = !!(ath_rc_priv->ht_cap & WLAN_RC_SGI_FLAG);

	/* FIXME: Handle AP mode later when we support CWM */

	if (changed & IEEE80211_RC_HT_CHANGED) {
		if (sc->sc_ah->opmode != NL80211_IFTYPE_STATION)
			return;

		if (oper_chan_type == NL80211_CHAN_HT40MINUS ||
		    oper_chan_type == NL80211_CHAN_HT40PLUS)
			oper_cw40 = true;

		if (oper_cw40)
			oper_sgi = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40) ?
				   true : false;
		else if (sc->sc_ah->caps.hw_caps & ATH9K_HW_CAP_SGI_20)
			oper_sgi = (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20) ?
				   true : false;
		else
			oper_sgi = false;

		if ((local_cw40 != oper_cw40) || (local_sgi != oper_sgi)) {
			rate_table = ath_choose_rate_table(sc, sband->band,
						   sta->ht_cap.ht_supported);
			ath_rc_priv->ht_cap = ath_rc_build_ht_caps(sc, sta,
						   oper_cw40, oper_sgi);
			ath_rc_init(sc, priv_sta, sband, sta, rate_table);

			ath_dbg(ath9k_hw_common(sc->sc_ah), ATH_DBG_CONFIG,
				"Operating HT Bandwidth changed to: %d\n",
				sc->hw->conf.channel_type);
		}
	}
}

#ifdef CONFIG_ATH9K_DEBUGFS

static int ath9k_debugfs_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t read_file_rcstat(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath_rate_priv *rc = file->private_data;
	char *buf;
	unsigned int len = 0, max;
	int i = 0;
	ssize_t retval;

	if (rc->rate_table == NULL)
		return 0;

	max = 80 + rc->rate_table_size * 1024 + 1;
	buf = kmalloc(max, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len += sprintf(buf, "%6s %6s %6s "
		       "%10s %10s %10s %10s\n",
		       "HT", "MCS", "Rate",
		       "Success", "Retries", "XRetries", "PER");

	for (i = 0; i < rc->rate_table_size; i++) {
		u32 ratekbps = rc->rate_table->info[i].ratekbps;
		struct ath_rc_stats *stats = &rc->rcstats[i];
		char mcs[5];
		char htmode[5];
		int used_mcs = 0, used_htmode = 0;

		if (WLAN_RC_PHY_HT(rc->rate_table->info[i].phy)) {
			used_mcs = snprintf(mcs, 5, "%d",
				rc->rate_table->info[i].ratecode);

			if (WLAN_RC_PHY_40(rc->rate_table->info[i].phy))
				used_htmode = snprintf(htmode, 5, "HT40");
			else if (WLAN_RC_PHY_20(rc->rate_table->info[i].phy))
				used_htmode = snprintf(htmode, 5, "HT20");
			else
				used_htmode = snprintf(htmode, 5, "????");
		}

		mcs[used_mcs] = '\0';
		htmode[used_htmode] = '\0';

		len += snprintf(buf + len, max - len,
			"%6s %6s %3u.%d: "
			"%10u %10u %10u %10u\n",
			htmode,
			mcs,
			ratekbps / 1000,
			(ratekbps % 1000) / 100,
			stats->success,
			stats->retries,
			stats->xretries,
			stats->per);
	}

	if (len > max)
		len = max;

	retval = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return retval;
}

static const struct file_operations fops_rcstat = {
	.read = read_file_rcstat,
	.open = ath9k_debugfs_open,
	.owner = THIS_MODULE
};

static void ath_rate_add_sta_debugfs(void *priv, void *priv_sta,
				     struct dentry *dir)
{
	struct ath_rate_priv *rc = priv_sta;
	debugfs_create_file("rc_stats", S_IRUGO, dir, rc, &fops_rcstat);
}

#endif /* CONFIG_ATH9K_DEBUGFS */

static void *ath_rate_alloc(struct ieee80211_hw *hw, struct dentry *debugfsdir)
{
	return hw->priv;
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
		ath_err(ath9k_hw_common(sc->sc_ah),
			"Unable to allocate private rc structure\n");
		return NULL;
	}

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
#ifdef CONFIG_ATH9K_DEBUGFS
	.add_sta_debugfs = ath_rate_add_sta_debugfs,
#endif
};

int ath_rate_control_register(void)
{
	return ieee80211_rate_control_register(&ath_rate_ops);
}

void ath_rate_control_unregister(void)
{
	ieee80211_rate_control_unregister(&ath_rate_ops);
}
