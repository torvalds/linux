/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#include <drv_types.h>
#include <hal_btcoex_wifionly.h>
#include <hal_data.h>

void rtw_btcoex_wifionly_switchband_notify(PADAPTER padapter)
{
	hal_btcoex_wifionly_switchband_notify(padapter);
}

void rtw_btcoex_wifionly_scan_notify(PADAPTER padapter)
{
	hal_btcoex_wifionly_scan_notify(padapter);
}

void rtw_btcoex_wifionly_hw_config(PADAPTER padapter)
{
	hal_btcoex_wifionly_hw_config(padapter);
}

void rtw_btcoex_wifionly_initialize(PADAPTER padapter)
{
	hal_btcoex_wifionly_initlizevariables(padapter);
}
