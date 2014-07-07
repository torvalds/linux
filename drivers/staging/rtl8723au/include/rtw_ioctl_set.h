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

int rtw_set_802_11_authentication_mode23a(struct rtw_adapter *pdapter,
					  enum ndis_802_11_auth_mode authmode);
int rtw_set_802_11_bssid23a_list_scan(struct rtw_adapter *padapter,
				      struct cfg80211_ssid *pssid,
				      int ssid_max_num);
int rtw_set_802_11_ssid23a(struct rtw_adapter * padapter,
			   struct cfg80211_ssid * ssid);

u16 rtw_get_cur_max_rate23a(struct rtw_adapter *adapter);
s32 FillH2CCmd(struct rtw_adapter *padapter, u8 ElementID, u32 CmdLen, u8 *pCmdBuffer);
int rtw_do_join23a(struct rtw_adapter *padapter);

#endif
