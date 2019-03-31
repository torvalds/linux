// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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

/***************************************************
 *		Debug related function
 ***************************************************/

static const char *const gl_btc_wifi_bw_string[] = {
	"11bg",
	"HT20",
	"HT40",
	"HT80",
	"HT160"
};

static const char *const gl_btc_wifi_freq_string[] = {
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
	struct rtl_phy *rtlphy = &rtlpriv->phy;
	u32 wifi_bw = BTC_WIFI_BW_HT20;

	if (halbtc_legacy(rtlpriv)) {
		wifi_bw = BTC_WIFI_BW_LEGACY;
	} else {
		switch (rtlphy->current_chan_bw) {
		case HT_CHANNEL_WIDTH_20:
			wifi_bw = BTC_WIFI_BW_HT20;
			break;
		case HT_CHANNEL_WIDTH_20_40:
			wifi_bw = BTC_WIFI_BW_HT40;
			break;
		case HT_CHANNEL_WIDTH_80:
			wifi_bw = BTC_WIFI_BW_HT80;
			break;
		}
	}

	return wifi_bw;
}

static u8 halbtc_get_wifi_central_chnl(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phy	*rtlphy = &rtlpriv->phy;
	u8 chnl = 1;

	if (rtlphy->current_channel != 0)
		chnl = rtlphy->current_channel;
	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "%s:%d\n", __func__, chnl);
	return chnl;
}

static u8 rtl_get_hwpg_single_ant_path(struct rtl_priv *rtlpriv)
{
	return rtlpriv->btcoexist.btc_info.single_ant_path;
}

static u8 rtl_get_hwpg_bt_type(struct rtl_priv *rtlpriv)
{
	return rtlpriv->btcoexist.btc_info.bt_type;
}

static u8 rtl_get_hwpg_ant_num(struct rtl_priv *rtlpriv)
{
	u8 num;

	if (rtlpriv->btcoexist.btc_info.ant_num == ANT_X2)
		num = 2;
	else
		num = 1;

	return num;
}

static u8 rtl_get_hwpg_package_type(struct rtl_priv *rtlpriv)
{
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	return rtlhal->package_type;
}

static
u8 rtl_get_hwpg_rfe_type(struct rtl_priv *rtlpriv)
{
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);

	return rtlhal->rfe_type;
}

/* ************************************
 *         Hal helper function
 * ************************************
 */
static
bool halbtc_is_hw_mailbox_exist(struct btc_coexist *btcoexist)
{
	if (IS_HARDWARE_TYPE_8812(btcoexist->adapter))
		return false;
	else
		return true;
}

