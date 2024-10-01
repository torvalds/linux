/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D_PHY_H__
#define __RTL92D_PHY_H__

#define MAX_PRECMD_CNT				16
#define MAX_RFDEPENDCMD_CNT			16
#define MAX_POSTCMD_CNT				16

#define MAX_DOZE_WAITING_TIMES_9x		64

#define HIGHPOWER_RADIOA_ARRAYLEN		22

#define	APK_BB_REG_NUM				5
#define	APK_AFE_REG_NUM				16
#define	APK_CURVE_REG_NUM			4
#define	PATH_NUM				2

#define LOOP_LIMIT				5
#define MAX_STALL_TIME				50
#define ANTENNA_DIVERSITY_VALUE			0x80
#define MAX_TXPWR_IDX_NMODE_92S			63
#define RESET_CNT_LIMIT				3

#define IQK_ADDA_REG_NUM			16
#define IQK_BB_REG_NUM_test			6
#define IQK_MAC_REG_NUM				4

#define CT_OFFSET_MAC_ADDR			0X16

#define CT_OFFSET_CCK_TX_PWR_IDX		0x5A
#define CT_OFFSET_HT401S_TX_PWR_IDX		0x60
#define CT_OFFSET_HT402S_TX_PWR_IDX_DIFF	0x66
#define CT_OFFSET_HT20_TX_PWR_IDX_DIFF		0x69
#define CT_OFFSET_OFDM_TX_PWR_IDX_DIFF		0x6C

#define CT_OFFSET_HT40_MAX_PWR_OFFSET		0x6F
#define CT_OFFSET_HT20_MAX_PWR_OFFSET		0x72

#define CT_OFFSET_CHANNEL_PLAH			0x75
#define CT_OFFSET_THERMAL_METER			0x78
#define CT_OFFSET_RF_OPTION			0x79
#define CT_OFFSET_VERSION			0x7E
#define CT_OFFSET_CUSTOMER_ID			0x7F

enum swchnlcmd_id {
	CMDID_END,
	CMDID_SET_TXPOWEROWER_LEVEL,
	CMDID_BBREGWRITE10,
	CMDID_WRITEPORT_ULONG,
	CMDID_WRITEPORT_USHORT,
	CMDID_WRITEPORT_UCHAR,
	CMDID_RF_WRITEREG,
};

struct swchnlcmd {
	enum swchnlcmd_id cmdid;
	u32 para1;
	u32 para2;
	u32 msdelay;
};

u32 rtl92d_phy_query_bb_reg(struct ieee80211_hw *hw,
			    u32 regaddr, u32 bitmask);
void rtl92d_phy_set_bb_reg(struct ieee80211_hw *hw,
			   u32 regaddr, u32 bitmask, u32 data);
bool rtl92d_phy_mac_config(struct ieee80211_hw *hw);
bool rtl92d_phy_bb_config(struct ieee80211_hw *hw);
bool rtl92d_phy_rf_config(struct ieee80211_hw *hw);
bool rtl92c_phy_config_rf_with_feaderfile(struct ieee80211_hw *hw,
					  enum radio_path rfpath);
void rtl92d_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type);
u8 rtl92d_phy_sw_chnl(struct ieee80211_hw *hw);
bool rtl92d_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					  enum rf_content content,
					  enum radio_path rfpath);
bool rtl92d_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state);

void rtl92d_phy_set_poweron(struct ieee80211_hw *hw);
bool rtl92d_phy_check_poweroff(struct ieee80211_hw *hw);
void rtl92d_phy_lc_calibrate(struct ieee80211_hw *hw, bool is2t);
void rtl92d_update_bbrf_configuration(struct ieee80211_hw *hw);
void rtl92d_phy_ap_calibrate(struct ieee80211_hw *hw, s8 delta);
void rtl92d_phy_iq_calibrate(struct ieee80211_hw *hw);
void rtl92d_phy_reload_iqk_setting(struct ieee80211_hw *hw, u8 channel);

#endif
