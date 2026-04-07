/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2026  Realtek Corporation
 */

#ifndef __RTW89_8922D_H__
#define __RTW89_8922D_H__

#include "core.h"

#define RF_PATH_NUM_8922D 2
#define BB_PATH_NUM_8922D 2

struct rtw8922d_tssi_offset {
	u8 cck_tssi[TSSI_CCK_CH_GROUP_NUM];
	u8 bw40_tssi[TSSI_MCS_2G_CH_GROUP_NUM];
	u8 rsvd[7];
	u8 bw40_1s_tssi_5g[TSSI_MCS_5G_CH_GROUP_NUM];
	u8 bw_diff_5g[10];
} __packed;

struct rtw8922d_tssi_offset_6g {
	u8 bw40_1s_tssi_6g[TSSI_MCS_6G_CH_GROUP_NUM];
	u8 rsvd[0xa];
} __packed;

struct rtw8922d_rx_gain {
	u8 _2g_ofdm;
	u8 _2g_cck;
	u8 _5g_low;
	u8 _5g_mid;
	u8 _5g_high;
} __packed;

struct rtw8922d_rx_gain_6g {
	u8 _6g_l0;
	u8 _6g_l1;
	u8 _6g_m0;
	u8 _6g_m1;
	u8 _6g_h0;
	u8 _6g_h1;
	u8 _6g_uh0;
	u8 _6g_uh1;
} __packed;

struct rtw8922d_efuse {
	u8 country_code[2];
	u8 rsvd[0xe];
	struct rtw8922d_tssi_offset path_a_tssi;
	struct rtw8922d_tssi_offset path_b_tssi;
	u8 rsvd1[0x54];
	u8 channel_plan;
	u8 xtal_k;
	u8 rsvd2[0x7];
	u8 board_info;
	u8 rsvd3[0x8];
	u8 rfe_type;
	u8 rsvd4[2];
	u8 bt_setting_2;
	u8 bt_setting_3;
	u8 rsvd4_2;
	u8 path_a_therm;
	u8 path_b_therm;
	u8 rsvd5[0x2];
	struct rtw8922d_rx_gain rx_gain_a;
	struct rtw8922d_rx_gain rx_gain_b;
	u8 rsvd6[0x18];
	struct rtw8922d_rx_gain rx_gain_a_2;
	struct rtw8922d_rx_gain rx_gain_b_2;
	struct rtw8922d_tssi_offset_6g path_a_tssi_6g;
	struct rtw8922d_tssi_offset_6g path_b_tssi_6g;
	struct rtw8922d_tssi_offset_6g path_c_tssi_6g;
	struct rtw8922d_tssi_offset_6g path_d_tssi_6g;
	struct rtw8922d_rx_gain_6g rx_gain_6g_a;
	struct rtw8922d_rx_gain_6g rx_gain_6g_b;
	u8 rsvd7[0x5a];
	struct rtw8922d_rx_gain_6g rx_gain_6g_a_2;
	struct rtw8922d_rx_gain_6g rx_gain_6g_b_2;
} __packed;

extern const struct rtw89_chip_info rtw8922d_chip_info;
extern const struct rtw89_chip_variant rtw8922de_vs_variant;

#endif
