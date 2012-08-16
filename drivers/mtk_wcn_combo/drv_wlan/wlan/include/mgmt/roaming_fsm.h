/*
** $Id:
*/

/*! \file   "roaming_fsm.h"
    \brief  This file defines the FSM for Roaming MODULE.

    This file defines the FSM for Roaming MODULE.
*/

/*******************************************************************************
* Copyright (c) 2009 MediaTek Inc.
*
* All rights reserved. Copying, compilation, modification, distribution
* or any other use whatsoever of this material is strictly prohibited
* except in accordance with a Software License Agreement with
* MediaTek Inc.
********************************************************************************
*/

/*******************************************************************************
* LEGAL DISCLAIMER
*
* BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND
* AGREES THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK
* SOFTWARE") RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE
* PROVIDED TO BUYER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY
* DISCLAIMS ANY AND ALL WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT
* LIMITED TO THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
* PARTICULAR PURPOSE OR NONINFRINGEMENT. NEITHER DOES MEDIATEK PROVIDE
* ANY WARRANTY WHATSOEVER WITH RESPECT TO THE SOFTWARE OF ANY THIRD PARTY
* WHICH MAY BE USED BY, INCORPORATED IN, OR SUPPLIED WITH THE MEDIATEK
* SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH THIRD PARTY FOR ANY
* WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE
* FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S SPECIFICATION OR TO
* CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
* BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
* LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL
* BE, AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT
* ISSUE, OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY
* BUYER TO MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
* THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
* WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT
* OF LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING
* THEREOF AND RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN
* FRANCISCO, CA, UNDER THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE
* (ICC).
********************************************************************************
*/

/*
** $Log: roaming_fsm.h $
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 16 2011 tsaiyuan.hsu
 * [WCXRP00000517] [MT6620 Wi-Fi][Driver][FW] Fine Tune Performance of Roaming
 * remove obsolete definition and unused variables.
 *
 * 02 26 2011 tsaiyuan.hsu
 * [WCXRP00000391] [MT6620 Wi-Fi][FW] Add Roaming Support
 * not send disassoc or deauth to leaving AP so as to improve performace of roaming.
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
*/

#ifndef _ROAMING_FSM_H
#define _ROAMING_FSM_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* Roaming Discovery interval, SCAN result need to be updated */
#define ROAMING_DISCOVERY_TIMEOUT_SEC               5 // Seconds.

//#define ROAMING_NO_SWING_RCPI_STEP                  5 //rcpi
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_ROAMING_FAIL_REASON_T {
    ROAMING_FAIL_REASON_CONNLIMIT = 0,
    ROAMING_FAIL_REASON_NOCANDIDATE,
    ROAMING_FAIL_REASON_NUM
} ENUM_ROAMING_FAIL_REASON_T;

/* events of roaming between driver and firmware */
typedef enum _ENUM_ROAMING_EVENT_T {
    ROAMING_EVENT_START = 0,
    ROAMING_EVENT_DISCOVERY,
    ROAMING_EVENT_ROAM,
    ROAMING_EVENT_FAIL,
    ROAMING_EVENT_ABORT,    
    ROAMING_EVENT_NUM
} ENUM_ROAMING_EVENT_T;

typedef struct _ROAMING_PARAM_T {
    UINT_16     u2Event;
    UINT_16     u2Data;
} ROAMING_PARAM_T, *P_ROAMING_PARAM_T;

/**/
typedef enum _ENUM_ROAMING_STATE_T {
    ROAMING_STATE_IDLE = 0,
    ROAMING_STATE_DECISION,
    ROAMING_STATE_DISCOVERY,
    ROAMING_STATE_ROAM,
    ROAMING_STATE_NUM
} ENUM_ROAMING_STATE_T;

typedef struct _ROAMING_INFO_T {	  
	  BOOLEAN                 fgIsEnableRoaming;
	  
    ENUM_ROAMING_STATE_T    eCurrentState;

    OS_SYSTIME              rRoamingDiscoveryUpdateTime;

} ROAMING_INFO_T, *P_ROAMING_INFO_T;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#if CFG_SUPPORT_ROAMING
#define IS_ROAMING_ACTIVE(prAdapter) \
        (prAdapter->rWifiVar.rRoamingInfo.eCurrentState == ROAMING_STATE_ROAM)
#else
#define IS_ROAMING_ACTIVE(prAdapter) FALSE
#endif /* CFG_SUPPORT_ROAMING */

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
VOID
roamingFsmInit (
    IN P_ADAPTER_T prAdapter
    );

VOID
roamingFsmUninit (
    IN P_ADAPTER_T prAdapter
    );

VOID
roamingFsmSendCmd (
    IN P_ADAPTER_T prAdapter,
    IN P_ROAMING_PARAM_T prParam
    );

VOID
roamingFsmScanResultsUpdate (
    IN P_ADAPTER_T prAdapter
    );
    
VOID
roamingFsmSteps (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_ROAMING_STATE_T eNextState
    );

VOID
roamingFsmRunEventStart (
    IN P_ADAPTER_T prAdapter
    );

VOID
roamingFsmRunEventDiscovery (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Param
    );    

VOID
roamingFsmRunEventRoam (
    IN P_ADAPTER_T prAdapter
    );

VOID
roamingFsmRunEventFail (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Reason
    );

VOID
roamingFsmRunEventAbort (
    IN P_ADAPTER_T prAdapter
    );
        
WLAN_STATUS
roamingFsmProcessEvent (
    IN P_ADAPTER_T prAdapter,
    IN P_ROAMING_PARAM_T prParam
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _ROAMING_FSM_H */