static
bool halbtc_send_bt_mp_operation(struct btc_coexist *btcoexist, u8 op_code,
				 u8 *cmd, u32 len, unsigned long wait_ms)
{
	struct rtl_priv *rtlpriv;
	const u8 oper_ver = 0;
	u8 req_num;

	if (!halbtc_is_hw_mailbox_exist(btcoexist))
		return false;

	if (wait_ms)	/* before h2c to avoid race condition */
		reinit_completion(&btcoexist->bt_mp_comp);

	rtlpriv = btcoexist->adapter;

	/*
	 * fill req_num by op_code, and rtl_btc_btmpinfo_notify() use it
	 * to know message type
	 */
	switch (op_code) {
	case BT_OP_GET_BT_VERSION:
		req_num = BT_SEQ_GET_BT_VERSION;
		break;
	case BT_OP_GET_AFH_MAP_L:
		req_num = BT_SEQ_GET_AFH_MAP_L;
		break;
	case BT_OP_GET_AFH_MAP_M:
		req_num = BT_SEQ_GET_AFH_MAP_M;
		break;
	case BT_OP_GET_AFH_MAP_H:
		req_num = BT_SEQ_GET_AFH_MAP_H;
		break;
	case BT_OP_GET_BT_COEX_SUPPORTED_FEATURE:
		req_num = BT_SEQ_GET_BT_COEX_SUPPORTED_FEATURE;
		break;
	case BT_OP_GET_BT_COEX_SUPPORTED_VERSION:
		req_num = BT_SEQ_GET_BT_COEX_SUPPORTED_VERSION;
		break;
	case BT_OP_GET_BT_ANT_DET_VAL:
		req_num = BT_SEQ_GET_BT_ANT_DET_VAL;
		break;
	case BT_OP_GET_BT_BLE_SCAN_PARA:
		req_num = BT_SEQ_GET_BT_BLE_SCAN_PARA;
		break;
	case BT_OP_GET_BT_BLE_SCAN_TYPE:
		req_num = BT_SEQ_GET_BT_BLE_SCAN_TYPE;
		break;
	case BT_OP_WRITE_REG_ADDR:
	case BT_OP_WRITE_REG_VALUE:
	case BT_OP_READ_REG:
	default:
		req_num = BT_SEQ_DONT_CARE;
		break;
	}

	cmd[0] |= (oper_ver & 0x0f);		/* Set OperVer */
	cmd[0] |= ((req_num << 4) & 0xf0);	/* Set ReqNum */
	cmd[1] = op_code;
	rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->mac80211.hw, 0x67, len, cmd);

	/* wait? */
	if (!wait_ms)
		return true;

	RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		 "btmpinfo wait req_num=%d wait=%ld\n", req_num, wait_ms);

	if (in_interrupt())
		return false;

	if (wait_for_completion_timeout(&btcoexist->bt_mp_comp,
					msecs_to_jiffies(wait_ms)) == 0) {
		RT_TRACE(rtlpriv, COMP_BT_COEXIST, DBG_DMESG,
			 "btmpinfo wait (req_num=%d) timeout\n", req_num);

		return false;	/* timeout */
	}

	return true;
}

static void halbtc_leave_lps(struct btc_coexist *btcoexist)
{
	struct rtl_priv *rtlpriv;
	bool ap_enable = false;

	rtlpriv = btcoexist->adapter;

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
	bool ap_enable = false;

	rtlpriv = btcoexist->adapter;

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
	u8 cmd_buffer[4] = {0};

	if (btcoexist->bt_info.bt_real_fw_ver)
		goto label_done;

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	halbtc_send_bt_mp_operation(btcoexist, BT_OP_GET_BT_VERSION,
				    cmd_buffer, 4, 200);

label_done:
	return btcoexist->bt_info.bt_real_fw_ver;
}

static u32 halbtc_get_bt_coex_supported_feature(void *btc_context)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	u8 cmd_buffer[4] = {0};

	if (btcoexist->bt_info.bt_supported_feature)
		goto label_done;

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	halbtc_send_bt_mp_operation(btcoexist,
				    BT_OP_GET_BT_COEX_SUPPORTED_FEATURE,
				    cmd_buffer, 4, 200);

label_done:
	return btcoexist->bt_info.bt_supported_feature;
}

static u32 halbtc_get_bt_coex_supported_version(void *btc_context)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	u8 cmd_buffer[4] = {0};

	if (btcoexist->bt_info.bt_supported_version)
		goto label_done;

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	halbtc_send_bt_mp_operation(btcoexist,
				    BT_OP_GET_BT_COEX_SUPPORTED_VERSION,
				    cmd_buffer, 4, 200);

label_done:
	return btcoexist->bt_info.bt_supported_version;
}

static u32 halbtc_get_wifi_link_status(struct btc_coexist *btcoexist)
{
	/* return value:
	 * [31:16] => connected port number
	 * [15:0]  => port connected bit define
	 */
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_mac *mac = rtl_mac(rtlpriv);
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

	return (num_of_connected_port << 16) | port_connected_status;
}

