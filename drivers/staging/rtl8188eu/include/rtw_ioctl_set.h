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
 ******************************************************************************/
#ifndef __RTW_IOCTL_SET_H_
#define __RTW_IOCTL_SET_H_

#include <drv_types.h>


typedef u8 NDIS_802_11_PMKID_VALUE[16];

u8 rtw_set_802_11_authentication_mode(struct adapter *adapt,
				      enum ndis_802_11_auth_mode authmode);
u8 rtw_set_802_11_bssid(struct adapter *adapter, u8 *bssid);
u8 rtw_set_802_11_add_wep(struct adapter *adapter, struct ndis_802_11_wep *wep);
u8 rtw_set_802_11_disassociate(struct adapter *adapter);
u8 rtw_set_802_11_bssid_list_scan(struct adapter *adapter,
				  struct ndis_802_11_ssid *pssid,
				  int ssid_max_num);
u8 rtw_set_802_11_infrastructure_mode(struct adapter *adapter,
				      enum ndis_802_11_network_infra type);
u8 rtw_set_802_11_ssid(struct adapter *adapt, struct ndis_802_11_ssid *ssid);
u16 rtw_get_cur_max_rate(struct adapter *adapter);
int rtw_set_country(struct adapter *adapter, const char *country_code);

#endif
