/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
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
 ******************************************************************************/

#include "halbt_precomp.h"

/***********************************************
 *		Global variables
 ***********************************************/

struct btc_coexist gl_bt_coexist;

u32 btc_dbg_type[BTC_MSG_MAX];

/***************************************************
 *		Debug related function
 ***************************************************/

const char *const gl_btc_wifi_bw_string[] = {
	"11bg",
	"HT20",
	"HT40",
	"HT80",
	"HT160"
};

const char *const gl_btc_wifi_freq_string[] = {
	"2.4G",
	"5G"
};

static bool halbtc_is_bt_coexist_available(struct btc_coexist *btcoexist)
{
	if (!btcoexist->binded || NULL == btcoexist->adapter)
		return false;

	return true;
}

static bool halbtc_is_wifi_busy(struct rtl_priv *rtlpriv)
{
	if (rtlpriv->link_info.busytraffic)
		return true;
	else
		return false;
}

static void halbtc_dbg_init(void)
{
}

/***************************************************
 *		helper function
 ***************************************************/
static bool is_any_client_connect_to_ap(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_sta_info *drv_priv;
	u8 cnt = 0;

	if (mac->opmode == NL80211_IFTYPE_ADHOC ||
	    mac->opmode == NL80211_IFTYPE_MESH_POINT ||
	    mac->opmode == NL80211_IFTYPE_AP) {
		if (in_interrupt() > 0) {
			list_for_each_entry(drv_priv, &rtlpriv->entry_list,
					    list) {
				cnt++;
			}
		} else {
			spin_lock_bh(&rtlpriv->locks.entry_list_lock);
			list_for_each_entry(drv_priv, &rtlpriv->entry_list,
					    list) {
				cnt++;
			}
			spin_unlock_bh(&rtlpriv->locks.entry_list_lock);
		}
	}
	if (cnt > 0)
		return true;
	else
		return false;
}

static bool halbtc_is_bt40(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	bool is_ht40 = true;
	enum ht_channel_width bw = rtlphy->current_chan_bw;

	if (bw == HT_CHANNEL_WIDTH_20)
		is_ht40 = false;
	else if (bw == HT_CHANNEL_WIDTH_20_40)
		is_ht40 = true;

	return is_ht40;
}

static bool halbtc_legacy(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	struct rtl_mac *mac = rtl_mac(rtlpriv);

	bool is_legacy = false;

	if ((mac->mode == WIRELESS_MODE_B) || (mac->mode == WIRELESS_MODE_G))
		is_legacy = true;

	return is_legacy;
}

bool halbtc_is_wifi_uplink(struct rtl_priv *adapter)
{
	struct rtl_priv *rtlpriv = adapter;

	if (rtlpriv->link_info.tx_busy_traffic)
		return true;
	else
		return false;
}

static u32 halbtc_get_wifi_bw(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv =
		(struct rtl_priv *)btcoexist->adapter;
	u32 wifi_bw = BTC_WIFI_BW_HT20;

	if (halbtc_is_bt40(rtlpriv)) {
		wifi_bw = BTC_WIFI_BW_HT40;
	} else {
		if (halbtc_legacy(rtlpriv))
			wifi_bw = BTC_WIFI_BW_LEGACY;
		else
			wifi_bw = BTC_WIFI_BW_HT20;
	}
	return wifi_bw;
}

static u8 halbtc_get_wifi_central_chnl(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phy	*rtlphy = &(rtlpriv->phy);
	u8 chnl = 1;

	if (rtlphy->current_channel != 0)
		chnl = rtlphy->current_channel;
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "static halbtc_get_wifi_central_chnl:%d\n", chnl);
	return chnl;
}

u8 rtl_get_hwpg_single_ant_path(struct rtl_priv *rtlpriv)
{
	return rtlpriv->btcoexist.btc_info.single_ant_path;
}

u8 rtl_get_hwpg_bt_type(struct rtl_priv *rtlpriv)
{
	return rtlpriv->btcoexist.btc_info.bt_type;
}

u8 rtl_get_hwpg_ant_num(struct rtl_priv *rtlpriv)
{
	u8 num;

	if (rtlpriv->btcoexist.btc_info.ant_num == ANT_X2)
		num = 2;
	else
		num = 1;

	return num;
}

u8 rtl_get_hwpg_package_type(struct rtl_priv *rtlpriv)
{
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	return rtlhal->package_type;
}

static void halbtc_leave_lps(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv;
	struct rtl_ps_ctl *ppsc;
	bool ap_enable = false;

	rtlpriv = btcoexist->adapter;
	ppsc = rtl_psc(rtlpriv);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if (ap_enable) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "%s()<--dont leave lps under AP mode\n", __func__);
		return;
	}

	btcoexist->bt_info.bt_ctrl_lps = true;
	btcoexist->bt_info.bt_lps_on = false;
	rtl_lps_leave(rtlpriv->mac80211.hw);
}