static bool halbtc_get(void *void_btcoexist, u8 get_type, void *out_buf)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)void_btcoexist;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phy *rtlphy = &rtlpriv->phy;
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
		if (rtlpriv->sec.pairwise_enc_algorithm == NO_ENCRYPTION)
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
	case BTC_GET_BL_RF4CE_CONNECTED:
		*bool_tmp = false;
		break;
	case BTC_GET_S4_WIFI_RSSI:
		*s32_tmp = rtlpriv->dm.undec_sm_pwdb;
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
	case BTC_GET_U4_SUPPORTED_VERSION:
		*u32_tmp = halbtc_get_bt_coex_supported_version(btcoexist);
		break;
	case BTC_GET_U4_SUPPORTED_FEATURE:
		*u32_tmp = halbtc_get_bt_coex_supported_feature(btcoexist);
		break;
	case BTC_GET_U4_WIFI_IQK_TOTAL:
		*u32_tmp = btcoexist->btc_phydm_query_phy_counter(btcoexist,
								  "IQK_TOTAL");
		break;
	case BTC_GET_U4_WIFI_IQK_OK:
		*u32_tmp = btcoexist->btc_phydm_query_phy_counter(btcoexist,
								  "IQK_OK");
		break;
	case BTC_GET_U4_WIFI_IQK_FAIL:
		*u32_tmp = btcoexist->btc_phydm_query_phy_counter(btcoexist,
								  "IQK_FAIL");
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

static void halbtc_display_coex_statistics(struct btc_coexist *btcoexist,
					   struct seq_file *m)
{
}

static void halbtc_display_bt_link_info(struct btc_coexist *btcoexist,
					struct seq_file *m)
{
}

static void halbtc_display_wifi_status(struct btc_coexist *btcoexist,
				       struct seq_file *m)
{
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	s32 wifi_rssi = 0, bt_hs_rssi = 0;
	bool scan = false, link = false, roam = false, wifi_busy = false;
	bool wifi_under_b_mode = false, wifi_under_5g = false;
	u32 wifi_bw = BTC_WIFI_BW_HT20;
	u32 wifi_traffic_dir = BTC_WIFI_TRAFFIC_TX;
	u32 wifi_freq = BTC_FREQ_2_4G;
	u32 wifi_link_status = 0x0;
	bool bt_hs_on = false, under_ips = false, under_lps = false;
	bool low_power = false, dc_mode = false;
	u8 wifi_chnl = 0, wifi_hs_chnl = 0;
	u8 ap_num = 0;

	wifi_link_status = halbtc_get_wifi_link_status(btcoexist);
	seq_printf(m, "\n %-35s = %d/ %d/ %d/ %d/ %d",
		   "STA/vWifi/HS/p2pGo/p2pGc",
		   ((wifi_link_status & WIFI_STA_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_AP_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_HS_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_P2P_GO_CONNECTED) ? 1 : 0),
		   ((wifi_link_status & WIFI_P2P_GC_CONNECTED) ? 1 : 0));

