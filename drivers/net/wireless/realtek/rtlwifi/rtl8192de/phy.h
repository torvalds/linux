/******************************************************************************
 *
 * Copyright(c) 2009-2012  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL92D_PHY_H__
#define __RTL92D_PHY_H__

#define MAX_PRECMD_CNT				16
#define MAX_RFDEPENDCMD_CNT			16
#define MAX_POSTCMD_CNT				16

#define MAX_DOZE_WAITING_TIMES_9x		64

#define RT_CANNOT_IO(hw)			false
#define HIGHPOWER_RADIOA_ARRAYLEN		22

#define MAX_TOLERANCE				5

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
#define IQK_BB_REG_NUM				10
#define IQK_BB_REG_NUM_test			6
#define IQK_MAC_REG_NUM				4
#define RX_INDEX_MAPPING_NUM			15

#define IQK_DELAY_TIME				1

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

enum baseband_config_type {
	BASEBAND_CONFIG_PHY_REG = 0,
	BASEBAND_CONFIG_AGC_TAB = 1,
};

enum rf_content {
	radioa_txt = 0,
	radiob_txt = 1,
	radioc_txt = 2,
	radiod_txt = 3
};

static inline void rtl92d_acquire_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
						     unsigned long *flag)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.interfaceindex == 1)
		spin_lock_irqsave(&rtlpriv->locks.cck_and_rw_pagea_lock, *flag);
}

static inline void rtl92d_release_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
						     unsigned long *flag)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	if (rtlpriv->rtlhal.interfaceindex == 1)
		spin_unlock_irqrestore(&rtlpriv->locks.cck_and_rw_pagea_lock,
			*flag);
}

u32 rtl92d_phy_query_bb_reg(struct ieee80211_hw *hw,
			    u32 regaddr, u32 bitmask);
void rtl92d_phy_set_bb_reg(struct ieee80211_hw *hw,
			   u32 regaddr, u32 bitmask, u32 data);
u32 rtl92d_phy_query_rf_reg(struct ieee80211_hw *hw,
			    enum radio_path rfpath, u32 regaddr,
			    u32 bitmask);
void rtl92d_phy_set_rf_reg(struct ieee80211_hw *hw,
			   enum radio_path rfpath, u32 regaddr,
			   u32 bitmask, u32 data);
bool rtl92d_phy_mac_config(struct ieee80211_hw *hw);
bool rtl92d_phy_bb_config(struct ieee80211_hw *hw);
bool rtl92d_phy_rf_config(struct ieee80211_hw *hw);
bool rtl92c_phy_config_rf_with_feaderfile(struct ieee80211_hw *hw,
					  enum radio_path rfpath);
void rtl92d_phy_get_hw_reg_originalvalue(struct ieee80211_hw *hw);
void rtl92d_phy_set_txpower_level(struct ieee80211_hw *hw, u8 channel);
void rtl92d_phy_set_bw_mode(struct ieee80211_hw *hw,
			    enum nl80211_channel_type ch_type);
u8 rtl92d_phy_sw_chnl(struct ieee80211_hw *hw);
bool rtl92d_phy_config_rf_with_headerfile(struct ieee80211_hw *hw,
					  enum rf_content content,
					  enum radio_path rfpath);
bool rtl92d_phy_set_io_cmd(struct ieee80211_hw *hw, enum io_type iotype);
bool rtl92d_phy_set_rf_power_state(struct ieee80211_hw *hw,
				   enum rf_pwrstate rfpwr_state);

void rtl92d_phy_config_macphymode(struct ieee80211_hw *hw);
void rtl92d_phy_config_macphymode_info(struct ieee80211_hw *hw);
u8 rtl92d_get_chnlgroup_fromarray(u8 chnl);
void rtl92d_phy_set_poweron(struct ieee80211_hw *hw);
void rtl92d_phy_config_maccoexist_rfpage(struct ieee80211_hw *hw);
bool rtl92d_phy_check_poweroff(struct ieee80211_hw *hw);
void rtl92d_phy_lc_calibrate(struct ieee80211_hw *hw);
void rtl92d_update_bbrf_configuration(struct ieee80211_hw *hw);
void rtl92d_phy_ap_calibrate(struct ieee80211_hw *hw, s8 delta);
void rtl92d_phy_iq_calibrate(struct ieee80211_hw *hw);
void rtl92d_phy_reset_iqk_result(struct ieee80211_hw *hw);
void rtl92d_release_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
				       unsigned long *flag);
void rtl92d_acquire_cckandrw_pagea_ctl(struct ieee80211_hw *hw,
				       unsigned long *flag);
u8 rtl92d_get_rightchnlplace_for_iqk(u8 chnl);
void rtl92d_phy_reload_iqk_setting(struct ieee80211_hw *hw, u8 channel);

#endif