static void halbtc_enter_lps(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv;
	struct rtl_ps_ctl *ppsc;
	bool ap_enable = false;

	rtlpriv = btcoexist->adapter;
	ppsc = rtl_psc(rtlpriv);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE,
			   &ap_enable);

	if (ap_enable) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "%s()<--dont enter lps under AP mode\n", __func__);
		return;
	}

	btcoexist->bt_info.bt_ctrl_lps = true;
	btcoexist->bt_info.bt_lps_on = true;
	rtl_lps_enter(rtlpriv->mac80211.hw);
}

static void halbtc_normal_lps(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv;

	rtlpriv = btcoexist->adapter;

	if (btcoexist->bt_info.bt_ctrl_lps) {
		btcoexist->bt_info.bt_lps_on = false;
		rtl_lps_leave(rtlpriv->mac80211.hw);
		btcoexist->bt_info.bt_ctrl_lps = false;
	}
}

static void halbtc_leave_low_power(struct btc_coexist *btcoexist)
{
}

static void halbtc_normal_low_power(struct btc_coexist *btcoexist)
{
}

static void halbtc_disable_low_power(struct btc_coexist *btcoexist,
				     bool low_pwr_disable)
{
	/* TODO: original/leave 32k low power */
	btcoexist->bt_info.bt_disable_low_pwr = low_pwr_disable;
}

static void halbtc_aggregation_check(struct btc_coexist *btcoexist)
{
	bool need_to_act = false;
	static unsigned long pre_time;
	unsigned long cur_time = 0;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	/* To void continuous deleteBA=>addBA=>deleteBA=>addBA
	 * This function is not allowed to continuous called
	 * It can only be called after 8 seconds
	 */

	cur_time = jiffies;
	if (jiffies_to_msecs(cur_time - pre_time) <= 8000) {
		/* over 8 seconds you can execute this function again. */
		return;
	}
	pre_time = cur_time;

	if (btcoexist->bt_info.reject_agg_pkt) {
		need_to_act = true;
		btcoexist->bt_info.pre_reject_agg_pkt =
			btcoexist->bt_info.reject_agg_pkt;
	} else {
		if (btcoexist->bt_info.pre_reject_agg_pkt) {
			need_to_act = true;
			btcoexist->bt_info.pre_reject_agg_pkt =
				btcoexist->bt_info.reject_agg_pkt;
		}

		if (btcoexist->bt_info.pre_bt_ctrl_agg_buf_size !=
		    btcoexist->bt_info.bt_ctrl_agg_buf_size) {
			need_to_act = true;
			btcoexist->bt_info.pre_bt_ctrl_agg_buf_size =
				btcoexist->bt_info.bt_ctrl_agg_buf_size;
		}

		if (btcoexist->bt_info.bt_ctrl_agg_buf_size) {
			if (btcoexist->bt_info.pre_agg_buf_size !=
			    btcoexist->bt_info.agg_buf_size) {
				need_to_act = true;
			}
			btcoexist->bt_info.pre_agg_buf_size =
				btcoexist->bt_info.agg_buf_size;
		}

		if (need_to_act)
			rtl_rx_ampdu_apply(rtlpriv);
	}
}

static u32 halbtc_get_bt_patch_version(struct btc_coexist *btcoexist)
{
	return 0;
}

u32 halbtc_get_wifi_link_status(struct btc_coexist *btcoexist)
{
	/* return value:
	 * [31:16] => connected port number
	 * [15:0]  => port connected bit define
	 */
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	u32 ret_val = 0;
	u32 port_connected_status = 0, num_of_connected_port = 0;

	if (mac->opmode == NL80211_IFTYPE_STATION &&
	    mac->link_state >= MAC80211_LINKED) {
		port_connected_status |= WIFI_STA_CONNECTED;
		num_of_connected_port++;
	}
	/* AP & ADHOC & MESH */
	if (is_any_client_connect_to_ap(btcoexist)) {
		port_connected_status |= WIFI_AP_CONNECTED;
		num_of_connected_port++;
	}
	/* TODO: P2P Connected Status */

	ret_val = (num_of_connected_port << 16) | port_connected_status;

	return ret_val;
}

static s32 halbtc_get_wifi_rssi(struct rtl_priv *rtlpriv)
{
	int undec_sm_pwdb = 0;

	if (rtlpriv->mac80211.link_state >= MAC80211_LINKED)
		undec_sm_pwdb = rtlpriv->dm.undec_sm_pwdb;
	else /* associated entry pwdb */
		undec_sm_pwdb = rtlpriv->dm.undec_sm_pwdb;
	return undec_sm_pwdb;
}

