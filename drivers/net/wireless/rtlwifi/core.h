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

#ifndef __RTL_CORE_H__
#define __RTL_CORE_H__

#define RTL_SUPPORTED_FILTERS		\
	(FIF_PROMISC_IN_BSS | \
	FIF_ALLMULTI | FIF_CONTROL | \
	FIF_OTHER_BSS | \
	FIF_FCSFAIL | \
	FIF_BCN_PRBRESP_PROMISC)

#define RTL_SUPPORTED_CTRL_FILTER	0xFF

extern const struct ieee80211_ops rtl_ops;
void rtl_fw_cb(const struct firmware *firmware, void *context);
void rtl_wowlan_fw_cb(const struct firmware *firmware, void *context);
void rtl_addr_delay(u32 addr);
void rtl_rfreg_delay(struct ieee80211_hw *hw, enum radio_path rfpath, u32 addr,
		     u32 mask, u32 data);
void rtl_bb_delay(struct ieee80211_hw *hw, u32 addr, u32 data);
bool rtl_cmd_send_packet(struct ieee80211_hw *hw, struct sk_buff *skb);
bool rtl_btc_status_false(void);

#endif
