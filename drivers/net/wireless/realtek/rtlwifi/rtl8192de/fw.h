/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL92D__FW__H__
#define __RTL92D__FW__H__

int rtl92d_download_fw(struct ieee80211_hw *hw);
void rtl92d_set_fw_rsvdpagepkt(struct ieee80211_hw *hw, bool b_dl_finished);

#endif