static bool halbtc_get(void *void_btcoexist, u8 get_type, void *out_buf)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)void_btcoexist;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phy *rtlphy = &(rtlpriv->phy);
	struct rtl_mac *mac = rtl_mac(rtlpriv);
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	bool *bool_tmp = (bool *)out_buf;
	int *s32_tmp = (int *)out_buf;
	u32 *u32_tmp = (u32 *)out_buf;
	u8 *u8_tmp = (u8 *)out_buf;
	bool tmp = false;
	bool ret = true;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return false;

	switch (get_type) {
	case BTC_GET_BL_HS_OPERATION:
		*bool_tmp = false;
		ret = false;
		break;
	case BTC_GET_BL_HS_CONNECTING:
		*bool_tmp = false;
		ret = false;
		break;
	case BTC_GET_BL_WIFI_CONNECTED:
		if (rtlpriv->mac80211.opmode == NL80211_IFTYPE_STATION &&
		    rtlpriv->mac80211.link_state >= MAC80211_LINKED)
			tmp = true;
		if (is_any_client_connect_to_ap(btcoexist))
			tmp = true;
		*bool_tmp = tmp;
		break;
	case BTC_GET_BL_WIFI_BUSY:
		if (halbtc_is_wifi_busy(rtlpriv))
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_SCAN:
		if (mac->act_scanning)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_LINK:
		if (mac->link_state == MAC80211_LINKING)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_ROAM:
		if (mac->link_state == MAC80211_LINKING)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_4_WAY_PROGRESS:
		*bool_tmp = rtlpriv->btcoexist.btc_info.in_4way;
		break;
	case BTC_GET_BL_WIFI_UNDER_5G:
		if (rtlhal->current_bandtype == BAND_ON_5G)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_AP_MODE_ENABLE:
		if (mac->opmode == NL80211_IFTYPE_AP)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_ENABLE_ENCRYPTION:
		if (NO_ENCRYPTION == rtlpriv->sec.pairwise_enc_algorithm)
			*bool_tmp = false;
		else
			*bool_tmp = true;
		break;
	case BTC_GET_BL_WIFI_UNDER_B_MODE:
		if (rtlpriv->mac80211.mode == WIRELESS_MODE_B)
			*bool_tmp = true;
		else
			*bool_tmp = false;
		break;
	case BTC_GET_BL_EXT_SWITCH:
		*bool_tmp = false;
		break;
	case BTC_GET_BL_WIFI_IS_IN_MP_MODE:
		*bool_tmp = false;
		break;
	case BTC_GET_BL_IS_ASUS_8723B:
		*bool_tmp = false;
		break;
	case BTC_GET_S4_WIFI_RSSI:
		*s32_tmp = halbtc_get_wifi_rssi(rtlpriv);
		break;
	case BTC_GET_S4_HS_RSSI:
		*s32_tmp = 0;
		ret = false;
		break;
	case BTC_GET_U4_WIFI_BW:
		*u32_tmp = halbtc_get_wifi_bw(btcoexist);
		break;
	case BTC_GET_U4_WIFI_TRAFFIC_DIRECTION:
		if (halbtc_is_wifi_uplink(rtlpriv))
			*u32_tmp = BTC_WIFI_TRAFFIC_TX;
		else
			*u32_tmp = BTC_WIFI_TRAFFIC_RX;
		break;
	case BTC_GET_U4_WIFI_FW_VER:
		*u32_tmp = (rtlhal->fw_version << 16) | rtlhal->fw_subversion;
		break;
	case BTC_GET_U4_WIFI_LINK_STATUS:
		*u32_tmp = halbtc_get_wifi_link_status(btcoexist);
		break;
	case BTC_GET_U4_BT_PATCH_VER:
		*u32_tmp = halbtc_get_bt_patch_version(btcoexist);
		break;
	case BTC_GET_U4_VENDOR:
		*u32_tmp = BTC_VENDOR_OTHER;
		break;
	case BTC_GET_U1_WIFI_DOT11_CHNL:
		*u8_tmp = rtlphy->current_channel;
		break;
	case BTC_GET_U1_WIFI_CENTRAL_CHNL:
		*u8_tmp = halbtc_get_wifi_central_chnl(btcoexist);
		break;
	case BTC_GET_U1_WIFI_HS_CHNL:
		*u8_tmp = 0;
		ret = false;
		break;
	case BTC_GET_U1_AP_NUM:
		*u8_tmp = rtlpriv->btcoexist.btc_info.ap_num;
		break;
	case BTC_GET_U1_ANT_TYPE:
		*u8_tmp = (u8)BTC_ANT_TYPE_0;
		break;
	case BTC_GET_U1_IOT_PEER:
		*u8_tmp = 0;
		break;

		/************* 1Ant **************/
	case BTC_GET_U1_LPS_MODE:
		*u8_tmp = btcoexist->pwr_mode_val[0];
		break;

	default:
		ret = false;
		break;
	}

	return ret;
}

