/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D_DM_COMMON_H__
#define __RTL92D_DM_COMMON_H__

#define HAL_DM_DIG_DISABLE			BIT(0)
#define HAL_DM_HIPWR_DISABLE			BIT(1)

#define OFDM_TABLE_LENGTH			37
#define OFDM_TABLE_SIZE_92D			43
#define CCK_TABLE_LENGTH			33

#define CCK_TABLE_SIZE				33

#define BW_AUTO_SWITCH_HIGH_LOW			25
#define BW_AUTO_SWITCH_LOW_HIGH			30

#define DM_DIG_FA_UPPER				0x32
#define DM_DIG_FA_LOWER				0x20
#define DM_DIG_FA_TH0				0x100
#define DM_DIG_FA_TH1				0x400
#define DM_DIG_FA_TH2				0x600

#define RXPATHSELECTION_SS_TH_LOW		30
#define RXPATHSELECTION_DIFF_TH			18

#define DM_RATR_STA_INIT			0
#define DM_RATR_STA_HIGH			1
#define DM_RATR_STA_MIDDLE			2
#define DM_RATR_STA_LOW				3

#define CTS2SELF_THVAL				30
#define REGC38_TH				20

#define WAIOTTHVAL				25

#define TXHIGHPWRLEVEL_NORMAL			0
#define TXHIGHPWRLEVEL_LEVEL1			1
#define TXHIGHPWRLEVEL_LEVEL2			2
#define TXHIGHPWRLEVEL_BT1			3
#define TXHIGHPWRLEVEL_BT2			4

#define DM_TYPE_BYFW				0
#define DM_TYPE_BYDRIVER			1

#define TX_POWER_NEAR_FIELD_THRESH_LVL2		74
#define TX_POWER_NEAR_FIELD_THRESH_LVL1		67
#define INDEX_MAPPING_NUM			13

enum dm_1r_cca {
	CCA_1R = 0,
	CCA_2R = 1,
	CCA_MAX = 2,
};

enum dm_rf {
	RF_SAVE = 0,
	RF_NORMAL = 1,
	RF_MAX = 2,
};

enum dm_sw_ant_switch {
	ANS_ANTENNA_B = 1,
	ANS_ANTENNA_A = 2,
	ANS_ANTENNA_MAX = 3,
};

void rtl92d_dm_initialize_txpower_tracking(struct ieee80211_hw *hw);
void rtl92d_dm_check_txpower_tracking_thermal_meter(struct ieee80211_hw *hw);
void rtl92d_dm_false_alarm_counter_statistics(struct ieee80211_hw *hw);
void rtl92d_dm_find_minimum_rssi(struct ieee80211_hw *hw);
void rtl92d_dm_write_dig(struct ieee80211_hw *hw);
void rtl92d_dm_dig(struct ieee80211_hw *hw);
void rtl92d_dm_init_edca_turbo(struct ieee80211_hw *hw);
void rtl92d_dm_check_edca_turbo(struct ieee80211_hw *hw);
void rtl92d_dm_init_rate_adaptive_mask(struct ieee80211_hw *hw);

#endif
