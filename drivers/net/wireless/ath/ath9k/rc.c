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
#include <linux/export.h>

#include "ath9k.h"

static const struct ath_rate_table ar5416_11na_ratetable = {
	68,
	8, /* MCS start */
	{
		[0] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 6000,
			5400, 0, 12 }, /* 6 Mb */
		[1] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 9000,
			7800,  1, 18 }, /* 9 Mb */
		[2] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 12000,
			10000, 2, 24 }, /* 12 Mb */
		[3] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 18000,
			13900, 3, 36 }, /* 18 Mb */
		[4] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 24000,
			17300, 4, 48 }, /* 24 Mb */
		[5] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 36000,
			23000, 5, 72 }, /* 36 Mb */
		[6] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 48000,
			27400, 6, 96 }, /* 48 Mb */
		[7] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 54000,
			29300, 7, 108 }, /* 54 Mb */
		[8] = { RC_HT_SDT_2040, WLAN_RC_PHY_HT_20_SS, 6500,
			6400, 0, 0 }, /* 6.5 Mb */
		[9] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 13000,
			12700, 1, 1 }, /* 13 Mb */
		[10] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 19500,
			18800, 2, 2 }, /* 19.5 Mb */
		[11] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 26000,
			25000, 3, 3 }, /* 26 Mb */
		[12] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 39000,
			36700, 4, 4 }, /* 39 Mb */
		[13] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 52000,
			48100, 5, 5 }, /* 52 Mb */
		[14] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 58500,
			53500, 6, 6 }, /* 58.5 Mb */
		[15] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 65000,
			59000, 7, 7 }, /* 65 Mb */
		[16] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS_HGI, 72200,
			65400, 7, 7 }, /* 75 Mb */
		[17] = { RC_INVALID, WLAN_RC_PHY_HT_20_DS, 13000,
			12700, 8, 8 }, /* 13 Mb */
		[18] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 26000,
			24800, 9, 9 }, /* 26 Mb */
		[19] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 39000,
			36600, 10, 10 }, /* 39 Mb */
		[20] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 52000,
			48100, 11, 11 }, /* 52 Mb */
		[21] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 78000,
			69500, 12, 12 }, /* 78 Mb */
		[22] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 104000,
			89500, 13, 13 }, /* 104 Mb */
		[23] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 117000,
			98900, 14, 14 }, /* 117 Mb */
		[24] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 130000,
			108300, 15, 15 }, /* 130 Mb */
		[25] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS_HGI, 144400,
			120000, 15, 15 }, /* 144.4 Mb */
		[26] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 19500,
			17400, 16, 16 }, /* 19.5 Mb */
		[27] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 39000,
			35100, 17, 17 }, /* 39 Mb */
		[28] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 58500,
			52600, 18, 18 }, /* 58.5 Mb */
		[29] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 78000,
			70400, 19, 19 }, /* 78 Mb */
		[30] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 117000,
			104900, 20, 20 }, /* 117 Mb */
		[31] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS_HGI, 130000,
			115800, 20, 20 }, /* 130 Mb*/
		[32] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 156000,
			137200, 21, 21 }, /* 156 Mb */
		[33] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 173300,
			151100, 21, 21 }, /* 173.3 Mb */
		[34] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 175500,
			152800, 22, 22 }, /* 175.5 Mb */
		[35] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 195000,
			168400, 22, 22 }, /* 195 Mb*/
		[36] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 195000,
			168400, 23, 23 }, /* 195 Mb */
		[37] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 216700,
			185000, 23, 23 }, /* 216.7 Mb */
		[38] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 13500,
			13200, 0, 0 }, /* 13.5 Mb*/
		[39] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 27500,
			25900, 1, 1 }, /* 27.0 Mb*/
		[40] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 40500,
			38600, 2, 2 }, /* 40.5 Mb*/
		[41] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 54000,
			49800, 3, 3 }, /* 54 Mb */
		[42] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 81500,
			72200, 4, 4 }, /* 81 Mb */
		[43] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 108000,
			92900, 5, 5 }, /* 108 Mb */
		[44] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 121500,
			102700, 6, 6 }, /* 121.5 Mb*/
		[45] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 135000,
			112000, 7, 7 }, /* 135 Mb */
		[46] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS_HGI, 150000,
			122000, 7, 7 }, /* 150 Mb */
		[47] = { RC_INVALID, WLAN_RC_PHY_HT_40_DS, 27000,
			25800, 8, 8 }, /* 27 Mb */
		[48] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 54000,
			49800, 9, 9 }, /* 54 Mb */
		[49] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 81000,
			71900, 10, 10 }, /* 81 Mb */
		[50] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 108000,
			92500, 11, 11 }, /* 108 Mb */
		[51] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 162000,
			130300, 12, 12 }, /* 162 Mb */
		[52] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 216000,
			162800, 13, 13 }, /* 216 Mb */
		[53] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 243000,
			178200, 14, 14 }, /* 243 Mb */
		[54] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 270000,
			192100, 15, 15 }, /* 270 Mb */
		[55] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS_HGI, 300000,
			207000, 15, 15 }, /* 300 Mb */
		[56] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 40500,
			36100, 16, 16 }, /* 40.5 Mb */
		[57] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 81000,
			72900, 17, 17 }, /* 81 Mb */
		[58] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 121500,
			108300, 18, 18 }, /* 121.5 Mb */
		[59] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 162000,
			142000, 19, 19 }, /*  162 Mb */
		[60] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 243000,
			205100, 20, 20 }, /*  243 Mb */
		[61] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS_HGI, 270000,
			224700, 20, 20 }, /*  270 Mb */
		[62] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 324000,
			263100, 21, 21 }, /*  324 Mb */
		[63] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 360000,
			288000, 21, 21 }, /*  360 Mb */
		[64] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 364500,
			290700, 22, 22 }, /* 364.5 Mb */
		[65] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 405000,
			317200, 22, 22 }, /* 405 Mb */
		[66] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 405000,
			317200, 23, 23 }, /* 405 Mb */
		[67] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 450000,
			346400, 23, 23 }, /* 450 Mb */
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
			900, 0, 2 }, /* 1 Mb */
		[1] = { RC_ALL, WLAN_RC_PHY_CCK, 2000,
			1900, 1, 4 }, /* 2 Mb */
		[2] = { RC_ALL, WLAN_RC_PHY_CCK, 5500,
			4900, 2, 11 }, /* 5.5 Mb */
		[3] = { RC_ALL, WLAN_RC_PHY_CCK, 11000,
			8100, 3, 22 }, /* 11 Mb */
		[4] = { RC_INVALID, WLAN_RC_PHY_OFDM, 6000,
			5400, 4, 12 }, /* 6 Mb */
		[5] = { RC_INVALID, WLAN_RC_PHY_OFDM, 9000,
			7800, 5, 18 }, /* 9 Mb */
		[6] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 12000,
			10100, 6, 24 }, /* 12 Mb */
		[7] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 18000,
			14100, 7, 36 }, /* 18 Mb */
		[8] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 24000,
			17700, 8, 48 }, /* 24 Mb */
		[9] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 36000,
			23700, 9, 72 }, /* 36 Mb */
		[10] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 48000,
			27400, 10, 96 }, /* 48 Mb */
		[11] = { RC_L_SDT, WLAN_RC_PHY_OFDM, 54000,
			30900, 11, 108 }, /* 54 Mb */
		[12] = { RC_INVALID, WLAN_RC_PHY_HT_20_SS, 6500,
			6400, 0, 0 }, /* 6.5 Mb */
		[13] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 13000,
			12700, 1, 1 }, /* 13 Mb */
		[14] = { RC_HT_SDT_20, WLAN_RC_PHY_HT_20_SS, 19500,
			18800, 2, 2 }, /* 19.5 Mb*/
		[15] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 26000,
			25000, 3, 3 }, /* 26 Mb */
		[16] = { RC_HT_SD_20, WLAN_RC_PHY_HT_20_SS, 39000,
			36700, 4, 4 }, /* 39 Mb */
		[17] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 52000,
			48100, 5, 5 }, /* 52 Mb */
		[18] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 58500,
			53500, 6, 6 }, /* 58.5 Mb */
		[19] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS, 65000,
			59000, 7, 7 }, /* 65 Mb */
		[20] = { RC_HT_S_20, WLAN_RC_PHY_HT_20_SS_HGI, 72200,
			65400, 7, 7 }, /* 65 Mb*/
		[21] = { RC_INVALID, WLAN_RC_PHY_HT_20_DS, 13000,
			12700, 8, 8 }, /* 13 Mb */
		[22] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 26000,
			24800, 9, 9 }, /* 26 Mb */
		[23] = { RC_HT_T_20, WLAN_RC_PHY_HT_20_DS, 39000,
			36600, 10, 10 }, /* 39 Mb */
		[24] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 52000,
			48100, 11, 11 }, /* 52 Mb */
		[25] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 78000,
			69500, 12, 12 }, /* 78 Mb */
		[26] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 104000,
			89500, 13, 13 }, /* 104 Mb */
		[27] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 117000,
			98900, 14, 14 }, /* 117 Mb */
		[28] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS, 130000,
			108300, 15, 15 }, /* 130 Mb */
		[29] = { RC_HT_DT_20, WLAN_RC_PHY_HT_20_DS_HGI, 144400,
			120000, 15, 15 }, /* 144.4 Mb */
		[30] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 19500,
			17400, 16, 16 }, /* 19.5 Mb */
		[31] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 39000,
			35100, 17, 17 }, /* 39 Mb */
		[32] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 58500,
			52600, 18, 18 }, /* 58.5 Mb */
		[33] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 78000,
			70400, 19, 19 }, /* 78 Mb */
		[34] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS, 117000,
			104900, 20, 20 }, /* 117 Mb */
		[35] = {  RC_INVALID, WLAN_RC_PHY_HT_20_TS_HGI, 130000,
			115800, 20, 20 }, /* 130 Mb */
		[36] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 156000,
			137200, 21, 21 }, /* 156 Mb */
		[37] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 173300,
			151100, 21, 21 }, /* 173.3 Mb */
		[38] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 175500,
			152800, 22, 22 }, /* 175.5 Mb */
		[39] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 195000,
			168400, 22, 22 }, /* 195 Mb */
		[40] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS, 195000,
			168400, 23, 23 }, /* 195 Mb */
		[41] = {  RC_HT_T_20, WLAN_RC_PHY_HT_20_TS_HGI, 216700,
			185000, 23, 23 }, /* 216.7 Mb */
		[42] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 13500,
			13200, 0, 0 }, /* 13.5 Mb */
		[43] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 27500,
			25900, 1, 1 }, /* 27.0 Mb */
		[44] = { RC_HT_SDT_40, WLAN_RC_PHY_HT_40_SS, 40500,
			38600, 2, 2 }, /* 40.5 Mb */
		[45] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 54000,
			49800, 3, 3 }, /* 54 Mb */
		[46] = { RC_HT_SD_40, WLAN_RC_PHY_HT_40_SS, 81500,
			72200, 4, 4 }, /* 81 Mb */
		[47] = { RC_HT_S_40 , WLAN_RC_PHY_HT_40_SS, 108000,
			92900, 5, 5 }, /* 108 Mb */
		[48] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 121500,
			102700, 6, 6 }, /* 121.5 Mb */
		[49] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS, 135000,
			112000, 7, 7 }, /* 135 Mb */
		[50] = { RC_HT_S_40, WLAN_RC_PHY_HT_40_SS_HGI, 150000,
			122000, 7, 7 }, /* 150 Mb */
		[51] = { RC_INVALID, WLAN_RC_PHY_HT_40_DS, 27000,
			25800, 8, 8 }, /* 27 Mb */
		[52] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 54000,
			49800, 9, 9 }, /* 54 Mb */
		[53] = { RC_HT_T_40, WLAN_RC_PHY_HT_40_DS, 81000,
			71900, 10, 10 }, /* 81 Mb */
		[54] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 108000,
			92500, 11, 11 }, /* 108 Mb */
		[55] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 162000,
			130300, 12, 12 }, /* 162 Mb */
		[56] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 216000,
			162800, 13, 13 }, /* 216 Mb */
		[57] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 243000,
			178200, 14, 14 }, /* 243 Mb */
		[58] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS, 270000,
			192100, 15, 15 }, /* 270 Mb */
		[59] = { RC_HT_DT_40, WLAN_RC_PHY_HT_40_DS_HGI, 300000,
			207000, 15, 15 }, /* 300 Mb */
		[60] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 40500,
			36100, 16, 16 }, /* 40.5 Mb */
		[61] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 81000,
			72900, 17, 17 }, /* 81 Mb */
		[62] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 121500,
			108300, 18, 18 }, /* 121.5 Mb */
		[63] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 162000,
			142000, 19, 19 }, /* 162 Mb */
		[64] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS, 243000,
			205100, 20, 20 }, /* 243 Mb */
		[65] = {  RC_INVALID, WLAN_RC_PHY_HT_40_TS_HGI, 270000,
			224700, 20, 20 }, /* 270 Mb */
		[66] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 324000,
			263100, 21, 21 }, /* 324 Mb */
		[67] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 360000,
			288000, 21, 21 }, /* 360 Mb */
		[68] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 364500,
			290700, 22, 22 }, /* 364.5 Mb */
		[69] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 405000,
			317200, 22, 22 }, /* 405 Mb */
		[70] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS, 405000,
			317200, 23, 23 }, /* 405 Mb */
		[71] = {  RC_HT_T_40, WLAN_RC_PHY_HT_40_TS_HGI, 450000,
			346400, 23, 23 }, /* 450 Mb */
	},
	50,  /* probe interval */
	WLAN_RC_HT_FLAG,  /* Phy rates allowed initially */
};

