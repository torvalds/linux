/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2022-2023  Realtek Corporation
 */

#ifndef __RTW89_8851B_H__
#define __RTW89_8851B_H__

#include "core.h"

#define RF_PATH_NUM_8851B 1
#define BB_PATH_NUM_8851B 1

struct rtw8851bu_efuse {
	u8 rsvd[0x88];
	u8 mac_addr[ETH_ALEN];
};

struct rtw8851be_efuse {
	u8 mac_addr[ETH_ALEN];
};

struct rtw8851b_tssi_offset {
	u8 cck_tssi[TSSI_CCK_CH_GROUP_NUM];
	u8 bw40_tssi[TSSI_MCS_2G_CH_GROUP_NUM];
	u8 rsvd[7];
	u8 bw40_1s_tssi_5g[TSSI_MCS_5G_CH_GROUP_NUM];
} __packed;

struct rtw8851b_efuse {
	u8 rsvd[0x210];
	struct rtw8851b_tssi_offset path_a_tssi;
	u8 rsvd1[136];
	u8 channel_plan;
	u8 xtal_k;
	u8 rsvd2;
	u8 iqk_lck;
	u8 rsvd3[8];
	u8 eeprom_version;
	u8 customer_id;
	u8 tx_bb_swing_2g;
	u8 tx_bb_swing_5g;
	u8 tx_cali_pwr_trk_mode;
	u8 trx_path_selection;
	u8 rfe_type;
	u8 country_code[2];
	u8 rsvd4[3];
	u8 path_a_therm;
	u8 rsvd5[3];
	u8 rx_gain_2g_ofdm;
	u8 rsvd6;
	u8 rx_gain_2g_cck;
	u8 rsvd7;
	u8 rx_gain_5g_low;
	u8 rsvd8;
	u8 rx_gain_5g_mid;
	u8 rsvd9;
	u8 rx_gain_5g_high;
	u8 rsvd10[35];
	u8 path_a_cck_pwr_idx[6];
	u8 path_a_bw40_1tx_pwr_idx[5];
	u8 path_a_ofdm_1tx_pwr_idx_diff:4;
	u8 path_a_bw20_1tx_pwr_idx_diff:4;
	u8 path_a_bw20_2tx_pwr_idx_diff:4;
	u8 path_a_bw40_2tx_pwr_idx_diff:4;
	u8 path_a_cck_2tx_pwr_idx_diff:4;
	u8 path_a_ofdm_2tx_pwr_idx_diff:4;
	u8 rsvd11[0xf2];
	union {
		struct rtw8851bu_efuse u;
		struct rtw8851be_efuse e;
	};
} __packed;

extern const struct rtw89_chip_info rtw8851b_chip_info;

#endif
