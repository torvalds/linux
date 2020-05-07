/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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
 *****************************************************************************/

#ifndef _RTW_RM_UTIL_H_
#define _RTW_RM_UTIL_H_
/*
 * define the following channels as the max channels in each channel plan.
 * 2G, total 14 chnls
 * {1,2,3,4,5,6,7,8,9,10,11,12,13,14}
 * 5G, total 25 chnls
 * {36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165}
 */
#ifndef MAX
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#endif

u8 rm_get_oper_class_via_ch(u8 ch);
u8 rm_get_ch_set( struct rtw_ieee80211_channel *pch_set, u8 op_class, u8 ch_num);
u8 rm_get_bcn_rsni(struct rm_obj *prm, struct wlan_network *pnetwork);
u8 rm_get_bcn_rcpi(struct rm_obj *prm, struct wlan_network *pnetwork);
u8 rm_get_frame_rsni(struct rm_obj *prm, union recv_frame *pframe);
u8 translate_percentage_to_rcpi(u32 SignalStrengthIndex);
u8 translate_dbm_to_rcpi(s8 SignalPower);
int is_wildcard_bssid(u8 *bssid);

int rm_get_path_a_max_tx_power(_adapter *adapter, s8 *path_a);
int rm_get_tx_power(PADAPTER adapter, enum rf_path path, enum MGN_RATE rate, s8 *pwr);
int rm_get_rx_sensitivity(PADAPTER adapter, enum channel_width bw, enum MGN_RATE rate, s8 *pwr);

#endif /* _RTW_RM_UTIL_H_ */