static bool halbtc_set(void *void_btcoexist, u8 set_type, void *in_buf)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)void_btcoexist;
	bool *bool_tmp = (bool *)in_buf;
	u8 *u8_tmp = (u8 *)in_buf;
	u32 *u32_tmp = (u32 *)in_buf;
	bool ret = true;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return false;

	switch (set_type) {
	/* set some bool type variables. */
	case BTC_SET_BL_BT_DISABLE:
		btcoexist->bt_info.bt_disabled = *bool_tmp;
		break;
	case BTC_SET_BL_BT_TRAFFIC_BUSY:
		btcoexist->bt_info.bt_busy = *bool_tmp;
		break;
	case BTC_SET_BL_BT_LIMITED_DIG:
		btcoexist->bt_info.limited_dig = *bool_tmp;
		break;
	case BTC_SET_BL_FORCE_TO_ROAM:
		btcoexist->bt_info.force_to_roam = *bool_tmp;
		break;
	case BTC_SET_BL_TO_REJ_AP_AGG_PKT:
		btcoexist->bt_info.reject_agg_pkt = *bool_tmp;
		break;
	case BTC_SET_BL_BT_CTRL_AGG_SIZE:
		btcoexist->bt_info.bt_ctrl_agg_buf_size = *bool_tmp;
		break;
	case BTC_SET_BL_INC_SCAN_DEV_NUM:
		btcoexist->bt_info.increase_scan_dev_num = *bool_tmp;
		break;
	case BTC_SET_BL_BT_TX_RX_MASK:
		btcoexist->bt_info.bt_tx_rx_mask = *bool_tmp;
		break;
	case BTC_SET_BL_MIRACAST_PLUS_BT:
		btcoexist->bt_info.miracast_plus_bt = *bool_tmp;
		break;
		/* set some u1Byte type variables. */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_AGC_TABLE_ON:
		btcoexist->bt_info.rssi_adjust_for_agc_table_on = *u8_tmp;
		break;
	case BTC_SET_U1_AGG_BUF_SIZE:
		btcoexist->bt_info.agg_buf_size = *u8_tmp;
		break;

	/* the following are some action which will be triggered */
	case BTC_SET_ACT_GET_BT_RSSI:
		ret = false;
		break;
	case BTC_SET_ACT_AGGREGATE_CTRL:
		halbtc_aggregation_check(btcoexist);
		break;

	/* 1Ant */
	case BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE:
		btcoexist->bt_info.rssi_adjust_for_1ant_coex_type = *u8_tmp;
		break;
	case BTC_SET_UI_SCAN_SIG_COMPENSATION:
		break;
	case BTC_SET_U1_LPS_VAL:
		btcoexist->bt_info.lps_val = *u8_tmp;
		break;
	case BTC_SET_U1_RPWM_VAL:
		btcoexist->bt_info.rpwm_val = *u8_tmp;
		break;
	/* the following are some action which will be triggered  */
	case BTC_SET_ACT_LEAVE_LPS:
		halbtc_leave_lps(btcoexist);
		break;
	case BTC_SET_ACT_ENTER_LPS:
		halbtc_enter_lps(btcoexist);
		break;
	case BTC_SET_ACT_NORMAL_LPS:
		halbtc_normal_lps(btcoexist);
		break;
	case BTC_SET_ACT_DISABLE_LOW_POWER:
		halbtc_disable_low_power(btcoexist, *bool_tmp);
		break;
	case BTC_SET_ACT_UPDATE_RAMASK:
		btcoexist->bt_info.ra_mask = *u32_tmp;
		break;
	case BTC_SET_ACT_SEND_MIMO_PS:
		break;
	case BTC_SET_ACT_CTRL_BT_INFO: /*wait for 8812/8821*/
		break;
	case BTC_SET_ACT_CTRL_BT_COEX:
		break;
	case BTC_SET_ACT_CTRL_8723B_ANT:
		break;
	default:
		break;
	}

	return ret;
}

/************************************************************
 *		IO related function
 ************************************************************/
static u8 halbtc_read_1byte(void *bt_context, u32 reg_addr)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return	rtl_read_byte(rtlpriv, reg_addr);
}

static u16 halbtc_read_2byte(void *bt_context, u32 reg_addr)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return	rtl_read_word(rtlpriv, reg_addr);
}

static u32 halbtc_read_4byte(void *bt_context, u32 reg_addr)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return	rtl_read_dword(rtlpriv, reg_addr);
}

static void halbtc_write_1byte(void *bt_context, u32 reg_addr, u32 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_write_byte(rtlpriv, reg_addr, data);
}

