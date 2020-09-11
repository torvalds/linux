// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2009-2013  Realtek Corporation.*/

#include "../wifi.h"
#include <linux/vmalloc.h>
#include <linux/module.h>

#include "rtl_btc.h"
#include "halbt_precomp.h"

static struct rtl_btc_ops rtl_btc_operation = {
	.btc_init_variables = rtl_btc_init_variables,
	.btc_init_variables_wifi_only = rtl_btc_init_variables_wifi_only,
	.btc_deinit_variables = rtl_btc_deinit_variables,
	.btc_init_hal_vars = rtl_btc_init_hal_vars,
	.btc_power_on_setting = rtl_btc_power_on_setting,
	.btc_init_hw_config = rtl_btc_init_hw_config,
	.btc_init_hw_config_wifi_only = rtl_btc_init_hw_config_wifi_only,
	.btc_ips_notify = rtl_btc_ips_notify,
	.btc_lps_notify = rtl_btc_lps_notify,
	.btc_scan_notify = rtl_btc_scan_notify,
	.btc_scan_notify_wifi_only = rtl_btc_scan_notify_wifi_only,
	.btc_connect_notify = rtl_btc_connect_notify,
	.btc_mediastatus_notify = rtl_btc_mediastatus_notify,
	.btc_periodical = rtl_btc_periodical,
	.btc_halt_notify = rtl_btc_halt_notify,
	.btc_btinfo_notify = rtl_btc_btinfo_notify,
	.btc_btmpinfo_notify = rtl_btc_btmpinfo_notify,
	.btc_is_limited_dig = rtl_btc_is_limited_dig,
	.btc_is_disable_edca_turbo = rtl_btc_is_disable_edca_turbo,
	.btc_is_bt_disabled = rtl_btc_is_bt_disabled,
	.btc_special_packet_notify = rtl_btc_special_packet_notify,
	.btc_switch_band_notify = rtl_btc_switch_band_notify,
	.btc_switch_band_notify_wifi_only = rtl_btc_switch_band_notify_wifionly,
	.btc_record_pwr_mode = rtl_btc_record_pwr_mode,
	.btc_get_lps_val = rtl_btc_get_lps_val,
	.btc_get_rpwm_val = rtl_btc_get_rpwm_val,
	.btc_is_bt_ctrl_lps = rtl_btc_is_bt_ctrl_lps,
	.btc_is_bt_lps_on = rtl_btc_is_bt_lps_on,
	.btc_get_ampdu_cfg = rtl_btc_get_ampdu_cfg,
	.btc_display_bt_coex_info = rtl_btc_display_bt_coex_info,
};

void rtl_btc_display_bt_coex_info(struct rtl_priv *rtlpriv, struct seq_file *m)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist) {
		seq_puts(m, "btc_coexist context is NULL!\n");
		return;
	}

	exhalbtc_display_bt_coex_info(btcoexist, m);
}

void rtl_btc_record_pwr_mode(struct rtl_priv *rtlpriv, u8 *buf, u8 len)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);
	u8 safe_len;

	if (!btcoexist)
		return;

	safe_len = sizeof(btcoexist->pwr_mode_val);

	if (safe_len > len)
		safe_len = len;

	memcpy(btcoexist->pwr_mode_val, buf, safe_len);
}

u8 rtl_btc_get_lps_val(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return 0;

	return btcoexist->bt_info.lps_val;
}

u8 rtl_btc_get_rpwm_val(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return 0;

	return btcoexist->bt_info.rpwm_val;
}

bool rtl_btc_is_bt_ctrl_lps(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return false;

	return btcoexist->bt_info.bt_ctrl_lps;
}

bool rtl_btc_is_bt_lps_on(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return false;

	return btcoexist->bt_info.bt_lps_on;
}

