/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2010  Realtek Corporation.*/

#ifndef __RTL_BTC_H__
#define __RTL_BTC_H__

#include "halbt_precomp.h"

void rtl_btc_init_variables(struct rtl_priv *rtlpriv);
void rtl_btc_init_variables_wifi_only(struct rtl_priv *rtlpriv);
void rtl_btc_deinit_variables(struct rtl_priv *rtlpriv);
void rtl_btc_init_hal_vars(struct rtl_priv *rtlpriv);
void rtl_btc_power_on_setting(struct rtl_priv *rtlpriv);
void rtl_btc_init_hw_config(struct rtl_priv *rtlpriv);
void rtl_btc_init_hw_config_wifi_only(struct rtl_priv *rtlpriv);
void rtl_btc_ips_notify(struct rtl_priv *rtlpriv, u8 type);
void rtl_btc_lps_notify(struct rtl_priv *rtlpriv, u8 type);
void rtl_btc_scan_notify(struct rtl_priv *rtlpriv, u8 scantype);
void rtl_btc_scan_notify_wifi_only(struct rtl_priv *rtlpriv, u8 scantype);
void rtl_btc_connect_notify(struct rtl_priv *rtlpriv, u8 action);
void rtl_btc_mediastatus_notify(struct rtl_priv *rtlpriv,
				enum rt_media_status mstatus);
void rtl_btc_periodical(struct rtl_priv *rtlpriv);
void rtl_btc_halt_notify(struct rtl_priv *rtlpriv);
void rtl_btc_btinfo_notify(struct rtl_priv *rtlpriv, u8 *tmpbuf, u8 length);
void rtl_btc_btmpinfo_notify(struct rtl_priv *rtlpriv, u8 *tmp_buf, u8 length);
bool rtl_btc_is_limited_dig(struct rtl_priv *rtlpriv);
bool rtl_btc_is_disable_edca_turbo(struct rtl_priv *rtlpriv);
bool rtl_btc_is_bt_disabled(struct rtl_priv *rtlpriv);
void rtl_btc_special_packet_notify(struct rtl_priv *rtlpriv, u8 pkt_type);
void rtl_btc_switch_band_notify(struct rtl_priv *rtlpriv, u8 band_type,
				bool scanning);
void rtl_btc_switch_band_notify_wifionly(struct rtl_priv *rtlpriv, u8 band_type,
					 bool scanning);
void rtl_btc_display_bt_coex_info(struct rtl_priv *rtlpriv, struct seq_file *m);
void rtl_btc_record_pwr_mode(struct rtl_priv *rtlpriv, u8 *buf, u8 len);
u8   rtl_btc_get_lps_val(struct rtl_priv *rtlpriv);
u8   rtl_btc_get_rpwm_val(struct rtl_priv *rtlpriv);
bool rtl_btc_is_bt_ctrl_lps(struct rtl_priv *rtlpriv);
bool rtl_btc_is_bt_lps_on(struct rtl_priv *rtlpriv);
void rtl_btc_get_ampdu_cfg(struct rtl_priv *rtlpriv, u8 *reject_agg,
			   u8 *ctrl_agg_size, u8 *agg_size);

struct rtl_btc_ops *rtl_btc_get_ops_pointer(void);

u8 rtl_get_hwpg_bt_exist(struct rtl_priv *rtlpriv);
u8 rtl_get_hwpg_bt_type(struct rtl_priv *rtlpriv);
u8 rtl_get_hwpg_ant_num(struct rtl_priv *rtlpriv);
u8 rtl_get_hwpg_single_ant_path(struct rtl_priv *rtlpriv);
u8 rtl_get_hwpg_package_type(struct rtl_priv *rtlpriv);

enum rt_media_status mgnt_link_status_query(struct ieee80211_hw *hw);

#endif
