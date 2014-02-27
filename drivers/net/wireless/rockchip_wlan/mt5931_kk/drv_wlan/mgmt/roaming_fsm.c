/*
** $Id:
*/

/*! \file   "roaming_fsm.c"
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
** $Log: roaming_fsm.c $
 *
 * 11 24 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Adjust code for DBG and CONFIG_XLOG.
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 11 02 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * adding the code for XLOG.
 *
 * 08 31 2011 tsaiyuan.hsu
 * [WCXRP00000931] [MT5931 Wi-Fi][DRV/FW] add swcr to disable roaming from driver
 * remove obsolete code.
 *
 * 08 15 2011 tsaiyuan.hsu
 * [WCXRP00000931] [MT5931 Wi-Fi][DRV/FW] add swcr to disable roaming from driver
 * add swcr in driver reg, 0x9fxx0000, to disable roaming .
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

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

#if CFG_SUPPORT_ROAMING
/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

#if DBG
/*lint -save -e64 Type mismatch */
static PUINT_8 apucDebugRoamingState[ROAMING_STATE_NUM] = {
    (PUINT_8)DISP_STRING("ROAMING_STATE_IDLE"),
    (PUINT_8)DISP_STRING("ROAMING_STATE_DECISION"),
    (PUINT_8)DISP_STRING("ROAMING_STATE_DISCOVERY"),
    (PUINT_8)DISP_STRING("ROAMING_STATE_ROAM")
};
/*lint -restore */
#endif /* DBG */

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define ROAMING_ENABLE_CHECK(_roam) \
        { \
            if (!(_roam->fgIsEnableRoaming)) {return;} \
        }

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* @brief Initialize the value in ROAMING_FSM_INFO_T for ROAMING FSM operation
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmInit (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    P_CONNECTION_SETTINGS_T prConnSettings;

    DBGLOG(ROAMING, LOUD, ("->roamingFsmInit(): Current Time = %ld\n", kalGetTimeTick()));

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);
    prConnSettings = &(prAdapter->rWifiVar.rConnSettings);

    //4 <1> Initiate FSM
    prRoamingFsmInfo->fgIsEnableRoaming = prConnSettings->fgIsEnableRoaming;
    prRoamingFsmInfo->eCurrentState = ROAMING_STATE_IDLE;
    prRoamingFsmInfo->rRoamingDiscoveryUpdateTime = 0;

    return;
} /* end of roamingFsmInit() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Uninitialize the value in AIS_FSM_INFO_T for AIS FSM operation
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmUninit (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;

    DBGLOG(ROAMING, LOUD, ("->roamingFsmUninit(): Current Time = %ld\n", kalGetTimeTick()));

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    prRoamingFsmInfo->eCurrentState = ROAMING_STATE_IDLE;

    return;
} /* end of roamingFsmUninit() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Send commands to firmware
*
* @param [IN P_ADAPTER_T]       prAdapter
*        [IN P_ROAMING_PARAM_T] prParam
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmSendCmd (
    IN P_ADAPTER_T prAdapter,
    IN P_ROAMING_PARAM_T prParam
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    WLAN_STATUS rStatus;

    DBGLOG(ROAMING, LOUD, ("->roamingFsmSendCmd(): Current Time = %ld\n", kalGetTimeTick()));

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    rStatus = wlanSendSetQueryCmd (
                prAdapter,                  /* prAdapter */
                CMD_ID_ROAMING_TRANSIT,     /* ucCID */
                TRUE,                       /* fgSetQuery */
                FALSE,                      /* fgNeedResp */
                FALSE,                      /* fgIsOid */
                NULL,                       /* pfCmdDoneHandler */
                NULL,                       /* pfCmdTimeoutHandler */
                sizeof(ROAMING_PARAM_T),    /* u4SetQueryInfoLen */
                (PUINT_8) prParam,          /* pucInfoBuffer */
                NULL,                       /* pvSetQueryBuffer */
                0                           /* u4SetQueryBufferLen */
                );

    ASSERT(rStatus == WLAN_STATUS_PENDING);

    return;
} /* end of roamingFsmSendCmd() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Update the recent time when ScanDone occurred
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmScanResultsUpdate (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    /* Check Roaming Conditions */
    ROAMING_ENABLE_CHECK(prRoamingFsmInfo);

    DBGLOG(ROAMING, LOUD, ("->roamingFsmScanResultsUpdate(): Current Time = %ld\n", kalGetTimeTick()));

    GET_CURRENT_SYSTIME(&prRoamingFsmInfo->rRoamingDiscoveryUpdateTime);

    return;
} /* end of roamingFsmScanResultsUpdate() */

/*----------------------------------------------------------------------------*/
/*!
* @brief The Core FSM engine of ROAMING for AIS Infra.
*
* @param [IN P_ADAPTER_T]          prAdapter
*        [IN ENUM_ROAMING_STATE_T] eNextState Enum value of next AIS STATE
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmSteps (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_ROAMING_STATE_T eNextState
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    ENUM_ROAMING_STATE_T ePreviousState;
    BOOLEAN fgIsTransition = (BOOLEAN)FALSE;

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    do {

        /* Do entering Next State */
