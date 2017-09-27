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

#ifndef	__ODM_PRECOMP_H__
#define __ODM_PRECOMP_H__

#include "odm_types.h"

#define		TEST_FALG___		1

/* 2 Config Flags and Structs - defined by each ODM Type */

#include <osdep_service.h>
#include <drv_types.h>
#include <hal_intf.h>
#include <usb_ops_linux.h>

/* 2 OutSrc Header Files */

#include "odm.h"
#include "odm_HWConfig.h"
#include "odm_debug.h"
#include "odm_RegDefine11N.h"

#include "Hal8188ERateAdaptive.h"/* for  RA,Power training */
#include "rtl8188e_hal.h"

#include "odm_reg.h"

#include "odm_RTL8188E.h"

void odm_CmnInfoHook_Debug(struct odm_dm_struct *pDM_Odm);
void odm_CmnInfoInit_Debug(struct odm_dm_struct *pDM_Odm);
void odm_DIGInit(struct odm_dm_struct *pDM_Odm);
void odm_RateAdaptiveMaskInit(struct odm_dm_struct *pDM_Odm);
void odm_DynamicBBPowerSavingInit(struct odm_dm_struct *pDM_Odm);
void odm_DynamicTxPowerInit(struct odm_dm_struct *pDM_Odm);
void odm_TXPowerTrackingInit(struct odm_dm_struct *pDM_Odm);
void ODM_EdcaTurboInit(struct odm_dm_struct *pDM_Odm);
void odm_SwAntDivInit_NIC(struct odm_dm_struct *pDM_Odm);
void odm_CmnInfoUpdate_Debug(struct odm_dm_struct *pDM_Odm);
void odm_CommonInfoSelfUpdate(struct odm_dm_struct *pDM_Odm);
void odm_FalseAlarmCounterStatistics(struct odm_dm_struct *pDM_Odm);
void odm_DIG(struct odm_dm_struct *pDM_Odm);
void odm_CCKPacketDetectionThresh(struct odm_dm_struct *pDM_Odm);
void odm_RefreshRateAdaptiveMaskMP(struct odm_dm_struct *pDM_Odm);
void odm_DynamicBBPowerSaving(struct odm_dm_struct *pDM_Odm);
void odm_SwAntDivChkAntSwitch(struct odm_dm_struct *pDM_Odm, u8 Step);
void odm_EdcaTurboCheck(struct odm_dm_struct *pDM_Odm);
void odm_CommonInfoSelfInit(struct odm_dm_struct *pDM_Odm);
void odm_RSSIMonitorCheck(struct odm_dm_struct *pDM_Odm);
void odm_RefreshRateAdaptiveMask(struct odm_dm_struct *pDM_Odm);
void odm_1R_CCA(struct odm_dm_struct *pDM_Odm);
void odm_RefreshRateAdaptiveMaskCE(struct odm_dm_struct *pDM_Odm);
void odm_RefreshRateAdaptiveMaskAPADSL(struct odm_dm_struct *pDM_Odm);
void odm_DynamicTxPowerNIC(struct odm_dm_struct *pDM_Odm);
void odm_RSSIMonitorCheckCE(struct odm_dm_struct *pDM_Odm);
void odm_TXPowerTrackingThermalMeterInit(struct odm_dm_struct *pDM_Odm);
void odm_EdcaTurboCheckCE(struct odm_dm_struct *pDM_Odm);
void odm_TXPowerTrackingCheckCE(struct odm_dm_struct *pDM_Odm);
void odm_SwAntDivChkAntSwitchCallback(void *FunctionContext);
void odm_InitHybridAntDiv(struct odm_dm_struct *pDM_Odm);
void odm_HwAntDiv(struct odm_dm_struct *pDM_Odm);

#endif	/*  __ODM_PRECOMP_H__ */