static void halbtc_bitmask_write_1byte(void *bt_context, u32 reg_addr,
				       u32 bit_mask, u8 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 original_value, bit_shift = 0;
	u8 i;

	if (bit_mask != MASKDWORD) {/*if not "double word" write*/
		original_value = rtl_read_byte(rtlpriv, reg_addr);
		for (i = 0; i <= 7; i++) {
			if ((bit_mask>>i) & 0x1)
				break;
		}
		bit_shift = i;
		data = (original_value & (~bit_mask)) |
			((data << bit_shift) & bit_mask);
	}
	rtl_write_byte(rtlpriv, reg_addr, data);
}

static void halbtc_write_2byte(void *bt_context, u32 reg_addr, u16 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_write_word(rtlpriv, reg_addr, data);
}

static void halbtc_write_4byte(void *bt_context, u32 reg_addr, u32 data)
{
	struct btc_coexist *btcoexist =
		(struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_write_dword(rtlpriv, reg_addr, data);
}

void halbtc_write_local_reg_1byte(void *btc_context, u32 reg_addr, u8 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (btcoexist->chip_interface == BTC_INTF_SDIO)
		;
	else if (btcoexist->chip_interface == BTC_INTF_PCI)
		rtl_write_byte(rtlpriv, reg_addr, data);
	else if (btcoexist->chip_interface == BTC_INTF_USB)
		rtl_write_byte(rtlpriv, reg_addr, data);
}

void halbtc_set_macreg(void *btc_context, u32 reg_addr, u32 bit_mask, u32 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_set_bbreg(rtlpriv->mac80211.hw, reg_addr, bit_mask, data);
}

u32 halbtc_get_macreg(void *btc_context, u32 reg_addr, u32 bit_mask)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return rtl_get_bbreg(rtlpriv->mac80211.hw, reg_addr, bit_mask);
}

static void halbtc_set_bbreg(void *bt_context, u32 reg_addr, u32 bit_mask,
			     u32 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_set_bbreg(rtlpriv->mac80211.hw, reg_addr, bit_mask, data);
}

static u32 halbtc_get_bbreg(void *bt_context, u32 reg_addr, u32 bit_mask)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return rtl_get_bbreg(rtlpriv->mac80211.hw, reg_addr, bit_mask);
}

static void halbtc_set_rfreg(void *bt_context, u8 rf_path, u32 reg_addr,
			     u32 bit_mask, u32 data)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtl_set_rfreg(rtlpriv->mac80211.hw, rf_path, reg_addr, bit_mask, data);
}

static u32 halbtc_get_rfreg(void *bt_context, u8 rf_path, u32 reg_addr,
			    u32 bit_mask)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	return rtl_get_rfreg(rtlpriv->mac80211.hw, rf_path, reg_addr, bit_mask);
}

static void halbtc_fill_h2c_cmd(void *bt_context, u8 element_id,
				u32 cmd_len, u8 *cmd_buf)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->mac80211.hw, element_id,
					cmd_len, cmd_buf);
}

void halbtc_set_bt_reg(void *btc_context, u8 reg_type, u32 offset, u32 set_val)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 cmd_buffer1[4] = {0};
	u8 cmd_buffer2[4] = {0};
	u8 *addr_to_set = (u8 *)&offset;
	u8 *value_to_set = (u8 *)&set_val;
	u8 oper_ver = 0;
	u8 req_num = 0;

	if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		cmd_buffer1[0] |= (oper_ver & 0x0f);	/* Set OperVer */
		cmd_buffer1[0] |= ((req_num << 4) & 0xf0);	/* Set ReqNum */
		cmd_buffer1[1] = 0x0d;	/* OpCode: BT_LO_OP_WRITE_REG_VALUE */
		cmd_buffer1[2] = value_to_set[0];	/* Set WriteRegValue */
		rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->mac80211.hw, 0x67, 4,
						&cmd_buffer1[0]);

		msleep(200);
		req_num++;

		cmd_buffer2[0] |= (oper_ver & 0x0f);	/* Set OperVer */
		cmd_buffer2[0] |= ((req_num << 4) & 0xf0);	/* Set ReqNum */
		cmd_buffer2[1] = 0x0c;	/* OpCode: BT_LO_OP_WRITE_REG_ADDR */
		cmd_buffer2[3] = addr_to_set[0];	/* Set WriteRegAddr */
		rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->mac80211.hw, 0x67, 4,
						&cmd_buffer2[0]);
	}
}

bool halbtc_under_ips(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_ps_ctl *ppsc = rtl_psc(rtlpriv);
	enum rf_pwrstate rtstate;

	if (ppsc->inactiveps) {
		rtstate = ppsc->rfpwr_state;

		if (rtstate != ERFON &&
		    ppsc->rfoff_reason == RF_CHANGE_BY_IPS) {
			return true;
		}
	}

	return false;
}

/*****************************************************************
 *         Extern functions called by other module
 *****************************************************************/
