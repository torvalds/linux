/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
#ifndef __RTW_IOCTL_SET_H_
#define __RTW_IOCTL_SET_H_


typedef u8 NDIS_802_11_PMKID_VALUE[16];

typedef struct _BSSIDInfo {
	NDIS_802_11_MAC_ADDRESS  BSSID;
	NDIS_802_11_PMKID_VALUE  PMKID;
} BSSIDInfo, *PBSSIDInfo;


#ifdef PLATFORM_OS_XP
typedef struct _NDIS_802_11_PMKID {
	u32	Length;
	u32	BSSIDInfoCount;
	BSSIDInfo BSSIDInfo[1];
} NDIS_802_11_PMKID, *PNDIS_802_11_PMKID;
#endif


#ifdef PLATFORM_WINDOWS
u8 rtw_set_802_11_reload_defaults(_adapter *padapter, NDIS_802_11_RELOAD_DEFAULTS reloadDefaults);
u8 rtw_set_802_11_test(_adapter *padapter, NDIS_802_11_TEST *test);
u8 rtw_set_802_11_pmkid(_adapter *pdapter, NDIS_802_11_PMKID *pmkid);

u8 rtw_pnp_set_power_sleep(_adapter *padapter);
u8 rtw_pnp_set_power_wakeup(_adapter *padapter);

void rtw_pnp_resume_wk(void *context);
void rtw_pnp_sleep_wk(void *context);

#endif

u8 rtw_set_802_11_add_key(_adapter *padapter, NDIS_802_11_KEY *key);
u8 rtw_set_802_11_authentication_mode(_adapter *pdapter, NDIS_802_11_AUTHENTICATION_MODE authmode);
u8 rtw_set_802_11_bssid(_adapter *padapter, u8 *bssid);
u8 rtw_set_802_11_add_wep(_adapter *padapter, NDIS_802_11_WEP *wep);
u8 rtw_set_802_11_disassociate(_adapter *padapter);
u8 rtw_set_802_11_bssid_list_scan(_adapter *padapter, NDIS_802_11_SSID *pssid, int ssid_max_num, struct rtw_ieee80211_channel *ch, int ch_num);
u8 rtw_set_802_11_infrastructure_mode(_adapter *padapter, NDIS_802_11_NETWORK_INFRASTRUCTURE networktype);
u8 rtw_set_802_11_remove_wep(_adapter *padapter, u32 keyindex);
u8 rtw_set_802_11_ssid(_adapter *padapter, NDIS_802_11_SSID *ssid);
u8 rtw_set_802_11_connect(_adapter *padapter, u8 *bssid, NDIS_802_11_SSID *ssid);
u8 rtw_set_802_11_remove_key(_adapter *padapter, NDIS_802_11_REMOVE_KEY *key);

u8 rtw_validate_bssid(u8 *bssid);
u8 rtw_validate_ssid(NDIS_802_11_SSID *ssid);

u16 rtw_get_cur_max_rate(_adapter *adapter);
int rtw_set_scan_mode(_adapter *adapter, RT_SCAN_TYPE scan_mode);
int rtw_set_channel_plan(_adapter *adapter, u8 channel_plan);
int rtw_set_country(_adapter *adapter, const char *country_code);
int rtw_set_band(_adapter *adapter, u8 band);

#endif
