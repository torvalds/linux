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
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __RTL8723E_HAL_BT_COEXIST_H__
#define __RTL8723E_HAL_BT_COEXIST_H__

#include "../wifi.h"

/* The reg define is for 8723 */
#define	REG_HIGH_PRIORITY_TXRX			0x770
#define	REG_LOW_PRIORITY_TXRX			0x774

#define BT_FW_COEX_THRESH_TOL			6
#define BT_FW_COEX_THRESH_20			20
#define BT_FW_COEX_THRESH_23			23
#define BT_FW_COEX_THRESH_25			25
#define BT_FW_COEX_THRESH_30			30
#define BT_FW_COEX_THRESH_35			35
#define BT_FW_COEX_THRESH_40			40
#define BT_FW_COEX_THRESH_45			45
#define BT_FW_COEX_THRESH_47			47
#define BT_FW_COEX_THRESH_50			50
#define BT_FW_COEX_THRESH_55			55

#define BT_COEX_STATE_BT30			BIT(0)
#define BT_COEX_STATE_WIFI_HT20			BIT(1)
#define BT_COEX_STATE_WIFI_HT40			BIT(2)
#define BT_COEX_STATE_WIFI_LEGACY		BIT(3)

#define BT_COEX_STATE_WIFI_RSSI_LOW		BIT(4)
#define BT_COEX_STATE_WIFI_RSSI_MEDIUM	BIT(5)
#define BT_COEX_STATE_WIFI_RSSI_HIGH	BIT(6)
#define BT_COEX_STATE_DEC_BT_POWER		BIT(7)

#define BT_COEX_STATE_WIFI_IDLE			BIT(8)
#define BT_COEX_STATE_WIFI_UPLINK		BIT(9)
#define BT_COEX_STATE_WIFI_DOWNLINK		BIT(10)

#define BT_COEX_STATE_BT_INQ_PAGE		BIT(11)
#define BT_COEX_STATE_BT_IDLE			BIT(12)
#define BT_COEX_STATE_BT_UPLINK			BIT(13)
#define BT_COEX_STATE_BT_DOWNLINK		BIT(14)

#define BT_COEX_STATE_HOLD_FOR_BT_OPERATION	BIT(15)
#define BT_COEX_STATE_BT_RSSI_LOW		BIT(19)

#define BT_COEX_STATE_PROFILE_HID		BIT(20)
#define BT_COEX_STATE_PROFILE_A2DP		BIT(21)
#define BT_COEX_STATE_PROFILE_PAN		BIT(22)
#define BT_COEX_STATE_PROFILE_SCO		BIT(23)

#define BT_COEX_STATE_WIFI_RSSI_1_LOW		BIT(24)
#define BT_COEX_STATE_WIFI_RSSI_1_MEDIUM	BIT(25)
#define BT_COEX_STATE_WIFI_RSSI_1_HIGH		BIT(26)

#define BT_COEX_STATE_BTINFO_COMMON			BIT(30)
#define BT_COEX_STATE_BTINFO_B_HID_SCOESCO	BIT(31)
#define BT_COEX_STATE_BTINFO_B_FTP_A2DP		BIT(29)

#define BT_COEX_STATE_BT_CNT_LEVEL_0		BIT(0)
#define BT_COEX_STATE_BT_CNT_LEVEL_1		BIT(1)
#define BT_COEX_STATE_BT_CNT_LEVEL_2		BIT(2)
#define BT_COEX_STATE_BT_CNT_LEVEL_3		BIT(3)

#define BT_RSSI_STATE_HIGH			0
#define BT_RSSI_STATE_MEDIUM			1
#define BT_RSSI_STATE_LOW			2
#define BT_RSSI_STATE_STAY_HIGH			3
#define BT_RSSI_STATE_STAY_MEDIUM		4
#define BT_RSSI_STATE_STAY_LOW			5

#define	BT_AGCTABLE_OFF				0
#define	BT_AGCTABLE_ON				1
#define	BT_BB_BACKOFF_OFF			0
#define	BT_BB_BACKOFF_ON			1
#define	BT_FW_NAV_OFF				0
#define	BT_FW_NAV_ON				1

#define	BT_COEX_MECH_NONE			0
#define	BT_COEX_MECH_SCO			1
#define	BT_COEX_MECH_HID			2
#define	BT_COEX_MECH_A2DP			3
#define	BT_COEX_MECH_PAN			4
#define	BT_COEX_MECH_HID_A2DP			5
#define	BT_COEX_MECH_HID_PAN			6
#define	BT_COEX_MECH_PAN_A2DP			7
#define	BT_COEX_MECH_HID_SCO_ESCO		8
#define	BT_COEX_MECH_FTP_A2DP			9
#define	BT_COEX_MECH_COMMON			10
#define	BT_COEX_MECH_MAX			11

#define	BT_DBG_PROFILE_NONE			0
#define	BT_DBG_PROFILE_SCO			1
#define	BT_DBG_PROFILE_HID			2
#define	BT_DBG_PROFILE_A2DP			3
#define	BT_DBG_PROFILE_PAN			4
#define	BT_DBG_PROFILE_HID_A2DP			5
#define	BT_DBG_PROFILE_HID_PAN			6
#define	BT_DBG_PROFILE_PAN_A2DP			7
#define	BT_DBG_PROFILE_MAX			9

#define	BTINFO_B_FTP				BIT(7)
#define	BTINFO_B_A2DP				BIT(6)
#define	BTINFO_B_HID				BIT(5)
#define	BTINFO_B_SCO_BUSY			BIT(4)
#define	BTINFO_B_ACL_BUSY			BIT(3)
#define	BTINFO_B_INQ_PAGE			BIT(2)
#define	BTINFO_B_SCO_ESCO			BIT(1)
#define	BTINFO_B_CONNECTION			BIT(0)

void rtl8723e_btdm_coex_all_off(struct ieee80211_hw *hw);
void rtl8723e_dm_bt_fw_coex_all_off(struct ieee80211_hw *hw);

void rtl8723e_dm_bt_sw_coex_all_off(struct ieee80211_hw *hw);
void rtl8723e_dm_bt_hw_coex_all_off(struct ieee80211_hw *hw);
long rtl8723e_dm_bt_get_rx_ss(struct ieee80211_hw *hw);
void rtl8723e_dm_bt_balance(struct ieee80211_hw *hw,
			    bool balance_on, u8 ms0, u8 ms1);
void rtl8723e_dm_bt_agc_table(struct ieee80211_hw *hw, u8 tyep);
void rtl8723e_dm_bt_bb_back_off_level(struct ieee80211_hw *hw, u8 type);
u8 rtl8723e_dm_bt_check_coex_rssi_state(struct ieee80211_hw *hw,
					u8 level_num, u8 rssi_thresh,
					u8 rssi_thresh1);
u8 rtl8723e_dm_bt_check_coex_rssi_state1(struct ieee80211_hw *hw,
					 u8 level_num, u8 rssi_thresh,
					 u8 rssi_thresh1);
void _rtl8723_dm_bt_check_wifi_state(struct ieee80211_hw *hw);
void rtl8723e_dm_bt_reject_ap_aggregated_packet(struct ieee80211_hw *hw,
						bool b_reject);
bool rtl8723e_dm_bt_is_coexist_state_changed(struct ieee80211_hw *hw);
bool rtl8723e_dm_bt_is_wifi_up_link(struct ieee80211_hw *hw);

#endif