	btcoexist->btc_get(btcoexist, BTC_GET_BL_HS_OPERATION, &bt_hs_on);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_DOT11_CHNL, &wifi_chnl);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_WIFI_HS_CHNL, &wifi_hs_chnl);
	seq_printf(m, "\n %-35s = %d / %d(%d)",
		   "Dot11 channel / HsChnl(High Speed)",
		   wifi_chnl, wifi_hs_chnl, bt_hs_on);

	btcoexist->btc_get(btcoexist, BTC_GET_S4_WIFI_RSSI, &wifi_rssi);
	btcoexist->btc_get(btcoexist, BTC_GET_S4_HS_RSSI, &bt_hs_rssi);
	seq_printf(m, "\n %-35s = %d/ %d",
		   "Wifi rssi/ HS rssi",
		   wifi_rssi - 100, bt_hs_rssi - 100);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_SCAN, &scan);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_LINK, &link);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_ROAM, &roam);
	seq_printf(m, "\n %-35s = %d/ %d/ %d ",
		   "Wifi link/ roam/ scan",
		   link, roam, scan);

	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_5G, &wifi_under_5g);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_BW, &wifi_bw);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_BUSY, &wifi_busy);
	btcoexist->btc_get(btcoexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION,
			   &wifi_traffic_dir);
	btcoexist->btc_get(btcoexist, BTC_GET_U1_AP_NUM, &ap_num);
	wifi_freq = (wifi_under_5g ? BTC_FREQ_5G : BTC_FREQ_2_4G);
	btcoexist->btc_get(btcoexist, BTC_GET_BL_WIFI_UNDER_B_MODE,
			   &wifi_under_b_mode);

	seq_printf(m, "\n %-35s = %s / %s/ %s/ AP=%d ",
		   "Wifi freq/ bw/ traffic",
		   gl_btc_wifi_freq_string[wifi_freq],
		   ((wifi_under_b_mode) ? "11b" :
		    gl_btc_wifi_bw_string[wifi_bw]),
		   ((!wifi_busy) ? "idle" : ((BTC_WIFI_TRAFFIC_TX ==
					      wifi_traffic_dir) ? "uplink" :
					     "downlink")),
		   ap_num);

	/* power status	 */
	dc_mode = true;	/*TODO*/
	under_ips = rtlpriv->psc.inactive_pwrstate == ERFOFF ? 1 : 0;
	under_lps = rtlpriv->psc.dot11_psmode == EACTIVE ? 0 : 1;
	low_power = false; /*TODO*/
	seq_printf(m, "\n %-35s = %s%s%s%s",
		   "Power Status",
		   (dc_mode ? "DC mode" : "AC mode"),
		   (under_ips ? ", IPS ON" : ""),
		   (under_lps ? ", LPS ON" : ""),
		   (low_power ? ", 32k" : ""));

	seq_printf(m,
		   "\n %-35s = %02x %02x %02x %02x %02x %02x (0x%x/0x%x)",
		   "Power mode cmd(lps/rpwm)",
		   btcoexist->pwr_mode_val[0], btcoexist->pwr_mode_val[1],
		   btcoexist->pwr_mode_val[2], btcoexist->pwr_mode_val[3],
		   btcoexist->pwr_mode_val[4], btcoexist->pwr_mode_val[5],
		   btcoexist->bt_info.lps_val,
		   btcoexist->bt_info.rpwm_val);
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
			if ((bit_mask >> i) & 0x1)
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

static void halbtc_write_local_reg_1byte(void *btc_context, u32 reg_addr,
					 u8 data)
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

static void halbtc_send_wifi_port_id_cmd(void *bt_context)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	u8 cmd_buf[1] = {0};	/* port id [2:0] = 0 */

	rtlpriv->cfg->ops->fill_h2c_cmd(rtlpriv->mac80211.hw, 0x71, 1,
					cmd_buf);
}

static void halbtc_set_default_port_id_cmd(void *bt_context)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct ieee80211_hw *hw = rtlpriv->mac80211.hw;

	if (!rtlpriv->cfg->ops->set_default_port_id_cmd)
		return;

	rtlpriv->cfg->ops->set_default_port_id_cmd(hw);
}

static
void halbtc_set_bt_reg(void *btc_context, u8 reg_type, u32 offset, u32 set_val)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	u8 cmd_buffer1[4] = {0};
	u8 cmd_buffer2[4] = {0};

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	*((__le16 *)&cmd_buffer1[2]) = cpu_to_le16((u16)set_val);
	if (!halbtc_send_bt_mp_operation(btcoexist, BT_OP_WRITE_REG_VALUE,
					 cmd_buffer1, 4, 200))
		return;

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	cmd_buffer2[2] = reg_type;
	*((u8 *)&cmd_buffer2[3]) = (u8)offset;
	halbtc_send_bt_mp_operation(btcoexist, BT_OP_WRITE_REG_ADDR,
				    cmd_buffer2, 4, 200);
}

static void halbtc_display_dbg_msg(void *bt_context, u8 disp_type,
				   struct seq_file *m)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)bt_context;

	switch (disp_type) {
	case BTC_DBG_DISP_COEX_STATISTICS:
		halbtc_display_coex_statistics(btcoexist, m);
		break;
	case BTC_DBG_DISP_BT_LINK_INFO:
		halbtc_display_bt_link_info(btcoexist, m);
		break;
	case BTC_DBG_DISP_WIFI_STATUS:
		halbtc_display_wifi_status(btcoexist, m);
		break;
	default:
		break;
	}
}

