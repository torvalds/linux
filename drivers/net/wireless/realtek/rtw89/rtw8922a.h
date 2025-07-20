/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2023  Realtek Corporation
 */

#ifndef __RTW89_8922A_H__
#define __RTW89_8922A_H__

#include "core.h"

#define RF_PATH_NUM_8922A 2
#define BB_PATH_NUM_8922A 2

struct rtw8922a_tssi_offset {
	u8 cck_tssi[TSSI_CCK_CH_GROUP_NUM];
	u8 bw40_tssi[TSSI_MCS_2G_CH_GROUP_NUM];
	u8 rsvd[7];
	u8 bw40_1s_tssi_5g[TSSI_MCS_5G_CH_GROUP_NUM];
	u8 bw_diff_5g[10];
} __packed;

struct rtw8922a_rx_gain {
	u8 _2g_ofdm;
	u8 _2g_cck;
	u8 _5g_low;
	u8 _5g_mid;
	u8 _5g_high;
} __packed;

struct rtw8922a_rx_gain_6g {
	u8 _6g_l0;
	u8 _6g_l1;
	u8 _6g_m0;
	u8 _6g_m1;
	u8 _6g_h0;
	u8 _6g_h1;
	u8 _6g_uh0;
	u8 _6g_uh1;
} __packed;

struct rtw8922a_efuse {
	u8 country_code[2];
	u8 rsvd[0xe];
	struct rtw8922a_tssi_offset path_a_tssi;
	struct rtw8922a_tssi_offset path_b_tssi;
	u8 rsvd1[0x54];
	u8 channel_plan;
	u8 xtal_k;
	u8 rsvd2[0x7];
	u8 board_info;
	u8 rsvd3[0x8];
	u8 rfe_type;
	u8 rsvd4[0x5];
	u8 path_a_therm;
	u8 path_b_therm;
	u8 rsvd5[0x2];
	struct rtw8922a_rx_gain rx_gain_a;
	struct rtw8922a_rx_gain rx_gain_b;
	u8 rsvd6[0x22];
	u8 bw40_1s_tssi_6g_a[TSSI_MCS_6G_CH_GROUP_NUM];
	u8 rsvd7[0xa];
	u8 bw40_1s_tssi_6g_b[TSSI_MCS_6G_CH_GROUP_NUM];
	u8 rsvd8[0xa];
	u8 bw40_1s_tssi_6g_c[TSSI_MCS_6G_CH_GROUP_NUM];
	u8 rsvd9[0xa];
	u8 bw40_1s_tssi_6g_d[TSSI_MCS_6G_CH_GROUP_NUM];
	u8 rsvd10[0xa];
	struct rtw8922a_rx_gain_6g rx_gain_6g_a;
	struct rtw8922a_rx_gain_6g rx_gain_6g_b;
} __packed;

extern const struct rtw89_chip_info rtw8922a_chip_info;
extern const struct rtw89_chip_variant rtw8922ae_vs_variant;

#endif