#if DBG
        DBGLOG(ROAMING, STATE, ("TRANSITION: [%s] -> [%s]\n",
                            apucDebugRoamingState[prRoamingFsmInfo->eCurrentState],
                            apucDebugRoamingState[eNextState]));
#else
        DBGLOG(ROAMING, STATE, ("[%d] TRANSITION: [%d] -> [%d]\n",
                            DBG_ROAMING_IDX,
                            prRoamingFsmInfo->eCurrentState,
                            eNextState));
#endif
        /* NOTE(Kevin): This is the only place to change the eCurrentState(except initial) */
        ePreviousState = prRoamingFsmInfo->eCurrentState;
        prRoamingFsmInfo->eCurrentState = eNextState;

        fgIsTransition = (BOOLEAN)FALSE;

        /* Do tasks of the State that we just entered */
        switch (prRoamingFsmInfo->eCurrentState) {
        /* NOTE(Kevin): we don't have to rearrange the sequence of following
         * switch case. Instead I would like to use a common lookup table of array
         * of function pointer to speed up state search.
         */
        case ROAMING_STATE_IDLE:
        case ROAMING_STATE_DECISION:
        	  break;

        case ROAMING_STATE_DISCOVERY:
        	  {
        	      OS_SYSTIME rCurrentTime;

        	      GET_CURRENT_SYSTIME(&rCurrentTime);
                if (CHECK_FOR_TIMEOUT(rCurrentTime, prRoamingFsmInfo->rRoamingDiscoveryUpdateTime,
                                      SEC_TO_SYSTIME(ROAMING_DISCOVERY_TIMEOUT_SEC))) {
                    DBGLOG(ROAMING, LOUD, ("roamingFsmSteps: DiscoveryUpdateTime Timeout"));
                    aisFsmRunEventRoamingDiscovery(prAdapter, TRUE);
                }
                else {
                	  DBGLOG(ROAMING, LOUD, ("roamingFsmSteps: DiscoveryUpdateTime Updated"));
                	  aisFsmRunEventRoamingDiscovery(prAdapter, FALSE);
                }
            }
        	  break;

        case ROAMING_STATE_ROAM:
        	  break;

        default:
            ASSERT(0); /* Make sure we have handle all STATEs */
        }
    }
    while (fgIsTransition);

    return;

} /* end of roamingFsmSteps() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Transit to Decision state after join completion
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmRunEventStart (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    ENUM_ROAMING_STATE_T eNextState;
    P_BSS_INFO_T prAisBssInfo;
    ROAMING_PARAM_T rParam;

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    /* Check Roaming Conditions */
    ROAMING_ENABLE_CHECK(prRoamingFsmInfo);

    prAisBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
    if (prAisBssInfo->eCurrentOPMode != OP_MODE_INFRASTRUCTURE) {
        return;
    }

    DBGLOG(ROAMING, EVENT, ("EVENT-ROAMING START: Current Time = %ld\n", kalGetTimeTick()));

    /* IDLE, ROAM -> DECISION */
    /* Errors as DECISION, DISCOVERY -> DECISION */
    if (!(prRoamingFsmInfo->eCurrentState == ROAMING_STATE_IDLE
    	  || prRoamingFsmInfo->eCurrentState == ROAMING_STATE_ROAM)) {
        return;
    }

    eNextState = ROAMING_STATE_DECISION;
    if (eNextState != prRoamingFsmInfo->eCurrentState) {
    	  rParam.u2Event = ROAMING_EVENT_START;
    	  roamingFsmSendCmd(prAdapter, (P_ROAMING_PARAM_T) &rParam);

    	  /* Step to next state */
        roamingFsmSteps(prAdapter, eNextState);
    }

    return;
} /* end of roamingFsmRunEventStart() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Transit to Discovery state when deciding to find a candidate
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmRunEventDiscovery (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Param
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    ENUM_ROAMING_STATE_T eNextState;

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    /* Check Roaming Conditions */
    ROAMING_ENABLE_CHECK(prRoamingFsmInfo);

    DBGLOG(ROAMING, EVENT, ("EVENT-ROAMING DISCOVERY: Current Time = %ld\n", kalGetTimeTick()));

    /* DECISION -> DISCOVERY */
    /* Errors as IDLE, DISCOVERY, ROAM -> DISCOVERY */
    if (prRoamingFsmInfo->eCurrentState != ROAMING_STATE_DECISION) {
        return;
    }

    eNextState = ROAMING_STATE_DISCOVERY;
    /* DECISION -> DISCOVERY */
    if (eNextState != prRoamingFsmInfo->eCurrentState) {
        P_BSS_INFO_T prAisBssInfo;
        P_BSS_DESC_T prBssDesc;

        // sync. rcpi with firmware
        prAisBssInfo = &(prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX]);
        prBssDesc = scanSearchBssDescByBssid(prAdapter, prAisBssInfo->aucBSSID);
        if (prBssDesc) {
        	  prBssDesc->ucRCPI = (UINT_8)(u4Param&0xff);
        }

        roamingFsmSteps(prAdapter, eNextState);
    }

    return;
} /* end of roamingFsmRunEventDiscovery() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Transit to Roam state after Scan Done
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmRunEventRoam (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    ENUM_ROAMING_STATE_T eNextState;
    ROAMING_PARAM_T rParam;

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    /* Check Roaming Conditions */
    ROAMING_ENABLE_CHECK(prRoamingFsmInfo);

    DBGLOG(ROAMING, EVENT, ("EVENT-ROAMING ROAM: Current Time = %ld\n", kalGetTimeTick()));

    /* IDLE, ROAM -> DECISION */
    /* Errors as IDLE, DECISION, ROAM -> ROAM */
    if (prRoamingFsmInfo->eCurrentState != ROAMING_STATE_DISCOVERY) {
        return;
    }

    eNextState = ROAMING_STATE_ROAM;
    /* DISCOVERY -> ROAM */
    if (eNextState != prRoamingFsmInfo->eCurrentState) {
    	  rParam.u2Event = ROAMING_EVENT_ROAM;
    	  roamingFsmSendCmd(prAdapter, (P_ROAMING_PARAM_T) &rParam);

    	  /* Step to next state */
        roamingFsmSteps(prAdapter, eNextState);
    }

    return;
} /* end of roamingFsmRunEventRoam() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Transit to Decision state as being failed to find out any candidate
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmRunEventFail (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Param
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    ENUM_ROAMING_STATE_T eNextState;
    ROAMING_PARAM_T rParam;

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    /* Check Roaming Conditions */
    ROAMING_ENABLE_CHECK(prRoamingFsmInfo);

    DBGLOG(ROAMING, EVENT, ("EVENT-ROAMING FAIL: reason %x Current Time = %ld\n", u4Param, kalGetTimeTick()));

    /* IDLE, ROAM -> DECISION */
    /* Errors as IDLE, DECISION, DISCOVERY -> DECISION */
    if (prRoamingFsmInfo->eCurrentState != ROAMING_STATE_ROAM) {
        return;
    }

    eNextState = ROAMING_STATE_DECISION;
    /* ROAM -> DECISION */
    if (eNextState != prRoamingFsmInfo->eCurrentState) {
    	  rParam.u2Event = ROAMING_EVENT_FAIL;
    	  rParam.u2Data = (UINT_16)(u4Param&0xffff);
    	  roamingFsmSendCmd(prAdapter, (P_ROAMING_PARAM_T) &rParam);

    	  /* Step to next state */
        roamingFsmSteps(prAdapter, eNextState);
    }

    return;
} /* end of roamingFsmRunEventFail() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Transit to Idle state as beging aborted by other moduels, AIS
*
* @param [IN P_ADAPTER_T] prAdapter
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
roamingFsmRunEventAbort (
    IN P_ADAPTER_T prAdapter
    )
{
    P_ROAMING_INFO_T prRoamingFsmInfo;
    ENUM_ROAMING_STATE_T eNextState;
    ROAMING_PARAM_T rParam;

    prRoamingFsmInfo = (P_ROAMING_INFO_T)&(prAdapter->rWifiVar.rRoamingInfo);

    ROAMING_ENABLE_CHECK(prRoamingFsmInfo);

    DBGLOG(ROAMING, EVENT, ("EVENT-ROAMING ABORT: Current Time = %ld\n", kalGetTimeTick()));

    eNextState = ROAMING_STATE_IDLE;
    /* IDLE, DECISION, DISCOVERY, ROAM -> IDLE */
    if (eNextState != prRoamingFsmInfo->eCurrentState) {
    	  rParam.u2Event = ROAMING_EVENT_ABORT;
    	  roamingFsmSendCmd(prAdapter, (P_ROAMING_PARAM_T) &rParam);

        /* Step to next state */
        roamingFsmSteps(prAdapter, eNextState);
    }

    return;
} /* end of roamingFsmRunEventAbort() */

/*----------------------------------------------------------------------------*/
/*!
* @brief Process events from firmware
*
* @param [IN P_ADAPTER_T]       prAdapter
*        [IN P_ROAMING_PARAM_T] prParam
*
* @return none
*/
/*----------------------------------------------------------------------------*/
WLAN_STATUS
roamingFsmProcessEvent (
    IN P_ADAPTER_T prAdapter,
    IN P_ROAMING_PARAM_T prParam
    )
{
    DBGLOG(ROAMING, LOUD, ("ROAMING Process Events: Current Time = %ld\n", kalGetTimeTick()));

    if (ROAMING_EVENT_DISCOVERY == prParam->u2Event) {
        roamingFsmRunEventDiscovery(prAdapter, prParam->u2Data);
    }

    return WLAN_STATUS_SUCCESS;
}

#endif
