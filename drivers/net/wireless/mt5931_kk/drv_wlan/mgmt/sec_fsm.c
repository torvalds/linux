/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/mgmt/sec_fsm.c#1 $
*/

/*! \file   "sec_fsm.c"
    \brief  This is the file implement security check state machine.

    In security module, do the port control check after success join to an AP,
    and the path to NORMAL TR, the state machine handle these state transition.
*/

/*******************************************************************************
* Copyright (c) 2007 MediaTek Inc.
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
** $Log: sec_fsm.c $
 *
 * 11 24 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Adjust code for DBG and CONFIG_XLOG.
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 11 10 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * change the debug module level.
 *
 * 11 02 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * adding the code for XLOG.
 *
 * 03 29 2011 wh.su
 * [WCXRP00000248] [MT6620 Wi-Fi][FW]Fixed the Klockwork error
 * fixed the kclocwork error.
 *
 * 01 26 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * .
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Fix Compile Error when DBG is disabled.
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Change Station Type in Station Record, Modify MACRO definition for getting station type & network type index & Role.
 *
 * 09 29 2010 wh.su
 * [WCXRP00000072] [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue
 * [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue.
 *
 * 09 24 2010 wh.su
 * NULL
 * [WCXRP00005002][MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 20 2010 wh.su
 * NULL
 * adding the eapol callback setting.
 *
 * 08 19 2010 wh.su
 * NULL
 * adding the tx pkt call back handle for countermeasure.
 *
 * 07 19 2010 wh.su
 *
 * fixed the compilng error at debug mode.
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 21 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * modify some code for concurrent network.
 *
 * 06 19 2010 wh.su
 * [WPD00003840][MT6620 5931] Security migration
 * consdier the concurrent network setting.
 *
 * 05 28 2010 wh.su
 * [BORA00000626][MT6620] Refine the remove key flow for WHQL testing
 * fixed the ad-hoc wpa-none send non-encrypted frame issue.
 *
 * 05 24 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Refine authSendAuthFrame() for NULL STA_RECORD_T case and minimum deauth interval.
 *
 * 04 24 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * g_aprBssInfo[] depends on CFG_SUPPORT_P2P and CFG_SUPPORT_BOW
 *
 * 04 13 2010 wh.su
 * [BORA00000680][MT6620] Support the statistic for Microsoft os query
 * fixed the Klocwork error and refine the class error message.
 *
 * 03 03 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * move the AIS specific variable for security to AIS specific structure.
 *
 * 03 03 2010 wh.su
 * [BORA00000637][MT6620 Wi-Fi] [Bug] WPA2 pre-authentication timer not correctly initialize
 * Fixed the pre-authentication timer not correctly init issue, and modify the security related callback function prototype.
 *
 * 03 01 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration
 * Refine the variable and parameter for security.
 *
 * 01 27 2010 wh.su
 * [BORA00000476][Wi-Fi][firmware] Add the security module initialize code
 * add and fixed some security function.
 *
 * 01 13 2010 wh.su
 * [BORA00000476][Wi-Fi][firmware] Add the security module initialize code
 * fixed the compiling warning
 *
 * 12 18 2009 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * .
 *
 * Dec 7 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * refine some code
 *
 * Dec 4 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * refine the code
 *
 * Dec 1 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * code refine
 *
 * Nov 23 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adjust the function name
 *
 * Nov 19 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adjust the state machine, to meet the firmware security design v1.1
 *
 * Nov 18 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 *
**
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

#if CFG_RSN_MIGRATION

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
static PUINT_8 apucDebugSecState[SEC_STATE_NUM] = {
    (PUINT_8)DISP_STRING("SEC_STATE_INIT"),
    (PUINT_8)DISP_STRING("SEC_STATE_INITIATOR_PORT_BLOCKED"),
    (PUINT_8)DISP_STRING("SEC_STATE_RESPONDER_PORT_BLOCKED"),
    (PUINT_8)DISP_STRING("SEC_STATE_CHECK_OK"),
    (PUINT_8)DISP_STRING("SEC_STATE_SEND_EAPOL"),
    (PUINT_8)DISP_STRING("SEC_STATE_SEND_DEAUTH"),
    (PUINT_8)DISP_STRING("SEC_STATE_COUNTERMEASURE"),
};
/*lint -restore */
#endif /* DBG */

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

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
* \brief This function will do initialization of Security FSM and all variables in
*        SEC_INFO_T.
*
* \param[in] prSta            Pointer to the STA record
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmInit (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T            prSecInfo;
    P_AIS_SPECIFIC_BSS_INFO_T prAisSpecBssInfo;

    ASSERT(prSta);

    prSecInfo = &prSta->rSecInfo;

    #if 1 /* MT6620 */
    //At MT5921, is ok, but at MT6620, firmware base ASIC, the firmware
    //will lost these data, thus, driver have to keep the wep material and
    //setting to firmware while awake from D3.
    #endif

    prSecInfo->eCurrentState = SEC_STATE_INIT;

    prSecInfo->fg2nd1xSend = FALSE;
    prSecInfo->fgKeyStored = FALSE;

    if (IS_STA_IN_AIS(prSta)) {
        prAisSpecBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;

        prAisSpecBssInfo->u4RsnaLastMICFailTime = 0;
        prAisSpecBssInfo->fgCheckEAPoLTxDone = FALSE;

        cnmTimerInitTimer(prAdapter,
                       &prAdapter->rWifiVar.rAisSpecificBssInfo.rRsnaEAPoLReportTimeoutTimer,
                       (PFN_MGMT_TIMEOUT_FUNC)secFsmEventEapolTxTimeout,
                       (UINT_32)prSta);

        cnmTimerInitTimer(prAdapter,
                       &prAdapter->rWifiVar.rAisSpecificBssInfo.rRsnaBlockTrafficTimer,
                       (PFN_MGMT_TIMEOUT_FUNC)secFsmEventEndOfCounterMeasure,
                       (UINT_32)prSta);

    }
    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do uninitialization of Security FSM and all variables in
*        SEC_INFO_T.
*
* \param[in] prSta            Pointer to the STA record
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID /* whsu:Todo: */
secFsmUnInit (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T prSecInfo;

    ASSERT(prSta);

    prSecInfo = &prSta->rSecInfo;

    prSecInfo->fg2nd1xSend = FALSE;
    prSecInfo->fgKeyStored = FALSE;

    //nicPrivacyRemoveWlanTable(prSta->ucWTEntry);

    if (IS_STA_IN_AIS(prSta)) {
        cnmTimerStopTimer(prAdapter,
                       &prAdapter->rWifiVar.rAisSpecificBssInfo.rRsnaEAPoLReportTimeoutTimer);
        cnmTimerStopTimer(prAdapter,
                       &prAdapter->rWifiVar.rAisSpecificBssInfo.rRsnaBlockTrafficTimer);
    }

}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        STANDBY to CHECK_OK.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return - none
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_INIT_to_CHECK_OK (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    secSetPortBlocked(prAdapter, prSta, FALSE);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        INIT to INITIATOR_PORT_BLOCKED.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return - none
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_INIT_to_INITIATOR_PORT_BLOCKED (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        INIT to RESPONDER_PORT_BLOCKED.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return - none
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_INIT_to_RESPONDER_PORT_BLOCKED (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        INITIATOR_PORT_BLOCKED to CHECK_OK.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return - none
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_INITIATOR_PORT_BLOCKED_to_CHECK_OK (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    secSetPortBlocked(prAdapter, prSta, FALSE);
    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        RESPONDER_PORT_BLOCKED to CHECK_OK.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return - none
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_RESPONDER_PORT_BLOCKED_to_CHECK_OK (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    secSetPortBlocked(prAdapter, prSta, FALSE);
    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        CHECK_OK to SEND_EAPOL
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_CHECK_OK_to_SEND_EAPOL (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{

    P_AIS_SPECIFIC_BSS_INFO_T prAisBssInfo;

    ASSERT(prAdapter);

    ASSERT(prSta);

    prAisBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;

    ASSERT(prAisBssInfo);

    if (!IS_STA_IN_AIS(prSta)) {
        DBGLOG(RSN, INFO, ("Counter Measure should occur at AIS network!!\n"));
        //ASSERT(0);
        return;
    }

    prAisBssInfo->fgCheckEAPoLTxDone = TRUE;

    //cnmTimerStartTimer(prAdapter,
    //              &prAisBssInfo->rRsnaEAPoLReportTimeoutTimer,
    //              SEC_TO_MSEC(EAPOL_REPORT_SEND_TIMEOUT_INTERVAL_SEC));

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        SEND_EAPOL to SEND_DEAUTH.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return - none
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_SEND_EAPOL_to_SEND_DEAUTH (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{

    if (!IS_STA_IN_AIS(prSta)) {
        DBGLOG(RSN, INFO, ("Counter Measure should occur at AIS network!!\n"));
        //ASSERT(0);
        return;
    }

    /* Compose deauth frame to AP, a call back function for tx done */
    if (authSendDeauthFrame(prAdapter,
                            prSta,
                            (P_SW_RFB_T)NULL,
                            REASON_CODE_MIC_FAILURE,
                            (PFN_TX_DONE_HANDLER)secFsmEventDeauthTxDone) != WLAN_STATUS_SUCCESS) {
        ASSERT(FALSE);
    }
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        SEND_DEAUTH to COUNTERMEASURE.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_SEND_DEAUTH_to_COUNTERMEASURE (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    ASSERT(prAdapter);
    ASSERT(prSta);

    if (!IS_STA_IN_AIS(prSta)) {
        DBGLOG(RSN, INFO, ("Counter Measure should occur at AIS network!!\n"));
        //ASSERT(0);
        return;
    }

    //Start the 60 sec timer
    cnmTimerStartTimer(prAdapter,
                &prAdapter->rWifiVar.rAisSpecificBssInfo.rRsnaBlockTrafficTimer,
                SEC_TO_MSEC(COUNTER_MEASURE_TIMEOUT_INTERVAL_SEC));
    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do action part while in STATE transition of
*        SEND_DEAUTH to COUNTERMEASURE.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
__KAL_INLINE__ VOID
secFsmTrans_COUNTERMEASURE_to_INIT (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{

    //Clear the counter measure flag
    return;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief The Core FSM engine of security module.
*
* \param[in] prSta            Pointer to the Sta record
* \param[in] eNextState    Enum value of next sec STATE
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmSteps (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta,
    IN ENUM_SEC_STATE_T     eNextState
    )
{
    P_SEC_INFO_T            prSecInfo;
    BOOLEAN                 fgIsTransition = (BOOLEAN)FALSE;

    ASSERT(prSta);

    prSecInfo = &prSta->rSecInfo;
    ASSERT(prSecInfo);

    DEBUGFUNC("secFsmSteps");
    do {
        /* Do entering Next State */
        prSecInfo->ePreviousState = prSecInfo->eCurrentState;

        /* Do entering Next State */
#if DBG
        DBGLOG(RSN, STATE, ("\n"MACSTR" TRANSITION: [%s] -> [%s]\n\n",
                            MAC2STR(prSta->aucMacAddr),
                            apucDebugSecState[prSecInfo->eCurrentState],
                            apucDebugSecState[eNextState]));
#else
        DBGLOG(RSN, STATE, ("\n"MACSTR" [%d] TRANSITION: [%d] -> [%d]\n\n",
                            MAC2STR(prSta->aucMacAddr),
                            DBG_RSN_IDX,
                            prSecInfo->eCurrentState,
                            eNextState));
#endif
        prSecInfo->eCurrentState = eNextState;

        fgIsTransition = (BOOLEAN)FALSE;
#if 0
        /* Do tasks of the State that we just entered */
        switch (prSecInfo->eCurrentState) {
        case SEC_STATE_INIT:
        break;
        case SEC_STATE_INITIATOR_PORT_BLOCKED:
        break;
        case SEC_STATE_RESPONDER_PORT_BLOCKED:
        break;
        case SEC_STATE_CHECK_OK:
        break;
        case SEC_STATE_SEND_EAPOL:
        break;
        case SEC_STATE_SEND_DEAUTH:
        break;
        case SEC_STATE_COUNTERMEASURE:
        break;
        default:
            ASSERT(0); /* Make sure we have handle all STATEs */
        break;
        }
#endif
    }
    while (fgIsTransition);

    return;

}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will do initialization of Security FSM and all variables in
*        SEC_INFO_T.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEventStart (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T            prSecInfo;
    BOOLEAN                 fgIsTransition = (BOOLEAN)FALSE;
    ENUM_SEC_STATE_T        eNextState;

    DBGLOG(RSN, TRACE, ("secFsmRunEventStart\n"));

    ASSERT(prSta);

    if (!prSta)
        return;

    if (!IS_STA_IN_AIS(prSta))
        return;

    DBGLOG(RSN, TRACE, ("secFsmRunEventStart for sta "MACSTR" network %d\n",
        MAC2STR(prSta->aucMacAddr), prSta->ucNetTypeIndex));

    prSecInfo = (P_SEC_INFO_T)&prSta->rSecInfo;

    eNextState = prSecInfo->eCurrentState;

    secSetPortBlocked(prAdapter, prSta, TRUE);

    //prSta->fgTransmitKeyExist = FALSE;
    //whsu:: nicPrivacySetStaDefaultWTIdx(prSta);

#if 1 /* Since the 1x and key can set to firmware in order, always enter the check ok state */
    SEC_STATE_TRANSITION(prAdapter, prSta, INIT, CHECK_OK);
#else
    if (IS_STA_IN_AIS(prSta->eStaType)) {
        if (secRsnKeyHandshakeEnabled(prAdapter) == TRUE
#if CFG_SUPPORT_WAPI
            || (prAdapter->rWifiVar.rConnSettings.fgWapiMode)
#endif
            ) {
            prSta->fgTransmitKeyExist = FALSE;
            //nicPrivacyInitialize(prSta->ucNetTypeIndex);
            SEC_STATE_TRANSITION(prAdapter, prSta, INIT, INITIATOR_PORT_BLOCKED);
        }
        else {
            SEC_STATE_TRANSITION(prAdapter, prSta, INIT, CHECK_OK);
        }
    }
#if CFG_ENABLE_WIFI_DIRECT || CFG_ENABLE_BT_OVER_WIFI
    #if CFG_ENABLE_WIFI_DIRECT && CFG_ENABLE_BT_OVER_WIFI
    else if ((prSta->eStaType == STA_TYPE_BOW_CLIENT) ||
        (prSta->eStaType == STA_TYPE_P2P_GC))
    #elif CFG_ENABLE_WIFI_DIRECT
    else if (prSta->eStaType == STA_TYPE_P2P_GC)
    #elif CFG_ENABLE_BT_OVER_WIFI
    else if (prSta->eStaType == STA_TYPE_BOW_CLIENT)
    #endif
    {
        SEC_STATE_TRANSITION(prAdapter, prSta, INIT, RESPONDER_PORT_BLOCKED);
    }
#endif
    else {
        SEC_STATE_TRANSITION(prAdapter, prSta, INIT, INITIATOR_PORT_BLOCKED);
    }
#endif
    if (prSecInfo->eCurrentState != eNextState) {
        secFsmSteps(prAdapter, prSta, eNextState);
    }

    return;
} /* secFsmRunEventStart */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function called by reset procedure to force the sec fsm enter
*        idle state
*
* \param[in] ucNetTypeIdx  The Specific Network type index
* \param[in] prSta         Pointer to the Sta record
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEventAbort (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T            prSecInfo;

    DBGLOG(RSN, TRACE, ("secFsmEventAbort for sta "MACSTR" network %d\n",
        MAC2STR(prSta->aucMacAddr), prSta->ucNetTypeIndex));

    ASSERT(prSta);

    if (!prSta)
        return;

    if (!IS_STA_IN_AIS(prSta))
        return;

    prSecInfo = (P_SEC_INFO_T)&prSta->rSecInfo;

    prSta->fgTransmitKeyExist = FALSE;

    secSetPortBlocked(prAdapter, prSta, TRUE);

    if (prSecInfo == NULL)
        return;

    if (IS_STA_IN_AIS(prSta)) {

        prAdapter->rWifiVar.rAisSpecificBssInfo.fgTransmitKeyExist = FALSE;

        if (prSecInfo->eCurrentState == SEC_STATE_SEND_EAPOL) {
            if (prAdapter->rWifiVar.rAisSpecificBssInfo.fgCheckEAPoLTxDone == FALSE) {
                DBGLOG(RSN, TRACE, ("EAPOL STATE not match the flag\n"));
                //cnmTimerStopTimer(prAdapter, &prAdapter->rWifiVar.rAisSpecificBssInfo.rRsnaEAPoLReportTimeoutTimer);
            }
        }
    }
    prSecInfo->eCurrentState = SEC_STATE_INIT;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will indicate an Event of "2nd EAPoL Tx is sending" to Sec FSM.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEvent2ndEapolTx (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T            prSecInfo;
    ENUM_SEC_STATE_T        eNextState;
    //BOOLEAN                 fgIsTransition = (BOOLEAN)FALSE;

    DEBUGFUNC("secFsmRunEvent2ndEapolTx");

    ASSERT(prSta);

    prSecInfo = &prSta->rSecInfo;
    eNextState = prSecInfo->eCurrentState;

#if DBG
    DBGLOG(RSN, TRACE, (MACSTR" Sec state %s\n", MAC2STR(prSta->aucMacAddr),
        apucDebugSecState[prSecInfo->eCurrentState]));
#else
    DBGLOG(RSN, TRACE, (MACSTR" Sec state [%d]\n", MAC2STR(prSta->aucMacAddr), prSecInfo->eCurrentState));
#endif

    switch(prSecInfo->eCurrentState) {
    case SEC_STATE_INITIATOR_PORT_BLOCKED :
    case SEC_STATE_CHECK_OK :
        prSecInfo->fg2nd1xSend = TRUE;
        break;
    default:
#if DBG
        DBGLOG(RSN, WARN, ("Rcv 2nd EAPoL at %s\n", apucDebugSecState[prSecInfo->eCurrentState]));
#else
        DBGLOG(RSN, WARN, ("Rcv 2nd EAPoL at [%d]\n", prSecInfo->eCurrentState));
#endif
        break;
    }

    if (prSecInfo->eCurrentState != eNextState) {
        secFsmSteps(prAdapter, prSta, eNextState);
    }

    return;

}/* secFsmRunEvent2ndEapolTx */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will indicate an Event of "4th EAPoL Tx is Tx done" to Sec FSM.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEvent4ndEapolTxDone (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T            prSecInfo;
    ENUM_SEC_STATE_T        eNextState;
    BOOLEAN                 fgIsTransition = (BOOLEAN)FALSE;
    P_CMD_802_11_KEY        prStoredKey;

    DEBUGFUNC("secFsmRunEvent4ndEapolTx");

    ASSERT(prSta);

    prSecInfo = &prSta->rSecInfo;
    eNextState = prSecInfo->eCurrentState;

#if DBG
    DBGLOG(RSN, TRACE, (MACSTR " Sec state %s\n", MAC2STR(prSta->aucMacAddr),
        apucDebugSecState[prSecInfo->eCurrentState]));
#else
    DBGLOG(RSN, TRACE, (MACSTR " Sec state [%d]\n", MAC2STR(prSta->aucMacAddr), prSecInfo->eCurrentState));
#endif

    switch(prSecInfo->eCurrentState) {
    case SEC_STATE_INITIATOR_PORT_BLOCKED :
    case SEC_STATE_CHECK_OK :
        prSecInfo->fg2nd1xSend = FALSE;
        if (prSecInfo->fgKeyStored) {
            prStoredKey = (P_CMD_802_11_KEY)prSecInfo->aucStoredKey;

            //prSta = rxmLookupStaRecIndexFromTA(prStoredKey->aucPeerAddr);
            //if (nicPrivacySetKeyEntry(prStoredKey, prSta->ucWTEntry) == FALSE)
            //    DBGLOG(RSN, WARN, ("nicPrivacySetKeyEntry() fail,..\n"));

            //key update
            prSecInfo->fgKeyStored = FALSE;
            prSta->fgTransmitKeyExist = TRUE;
        }
        if (prSecInfo->eCurrentState == SEC_STATE_INITIATOR_PORT_BLOCKED) {
            SEC_STATE_TRANSITION(prAdapter, prSta, INITIATOR_PORT_BLOCKED, CHECK_OK);
        }
        break;
    default:

#if DBG
        DBGLOG(RSN, WARN, ("Rcv thh EAPoL Tx done at %s\n", apucDebugSecState[prSecInfo->eCurrentState]));
#else
        DBGLOG(RSN, WARN, ("Rcv thh EAPoL Tx done at [%d]\n", prSecInfo->eCurrentState));
#endif
        break;
    }

    if (prSecInfo->eCurrentState != eNextState) {
        secFsmSteps(prAdapter, prSta, eNextState);
    }

    return;

}/* secFsmRunEvent4ndEapolTx */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will indicate an Event of "Pairwise key installed" to SEC FSM.
*
* \param[in] prSta            Pointer to the Sta record
*
* \retval TRUE The key can be installed to HW
* \retval FALSE The kay conflict with the current key, abort it
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
secFsmEventPTKInstalled (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T            prSecInfo;
    ENUM_SEC_STATE_T        eNextState;
    BOOLEAN                 fgStatus = TRUE;
    BOOLEAN                 fgIsTransition = (BOOLEAN)FALSE;

    ASSERT(prSta);

    prSecInfo = &prSta->rSecInfo;
    if (prSecInfo == NULL)
        return TRUE; /* Not PTK */

#if DBG
    DBGLOG(RSN, TRACE, (MACSTR " Sec state %s\n", MAC2STR(prSta->aucMacAddr),
        apucDebugSecState[prSecInfo->eCurrentState]));
#else
    DBGLOG(RSN, TRACE, (MACSTR " Sec state [%d]\n", MAC2STR(prSta->aucMacAddr), prSecInfo->eCurrentState));
#endif

    eNextState = prSecInfo->eCurrentState;

    switch(prSecInfo->eCurrentState) {
    case SEC_STATE_INIT:
        /* Legacy wep, wpa-none */
        break;

    case SEC_STATE_INITIATOR_PORT_BLOCKED:
        if (prSecInfo->fg2nd1xSend) {
        }
        else {
            SEC_STATE_TRANSITION(prAdapter, prSta, INITIATOR_PORT_BLOCKED, CHECK_OK);
        }
        break;

    case SEC_STATE_RESPONDER_PORT_BLOCKED:
        SEC_STATE_TRANSITION(prAdapter, prSta, RESPONDER_PORT_BLOCKED, CHECK_OK);
        break;


    case SEC_STATE_CHECK_OK:
        break;

    default:
        fgStatus = FALSE;
        break;
    }

    if (prSecInfo->eCurrentState != eNextState) {
        secFsmSteps(prAdapter, prSta, eNextState);
    }

    return fgStatus;

} /* end of secFsmRunEventPTKInstalled() */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will indicate an Event of "Counter Measure" to SEC FSM.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEventStartCounterMeasure (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prSta
    )
{
    P_SEC_INFO_T            prSecInfo;
    ENUM_SEC_STATE_T        eNextState;
    BOOLEAN                 fgIsTransition = (BOOLEAN)FALSE;

    DEBUGFUNC("secFsmRunEventStartCounterMeasure");

    ASSERT(prSta);

    if (!IS_STA_IN_AIS(prSta)) {
        DBGLOG(RSN, INFO, ("Counter Measure should occur at AIS network!!\n"));
        //ASSERT(0);
        return;
    }

    prSecInfo = &prSta->rSecInfo;

    eNextState = prSecInfo->eCurrentState;

#if DBG
    DBGLOG(RSN, TRACE, (MACSTR " Sec state %s\n", MAC2STR(prSta->aucMacAddr),
        apucDebugSecState[prSecInfo->eCurrentState]));
#else
    DBGLOG(RSN, TRACE, (MACSTR " Sec state [%d]\n", MAC2STR(prSta->aucMacAddr), prSecInfo->eCurrentState));
#endif

    prAdapter->rWifiVar.rAisSpecificBssInfo.u4RsnaLastMICFailTime = 0;

    switch(prSecInfo->eCurrentState) {
    case SEC_STATE_CHECK_OK:
        {
            prAdapter->rWifiVar.rAisSpecificBssInfo.fgCounterMeasure = TRUE;

            //<Todo> dls port control
            SEC_STATE_TRANSITION(prAdapter, prSta, CHECK_OK, SEND_EAPOL);
        }
        break;

    default:
        break;
    }

    /* Call arbFsmSteps() when we are going to change ARB STATE */
    if (prSecInfo->eCurrentState != eNextState) {
        secFsmSteps(prAdapter, prSta, eNextState);
    }

    return;

} /* secFsmRunEventStartCounterMeasure */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will indicate an Event of "802.1x EAPoL Tx Done" to Sec FSM.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEventEapolTxDone (
    IN P_ADAPTER_T            prAdapter,
    IN P_STA_RECORD_T         prStaRec,
    IN ENUM_TX_RESULT_CODE_T  rTxDoneStatus
    )
{
    P_SEC_INFO_T              prSecInfo;
    ENUM_SEC_STATE_T          eNextState;
    BOOLEAN                   fgIsTransition = (BOOLEAN)FALSE;
    P_AIS_SPECIFIC_BSS_INFO_T prAisBssInfo;

    DEBUGFUNC("secFsmRunEventEapolTxDone");

    ASSERT(prStaRec);

    if (rTxDoneStatus != TX_RESULT_SUCCESS) {
        DBGLOG(RSN, INFO, ("Error EAPoL fram fail to send!!\n"));
        //ASSERT(0);
        return;
    }

    if (!IS_STA_IN_AIS(prStaRec)) {
        DBGLOG(RSN, INFO, ("Counter Measure should occur at AIS network!!\n"));
        //ASSERT(0);
        return;
    }

    prAisBssInfo = &prAdapter->rWifiVar.rAisSpecificBssInfo;

    ASSERT(prAisBssInfo);

    prSecInfo = &prStaRec->rSecInfo;
    eNextState = prSecInfo->eCurrentState;

#if DBG
    DBGLOG(RSN, TRACE, (MACSTR " Sec state %s\n", MAC2STR(prStaRec->aucMacAddr),
        apucDebugSecState[prSecInfo->eCurrentState]));
#else
    DBGLOG(RSN, TRACE, (MACSTR " Sec state [%d]\n", MAC2STR(prStaRec->aucMacAddr), prSecInfo->eCurrentState));
#endif

    switch(prSecInfo->eCurrentState) {
    case SEC_STATE_SEND_EAPOL:
        if (prAisBssInfo->fgCheckEAPoLTxDone == FALSE) {
            ASSERT(0);
        }

        prAisBssInfo->fgCheckEAPoLTxDone = FALSE;
        //cnmTimerStopTimer(prAdapter, &prAisBssInfo->rRsnaEAPoLReportTimeoutTimer);

        SEC_STATE_TRANSITION(prAdapter, prStaRec, SEND_EAPOL, SEND_DEAUTH);
        break;
    default:
        break;
    }

    if (prSecInfo->eCurrentState != eNextState) {
        secFsmSteps(prAdapter, prStaRec, eNextState);
    }

    return;

}/* secFsmRunEventEapolTxDone */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will indicate an Event of "Deauth frame Tx Done" to Sec FSM.
*
* \param[in] pMsduInfo            Pointer to the Msdu Info
* \param[in] rStatus              The Tx done status
*
* \return -
*
* \note after receive deauth frame, callback function call this
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEventDeauthTxDone (
    IN P_ADAPTER_T            prAdapter,
    IN P_MSDU_INFO_T          prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T  rTxDoneStatus
    )
{
    P_STA_RECORD_T            prStaRec;
    P_SEC_INFO_T              prSecInfo;
    ENUM_SEC_STATE_T          eNextState;
    BOOLEAN                   fgIsTransition = (BOOLEAN)FALSE;

    DEBUGFUNC("secFsmRunEventDeauthTxDone");

    ASSERT(prMsduInfo);

    prStaRec = cnmGetStaRecByIndex(prAdapter, prMsduInfo->ucStaRecIndex);

    ASSERT(prStaRec);

    if (!prStaRec)
        return;

    if (!IS_STA_IN_AIS(prStaRec)) {
        DBGLOG(RSN, INFO, ("Counter Measure should occur at AIS network!!\n"));
        //ASSERT(0);
        return;
    }

    prSecInfo = (P_SEC_INFO_T)&prStaRec->rSecInfo;

#if DBG
    DBGLOG(RSN, TRACE, (MACSTR " Sec state %s\n", MAC2STR(prStaRec->aucMacAddr),
        apucDebugSecState[prSecInfo->eCurrentState]));
#else
    DBGLOG(RSN, TRACE, (MACSTR " Sec state [%d]\n", MAC2STR(prStaRec->aucMacAddr), prSecInfo->eCurrentState));
#endif

    switch(prSecInfo->eCurrentState) {
    case SEC_STATE_SEND_DEAUTH:

        DBGLOG(RSN, TRACE, ("Set timer %d\n", COUNTER_MEASURE_TIMEOUT_INTERVAL_SEC));

        SEC_STATE_TRANSITION(prAdapter, prStaRec, SEND_DEAUTH, COUNTERMEASURE);

        break;

    default:
        ASSERT(0);
        break;
    }

    return;
}/* secFsmRunEventDeauthTxDone */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will check the eapol error frame fail to send issue.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEventEapolTxTimeout (
    IN P_ADAPTER_T            prAdapter,
    IN UINT_32                u4Parm
    )
{
    P_STA_RECORD_T            prStaRec;

    DEBUGFUNC("secFsmRunEventEapolTxTimeout");

    prStaRec = (P_STA_RECORD_T)u4Parm;

    ASSERT(prStaRec);

    /* Todo:: How to handle the Eapol Error fail to send case? */
    ASSERT(0);

    return;

}/* secFsmEventEapolTxTimeout */


/*----------------------------------------------------------------------------*/
/*!
* \brief This function will stop the counterMeasure duration.
*
* \param[in] prSta            Pointer to the Sta record
*
* \return -
*/
/*----------------------------------------------------------------------------*/
VOID
secFsmEventEndOfCounterMeasure (
    IN P_ADAPTER_T          prAdapter,
    UINT_32                 u4Parm
    )
{
    P_STA_RECORD_T          prSta;
    P_SEC_INFO_T            prSecInfo;
    ENUM_SEC_STATE_T        eNextState;
    BOOLEAN                 fgIsTransition = (BOOLEAN)FALSE;

    DEBUGFUNC("secFsmRunEventEndOfCounterMeasure");

    prSta = (P_STA_RECORD_T)u4Parm;

    ASSERT(prSta);

    if (!IS_STA_IN_AIS(prSta)) {
        DBGLOG(RSN, INFO, ("Counter Measure should occur at AIS network!!\n"));
        //ASSERT(0);
        return;
    }

    prSecInfo = &prSta->rSecInfo;
    eNextState = prSecInfo->eCurrentState;

#if DBG
    DBGLOG(RSN, TRACE, (MACSTR " Sec state %s\n", MAC2STR(prSta->aucMacAddr),
        apucDebugSecState[prSecInfo->eCurrentState]));
#else
    DBGLOG(RSN, TRACE, (MACSTR " Sec state [%d]\n", MAC2STR(prSta->aucMacAddr), prSecInfo->eCurrentState));
#endif

    switch(prSecInfo->eCurrentState) {
    case SEC_STATE_SEND_DEAUTH:
        {
            prAdapter->rWifiVar.rAisSpecificBssInfo.fgCounterMeasure = FALSE;

            SEC_STATE_TRANSITION(prAdapter, prSta, COUNTERMEASURE, INIT);
        }
        break;

    default:
        ASSERT(0);
    }

    /* Call arbFsmSteps() when we are going to change ARB STATE */
    if (prSecInfo->eCurrentState != eNextState) {
        secFsmSteps(prAdapter, prSta, eNextState);
    }

    return;
}/* end of secFsmRunEventEndOfCounterMeasure */
#endif
