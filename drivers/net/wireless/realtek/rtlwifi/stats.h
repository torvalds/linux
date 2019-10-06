/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2009-2012  Realtek Corporation.*/

#ifndef __RTL_STATS_H__
#define __RTL_STATS_H__

#define	PHY_RSSI_SLID_WIN_MAX			100
#define	PHY_LINKQUALITY_SLID_WIN_MAX		20
#define	PHY_BEACON_RSSI_SLID_WIN_MAX		10

/* Rx smooth factor */
#define	RX_SMOOTH_FACTOR			20

u8 rtl_query_rxpwrpercentage(s8 antpower);
u8 rtl_evm_db_to_percentage(s8 value);
long rtl_signal_scale_mapping(struct ieee80211_hw *hw, long currsig);
void rtl_process_phyinfo(struct ieee80211_hw *hw, u8 *buffer,
			 struct rtl_stats *pstatus);

#endif