bool exhalbtc_initlize_variables(void)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	halbtc_dbg_init();

	btcoexist->btc_read_1byte = halbtc_read_1byte;
	btcoexist->btc_write_1byte = halbtc_write_1byte;
	btcoexist->btc_write_1byte_bitmask = halbtc_bitmask_write_1byte;
	btcoexist->btc_read_2byte = halbtc_read_2byte;
	btcoexist->btc_write_2byte = halbtc_write_2byte;
	btcoexist->btc_read_4byte = halbtc_read_4byte;
	btcoexist->btc_write_4byte = halbtc_write_4byte;
	btcoexist->btc_write_local_reg_1byte = halbtc_write_local_reg_1byte;

	btcoexist->btc_set_bb_reg = halbtc_set_bbreg;
	btcoexist->btc_get_bb_reg = halbtc_get_bbreg;

	btcoexist->btc_set_rf_reg = halbtc_set_rfreg;
	btcoexist->btc_get_rf_reg = halbtc_get_rfreg;

	btcoexist->btc_fill_h2c = halbtc_fill_h2c_cmd;

	btcoexist->btc_get = halbtc_get;
	btcoexist->btc_set = halbtc_set;
	btcoexist->btc_set_bt_reg = halbtc_set_bt_reg;


	btcoexist->bt_info.bt_ctrl_buf_size = false;
	btcoexist->bt_info.agg_buf_size = 5;

	btcoexist->bt_info.increase_scan_dev_num = false;
	return true;
}

bool exhalbtc_bind_bt_coex_withadapter(void *adapter)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;
	struct rtl_priv *rtlpriv = adapter;
	u8 ant_num = 2, chip_type, single_ant_path = 0;

	if (btcoexist->binded)
		return false;

	switch (rtlpriv->rtlhal.interface) {
	case INTF_PCI:
		btcoexist->chip_interface = BTC_INTF_PCI;
		break;
	case INTF_USB:
		btcoexist->chip_interface = BTC_INTF_USB;
		break;
	default:
		btcoexist->chip_interface = BTC_INTF_UNKNOWN;
		break;
	}

	btcoexist->binded = true;
	btcoexist->statistics.cnt_bind++;

	btcoexist->adapter = adapter;

	btcoexist->stack_info.profile_notified = false;

	btcoexist->bt_info.bt_ctrl_agg_buf_size = false;
	btcoexist->bt_info.agg_buf_size = 5;

	btcoexist->bt_info.increase_scan_dev_num = false;
	btcoexist->bt_info.miracast_plus_bt = false;

	chip_type = rtl_get_hwpg_bt_type(rtlpriv);
	exhalbtc_set_chip_type(chip_type);
	ant_num = rtl_get_hwpg_ant_num(rtlpriv);
	exhalbtc_set_ant_num(rtlpriv, BT_COEX_ANT_TYPE_PG, ant_num);

	/* set default antenna position to main  port */
	btcoexist->board_info.btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;

	single_ant_path = rtl_get_hwpg_single_ant_path(rtlpriv);
	exhalbtc_set_single_ant_path(single_ant_path);

	if (rtl_get_hwpg_package_type(rtlpriv) == 0)
		btcoexist->board_info.tfbga_package = false;
	else if (rtl_get_hwpg_package_type(rtlpriv) == 1)
		btcoexist->board_info.tfbga_package = false;
	else
		btcoexist->board_info.tfbga_package = true;

	if (btcoexist->board_info.tfbga_package)
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Package Type = TFBGA\n");
	else
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
			 "[BTCoex], Package Type = Non-TFBGA\n");

	return true;
}

void exhalbtc_power_on_setting(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_power_on++;

	if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_power_on_setting(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_power_on_setting(btcoexist);
	}
}

void exhalbtc_pre_load_firmware(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_pre_load_firmware++;

	if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_pre_load_firmware(btcoexist);
	}
}

void exhalbtc_init_hw_config(struct btc_coexist *btcoexist, bool wifi_only)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_init_hw_config++;

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_init_hwconfig(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_init_hwconfig(btcoexist, wifi_only);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_init_hwconfig(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_init_hwconfig(btcoexist, wifi_only);
	} else if (IS_HARDWARE_TYPE_8723A(btcoexist->adapter)) {
		/* 8723A has no this function */
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_init_hwconfig(btcoexist);
	}
}

