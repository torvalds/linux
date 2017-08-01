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

#ifndef __HAL_PHY_RF_8723D_H__
#define __HAL_PHY_RF_8723D_H__

/*--------------------------Define Parameters-------------------------------*/
#define	IQK_DELAY_TIME_8723D		10		//ms
#define	index_mapping_NUM_8723D	15
#define AVG_THERMAL_NUM_8723D	4
#define RF_T_METER_8723D 0x42

void ConfigureTxpowerTrack_8723D(
	PTXPWRTRACK_CFG	pConfig
	);

VOID
GetDeltaSwingTable_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PVOID		pDM_VOID,
#else
	IN PDM_ODM_T		pDM_Odm,
#endif
	OUT pu1Byte 			*TemperatureUP_A,
	OUT pu1Byte 			*TemperatureDOWN_A,
	OUT pu1Byte 			*TemperatureUP_B,
	OUT pu1Byte 			*TemperatureDOWN_B	
	);

VOID
setCCKFilterCoefficient_8723D(
	PDM_ODM_T	pDM_Odm,
	u1Byte 		CCKSwingIndex
);

void DoIQK_8723D(
	PVOID		pDM_VOID,
	u1Byte 		DeltaThermalIndex,
	u1Byte		ThermalValue,	
	u1Byte 		Threshold
	);

VOID
ODM_TxPwrTrackSetPwr_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PVOID		pDM_VOID,
#else
	IN PDM_ODM_T		pDM_Odm,
#endif
	PWRTRACK_METHOD 	Method,
	u1Byte 				RFPath,
	u1Byte 				ChannelMappedIndex
	);

VOID
ODM_TxXtalTrackSetXtal_8723D(
	PVOID		pDM_VOID
);

//1 7.	IQK

void	
PHY_IQCalibrate_8723D(	
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN PADAPTER	Adapter,
#endif
	IN	BOOLEAN 	bReCovery);


//
// LC calibrate
//
void	
PHY_LCCalibrate_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_CE)
	PVOID		pDM_VOID
#else
	IN PDM_ODM_T		pDM_Odm
#endif
);

//
// AP calibrate
//
void	
PHY_APCalibrate_8723D(		
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
							IN 	s1Byte		delta);
void	
PHY_DigitalPredistortion_8723D(		IN	PADAPTER	pAdapter);


VOID
_PHY_SaveADDARegisters_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		ADDAReg,
	IN	pu4Byte		ADDABackup,
	IN	u4Byte		RegisterNum
	);

VOID
_PHY_PathADDAOn_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		ADDAReg,
	IN	BOOLEAN		isPathAOn,
	IN	BOOLEAN		is2T
	);

VOID
_PHY_MACSettingCalibration_8723D(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm,
#else
	IN	PADAPTER	pAdapter,
#endif
	IN	pu4Byte		MACReg,
	IN	pu4Byte		MACBackup	
	);

								
#endif	// #ifndef __HAL_PHY_RF_8723D_H__								

