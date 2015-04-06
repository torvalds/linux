/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 *
 ******************************************************************************/
#ifndef __RTW_AP_H_
#define __RTW_AP_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


#ifdef CONFIG_92D_AP_MODE

/* external function */
extern void rtw_indicate_sta_assoc_event(struct rtw_adapter *padapter, struct sta_info *psta);
extern void rtw_indicate_sta_disassoc_event(struct rtw_adapter *padapter, struct sta_info *psta);


void init_mlme_ap_info(struct rtw_adapter *padapter);
void free_mlme_ap_info(struct rtw_adapter *padapter);
void rtw_add_bcn_ie(struct rtw_adapter *padapter, struct wlan_bssid_ex *pnetwork, u8 index, u8 *data, u8 len);
void rtw_remove_bcn_ie(struct rtw_adapter *padapter, struct wlan_bssid_ex *pnetwork, u8 index);
void update_beacon(struct rtw_adapter *padapter, u8 ie_id, u8 *oui, u8 tx);
void expire_timeout_chk(struct rtw_adapter *padapter);
void update_sta_info_apmode(struct rtw_adapter *padapter, struct sta_info *psta);
int rtw_check_beacon_data(struct rtw_adapter *padapter, u8 *pbuf,  int len);
void rtw_set_macaddr_acl(struct rtw_adapter *padapter, int mode);
int rtw_acl_add_sta(struct rtw_adapter *padapter, u8 *addr);
int rtw_acl_remove_sta(struct rtw_adapter *padapter, u8 *addr);

#ifdef CONFIG_NATIVEAP_MLME
void associated_clients_update(struct rtw_adapter *padapter, u8 updated);
void bss_cap_update_on_sta_join(struct rtw_adapter *padapter, struct sta_info *psta);
u8 bss_cap_update_on_sta_leave(struct rtw_adapter *padapter, struct sta_info *psta);
void sta_info_update(struct rtw_adapter *padapter, struct sta_info *psta);
void ap_sta_info_defer_update(struct rtw_adapter *padapter, struct sta_info *psta);
u8 ap_free_sta(struct rtw_adapter *padapter, struct sta_info *psta, bool active, u16 reason);
int rtw_sta_flush(struct rtw_adapter *padapter);
int rtw_ap_inform_ch_switch (struct rtw_adapter *padapter, u8 new_ch, u8 ch_offset);
void start_ap_mode(struct rtw_adapter *padapter);
void stop_ap_mode(struct rtw_adapter *padapter);
#endif
#endif /* end of CONFIG_92D_AP_MODE */

#endif
