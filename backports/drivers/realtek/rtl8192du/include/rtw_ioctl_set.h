/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#ifndef __RTW_IOCTL_SET_H_
#define __RTW_IOCTL_SET_H_

#include <drv_conf.h>
#include <drv_types.h>

u8 rtw_set_802_11_add_key(struct rtw_adapter * padapter, struct ndis_802_11_key *key);
u8 rtw_set_802_11_authentication_mode(struct rtw_adapter *pdapter, enum NDIS_802_11_AUTHENTICATION_MODE authmode);
u8 rtw_set_802_11_bssid(struct rtw_adapter* padapter, u8 *bssid);
u8 rtw_set_802_11_add_wep(struct rtw_adapter * padapter, struct ndis_802_11_wep * wep);
u8 rtw_set_802_11_disassociate(struct rtw_adapter * padapter);
u8 rtw_set_802_11_bssid_list_scan(struct rtw_adapter* padapter, struct ndis_802_11_ssid *pssid, int ssid_max_num);
u8 rtw_set_802_11_infrastructure_mode(struct rtw_adapter * padapter, enum NDIS_802_11_NETWORK_INFRASTRUCTURE networktype);
u8 rtw_set_802_11_remove_wep(struct rtw_adapter * padapter, u32 keyindex);
u8 rtw_set_802_11_ssid(struct rtw_adapter * padapter, struct ndis_802_11_ssid * ssid);
u8 rtw_set_802_11_remove_key(struct rtw_adapter * padapter, struct ndis_802_11_remove_key *key);

u8 rtw_validate_ssid(struct ndis_802_11_ssid *ssid);

u16 rtw_get_cur_max_rate(struct rtw_adapter *adapter);
int rtw_set_scan_mode(struct rtw_adapter *adapter, enum RT_SCAN_TYPE scan_mode);
int rtw_set_channel_plan(struct rtw_adapter *adapter, u8 channel_plan);
int rtw_set_country(struct rtw_adapter *adapter, const char *country_code);
int rtw_change_ifname(struct rtw_adapter *padapter, const char *ifname);

#endif
