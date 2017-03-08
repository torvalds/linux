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

#ifndef __HAL_PHY_RF_8822B_H__
#define __HAL_PHY_RF_8822B_H__

#define AVG_THERMAL_NUM_8822B	4
#define RF_T_METER_8822B		0x42

void ConfigureTxpowerTrack_8822B(
	PTXPWRTRACK_CFG	pConfig
	);

VOID
ODM_TxPwrTrackSetPwr8822B(
	PVOID				pDM_VOID,
	PWRTRACK_METHOD	Method,
	u1Byte				RFPath,
	u1Byte				ChannelMappedIndex
	);

VOID
GetDeltaSwingTable_8822B(
	PVOID		pDM_VOID,
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	pu1Byte		*TemperatureUP_A,
	pu1Byte		*TemperatureDOWN_A,
	pu1Byte		*TemperatureUP_B,
	pu1Byte		*TemperatureDOWN_B,
	pu1Byte		*TemperatureUP_CCK_A,
	pu1Byte		*TemperatureDOWN_CCK_A,
	pu1Byte		*TemperatureUP_CCK_B,
	pu1Byte		*TemperatureDOWN_CCK_B
#else
	pu1Byte		*TemperatureUP_A,
	pu1Byte		*TemperatureDOWN_A,
	pu1Byte		*TemperatureUP_B,
	pu1Byte		*TemperatureDOWN_B
#endif
	);

VOID
PHY_LCCalibrate_8822B(
	PVOID pDM_VOID
	);



VOID PHY_SetRFPathSwitch_8822B(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	BOOLEAN		bMain
	);

#endif	/* #ifndef __HAL_PHY_RF_8822B_H__ */