static u32 halbtc_get_bt_reg(void *btc_context, u8 reg_type, u32 offset)
{
	return 0;
}

static
u32 halbtc_get_phydm_version(void *btc_context)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;

	if (rtlpriv->phydm.ops)
		return rtlpriv->phydm.ops->phydm_get_version(rtlpriv);

	return 0;
}

static
void halbtc_phydm_modify_ra_pcr_threshold(void *btc_context,
					  u8 ra_offset_direction,
					  u8 ra_threshold_offset)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phydm_ops *phydm_ops = rtlpriv->phydm.ops;

	if (phydm_ops)
		phydm_ops->phydm_modify_ra_pcr_threshold(rtlpriv,
							 ra_offset_direction,
							 ra_threshold_offset);
}

static
u32 halbtc_phydm_query_phy_counter(void *btc_context, const char *info_type)
{
	/* info_type may be strings below:
	 * PHYDM_INFO_FA_OFDM, PHYDM_INFO_FA_CCK, PHYDM_INFO_CCA_OFDM,
	 * PHYDM_INFO_CCA_CCK
	 * IQK_TOTAL, IQK_OK, IQK_FAIL
	 */

	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	struct rtl_priv *rtlpriv = btcoexist->adapter;
	struct rtl_phydm_ops *phydm_ops = rtlpriv->phydm.ops;

	if (phydm_ops)
		return phydm_ops->phydm_query_counter(rtlpriv, info_type);

	return 0;
}

static u8 halbtc_get_ant_det_val_from_bt(void *btc_context)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	u8 cmd_buffer[4] = {0};

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	halbtc_send_bt_mp_operation(btcoexist, BT_OP_GET_BT_ANT_DET_VAL,
				    cmd_buffer, 4, 200);

	/* need wait completion to return correct value */

	return btcoexist->bt_info.bt_ant_det_val;
}

static u8 halbtc_get_ble_scan_type_from_bt(void *btc_context)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	u8 cmd_buffer[4] = {0};

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	halbtc_send_bt_mp_operation(btcoexist, BT_OP_GET_BT_BLE_SCAN_TYPE,
				    cmd_buffer, 4, 200);

	/* need wait completion to return correct value */

	return btcoexist->bt_info.bt_ble_scan_type;
}

static u32 halbtc_get_ble_scan_para_from_bt(void *btc_context, u8 scan_type)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	u8 cmd_buffer[4] = {0};

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	halbtc_send_bt_mp_operation(btcoexist, BT_OP_GET_BT_BLE_SCAN_PARA,
				    cmd_buffer, 4, 200);

	/* need wait completion to return correct value */

	return btcoexist->bt_info.bt_ble_scan_para;
}

static bool halbtc_get_bt_afh_map_from_bt(void *btc_context, u8 map_type,
					  u8 *afh_map)
{
	struct btc_coexist *btcoexist = (struct btc_coexist *)btc_context;
	u8 cmd_buffer[2] = {0};
	bool ret;
	u32 *afh_map_l = (u32 *)afh_map;
	u32 *afh_map_m = (u32 *)(afh_map + 4);
	u16 *afh_map_h = (u16 *)(afh_map + 8);

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	ret = halbtc_send_bt_mp_operation(btcoexist, BT_OP_GET_AFH_MAP_L,
					  cmd_buffer, 2, 200);
	if (!ret)
		goto exit;

	*afh_map_l = btcoexist->bt_info.afh_map_l;

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	ret = halbtc_send_bt_mp_operation(btcoexist, BT_OP_GET_AFH_MAP_M,
					  cmd_buffer, 2, 200);
	if (!ret)
		goto exit;

	*afh_map_m = btcoexist->bt_info.afh_map_m;

	/* cmd_buffer[0] and [1] is filled by halbtc_send_bt_mp_operation() */
	ret = halbtc_send_bt_mp_operation(btcoexist, BT_OP_GET_AFH_MAP_H,
					  cmd_buffer, 2, 200);
	if (!ret)
		goto exit;

	*afh_map_h = btcoexist->bt_info.afh_map_h;

exit:
	return ret;
}