void rtl_btc_get_ampdu_cfg(struct rtl_priv *rtlpriv, u8 *reject_agg,
			   u8 *ctrl_agg_size, u8 *agg_size)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist) {
		*reject_agg = false;
		*ctrl_agg_size = false;
		return;
	}

	if (reject_agg)
		*reject_agg = btcoexist->bt_info.reject_agg_pkt;
	if (ctrl_agg_size)
		*ctrl_agg_size = btcoexist->bt_info.bt_ctrl_agg_buf_size;
	if (agg_size)
		*agg_size = btcoexist->bt_info.agg_buf_size;
}

static void rtl_btc_alloc_variable(struct rtl_priv *rtlpriv, bool wifi_only)
{
	if (wifi_only)
		rtlpriv->btcoexist.wifi_only_context =
			kzalloc(sizeof(struct wifi_only_cfg), GFP_KERNEL);
	else
		rtlpriv->btcoexist.btc_context =
			kzalloc(sizeof(struct btc_coexist), GFP_KERNEL);
}

static void rtl_btc_free_variable(struct rtl_priv *rtlpriv)
{
	kfree(rtlpriv->btcoexist.btc_context);
	rtlpriv->btcoexist.btc_context = NULL;

	kfree(rtlpriv->btcoexist.wifi_only_context);
	rtlpriv->btcoexist.wifi_only_context = NULL;
}

void rtl_btc_init_variables(struct rtl_priv *rtlpriv)
{
	rtl_btc_alloc_variable(rtlpriv, false);

	exhalbtc_initlize_variables(rtlpriv);
	exhalbtc_bind_bt_coex_withadapter(rtlpriv);
}

void rtl_btc_init_variables_wifi_only(struct rtl_priv *rtlpriv)
{
	rtl_btc_alloc_variable(rtlpriv, true);

	exhalbtc_initlize_variables_wifi_only(rtlpriv);
}

void rtl_btc_deinit_variables(struct rtl_priv *rtlpriv)
{
	rtl_btc_free_variable(rtlpriv);
}

void rtl_btc_init_hal_vars(struct rtl_priv *rtlpriv)
{
	/* move ant_num, bt_type and single_ant_path to
	 * exhalbtc_bind_bt_coex_withadapter()
	 */
}

void rtl_btc_power_on_setting(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_power_on_setting(btcoexist);
}

void rtl_btc_init_hw_config(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	u8 bt_exist;

	bt_exist = rtl_get_hwpg_bt_exist(rtlpriv);
	rtl_dbg(rtlpriv, COMP_INIT, DBG_DMESG,
		"%s, bt_exist is %d\n", __func__, bt_exist);

	if (!btcoexist)
		return;

	exhalbtc_init_hw_config(btcoexist, !bt_exist);
	exhalbtc_init_coex_dm(btcoexist);
}

void rtl_btc_init_hw_config_wifi_only(struct rtl_priv *rtlpriv)
{
	struct wifi_only_cfg *wifionly_cfg = rtl_btc_wifi_only(rtlpriv);

	if (!wifionly_cfg)
		return;

	exhalbtc_init_hw_config_wifi_only(wifionly_cfg);
}

void rtl_btc_ips_notify(struct rtl_priv *rtlpriv, u8 type)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_ips_notify(btcoexist, type);

	if (type == ERFON) {
		/* In some situation, it doesn't scan after leaving IPS, and
		 * this will cause btcoex in wrong state.
		 */
		exhalbtc_scan_notify(btcoexist, 1);
		exhalbtc_scan_notify(btcoexist, 0);
	}
}

void rtl_btc_lps_notify(struct rtl_priv *rtlpriv, u8 type)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_lps_notify(btcoexist, type);
}

void rtl_btc_scan_notify(struct rtl_priv *rtlpriv, u8 scantype)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_scan_notify(btcoexist, scantype);
}

void rtl_btc_scan_notify_wifi_only(struct rtl_priv *rtlpriv, u8 scantype)
{
	struct rtl_hal *rtlhal = rtl_hal(rtlpriv);
	struct wifi_only_cfg *wifionly_cfg = rtl_btc_wifi_only(rtlpriv);
	u8 is_5g = (rtlhal->current_bandtype == BAND_ON_5G);

	if (!wifionly_cfg)
		return;

	exhalbtc_scan_notify_wifi_only(wifionly_cfg, is_5g);
}

