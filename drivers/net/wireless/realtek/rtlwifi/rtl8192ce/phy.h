/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92C_PHY_H__
#define __RTL92C_PHY_H__

#define MAX_PRECMD_CNT			16
#define MAX_RFDEPENDCMD_CNT		16
#define MAX_POSTCMD_CNT			16

#define MAX_DOZE_WAITING_TIMES_9x	64

#define RT_CANNOT_IO(hw)		false
#define HIGHPOWER_RADIOA_ARRAYLEN	22

#define MAX_TOLERANCE			5

#define	APK_BB_REG_NUM			5
#define	APK_AFE_REG_NUM			16
#define	APK_CURVE_REG_NUM		4
#define	PATH_NUM			2

#define LOOP_LIMIT			5
#define MAX_STALL_TIME			50
#define ANTENNADIVERSITYVALUE		0x80
#define MAX_TXPWR_IDX_NMODE_92S		63
#define RESET_CNT_LIMIT			3

#define IQK_ADDA_REG_NUM		16
#define IQK_MAC_REG_NUM			4

#define IQK_DELAY_TIME			1

#define RF90_PATH_MAX			2

#define CT_OFFSET_MAC_ADDR		0X16

#define CT_OFFSET_CCK_TX_PWR_IDX	0x5A
#define CT_OFFSET_HT401S_TX_PWR_IDX	0x60
#define CT_OFFSET_HT402S_TX_PWR_IDX_DIF	0x66
#define CT_OFFSET_HT20_TX_PWR_IDX_DIFF	0x69
#define CT_OFFSET_OFDM_TX_PWR_IDX_DIFF	0x6C

#define CT_OFFSET_HT40_MAX_PWR_OFFSET	0x6F
#define CT_OFFSET_HT20_MAX_PWR_OFFSET	0x72

#define CT_OFFSET_CHANNEL_PLAH		0x75
#define CT_OFFSET_THERMAL_METER		0x78
#define CT_OFFSET_RF_OPTION		0x79
#define CT_OFFSET_VERSION		0x7E
#define CT_OFFSET_CUSTOMER_ID		0x7F

#define RTL92C_MAX_PATH_NUM		2

bool rtl92c_phy_bb_config(struct ieee80211_hw *hw);
u32 rtl92c_phy_query_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask);
void rtl92c_phy_set_bb_reg(struct ieee80211_hw *hw, u32 regaddr, u32 bitmask,
			   u32 data);
u32 rtl92c_phy_query_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			    u32 regaddr, u32 bitmask);
void rtl92ce_phy_set_rf_reg(struct ieee80211_hw *hw, enum radio_path rfpath,
			    u32 regaddr, u32 bitmask, u32 data);
bool rtl92c_phy_mac_config(struct ieee80211_hw *hw);
bool rtl92ce_phy_bb_config(struct ieee80211_hw *hw);
bool rtl92c_phy_rf_config(struct ieee80211_hw *hw);
bool rtl92c_phy_config_rf_with_feaderfile(struct ieee80211_hw *hw,
					  enum radio_path rfpath);
void rtl92c_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw);
void rtl92c_phy_get_txpower_level(struct ieee80211_hw *hw, long *powerlevel);
void rtl92c_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel);
bool rtl92c_phy_update_txpower_dbm(struct ieee80211_hw *hw,
					  long power_indbm);
void rtl92c_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type);
void rtl92c_phy_sw_chnl_callback(struct ieee80211_hw *hw);
u8 rtl92c_phy_sw_chnl(struct ieee80211_hw *hw);
void rtl92c_phy_iq_calibrate(struct ieee80211_hw *hw, bool b_recovery);
void rtl92c_phy_set_beacon_hw_reg(struct ieee80211_hw *hw, u16 beaconinterval);
void rtl92c_phy_ap_calibrate(struct ieee80211_hw *hw, s8 delta);
void rtl92c_phy_lc_calibrate(struct ieee80211_hw *hw);
void _rtl92ce_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t);
void rtl92c_phy_set_rfpath_switch(struct ieee80211_hw *hw, bool bmain);
bool rtl92c_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					  enum radio_path rfpath);
bool rtl8192_phy_check_is_legal_rfpath(struct ieee80211_hw *hw,
				       u32 rfpath);
bool rtl92ce_phy_set_rf_power_state(struct ieee80211_hw *hw,
				    enum rf_pwrstate rfpwr_state);
void rtl92ce_phy_set_rf_on(struct ieee80211_hw *hw);
bool rtl92c_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype);
void rtl92c_phy_set_io(struct ieee80211_hw *hw);
void rtl92c_bb_block_on(struct ieee80211_hw *hw);
u32 _rtl92c_phy_rf_serial_read(struct ieee80211_hw *hw, enum radio_path rfpath,
			       u32 offset);
u32 _rtl92c_phy_fw_rf_serial_read(struct ieee80211_hw *hw,
				  enum radio_path rfpath, u32 offset);
u32 _rtl92c_phy_calculate_bit_shift(u32 bitmask);
void _rtl92c_phy_rf_serial_write(struct ieee80211_hw *hw,
				 enum radio_path rfpath, u32 offset, u32 data);
void _rtl92c_phy_fw_rf_serial_write(struct ieee80211_hw *hw,
				    enum radio_path rfpath, u32 offset,
				    u32 data);
void _rtl92c_store_pwrindex_diffrate_offset(struct ieee80211_hw *hw,
					    u32 regaddr, u32 bitmask, u32 data);
bool _rtl92ce_phy_config_mac_with_headerfile(struct ieee80211_hw *hw);
void _rtl92c_phy_init_bb_rf_register_definition(struct ieee80211_hw *hw);
bool _rtl92c_phy_bb8192c_config_parafile(struct ieee80211_hw *hw);
void _rtl92c_phy_set_rf_sleep(struct ieee80211_hw *hw);
bool rtl92c_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state);
bool _rtl92ce_phy_config_bb_with_headerfile(struct ieee80211_hw *hw,
					    u8 configtype);
bool _rtl92ce_phy_config_bb_with_pgheaderfile(struct ieee80211_hw *hw,
					      u8 configtype);
void rtl92ce_phy_set_bw_mode_callback(struct ieee80211_hw *hw);

#endif