/*****************************************************************
 *         Extern functions called by other module
 *****************************************************************/
bool exhalbtc_initlize_variables(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return false;

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
	btcoexist->btc_disp_dbg_msg = halbtc_display_dbg_msg;

	btcoexist->btc_get = halbtc_get;
	btcoexist->btc_set = halbtc_set;
	btcoexist->btc_set_bt_reg = halbtc_set_bt_reg;
	btcoexist->btc_get_bt_reg = halbtc_get_bt_reg;

	btcoexist->bt_info.bt_ctrl_buf_size = false;
	btcoexist->bt_info.agg_buf_size = 5;

	btcoexist->bt_info.increase_scan_dev_num = false;

	btcoexist->btc_get_bt_coex_supported_feature =
					halbtc_get_bt_coex_supported_feature;
	btcoexist->btc_get_bt_coex_supported_version =
					halbtc_get_bt_coex_supported_version;
	btcoexist->btc_get_bt_phydm_version = halbtc_get_phydm_version;
	btcoexist->btc_phydm_modify_ra_pcr_threshold =
					halbtc_phydm_modify_ra_pcr_threshold;
	btcoexist->btc_phydm_query_phy_counter = halbtc_phydm_query_phy_counter;
	btcoexist->btc_get_ant_det_val_from_bt = halbtc_get_ant_det_val_from_bt;
	btcoexist->btc_get_ble_scan_type_from_bt =
					halbtc_get_ble_scan_type_from_bt;
	btcoexist->btc_get_ble_scan_para_from_bt =
					halbtc_get_ble_scan_para_from_bt;
	btcoexist->btc_get_bt_afh_map_from_bt =
					halbtc_get_bt_afh_map_from_bt;

	init_completion(&btcoexist->bt_mp_comp);

	return true;
}

bool exhalbtc_initlize_variables_wifi_only(struct rtl_priv *rtlpriv)
{
	struct wifi_only_cfg *wifionly_cfg = rtl_btc_wifi_only(rtlpriv);
	struct wifi_only_haldata *wifionly_haldata;

	if (!wifionly_cfg)
		return false;

	wifionly_cfg->adapter = rtlpriv;

	switch (rtlpriv->rtlhal.interface) {
	case INTF_PCI:
		wifionly_cfg->chip_interface = WIFIONLY_INTF_PCI;
		break;
	case INTF_USB:
		wifionly_cfg->chip_interface = WIFIONLY_INTF_USB;
		break;
	default:
		wifionly_cfg->chip_interface = WIFIONLY_INTF_UNKNOWN;
		break;
	}

	wifionly_haldata = &wifionly_cfg->haldata_info;

	wifionly_haldata->customer_id = CUSTOMER_NORMAL;
	wifionly_haldata->efuse_pg_antnum = rtl_get_hwpg_ant_num(rtlpriv);
	wifionly_haldata->efuse_pg_antpath =
					rtl_get_hwpg_single_ant_path(rtlpriv);
	wifionly_haldata->rfe_type = rtl_get_hwpg_rfe_type(rtlpriv);
	wifionly_haldata->ant_div_cfg = 0;

	return true;
}

