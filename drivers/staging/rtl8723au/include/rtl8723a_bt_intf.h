/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 * Copyright(c) 2014, Jes Sorensen <Jes.Sorensen@redhat.com>
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
#ifndef __RTL8723A_BT_INTF_H__
#define __RTL8723A_BT_INTF_H__

#include <drv_types.h>

#ifdef CONFIG_8723AU_BT_COEXIST
enum rt_media_status;
bool rtl8723a_BT_using_antenna_1(struct rtw_adapter *padapter);
bool rtl8723a_BT_enabled(struct rtw_adapter *padapter);
bool rtl8723a_BT_coexist(struct rtw_adapter *padapter);
void rtl8723a_BT_do_coexist(struct rtw_adapter *padapter);
void rtl8723a_BT_wifiscan_notify(struct rtw_adapter *padapter, u8 scanType);
void rtl8723a_BT_mediastatus_notify(struct rtw_adapter *padapter,
				    enum rt_media_status mstatus);
void rtl8723a_BT_specialpacket_notify(struct rtw_adapter *padapter);
void rtl8723a_BT_lps_leave(struct rtw_adapter *padapter);
void rtl8723a_BT_disable_coexist(struct rtw_adapter *padapter);
bool rtl8723a_BT_disable_EDCA_turbo(struct rtw_adapter *padapter);
void rtl8723a_dual_antenna_detection(struct rtw_adapter *padapter);
void rtl8723a_BT_init_hwconfig(struct rtw_adapter *padapter);
void rtl8723a_BT_wifiassociate_notify(struct rtw_adapter *padapter, u8 action);
void rtl8723a_BT_init_hal_vars(struct rtw_adapter *padapter);
void rtl8723a_fw_c2h_BT_info(struct rtw_adapter *padapter, u8 *tmpBuf, u8 length);
#else
static inline bool rtl8723a_BT_using_antenna_1(struct rtw_adapter *padapter)
{
	return false;
}
static inline bool rtl8723a_BT_enabled(struct rtw_adapter *padapter)
{
	return false;
}
static inline bool rtl8723a_BT_coexist(struct rtw_adapter *padapter)
{
	return false;
}
#define rtl8723a_BT_do_coexist(padapter)			do {} while(0)
#define rtl8723a_BT_wifiscan_notify(padapter, scanType)		do {} while(0)
#define rtl8723a_BT_mediastatus_notify(padapter, mstatus)	do {} while(0)
#define rtl8723a_BT_specialpacket_notify(padapter)		do {} while(0)
#define rtl8723a_BT_lps_leave(padapter)				do {} while(0)
#define rtl8723a_BT_disable_coexist(padapter)			do {} while(0)
static inline bool rtl8723a_BT_disable_EDCA_turbo(struct rtw_adapter *padapter)
{
	return false;
}
#define rtl8723a_dual_antenna_detection(padapter)		do {} while(0)
#define rtl8723a_BT_init_hwconfig(padapter)			do {} while(0)
#define rtl8723a_BT_wifiassociate_notify(padapter, action)	do {} while(0)
#define rtl8723a_BT_init_hal_vars(padapter)			do {} while(0)
#define rtl8723a_fw_c2h_BT_info(padapter, tmpBuf, length)	do {} while(0)
#endif

#endif
