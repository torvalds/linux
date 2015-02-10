/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
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

#ifndef __RTL_BTC_H__
#define __RTL_BTC_H__

#include "halbt_precomp.h"

void rtl_btc_init_variables(struct rtl_priv *rtlpriv);
void rtl_btc_init_hal_vars(struct rtl_priv *rtlpriv);
void rtl_btc_init_hw_config(struct rtl_priv *rtlpriv);
void rtl_btc_ips_notify(struct rtl_priv *rtlpriv, u8 type);
void rtl_btc_lps_notify(struct rtl_priv *rtlpriv, u8 type);
void rtl_btc_scan_notify(struct rtl_priv *rtlpriv, u8 scantype);
void rtl_btc_connect_notify(struct rtl_priv *rtlpriv, u8 action);
void rtl_btc_mediastatus_notify(struct rtl_priv *rtlpriv,
				enum rt_media_status mstatus);
void rtl_btc_periodical(struct rtl_priv *rtlpriv);
void rtl_btc_halt_notify(void);
void rtl_btc_btinfo_notify(struct rtl_priv *rtlpriv, u8 *tmpbuf, u8 length);
bool rtl_btc_is_limited_dig(struct rtl_priv *rtlpriv);
bool rtl_btc_is_disable_edca_turbo(struct rtl_priv *rtlpriv);
bool rtl_btc_is_bt_disabled(struct rtl_priv *rtlpriv);
void rtl_btc_special_packet_notify(struct rtl_priv *rtlpriv, u8 pkt_type);

struct rtl_btc_ops *rtl_btc_get_ops_pointer(void);

u8 rtl_get_hwpg_ant_num(struct rtl_priv *rtlpriv);
u8 rtl_get_hwpg_bt_exist(struct rtl_priv *rtlpriv);
u8 rtl_get_hwpg_bt_type(struct rtl_priv *rtlpriv);
enum rt_media_status mgnt_link_status_query(struct ieee80211_hw *hw);

#endif