void exhalbtc_init_coex_dm(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_init_coex_dm++;

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_init_coex_dm(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_init_coex_dm(btcoexist);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_init_coex_dm(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_init_coex_dm(btcoexist);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_init_coex_dm(btcoexist);
	}

	btcoexist->initilized = true;
}

void exhalbtc_ips_notify(struct btc_coexist *btcoexist, u8 type)
{
	u8 ips_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_ips_notify++;
	if (btcoexist->manual_control)
		return;

	if (ERFOFF == type)
		ips_type = BTC_IPS_ENTER;
	else
		ips_type = BTC_IPS_LEAVE;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_ips_notify(btcoexist, ips_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_ips_notify(btcoexist, ips_type);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_ips_notify(btcoexist, ips_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_ips_notify(btcoexist, ips_type);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_ips_notify(btcoexist, ips_type);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_lps_notify(struct btc_coexist *btcoexist, u8 type)
{
	u8 lps_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_lps_notify++;
	if (btcoexist->manual_control)
		return;

	if (EACTIVE == type)
		lps_type = BTC_LPS_DISABLE;
	else
		lps_type = BTC_LPS_ENABLE;

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_lps_notify(btcoexist, lps_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_lps_notify(btcoexist, lps_type);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_lps_notify(btcoexist, lps_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_lps_notify(btcoexist, lps_type);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_lps_notify(btcoexist, lps_type);
	}
}

void exhalbtc_scan_notify(struct btc_coexist *btcoexist, u8 type)
{
	u8 scan_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_scan_notify++;
	if (btcoexist->manual_control)
		return;

	if (type)
		scan_type = BTC_SCAN_START;
	else
		scan_type = BTC_SCAN_FINISH;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_scan_notify(btcoexist, scan_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_scan_notify(btcoexist, scan_type);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_scan_notify(btcoexist, scan_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_scan_notify(btcoexist, scan_type);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_scan_notify(btcoexist, scan_type);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_connect_notify(struct btc_coexist *btcoexist, u8 action)
{
	u8 asso_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_connect_notify++;
	if (btcoexist->manual_control)
		return;

	if (action)
		asso_type = BTC_ASSOCIATE_START;
	else
		asso_type = BTC_ASSOCIATE_FINISH;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_connect_notify(btcoexist, asso_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_connect_notify(btcoexist, asso_type);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_connect_notify(btcoexist, asso_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_connect_notify(btcoexist, asso_type);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_connect_notify(btcoexist, asso_type);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_mediastatus_notify(struct btc_coexist *btcoexist,
				 enum rt_media_status media_status)
{
	u8 status;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_media_status_notify++;
	if (btcoexist->manual_control)
		return;

	if (RT_MEDIA_CONNECT == media_status)
		status = BTC_MEDIA_CONNECT;
	else
		status = BTC_MEDIA_DISCONNECT;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_media_status_notify(btcoexist, status);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_media_status_notify(btcoexist, status);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_media_status_notify(btcoexist, status);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_media_status_notify(btcoexist, status);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_media_status_notify(btcoexist, status);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_special_packet_notify(struct btc_coexist *btcoexist, u8 pkt_type)
{
	u8 packet_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_special_packet_notify++;
	if (btcoexist->manual_control)
		return;

	if (pkt_type == PACKET_DHCP) {
		packet_type = BTC_PACKET_DHCP;
	} else if (pkt_type == PACKET_EAPOL) {
		packet_type = BTC_PACKET_EAPOL;
	} else if (pkt_type == PACKET_ARP) {
		packet_type = BTC_PACKET_ARP;
	} else {
		packet_type = BTC_PACKET_UNKNOWN;
		return;
	}

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_special_packet_notify(btcoexist,
							      packet_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_special_packet_notify(btcoexist,
							      packet_type);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_special_packet_notify(btcoexist,
							      packet_type);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_special_packet_notify(btcoexist,
							      packet_type);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_special_packet_notify(btcoexist,
							      packet_type);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_bt_info_notify(struct btc_coexist *btcoexist,
			     u8 *tmp_buf, u8 length)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_bt_info_notify++;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_bt_info_notify(btcoexist, tmp_buf,
						       length);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_bt_info_notify(btcoexist, tmp_buf,
						       length);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_bt_info_notify(btcoexist, tmp_buf,
						       length);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_bt_info_notify(btcoexist, tmp_buf,
						       length);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_bt_info_notify(btcoexist, tmp_buf,
						       length);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_rf_status_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_rf_status_notify(btcoexist, type);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
	}
}

void exhalbtc_stack_operation_notify(struct btc_coexist *btcoexist, u8 type)
{
	u8 stack_op_type;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_stack_operation_notify++;
	if (btcoexist->manual_control)
		return;

	if ((type == HCI_BT_OP_INQUIRY_START) ||
	    (type == HCI_BT_OP_PAGING_START) ||
	    (type == HCI_BT_OP_PAIRING_START)) {
		stack_op_type = BTC_STACK_OP_INQ_PAGE_PAIR_START;
	} else if ((type == HCI_BT_OP_INQUIRY_FINISH) ||
		   (type == HCI_BT_OP_PAGING_SUCCESS) ||
		   (type == HCI_BT_OP_PAGING_UNSUCCESS) ||
		   (type == HCI_BT_OP_PAIRING_FINISH)) {
		stack_op_type = BTC_STACK_OP_INQ_PAGE_PAIR_FINISH;
	} else {
		stack_op_type = BTC_STACK_OP_NONE;
	}
}

void exhalbtc_halt_notify(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_halt_notify(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_halt_notify(btcoexist);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_halt_notify(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_halt_notify(btcoexist);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_halt_notify(btcoexist);
	}

	btcoexist->binded = false;
}

void exhalbtc_pnp_notify(struct btc_coexist *btcoexist, u8 pnp_state)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	/* currently only 1ant we have to do the notification,
	 * once pnp is notified to sleep state, we have to leave LPS that
	 * we can sleep normally.
	 */

	if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_pnp_notify(btcoexist, pnp_state);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_pnp_notify(btcoexist, pnp_state);
	} else if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_pnp_notify(btcoexist, pnp_state);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_pnp_notify(btcoexist, pnp_state);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
	}
}

void exhalbtc_coex_dm_switch(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_coex_dm_switch++;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1) {
			btcoexist->stop_coex_dm = true;
			ex_btc8723b1ant_coex_dm_reset(btcoexist);
			exhalbtc_set_ant_num(rtlpriv,
					     BT_COEX_ANT_TYPE_DETECTED, 2);
			ex_btc8723b2ant_init_hwconfig(btcoexist);
			ex_btc8723b2ant_init_coex_dm(btcoexist);
			btcoexist->stop_coex_dm = false;
		}
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_periodical(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_periodical++;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_periodical(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			if (!halbtc_under_ips(btcoexist))
				ex_btc8821a1ant_periodical(btcoexist);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_periodical(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_periodical(btcoexist);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_periodical(btcoexist);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_dbg_control(struct btc_coexist *btcoexist,
			  u8 code, u8 len, u8 *data)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_dbg_ctrl++;

	halbtc_leave_low_power(btcoexist);

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_antenna_detection(struct btc_coexist *btcoexist, u32 cent_freq,
				u32 offset, u32 span, u32 seconds)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
}

void exhalbtc_stack_update_profile_info(void)
{
}

void exhalbtc_update_min_bt_rssi(s8 bt_rssi)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->stack_info.min_bt_rssi = bt_rssi;
}

void exhalbtc_set_hci_version(u16 hci_version)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->stack_info.hci_version = hci_version;
}

void exhalbtc_set_bt_patch_version(u16 bt_hci_version, u16 bt_patch_version)
{
	struct btc_coexist *btcoexist = &gl_bt_coexist;

	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->bt_info.bt_real_fw_ver = bt_patch_version;
	btcoexist->bt_info.bt_hci_ver = bt_hci_version;
}

void exhalbtc_set_chip_type(u8 chip_type)
{
	switch (chip_type) {
	default:
	case BT_2WIRE:
	case BT_ISSC_3WIRE:
	case BT_ACCEL:
	case BT_RTL8756:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_UNDEF;
		break;
	case BT_CSR_BC4:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_CSR_BC4;
		break;
	case BT_CSR_BC8:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_CSR_BC8;
		break;
	case BT_RTL8723A:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_RTL8723A;
		break;
	case BT_RTL8821A:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_RTL8821;
		break;
	case BT_RTL8723B:
		gl_bt_coexist.board_info.bt_chip_type = BTC_CHIP_RTL8723B;
		break;
	}
}

void exhalbtc_set_ant_num(struct rtl_priv *rtlpriv, u8 type, u8 ant_num)
{
	if (BT_COEX_ANT_TYPE_PG == type) {
		gl_bt_coexist.board_info.pg_ant_num = ant_num;
		gl_bt_coexist.board_info.btdm_ant_num = ant_num;
	} else if (BT_COEX_ANT_TYPE_ANTDIV == type) {
		gl_bt_coexist.board_info.btdm_ant_num = ant_num;
	} else if (type == BT_COEX_ANT_TYPE_DETECTED) {
		gl_bt_coexist.board_info.btdm_ant_num = ant_num;
		if (rtlpriv->cfg->mod_params->ant_sel == 1)
			gl_bt_coexist.board_info.btdm_ant_pos =
				BTC_ANTENNA_AT_AUX_PORT;
		else
			gl_bt_coexist.board_info.btdm_ant_pos =
				BTC_ANTENNA_AT_MAIN_PORT;
	}
}

/* Currently used by 8723b only, S0 or S1 */
void exhalbtc_set_single_ant_path(u8 single_ant_path)
{
	gl_bt_coexist.board_info.single_ant_path = single_ant_path;
}

void exhalbtc_display_bt_coex_info(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8821(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8821a2ant_display_coex_info(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8821a1ant_display_coex_info(btcoexist);
	} else if (IS_HARDWARE_TYPE_8723B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8723b2ant_display_coex_info(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8723b1ant_display_coex_info(btcoexist);
	} else if (IS_HARDWARE_TYPE_8192E(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8192e2ant_display_coex_info(btcoexist);
	}

	halbtc_normal_low_power(btcoexist);
}