bool exhalbtc_bind_bt_coex_withadapter(void *adapter)
{
	struct rtl_priv *rtlpriv = adapter;
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);
	u8 ant_num = 2, chip_type, single_ant_path = 0;

	if (!btcoexist)
		return false;

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
	exhalbtc_set_chip_type(btcoexist, chip_type);
	ant_num = rtl_get_hwpg_ant_num(rtlpriv);
	exhalbtc_set_ant_num(rtlpriv, BT_COEX_ANT_TYPE_PG, ant_num);

	/* set default antenna position to main  port */
	btcoexist->board_info.btdm_ant_pos = BTC_ANTENNA_AT_MAIN_PORT;

	single_ant_path = rtl_get_hwpg_single_ant_path(rtlpriv);
	exhalbtc_set_single_ant_path(btcoexist, single_ant_path);

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

	btcoexist->board_info.rfe_type = rtl_get_hwpg_rfe_type(rtlpriv);
	btcoexist->board_info.ant_div_cfg = 0;

	return true;
}

void exhalbtc_power_on_setting(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_power_on++;

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_power_on_setting(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_power_on_setting(btcoexist);
	}
}

void exhalbtc_pre_load_firmware(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_pre_load_firmware++;

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_pre_load_firmware(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_pre_load_firmware(btcoexist);
	}
}

void exhalbtc_init_hw_config(struct btc_coexist *btcoexist, bool wifi_only)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_init_hw_config++;

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_init_hw_config(btcoexist, wifi_only);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_init_hw_config(btcoexist, wifi_only);

		halbtc_set_default_port_id_cmd(btcoexist);
		halbtc_send_wifi_port_id_cmd(btcoexist);
	}
}

void exhalbtc_init_hw_config_wifi_only(struct wifi_only_cfg *wifionly_cfg)
{
	if (IS_HARDWARE_TYPE_8822B(wifionly_cfg->adapter))
		ex_hal8822b_wifi_only_hw_config(wifionly_cfg);
}

void exhalbtc_init_coex_dm(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->statistics.cnt_init_coex_dm++;

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_init_coex_dm(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_init_coex_dm(btcoexist);
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

	if (type == ERFOFF)
		ips_type = BTC_IPS_ENTER;
	else
		ips_type = BTC_IPS_LEAVE;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_ips_notify(btcoexist, ips_type);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_ips_notify(btcoexist, ips_type);
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

	if (type == EACTIVE)
		lps_type = BTC_LPS_DISABLE;
	else
		lps_type = BTC_LPS_ENABLE;

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_lps_notify(btcoexist, lps_type);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_lps_notify(btcoexist, lps_type);
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

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_scan_notify(btcoexist, scan_type);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_scan_notify(btcoexist, scan_type);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_scan_notify_wifi_only(struct wifi_only_cfg *wifionly_cfg,
				    u8 is_5g)
{
	if (IS_HARDWARE_TYPE_8822B(wifionly_cfg->adapter))
		ex_hal8822b_wifi_only_scannotify(wifionly_cfg, is_5g);
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

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_connect_notify(btcoexist, asso_type);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_connect_notify(btcoexist, asso_type);
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

	if (media_status == RT_MEDIA_CONNECT)
		status = BTC_MEDIA_CONNECT;
	else
		status = BTC_MEDIA_DISCONNECT;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_media_status_notify(btcoexist, status);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_media_status_notify(btcoexist, status);
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

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_specific_packet_notify(btcoexist,
							       packet_type);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_specific_packet_notify(btcoexist,
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

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_bt_info_notify(btcoexist, tmp_buf,
						       length);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_bt_info_notify(btcoexist, tmp_buf,
						       length);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_rf_status_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_rf_status_notify(btcoexist, type);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_rf_status_notify(btcoexist, type);
	}
}

void exhalbtc_stack_operation_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_stack_operation_notify++;
}

void exhalbtc_halt_notify(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_halt_notify(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_halt_notify(btcoexist);
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

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_pnp_notify(btcoexist, pnp_state);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_pnp_notify(btcoexist, pnp_state);
	}
}

void exhalbtc_coex_dm_switch(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_coex_dm_switch++;

	halbtc_leave_low_power(btcoexist);

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_periodical(struct btc_coexist *btcoexist)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;
	btcoexist->statistics.cnt_periodical++;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_periodical(btcoexist);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_periodical(btcoexist);
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

void exhalbtc_update_min_bt_rssi(struct btc_coexist *btcoexist, s8 bt_rssi)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->stack_info.min_bt_rssi = bt_rssi;
}

void exhalbtc_set_hci_version(struct btc_coexist *btcoexist, u16 hci_version)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->stack_info.hci_version = hci_version;
}

