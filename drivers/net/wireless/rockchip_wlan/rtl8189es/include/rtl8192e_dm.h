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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTL8192E_DM_H__
#define __RTL8192E_DM_H__


void rtl8192e_init_dm_priv(IN PADAPTER Adapter);
void rtl8192e_deinit_dm_priv(IN PADAPTER Adapter);
void rtl8192e_InitHalDm(IN PADAPTER Adapter);
void rtl8192e_HalDmWatchDog(IN PADAPTER Adapter);

//VOID rtl8192c_dm_CheckTXPowerTracking(IN PADAPTER Adapter);

//void rtl8192c_dm_RF_Saving(IN PADAPTER pAdapter, IN u8 bForceInNormal);

#ifdef CONFIG_ANTENNA_DIVERSITY
void	AntDivCompare8192e(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
u8 AntDivBeforeLink8192e(PADAPTER Adapter );
#endif
#endif

