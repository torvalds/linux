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
 
 #ifndef __HAL_PHY_RF_H__
 #define __HAL_PHY_RF_H__
 
#include "phydm_kfree.h"
#if (RTL8814A_SUPPORT == 1)
#include "rtl8814a/phydm_iqk_8814a.h"
#endif

#if (RTL8822B_SUPPORT == 1)
#include "rtl8822b/phydm_iqk_8822b.h"
#endif
#include "phydm_powertracking_win.h"

typedef enum _SPUR_CAL_METHOD {
	PLL_RESET,
	AFE_PHASE_SEL
} SPUR_CAL_METHOD;

typedef enum _PWRTRACK_CONTROL_METHOD {
	BBSWING,
	TXAGC,
	MIX_MODE,
	TSSI_MODE
} PWRTRACK_METHOD;

typedef VOID 	(*FuncSetPwr)(PDM_ODM_T, PWRTRACK_METHOD, u1Byte, u1Byte);
typedef VOID(*FuncIQK)(PVOID, u1Byte, u1Byte, u1Byte);
typedef VOID 	(*FuncLCK)(PDM_ODM_T);
				//refine by YuChen for 8814A
typedef VOID  	(*FuncSwing)(PDM_ODM_T, pu1Byte*, pu1Byte*, pu1Byte*, pu1Byte*);
typedef VOID	(*FuncSwing8814only)(PDM_ODM_T, pu1Byte*, pu1Byte*, pu1Byte*, pu1Byte*);

typedef struct _TXPWRTRACK_CFG {
	u1Byte 		SwingTableSize_CCK;	
	u1Byte 		SwingTableSize_OFDM;
	u1Byte 		Threshold_IQK;
	u1Byte 		Threshold_DPK;	
	u1Byte 		AverageThermalNum;
	u1Byte 		RfPathCount;
	u4Byte 		ThermalRegAddr;	
	FuncSetPwr 	ODM_TxPwrTrackSetPwr;
	FuncIQK 	DoIQK;
	FuncLCK		PHY_LCCalibrate;
	FuncSwing	GetDeltaSwingTable;
	FuncSwing8814only	GetDeltaSwingTable8814only;
} TXPWRTRACK_CFG, *PTXPWRTRACK_CFG;

VOID 
ConfigureTxpowerTrack(
	IN 	PDM_ODM_T		pDM_Odm,
	OUT	PTXPWRTRACK_CFG	pConfig
	);


VOID
ODM_ClearTxPowerTrackingState(
	IN PDM_ODM_T		pDM_Odm
	);

VOID
ODM_TXPowerTrackingCallback_ThermalMeter(
#if (DM_ODM_SUPPORT_TYPE & ODM_AP)
	IN PDM_ODM_T		pDM_Odm
#else
	IN PADAPTER	Adapter
#endif
	);



#define ODM_TARGET_CHNL_NUM_2G_5G	59


VOID
ODM_ResetIQKResult(
	IN PDM_ODM_T	pDM_Odm 
);
u1Byte 
ODM_GetRightChnlPlaceforIQK(
    IN u1Byte chnl
);

VOID odm_IQCalibrate(IN	PDM_ODM_T	pDM_Odm);
VOID phydm_rf_init(	IN		PDM_ODM_T		pDM_Odm);
VOID phydm_rf_watchdog(	IN		PDM_ODM_T		pDM_Odm);
								
#endif	// #ifndef __HAL_PHY_RF_H__

