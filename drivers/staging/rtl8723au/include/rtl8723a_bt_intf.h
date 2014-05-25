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
bool rtl8723a_BT_using_antenna_1(struct rtw_adapter *padapter);
bool rtl8723a_BT_enabled(struct rtw_adapter *padapter);
bool rtl8723a_BT_coexist(struct rtw_adapter *padapter);
void rtl8723a_BT_do_coexist(struct rtw_adapter *padapter);
void rtl8723a_BT_wifiscan_notify(struct rtw_adapter *padapter, u8 scanType);
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
#define rtl8723a_BT_do_coexist(padapter)	do {} while(0)
#define rtl8723a_BT_wifiscan_notify(padapter, scanType)		do {} while(0)
#endif

#endif
