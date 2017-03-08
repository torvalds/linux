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
 
#ifndef	__PHYDMDYNAMICTXPOWER_H__
#define    __PHYDMDYNAMICTXPOWER_H__

/*#define DYNAMIC_TXPWR_VERSION	"1.0"*/
/*#define DYNAMIC_TXPWR_VERSION	"1.3" */ /*2015.08.26, Add 8814 Dynamic TX power*/
#define DYNAMIC_TXPWR_VERSION	"1.4" /*2015.11.06, Add CE 8821A Dynamic TX power*/

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#define		TX_POWER_NEAR_FIELD_THRESH_LVL2	74
	#define		TX_POWER_NEAR_FIELD_THRESH_LVL1	60
	#define		TX_POWER_NEAR_FIELD_THRESH_AP	0x3F
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define		TX_POWER_NEAR_FIELD_THRESH_LVL2	74
	#define		TX_POWER_NEAR_FIELD_THRESH_LVL1	67
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#define		TX_POWER_NEAR_FIELD_THRESH_LVL2	74
	#define		TX_POWER_NEAR_FIELD_THRESH_LVL1	60
#endif

#define		TxHighPwrLevel_Normal		0	
#define		TxHighPwrLevel_Level1		1
#define		TxHighPwrLevel_Level2		2

#define		TxHighPwrLevel_BT1			3
#define		TxHighPwrLevel_BT2			4
#define		TxHighPwrLevel_15			5
#define		TxHighPwrLevel_35			6
#define		TxHighPwrLevel_50			7
#define		TxHighPwrLevel_70			8
#define		TxHighPwrLevel_100			9

VOID 
odm_DynamicTxPowerInit(
	IN		PVOID					pDM_VOID
	);

VOID
odm_DynamicTxPowerRestorePowerIndex(
	IN		PVOID					pDM_VOID
	);

VOID 
odm_DynamicTxPowerNIC(
	IN		PVOID					pDM_VOID
	);

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
VOID
odm_DynamicTxPowerSavePowerIndex(
	IN		PVOID					pDM_VOID
	);

VOID
odm_DynamicTxPowerWritePowerIndex(
	IN		PVOID					pDM_VOID, 
	IN 	u1Byte		Value);

VOID 
odm_DynamicTxPower_8821(
	IN		PVOID					pDM_VOID,	
	IN		pu1Byte					pDesc,
	IN		u1Byte					macId
	);
#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
VOID 
odm_DynamicTxPower_8814A(
	IN		PVOID					pDM_VOID
	);

	
VOID
odm_SetTxPowerLevel8814(
	IN	PADAPTER		Adapter,
	IN	u1Byte			Channel,
	IN	u1Byte			PwrLvl
	);
#endif
#endif

VOID 
odm_DynamicTxPower(
	IN		PVOID					pDM_VOID
	);

VOID 
odm_DynamicTxPowerAP(
	IN		PVOID					pDM_VOID
	);

#endif