void exhalbtc_set_bt_patch_version(struct btc_coexist *btcoexist,
				   u16 bt_hci_version, u16 bt_patch_version)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	btcoexist->bt_info.bt_real_fw_ver = bt_patch_version;
	btcoexist->bt_info.bt_hci_ver = bt_hci_version;
}

void exhalbtc_set_chip_type(struct btc_coexist *btcoexist, u8 chip_type)
{
	switch (chip_type) {
	default:
	case BT_2WIRE:
	case BT_ISSC_3WIRE:
	case BT_ACCEL:
	case BT_RTL8756:
		btcoexist->board_info.bt_chip_type = BTC_CHIP_UNDEF;
		break;
	case BT_CSR_BC4:
		btcoexist->board_info.bt_chip_type = BTC_CHIP_CSR_BC4;
		break;
	case BT_CSR_BC8:
		btcoexist->board_info.bt_chip_type = BTC_CHIP_CSR_BC8;
		break;
	case BT_RTL8723A:
		btcoexist->board_info.bt_chip_type = BTC_CHIP_RTL8723A;
		break;
	case BT_RTL8821A:
		btcoexist->board_info.bt_chip_type = BTC_CHIP_RTL8821;
		break;
	case BT_RTL8723B:
		btcoexist->board_info.bt_chip_type = BTC_CHIP_RTL8723B;
		break;
	}
}

void exhalbtc_set_ant_num(struct rtl_priv *rtlpriv, u8 type, u8 ant_num)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	if (type == BT_COEX_ANT_TYPE_PG) {
		btcoexist->board_info.pg_ant_num = ant_num;
		btcoexist->board_info.btdm_ant_num = ant_num;
	} else if (type == BT_COEX_ANT_TYPE_ANTDIV) {
		btcoexist->board_info.btdm_ant_num = ant_num;
	} else if (type == BT_COEX_ANT_TYPE_DETECTED) {
		btcoexist->board_info.btdm_ant_num = ant_num;
		if (rtlpriv->cfg->mod_params->ant_sel == 1)
			btcoexist->board_info.btdm_ant_pos =
				BTC_ANTENNA_AT_AUX_PORT;
		else
			btcoexist->board_info.btdm_ant_pos =
				BTC_ANTENNA_AT_MAIN_PORT;
	}
}

/* Currently used by 8723b only, S0 or S1 */
void exhalbtc_set_single_ant_path(struct btc_coexist *btcoexist,
				  u8 single_ant_path)
{
	btcoexist->board_info.single_ant_path = single_ant_path;
}

void exhalbtc_display_bt_coex_info(struct btc_coexist *btcoexist,
				   struct seq_file *m)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_display_coex_info(btcoexist, m);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_display_coex_info(btcoexist, m);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_switch_band_notify(struct btc_coexist *btcoexist, u8 type)
{
	if (!halbtc_is_bt_coexist_available(btcoexist))
		return;

	if (btcoexist->manual_control)
		return;

	halbtc_leave_low_power(btcoexist);

	if (IS_HARDWARE_TYPE_8822B(btcoexist->adapter)) {
		if (btcoexist->board_info.btdm_ant_num == 1)
			ex_btc8822b1ant_switchband_notify(btcoexist, type);
		else if (btcoexist->board_info.btdm_ant_num == 2)
			ex_btc8822b2ant_switchband_notify(btcoexist, type);
	}

	halbtc_normal_low_power(btcoexist);
}

void exhalbtc_switch_band_notify_wifi_only(struct wifi_only_cfg *wifionly_cfg,
					   u8 is_5g)
{
	if (IS_HARDWARE_TYPE_8822B(wifionly_cfg->adapter))
		ex_hal8822b_wifi_only_switchbandnotify(wifionly_cfg, is_5g);
}