static const struct ath_rate_table ar5416_11a_ratetable = {
	8,
	0,
	{
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 0, 12},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800,  1, 18},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10000, 2, 24},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			13900, 3, 36},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17300, 4, 48},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23000, 5, 72},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 6, 96},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
			29300, 7, 108},
	},
	50,  /* probe interval */
	0,   /* Phy rates allowed initially */
};

static const struct ath_rate_table ar5416_11g_ratetable = {
	12,
	0,
	{
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 1000, /* 1 Mb */
			900, 0, 2},
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 2000, /* 2 Mb */
			1900, 1, 4},
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 5500, /* 5.5 Mb */
			4900, 2, 11},
		{ RC_L_SDT, WLAN_RC_PHY_CCK, 11000, /* 11 Mb */
			8100, 3, 22},
		{ RC_INVALID, WLAN_RC_PHY_OFDM, 6000, /* 6 Mb */
			5400, 4, 12},
		{ RC_INVALID, WLAN_RC_PHY_OFDM, 9000, /* 9 Mb */
			7800, 5, 18},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 12000, /* 12 Mb */
			10000, 6, 24},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 18000, /* 18 Mb */
			13900, 7, 36},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 24000, /* 24 Mb */
			17300, 8, 48},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 36000, /* 36 Mb */
			23000, 9, 72},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 48000, /* 48 Mb */
			27400, 10, 96},
		{ RC_L_SDT, WLAN_RC_PHY_OFDM, 54000, /* 54 Mb */
			29300, 11, 108},
	},
	50,  /* probe interval */
	0,   /* Phy rates allowed initially */
};

