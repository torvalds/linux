/******************************************************************************
 *
 * Copyright(c) 2009-2014  Realtek Corporation.
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

#ifndef __FW_COMMON_H__
#define __FW_COMMON_H__#endif

void rtl8723_enable_fw_download(struct ieee80211_hw *hw, bool enable);
void rtl8723_fw_block_write(struct ieee80211_hw *hw,
			    const u8 *buffer, u32 size);
void rtl8723_fw_page_write(struct ieee80211_hw *hw,
			   u32 page, const u8 *buffer, u32 size);
void rtl8723_write_fw(struct ieee80211_hw *hw,
		      enum version_8723be version,
		      u8 *buffer, u32 size);
int rtl8723_fw_free_to_go(struct ieee80211_hw *hw, bool is_8723be);
int rtl8723_download_fw(struct ieee80211_hw *hw,
			bool buse_wake_on_wlan_fw, bool is_8723be);
bool rtl8723_check_fw_read_last_h2c(struct ieee80211_hw *hw, u8 boxnum);
void rtl8723_fill_h2c_command(struct ieee80211_hw *hw, u8 element_id,
			      u32 cmd_len, u8 *p_cmdbuffer);
void rtl8723_fill_h2c_cmd(struct ieee80211_hw *hw, u8 element_id,
			  u32 cmd_len, u8 *p_cmdbuffer);
void rtl8723_set_fw_joinbss_report_cmd(struct ieee80211_hw *hw, u8 mstatus);
bool rtl8723_cmd_send_packet(struct ieee80211_hw *hw,
			     struct sk_buff *skb);
void rtl8723_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool dl_finished);
void rtl8723_set_p2p_ctw_period_cmd(struct ieee80211_hw *hw, u8 ctwindow);
void rtl8723_set_p2p_ps_offload_cmd(struct ieee80211_hw *hw, u8 p2p_ps_state);
