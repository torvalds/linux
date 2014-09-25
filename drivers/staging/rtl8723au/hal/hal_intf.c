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
 ******************************************************************************/

#define _HAL_INTF_C_
#include <osdep_service.h>
#include <drv_types.h>

#include <hal_intf.h>

#include <rtl8723a_hal.h>

void rtw_hal_update_ra_mask23a(struct sta_info *psta, u8 rssi_level)
{
	struct rtw_adapter *padapter;
	struct mlme_priv *pmlmepriv;

	if (!psta)
		return;

	padapter = psta->padapter;

	pmlmepriv = &padapter->mlmepriv;

	if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
#ifdef CONFIG_8723AU_AP_MODE
		add_RATid23a(padapter, psta, rssi_level);
#endif
	} else
		rtl8723a_update_ramask(padapter, psta->mac_id, rssi_level);
}