static int ath_rc_get_rateindex(struct ath_rate_priv *ath_rc_priv,
				struct ieee80211_tx_rate *rate)
{
	const struct ath_rate_table *rate_table = ath_rc_priv->rate_table;
	int rix, i, idx = 0;

	if (!(rate->flags & IEEE80211_TX_RC_MCS))
		return rate->idx;

	for (i = 0; i < ath_rc_priv->max_valid_rate; i++) {
		idx = ath_rc_priv->valid_rate_index[i];

		if (WLAN_RC_PHY_HT(rate_table->info[idx].phy) &&
		    rate_table->info[idx].ratecode == rate->idx)
			break;
	}

	rix = idx;

	if (rate->flags & IEEE80211_TX_RC_SHORT_GI)
		rix++;

	return rix;
}

static void ath_rc_sort_validrates(struct ath_rate_priv *ath_rc_priv)
{
	const struct ath_rate_table *rate_table = ath_rc_priv->rate_table;
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
ath_rc_get_lower_rix(struct ath_rate_priv *ath_rc_priv,
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

static u8 ath_rc_init_validrates(struct ath_rate_priv *ath_rc_priv)
{
	const struct ath_rate_table *rate_table = ath_rc_priv->rate_table;
	u8 i, hi = 0;

	for (i = 0; i < rate_table->rate_cnt; i++) {
		if (rate_table->info[i].rate_flags & RC_LEGACY) {
			u32 phy = rate_table->info[i].phy;
			u8 valid_rate_count = 0;

			if (!ath_rc_valid_phyrate(phy, ath_rc_priv->ht_cap, 0))
				continue;

			valid_rate_count = ath_rc_priv->valid_phy_ratecnt[phy];

			ath_rc_priv->valid_phy_rateidx[phy][valid_rate_count] = i;
			ath_rc_priv->valid_phy_ratecnt[phy] += 1;
			ath_rc_priv->valid_rate_index[i] = true;
			hi = i;
		}
	}

	return hi;
}

static inline bool ath_rc_check_legacy(u8 rate, u8 dot11rate, u16 rate_flags,
				       u32 phy, u32 capflag)
{
	if (rate != dot11rate || WLAN_RC_PHY_HT(phy))
		return false;

	if ((rate_flags & WLAN_RC_CAP_MODE(capflag)) != WLAN_RC_CAP_MODE(capflag))
		return false;

	if (!(rate_flags & WLAN_RC_CAP_STREAM(capflag)))
		return false;

	return true;
}

static inline bool ath_rc_check_ht(u8 rate, u8 dot11rate, u16 rate_flags,
				   u32 phy, u32 capflag)
{
	if (rate != dot11rate || !WLAN_RC_PHY_HT(phy))
		return false;

	if (!WLAN_RC_PHY_HT_VALID(rate_flags, capflag))
		return false;

	if (!(rate_flags & WLAN_RC_CAP_STREAM(capflag)))
		return false;

	return true;
}

static u8 ath_rc_setvalid_rates(struct ath_rate_priv *ath_rc_priv, bool legacy)
{
	const struct ath_rate_table *rate_table = ath_rc_priv->rate_table;
	struct ath_rateset *rateset;
	u32 phy, capflag = ath_rc_priv->ht_cap;
	u16 rate_flags;
	u8 i, j, hi = 0, rate, dot11rate, valid_rate_count;

	if (legacy)
		rateset = &ath_rc_priv->neg_rates;
	else
		rateset = &ath_rc_priv->neg_ht_rates;

	for (i = 0; i < rateset->rs_nrates; i++) {
		for (j = 0; j < rate_table->rate_cnt; j++) {
			phy = rate_table->info[j].phy;
			rate_flags = rate_table->info[j].rate_flags;
			rate = rateset->rs_rates[i];
			dot11rate = rate_table->info[j].dot11rate;

			if (legacy &&
			    !ath_rc_check_legacy(rate, dot11rate,
						 rate_flags, phy, capflag))
				continue;

			if (!legacy &&
			    !ath_rc_check_ht(rate, dot11rate,
					     rate_flags, phy, capflag))
				continue;

			if (!ath_rc_valid_phyrate(phy, capflag, 0))
				continue;

			valid_rate_count = ath_rc_priv->valid_phy_ratecnt[phy];
			ath_rc_priv->valid_phy_rateidx[phy][valid_rate_count] = j;
			ath_rc_priv->valid_phy_ratecnt[phy] += 1;
			ath_rc_priv->valid_rate_index[j] = true;
			hi = max(hi, j);
		}
	}

	return hi;
}

static u8 ath_rc_get_highest_rix(struct ath_rate_priv *ath_rc_priv,
				 int *is_probing)
{
	const struct ath_rate_table *rate_table = ath_rc_priv->rate_table;
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
	WARN_ON_ONCE(1);

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
	struct ieee80211_bss_conf *bss_conf;

	if (!tx_info->control.vif)
		return;
	/*
	 * For legacy frames, mac80211 takes care of CTS protection.
	 */
	if (!(tx_info->control.rates[0].flags & IEEE80211_TX_RC_MCS))
		return;

	bss_conf = &tx_info->control.vif->bss_conf;

	if (!bss_conf->basic_rates)
		return;

	/*
	 * For now, use the lowest allowed basic rate for HT frames.
	 */
	tx_info->control.rts_cts_rate_idx = __ffs(bss_conf->basic_rates);
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
	u8 try_per_rate, i = 0, rix;
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
	rix = ath_rc_get_highest_rix(ath_rc_priv, &is_probe);

	if (conf_is_ht(&sc->hw->conf) &&
	    (sta->ht_cap.cap & IEEE80211_HT_CAP_LDPC_CODING))
		tx_info->flags |= IEEE80211_TX_CTL_LDPC;

	if (conf_is_ht(&sc->hw->conf) &&
	    (sta->ht_cap.cap & IEEE80211_HT_CAP_TX_STBC))
		tx_info->flags |= (1 << IEEE80211_TX_CTL_STBC_SHIFT);

	if (is_probe) {
		/*
		 * Set one try for probe rates. For the
		 * probes don't enable RTS.
		 */
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       1, rix, 0);
		/*
		 * Get the next tried/allowed rate.
		 * No RTS for the next series after the probe rate.
		 */
		ath_rc_get_lower_rix(ath_rc_priv, rix, &rix);
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       try_per_rate, rix, 0);

		tx_info->flags |= IEEE80211_TX_CTL_RATE_CTRL_PROBE;
	} else {
		/*
		 * Set the chosen rate. No RTS for first series entry.
		 */
		ath_rc_rate_set_series(rate_table, &rates[i++], txrc,
				       try_per_rate, rix, 0);
	}

	for ( ; i < 4; i++) {
		/*
		 * Use twice the number of tries for the last MRR segment.
		 */
		if (i + 1 == 4)
			try_per_rate = 8;

		ath_rc_get_lower_rix(ath_rc_priv, rix, &rix);

		/*
		 * All other rates in the series have RTS enabled.
		 */
		ath_rc_rate_set_series(rate_table, &rates[i], txrc,
				       try_per_rate, rix, 1);
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
	if ((sc->hw->conf.chandef.chan->band == IEEE80211_BAND_2GHZ) &&
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
		ath_rc_get_lower_rix(ath_rc_priv, (u8)tx_rate,
				     &ath_rc_priv->rate_max_phy);

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
			     struct sk_buff *skb)
{
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_tx_rate *rates = tx_info->status.rates;
	struct ieee80211_tx_rate *rate;
	int final_ts_idx = 0, xretries = 0, long_retry = 0;
	u8 flags;
	u32 i = 0, rix;

	for (i = 0; i < sc->hw->max_rates; i++) {
		rate = &tx_info->status.rates[i];
		if (rate->idx < 0 || !rate->count)
			break;

		final_ts_idx = i;
		long_retry = rate->count - 1;
	}

	if (!(tx_info->flags & IEEE80211_TX_STAT_ACK))
		xretries = 1;

	/*
	 * If the first rate is not the final index, there
	 * are intermediate rate failures to be processed.
	 */
	if (final_ts_idx != 0) {
		for (i = 0; i < final_ts_idx ; i++) {
			if (rates[i].count != 0 && (rates[i].idx >= 0)) {
				flags = rates[i].flags;

				/* If HT40 and we have switched mode from
				 * 40 to 20 => don't update */

				if ((flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
				    !(ath_rc_priv->ht_cap & WLAN_RC_40_FLAG))
					return;

				rix = ath_rc_get_rateindex(ath_rc_priv, &rates[i]);
				ath_rc_update_ht(sc, ath_rc_priv, tx_info,
						 rix, xretries ? 1 : 2,
						 rates[i].count);
			}
		}
	}

	flags = rates[final_ts_idx].flags;

	/* If HT40 and we have switched mode from 40 to 20 => don't update */
	if ((flags & IEEE80211_TX_RC_40_MHZ_WIDTH) &&
	    !(ath_rc_priv->ht_cap & WLAN_RC_40_FLAG))
		return;

	rix = ath_rc_get_rateindex(ath_rc_priv, &rates[final_ts_idx]);
	ath_rc_update_ht(sc, ath_rc_priv, tx_info, rix, xretries, long_retry);
	ath_debug_stat_rc(ath_rc_priv, rix);
}

static const
struct ath_rate_table *ath_choose_rate_table(struct ath_softc *sc,
					     enum ieee80211_band band,
					     bool is_ht)
{
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
		return NULL;
	}
}

static void ath_rc_init(struct ath_softc *sc,
			struct ath_rate_priv *ath_rc_priv)
{
	const struct ath_rate_table *rate_table = ath_rc_priv->rate_table;
	struct ath_rateset *rateset = &ath_rc_priv->neg_rates;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	u8 i, j, k, hi = 0, hthi = 0;

	ath_rc_priv->rate_table_size = RATE_TABLE_SIZE;

	for (i = 0 ; i < ath_rc_priv->rate_table_size; i++) {
		ath_rc_priv->per[i] = 0;
		ath_rc_priv->valid_rate_index[i] = 0;
	}

	for (i = 0; i < WLAN_RC_PHY_MAX; i++) {
		for (j = 0; j < RATE_TABLE_SIZE; j++)
			ath_rc_priv->valid_phy_rateidx[i][j] = 0;
		ath_rc_priv->valid_phy_ratecnt[i] = 0;
	}

	if (!rateset->rs_nrates) {
		hi = ath_rc_init_validrates(ath_rc_priv);
	} else {
		hi = ath_rc_setvalid_rates(ath_rc_priv, true);

		if (ath_rc_priv->ht_cap & WLAN_RC_HT_FLAG)
			hthi = ath_rc_setvalid_rates(ath_rc_priv, false);

		hi = max(hi, hthi);
	}

	ath_rc_priv->rate_table_size = hi + 1;
	ath_rc_priv->rate_max_phy = 0;
	WARN_ON(ath_rc_priv->rate_table_size > RATE_TABLE_SIZE);

	for (i = 0, k = 0; i < WLAN_RC_PHY_MAX; i++) {
		for (j = 0; j < ath_rc_priv->valid_phy_ratecnt[i]; j++) {
			ath_rc_priv->valid_rate_index[k++] =
				ath_rc_priv->valid_phy_rateidx[i][j];
		}

		if (!ath_rc_valid_phyrate(i, rate_table->initial_ratemax, 1) ||
		    !ath_rc_priv->valid_phy_ratecnt[i])
			continue;

		ath_rc_priv->rate_max_phy = ath_rc_priv->valid_phy_rateidx[i][j-1];
	}
	WARN_ON(ath_rc_priv->rate_table_size > RATE_TABLE_SIZE);
	WARN_ON(k > RATE_TABLE_SIZE);

	ath_rc_priv->max_valid_rate = k;
	ath_rc_sort_validrates(ath_rc_priv);
	ath_rc_priv->rate_max_phy = (k > 4) ?
		ath_rc_priv->valid_rate_index[k-4] :
		ath_rc_priv->valid_rate_index[k-1];

	ath_dbg(common, CONFIG, "RC Initialized with capabilities: 0x%x\n",
		ath_rc_priv->ht_cap);
}

static u8 ath_rc_build_ht_caps(struct ath_softc *sc, struct ieee80211_sta *sta)
{
	u8 caps = 0;

	if (sta->ht_cap.ht_supported) {
		caps = WLAN_RC_HT_FLAG;
		if (sta->ht_cap.mcs.rx_mask[1] && sta->ht_cap.mcs.rx_mask[2])
			caps |= WLAN_RC_TS_FLAG | WLAN_RC_DS_FLAG;
		else if (sta->ht_cap.mcs.rx_mask[1])
			caps |= WLAN_RC_DS_FLAG;
		if (sta->bandwidth >= IEEE80211_STA_RX_BW_40) {
			caps |= WLAN_RC_40_FLAG;
			if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_40)
				caps |= WLAN_RC_SGI_FLAG;
		} else {
			if (sta->ht_cap.cap & IEEE80211_HT_CAP_SGI_20)
				caps |= WLAN_RC_SGI_FLAG;
		}
	}

	return caps;
}

static bool ath_tx_aggr_check(struct ath_softc *sc, struct ieee80211_sta *sta,
			      u8 tidno)
{
	struct ath_node *an = (struct ath_node *)sta->drv_priv;
	struct ath_atx_tid *txtid;

	if (!sta->ht_cap.ht_supported)
		return false;

	txtid = ATH_AN_2_TID(an, tidno);
	return !txtid->active;
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
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(skb);
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)skb->data;
	__le16 fc = hdr->frame_control;

	if (!priv_sta || !ieee80211_is_data(fc))
		return;

	/* This packet was aggregated but doesn't carry status info */
	if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) &&
	    !(tx_info->flags & IEEE80211_TX_STAT_AMPDU))
		return;

	if (tx_info->flags & IEEE80211_TX_STAT_TX_FILTERED)
		return;

	ath_rc_tx_status(sc, ath_rc_priv, skb);

	/* Check if aggregation has to be enabled for this tid */
	if (conf_is_ht(&sc->hw->conf) &&
	    !(skb->protocol == cpu_to_be16(ETH_P_PAE))) {
		if (ieee80211_is_data_qos(fc) &&
		    skb_get_queue_mapping(skb) != IEEE80211_AC_VO) {
			u8 *qc, tid;

			qc = ieee80211_get_qos_ctl(hdr);
			tid = qc[0] & 0xf;

			if(ath_tx_aggr_check(sc, sta, tid))
				ieee80211_start_tx_ba_session(sta, tid, 0);
		}
	}
}

