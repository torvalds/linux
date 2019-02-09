/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016  Realtek Corporation.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8822BE_PHY_H__
#define __RTL8822BE_PHY_H__

/* It must always set to 4, otherwise read
 * efuse table sequence will be wrong.
 */
#define MAX_TX_COUNT	4
#define TX_1S	0
#define TX_2S	1
#define TX_3S	2
#define TX_4S	3

#define MAX_POWER_INDEX	0x3F

#define MAX_PRECMD_CNT	16
#define MAX_RFDEPENDCMD_CNT	16
#define MAX_POSTCMD_CNT	16

#define MAX_DOZE_WAITING_TIMES_9x 64

#define RT_CANNOT_IO(hw) false
#define HIGHPOWER_RADIOA_ARRAYLEN	22

#define IQK_ADDA_REG_NUM	16
#define IQK_BB_REG_NUM	9
#define MAX_TOLERANCE	5
#define IQK_DELAY_TIME	10
#define index_mapping_NUM 15

#define APK_BB_REG_NUM	5
#define APK_AFE_REG_NUM	16
#define APK_CURVE_REG_NUM	4
#define PATH_NUM	2

#define LOOP_LIMIT	5
#define MAX_STALL_TIME	50
#define ANTENNA_DIVERSITY_VALUE	0x80
#define MAX_TXPWR_IDX_NMODE_92S	63
#define RESET_CNT_LIMIT	3

#define IQK_ADDA_REG_NUM	16
#define IQK_MAC_REG_NUM	4

#define RF6052_MAX_PATH	2

#define CT_OFFSET_MAC_ADDR	0X16

#define CT_OFFSET_CCK_TX_PWR_IDX	0x5A
#define CT_OFFSET_HT401S_TX_PWR_IDX	0x60
#define CT_OFFSET_HT402S_TX_PWR_IDX_DIFF	0x66
#define CT_OFFSET_HT20_TX_PWR_IDX_DIFF	0x69
#define CT_OFFSET_OFDM_TX_PWR_IDX_DIFF	0x6C

#define CT_OFFSET_HT40_MAX_PWR_OFFSET	0x6F
#define CT_OFFSET_HT20_MAX_PWR_OFFSET	0x72

#define CT_OFFSET_CHANNEL_PLAH	0x75
#define CT_OFFSET_THERMAL_METER	0x78
#define CT_OFFSET_RF_OPTION	0x79
#define CT_OFFSET_VERSION	0x7E
#define CT_OFFSET_CUSTOMER_ID	0x7F

#define RTL8822BE_MAX_PATH_NUM	2

#define TARGET_CHNL_NUM_2G_5G_8822B	59

u32 rtl8822be_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr,
			       u32 bitmask);
void rtl8822be_phy_set_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask,
			      u32 data);
u32 rtl8822be_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			       u32 regaddr, u32 bitmask);
void rtl8822be_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			      u32 regaddr, u32 bitmask, u32 data);
bool rtl8822be_phy_bb_config(struct ieee80211_hw *hw);
bool rtl8822be_phy_rf_config(struct ieee80211_hw *hw);
bool rtl8822be_halmac_cb_init_mac_register(struct rtl_priv *rtlpriv);
bool rtl8822be_halmac_cb_init_bb_rf_register(struct rtl_priv *rtlpriv);
void rtl8822be_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel);
void rtl8822be_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel);
void rtl8822be_phy_scan_operation_backup(struct ieee80211_hw *hw, u8 operation);
void rtl8822be_phy_set_bw_mode_callback(struct ieee80211_hw *hw);
void rtl8822be_phy_set_bw_mode(struct ieee80211_hw *hw,
			       enum nl80211_channel_type ch_type);
u8 rtl8822be_phy_sw_chnl(struct ieee80211_hw *hw);
void rtl8822be_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery);
void rtl8822be_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery);
void rtl8822be_phy_ap_calibrate(struct ieee80211_hw *hw, char delta);
void rtl8822be_phy_lc_calibrate(struct ieee80211_hw *hw);
void rtl8822be_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain);
bool rtl8822be_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					     enum radio_path rfpath);
bool rtl8822be_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					     enum radio_path rfpath);
bool rtl8822be_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype);
bool rtl8822be_phy_set_rf_power_state(struct ieee80211_hw *hw,
				      enum rf_pwrstate rfpwr_state);
void rtl8822be_phy_set_txpower_level_by_path(struct ieee80211_hw *hw,
					     u8 channel, u8 path);
void rtl8822be_do_iqk(struct ieee80211_hw *hw, u8 delta_thermal_index,
		      u8 thermal_value, u8 threshold);
void rtl8822be_do_iqk(struct ieee80211_hw *hw, u8 delta_thermal_index,
		      u8 thermal_value, u8 threshold);
void rtl8822be_reset_iqk_result(struct ieee80211_hw *hw);

u8 rtl8822be_get_txpower_index(struct ieee80211_hw *hw, u8 path, u8 rate,
			       u8 bandwidth, u8 channel);
void rtl8822be_phy_set_tx_power_index_by_rs(struct ieee80211_hw *hw, u8 channel,
					    u8 path, enum rate_section rs);
void rtl8822be_store_tx_power_by_rate(struct ieee80211_hw *hw, u32 band,
				      u32 rfpath, u32 txnum, u32 regaddr,
				      u32 bitmask, u32 data);
void rtl8822be_phy_set_txpower_limit(struct ieee80211_hw *hw, u8 *pregulation,
				     u8 *pband, u8 *pbandwidth,
				     u8 *prate_section, u8 *prf_path,
				     u8 *pchannel, u8 *ppower_limit);
bool rtl8822be_load_txpower_by_rate(struct ieee80211_hw *hw);
bool rtl8822be_load_txpower_limit(struct ieee80211_hw *hw);

#endif
