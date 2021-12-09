/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. i*/

#ifndef	__ODM_PRECOMP_H__
#define __ODM_PRECOMP_H__

#include "odm_types.h"

#define		TEST_FALG___		1

/* 2 Config Flags and Structs - defined by each ODM Type */

#include "osdep_service.h"
#include "drv_types.h"
#include "hal_intf.h"

/* 2 OutSrc Header Files */

#include "odm.h"
#include "odm_HWConfig.h"
#include "odm_RegDefine11N.h"

#include "HalPhyRf_8188e.h"/* for IQK,LCK,Power-tracking */
#include "Hal8188ERateAdaptive.h"/* for  RA,Power training */
#include "rtl8188e_hal.h"

#include "odm_interface.h"

#include "HalHWImg8188E_MAC.h"
#include "HalHWImg8188E_RF.h"
#include "HalHWImg8188E_BB.h"

#include "odm_RegConfig8188E.h"
#include "odm_RTL8188E.h"

void odm_DIGInit(struct odm_dm_struct *pDM_Odm);
void odm_RateAdaptiveMaskInit(struct odm_dm_struct *pDM_Odm);
void odm_DynamicBBPowerSavingInit(struct odm_dm_struct *pDM_Odm);
void odm_TXPowerTrackingInit(struct odm_dm_struct *pDM_Odm);
void ODM_EdcaTurboInit(struct odm_dm_struct *pDM_Odm);
void odm_SwAntDivInit_NIC(struct odm_dm_struct *pDM_Odm);
void odm_CommonInfoSelfUpdate(struct odm_dm_struct *pDM_Odm);
void odm_FalseAlarmCounterStatistics(struct odm_dm_struct *pDM_Odm);
void odm_DIG(struct odm_dm_struct *pDM_Odm);
void odm_CCKPacketDetectionThresh(struct odm_dm_struct *pDM_Odm);
void odm_EdcaTurboCheck(struct odm_dm_struct *pDM_Odm);
void odm_CommonInfoSelfInit(struct odm_dm_struct *pDM_Odm);
void odm_RSSIMonitorCheck(struct odm_dm_struct *pDM_Odm);
void odm_RefreshRateAdaptiveMask(struct odm_dm_struct *pDM_Odm);
void odm_TXPowerTrackingThermalMeterInit(struct odm_dm_struct *pDM_Odm);
void odm_InitHybridAntDiv(struct odm_dm_struct *pDM_Odm);
void odm_HwAntDiv(struct odm_dm_struct *pDM_Odm);

#endif	/*  __ODM_PRECOMP_H__ */