void rtl_btc_connect_notify(struct rtl_priv *rtlpriv, u8 action)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_connect_notify(btcoexist, action);
}

void rtl_btc_mediastatus_notify(struct rtl_priv *rtlpriv,
				enum rt_media_status mstatus)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_mediastatus_notify(btcoexist, mstatus);
}

void rtl_btc_periodical(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	/*rtl_bt_dm_monitor();*/
	exhalbtc_periodical(btcoexist);
}

void rtl_btc_halt_notify(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_halt_notify(btcoexist);
}

void rtl_btc_btinfo_notify(struct rtl_priv *rtlpriv, u8 *tmp_buf, u8 length)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	exhalbtc_bt_info_notify(btcoexist, tmp_buf, length);
}

void rtl_btc_btmpinfo_notify(struct rtl_priv *rtlpriv, u8 *tmp_buf, u8 length)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);
	u8 extid, seq;
	u16 bt_real_fw_ver;
	u8 bt_fw_ver;
	u8 *data;

	if (!btcoexist)
		return;

	if ((length < 4) || (!tmp_buf))
		return;

	extid = tmp_buf[0];
	/* not response from BT FW then exit*/
	if (extid != 1) /* C2H_TRIG_BY_BT_FW = 1 */
		return;

	seq = tmp_buf[2] >> 4;
	data = &tmp_buf[3];

	/* BT Firmware version response */
	switch (seq) {
	case BT_SEQ_GET_BT_VERSION:
		bt_real_fw_ver = tmp_buf[3] | (tmp_buf[4] << 8);
		bt_fw_ver = tmp_buf[5];

		btcoexist->bt_info.bt_real_fw_ver = bt_real_fw_ver;
		btcoexist->bt_info.bt_fw_ver = bt_fw_ver;
		break;
	case BT_SEQ_GET_AFH_MAP_L:
		btcoexist->bt_info.afh_map_l = le32_to_cpu(*(__le32 *)data);
		break;
	case BT_SEQ_GET_AFH_MAP_M:
		btcoexist->bt_info.afh_map_m = le32_to_cpu(*(__le32 *)data);
		break;
	case BT_SEQ_GET_AFH_MAP_H:
		btcoexist->bt_info.afh_map_h = le16_to_cpu(*(__le16 *)data);
		break;
	case BT_SEQ_GET_BT_COEX_SUPPORTED_FEATURE:
		btcoexist->bt_info.bt_supported_feature = tmp_buf[3] |
							  (tmp_buf[4] << 8);
		break;
	case BT_SEQ_GET_BT_COEX_SUPPORTED_VERSION:
		btcoexist->bt_info.bt_supported_version = tmp_buf[3] |
							  (tmp_buf[4] << 8);
		break;
	case BT_SEQ_GET_BT_ANT_DET_VAL:
		btcoexist->bt_info.bt_ant_det_val = tmp_buf[3];
		break;
	case BT_SEQ_GET_BT_BLE_SCAN_PARA:
		btcoexist->bt_info.bt_ble_scan_para = tmp_buf[3] |
						      (tmp_buf[4] << 8) |
						      (tmp_buf[5] << 16) |
						      (tmp_buf[6] << 24);
		break;
	case BT_SEQ_GET_BT_BLE_SCAN_TYPE:
		btcoexist->bt_info.bt_ble_scan_type = tmp_buf[3];
		break;
	case BT_SEQ_GET_BT_DEVICE_INFO:
		btcoexist->bt_info.bt_device_info =
						le32_to_cpu(*(__le32 *)data);
		break;
	case BT_OP_GET_BT_FORBIDDEN_SLOT_VAL:
		btcoexist->bt_info.bt_forb_slot_val =
						le32_to_cpu(*(__le32 *)data);
		break;
	}

	rtl_dbg(rtlpriv, COMP_BT_COEXIST, DBG_LOUD,
		"btmpinfo complete req_num=%d\n", seq);

	complete(&btcoexist->bt_mp_comp);
}

bool rtl_btc_is_limited_dig(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return false;

	return btcoexist->bt_info.limited_dig;
}

