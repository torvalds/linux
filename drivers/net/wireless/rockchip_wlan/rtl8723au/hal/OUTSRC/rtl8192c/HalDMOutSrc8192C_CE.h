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
#ifndef	__RTL8192C_ODM_H__
#define __RTL8192C_ODM_H__
//============================================================
// Description:
//
// This file is for 92CE/92CU dynamic mechanism only
//
//
//============================================================

#define	RSSI_CCK	0
#define	RSSI_OFDM	1
#define	RSSI_DEFAULT	2

#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM			9
#define HP_THERMAL_NUM		8


//============================================================
// structure and define
//============================================================




/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/
/*------------------------Export Marco Definition---------------------------*/
//#define DM_MultiSTA_InitGainChangeNotify(Event) {DM_DigTable.CurMultiSTAConnectState = Event;}


//============================================================
// function prototype
//============================================================

//
// IQ calibrate
//
VOID rtl8192c_PHY_IQCalibrate( IN PADAPTER pAdapter , IN BOOLEAN bReCovery);

//
// LC calibrate
//
VOID rtl8192c_PHY_LCCalibrate(IN	PADAPTER	pAdapter);

//
// AP calibrate
//
VOID rtl8192c_PHY_APCalibrate(IN	PADAPTER	pAdapter, IN 	char		delta);

VOID rtl8192c_odm_CheckTXPowerTracking(IN PADAPTER Adapter);

#ifdef CONFIG_ANTENNA_DIVERSITY
void	odm_AntDivCompare8192C(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
u8 odm_AntDivBeforeLink8192C(PADAPTER Adapter);
#endif

#endif	//__HAL8190PCIDM_H__