static void ath_rate_init(void *priv, struct ieee80211_supported_band *sband,
			  struct cfg80211_chan_def *chandef,
                          struct ieee80211_sta *sta, void *priv_sta)
{
	struct ath_softc *sc = priv;
	struct ath_common *common = ath9k_hw_common(sc->sc_ah);
	struct ath_rate_priv *ath_rc_priv = priv_sta;
	int i, j = 0;
	u32 rate_flags = ieee80211_chandef_rate_flags(&sc->hw->conf.chandef);

	for (i = 0; i < sband->n_bitrates; i++) {
		if (sta->supp_rates[sband->band] & BIT(i)) {
			if ((rate_flags & sband->bitrates[i].flags)
			    != rate_flags)
				continue;

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

	ath_rc_priv->rate_table = ath_choose_rate_table(sc, sband->band,
							sta->ht_cap.ht_supported);
	if (!ath_rc_priv->rate_table) {
		ath_err(common, "No rate table chosen\n");
		return;
	}

	ath_rc_priv->ht_cap = ath_rc_build_ht_caps(sc, sta);
	ath_rc_init(sc, priv_sta);
}

static void ath_rate_update(void *priv, struct ieee80211_supported_band *sband,
			    struct cfg80211_chan_def *chandef,
			    struct ieee80211_sta *sta, void *priv_sta,
			    u32 changed)
{
	struct ath_softc *sc = priv;
	struct ath_rate_priv *ath_rc_priv = priv_sta;

	if (changed & IEEE80211_RC_BW_CHANGED) {
		ath_rc_priv->ht_cap = ath_rc_build_ht_caps(sc, sta);
		ath_rc_init(sc, priv_sta);

		ath_dbg(ath9k_hw_common(sc->sc_ah), CONFIG,
			"Operating Bandwidth changed to: %d\n",
			sc->hw->conf.chandef.width);
	}
}

#if defined(CONFIG_MAC80211_DEBUGFS) && defined(CONFIG_ATH9K_DEBUGFS)

void ath_debug_stat_rc(struct ath_rate_priv *rc, int final_rate)
{
	struct ath_rc_stats *stats;

	stats = &rc->rcstats[final_rate];
	stats->success++;
}

void ath_debug_stat_retries(struct ath_rate_priv *rc, int rix,
			    int xretries, int retries, u8 per)
{
	struct ath_rc_stats *stats = &rc->rcstats[rix];

	stats->xretries += xretries;
	stats->retries += retries;
	stats->per = per;
}

static ssize_t read_file_rcstat(struct file *file, char __user *user_buf,
				size_t count, loff_t *ppos)
{
	struct ath_rate_priv *rc = file->private_data;
	char *buf;
	unsigned int len = 0, max;
	int rix;
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

	for (rix = 0; rix < rc->max_valid_rate; rix++) {
		u8 i = rc->valid_rate_index[rix];
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
	.open = simple_open,
	.owner = THIS_MODULE
};

static void ath_rate_add_sta_debugfs(void *priv, void *priv_sta,
				     struct dentry *dir)
{
	struct ath_rate_priv *rc = priv_sta;
	rc->debugfs_rcstats = debugfs_create_file("rc_stats", S_IRUGO,
						  dir, rc, &fops_rcstat);
}

static void ath_rate_remove_sta_debugfs(void *priv, void *priv_sta)
{
	struct ath_rate_priv *rc = priv_sta;
	debugfs_remove(rc->debugfs_rcstats);
}

#endif /* CONFIG_MAC80211_DEBUGFS && CONFIG_ATH9K_DEBUGFS */

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
	return kzalloc(sizeof(struct ath_rate_priv), gfp);
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

#if defined(CONFIG_MAC80211_DEBUGFS) && defined(CONFIG_ATH9K_DEBUGFS)
	.add_sta_debugfs = ath_rate_add_sta_debugfs,
	.remove_sta_debugfs = ath_rate_remove_sta_debugfs,
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