bool rtl_btc_is_disable_edca_turbo(struct rtl_priv *rtlpriv)
{
	bool bt_change_edca = false;
	u32 cur_edca_val;
	u32 edca_bt_hs_uplink = 0x5ea42b, edca_bt_hs_downlink = 0x5ea42b;
	u32 edca_hs;
	u32 edca_addr = 0x504;

	cur_edca_val = rtl_read_dword(rtlpriv, edca_addr);
	if (halbtc_is_wifi_uplink(rtlpriv)) {
		if (cur_edca_val != edca_bt_hs_uplink) {
			edca_hs = edca_bt_hs_uplink;
			bt_change_edca = true;
		}
	} else {
		if (cur_edca_val != edca_bt_hs_downlink) {
			edca_hs = edca_bt_hs_downlink;
			bt_change_edca = true;
		}
	}

	if (bt_change_edca)
		rtl_write_dword(rtlpriv, edca_addr, edca_hs);

	return true;
}

bool rtl_btc_is_bt_disabled(struct rtl_priv *rtlpriv)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return true;

	/* It seems 'bt_disabled' is never be initialized or set. */
	if (btcoexist->bt_info.bt_disabled)
		return true;
	else
		return false;
}

void rtl_btc_special_packet_notify(struct rtl_priv *rtlpriv, u8 pkt_type)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);

	if (!btcoexist)
		return;

	return exhalbtc_special_packet_notify(btcoexist, pkt_type);
}

void rtl_btc_switch_band_notify(struct rtl_priv *rtlpriv, u8 band_type,
				bool scanning)
{
	struct btc_coexist *btcoexist = rtl_btc_coexist(rtlpriv);
	u8 type = BTC_NOT_SWITCH;

	if (!btcoexist)
		return;

	switch (band_type) {
	case BAND_ON_2_4G:
		if (scanning)
			type = BTC_SWITCH_TO_24G;
		else
			type = BTC_SWITCH_TO_24G_NOFORSCAN;
		break;

	case BAND_ON_5G:
		type = BTC_SWITCH_TO_5G;
		break;
	}

	if (type != BTC_NOT_SWITCH)
		exhalbtc_switch_band_notify(btcoexist, type);
}

void rtl_btc_switch_band_notify_wifionly(struct rtl_priv *rtlpriv, u8 band_type,
					 bool scanning)
{
	struct wifi_only_cfg *wifionly_cfg = rtl_btc_wifi_only(rtlpriv);
	u8 is_5g = (band_type == BAND_ON_5G);

	if (!wifionly_cfg)
		return;

	exhalbtc_switch_band_notify_wifi_only(wifionly_cfg, is_5g);
}

struct rtl_btc_ops *rtl_btc_get_ops_pointer(void)
{
	return &rtl_btc_operation;
}
EXPORT_SYMBOL(rtl_btc_get_ops_pointer);


enum rt_media_status mgnt_link_status_query(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	enum rt_media_status    m_status = RT_MEDIA_DISCONNECT;

	u8 bibss = (mac->opmode == NL80211_IFTYPE_ADHOC) ? 1 : 0;

	if (bibss || rtlpriv->mac80211.link_state >= MAC80211_LINKED)
		m_status = RT_MEDIA_CONNECT;

	return m_status;
}

u8 rtl_get_hwpg_bt_exist(struct rtl_priv *rtlpriv)
{
	return rtlpriv->btcoexist.btc_info.btcoexist;
}

MODULE_AUTHOR("Page He	<page_he@realsil.com.cn>");
MODULE_AUTHOR("Realtek WlanFAE	<wlanfae@realtek.com>");
MODULE_AUTHOR("Larry Finger	<Larry.FInger@lwfinger.net>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Realtek 802.11n PCI wireless core");

static int __init rtl_btcoexist_module_init(void)
{
	return 0;
}

static void __exit rtl_btcoexist_module_exit(void)
{
	return;
}

module_init(rtl_btcoexist_module_init);
module_exit(rtl_btcoexist_module_exit);
