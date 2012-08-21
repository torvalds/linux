/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/mgmt/scan_fsm.c#1 $
*/

/*! \file   "scan_fsm.c"
    \brief  This file defines the state transition function for SCAN FSM.

    The SCAN FSM is part of SCAN MODULE and responsible for performing basic SCAN
    behavior as metioned in IEEE 802.11 2007 11.1.3.1 & 11.1.3.2 .
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
** $Log: scan_fsm.c $
 *
 * 01 20 2012 cp.wu
 * [ALPS00096191] [MT6620 Wi-Fi][Driver][Firmware] Porting to ALPS4.0_DEV branch
 * sync to up-to-date changes including:
 * 1. BOW bugfix
 * 2. XLOG
 *
 * 11 24 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Adjust code for DBG and CONFIG_XLOG.
 *
 * 11 14 2011 yuche.tsai
 * [WCXRP00001107] [Volunteer Patch][Driver] Large Network Type index assert in FW issue.
 * Avoid possible FW assert when unload P2P module.
 *
 * 11 11 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * modify the xlog related code.
 *
 * 11 02 2011 wh.su
 * [WCXRP00001078] [MT6620 Wi-Fi][Driver] Adding the mediatek log improment support : XLOG
 * Add XLOG related code and define.
 *
 * 10 19 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Branch 2.1
 * Davinci Maintrunk Label: MT6620_WIFI_DRIVER_FW_TRUNK_MT6620E5_111019_0926.
 *
 * 08 11 2011 cp.wu
 * [WCXRP00000830] [MT6620 Wi-Fi][Firmware] Use MDRDY counter to detect empty channel for shortening scan time
 * sparse channel detection:
 * driver: collect sparse channel information with scan-done event

 *
 * 07 18 2011 cp.wu
 * [WCXRP00000858] [MT5931][Driver][Firmware] Add support for scan to search for more than one SSID in a single scanning request
 * free mailbox message afte parsing is completed.
 *
 * 07 18 2011 cp.wu
 * [WCXRP00000858] [MT5931][Driver][Firmware] Add support for scan to search for more than one SSID in a single scanning request
 * add framework in driver domain for supporting new SCAN_REQ_V2 for more than 1 SSID support as well as uProbeDelay in NDIS 6.x driver model
 *
 * 04 18 2011 terry.wu
 * [WCXRP00000660] [MT6620 Wi-Fi][Driver] Remove flag CFG_WIFI_DIRECT_MOVED
 * Remove flag CFG_WIFI_DIRECT_MOVED.
 *
 * 03 29 2011 cp.wu
 * [WCXRP00000604] [MT6620 Wi-Fi][Driver] Surpress Klockwork Warning
 * surpress klock warning with code path rewritten
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 18 2011 cm.chang
 * [WCXRP00000576] [MT6620 Wi-Fi][Driver][FW] Remove P2P compile option in scan req/cancel command
 * As CR title
 *
 * 02 18 2011 yuche.tsai
 * [WCXRP00000478] [Volunteer Patch][MT6620][Driver] Probe request frame during search phase do not contain P2P wildcard SSID.
 * Take P2P wildcard SSID into consideration.
 *
 * 01 27 2011 yuche.tsai
 * [WCXRP00000399] [Volunteer Patch][MT6620/MT5931][Driver] Fix scan side effect after P2P module separate.
 * Fix scan channel extension issue when p2p module is not registered.
 *
 * 01 26 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * .
 *
 * 01 25 2011 yuche.tsai
 * [WCXRP00000388] [Volunteer Patch][MT6620][Driver/Fw] change Station Type in station record.
 * Fix Compile Error when DBG is disabled.
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 30 2010 cp.wu
 * NULL
 * eliminate klockwork errors
 *
 * 08 16 2010 cp.wu
 * NULL
 * add interface for RLM to trigger OBSS-SCAN.
 *
 * 08 16 2010 yuche.tsai
 * NULL
 * Fix bug for processing queued scan request.
 *
 * 08 11 2010 yuche.tsai
 * NULL
 * Add a function for returning channel.
 *
 * 08 05 2010 yuche.tsai
 * NULL
 * Update SCAN FSM for support P2P Device discovery scan.
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 07 26 2010 yuche.tsai
 *
 * Add option of channel extension while cancelling scan request.
 *
 * 07 21 2010 yuche.tsai
 *
 * Add P2P Scan & Scan Result Parsing & Saving.
 *
 * 07 20 2010 cp.wu
 *
 * pass band information for scan in an efficient way by mapping ENUM_BAND_T into UINT_8..
 *
 * 07 19 2010 cp.wu
 *
 * due to FW/DRV won't be sync. precisely, some strict assertions should be eased.
 *
 * 07 19 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * SCN module is now able to handle multiple concurrent scanning requests
 *
 * 07 16 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * bugfix for SCN migration
 * 1) modify QUEUE_CONCATENATE_QUEUES() so it could be used to concatence with an empty queue
 * 2) before AIS issues scan request, network(BSS) needs to be activated first
 * 3) only invoke COPY_SSID when using specified SSID for scan
 *
 * 07 15 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * driver no longer generates probe request frames
 *
 * 07 14 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * pass band with channel number information as scan parameter
 *
 * 07 14 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * remove timer in DRV-SCN.
 *
 * 07 09 2010 cp.wu
 *
 * 1) separate AIS_FSM state for two kinds of scanning. (OID triggered scan, and scan-for-connection)
 * 2) eliminate PRE_BSS_DESC_T, Beacon/PrebResp is now parsed in single pass
 * 3) implment DRV-SCN module, currently only accepts single scan request, other request will be directly dropped by returning BUSY
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * take use of RLM module for parsing/generating HT IEs for 11n capability
 *
 * 07 02 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * when returning to SCAN_IDLE state, send a correct message to source FSM.
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * implementation of DRV-SCN and related mailbox message handling.
 *
 * 06 22 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * comment out RLM APIs by CFG_RLM_MIGRATION.
 *
 * 06 21 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration
 * add scan_fsm into building.
 *
 * 05 14 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Refine the order of Stop TX Queue and Switch Channel
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Update pause/resume/flush API to new Bitmap API
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 03 18 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Ignore the PROBE_DELAY state if the value of Probe Delay == 0
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add support scan channel 1~14 and update scan result's frequency infou1rwduu`wvpghlqg|n`slk+mpdkb
 *
 * 01 08 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add set RX Filter to receive BCN from different BSSID during SCAN
 *
 * 12 18 2009 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * .
 *
 * Nov 25 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Remove flag of CFG_TEST_MGMT_FSM
 *
 * Nov 20 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Change parameter of scanSendProbeReqFrames()
 *
 * Nov 16 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Update scnFsmSteps()
 *
 * Nov 5 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Fix typo
 *
 * Nov 5 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
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
static PUINT_8 apucDebugScanState[SCAN_STATE_NUM] = {
    (PUINT_8)DISP_STRING("SCAN_STATE_IDLE"),
    (PUINT_8)DISP_STRING("SCAN_STATE_SCANNING"),
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
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnFsmSteps (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_SCAN_STATE_T eNextState
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    P_MSG_HDR_T prMsgHdr;

    BOOLEAN fgIsTransition = (BOOLEAN)FALSE;

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

    do {

#if CFG_SUPPORT_XLOG
        DBGLOG(SCN, STATE, ("[%d] TRANSITION: [%d] -> [%d]\n",
                             DBG_SCN_IDX,
                             prScanInfo->eCurrentState,
                             eNextState));
#else
        DBGLOG(SCN, STATE, ("TRANSITION: [%s] -> [%s]\n",
                             apucDebugScanState[prScanInfo->eCurrentState],
                             apucDebugScanState[eNextState]));
#endif

        /* NOTE(Kevin): This is the only place to change the eCurrentState(except initial) */
        prScanInfo->eCurrentState = eNextState;

        fgIsTransition = (BOOLEAN)FALSE;

        switch (prScanInfo->eCurrentState) {
        case SCAN_STATE_IDLE:
            /* check for pending scanning requests */
            if(!LINK_IS_EMPTY(&(prScanInfo->rPendingMsgList))) {
                // load next message from pending list as scan parameters
                LINK_REMOVE_HEAD(&(prScanInfo->rPendingMsgList), prMsgHdr, P_MSG_HDR_T);

                if(prMsgHdr->eMsgId == MID_AIS_SCN_SCAN_REQ
                        || prMsgHdr->eMsgId == MID_BOW_SCN_SCAN_REQ
                        || prMsgHdr->eMsgId == MID_P2P_SCN_SCAN_REQ
                        || prMsgHdr->eMsgId == MID_RLM_SCN_SCAN_REQ) {
                    scnFsmHandleScanMsg(prAdapter, (P_MSG_SCN_SCAN_REQ)prMsgHdr);
                    }
                    else {
                    scnFsmHandleScanMsgV2(prAdapter, (P_MSG_SCN_SCAN_REQ_V2)prMsgHdr);
                    }

                /* switch to next state */
                eNextState = SCAN_STATE_SCANNING;
                fgIsTransition = TRUE;

                cnmMemFree(prAdapter, prMsgHdr);
                }
            break;

        case SCAN_STATE_SCANNING:
            if(prScanParam->fgIsScanV2 == FALSE) {
                scnSendScanReq(prAdapter);
                }
                else {
                scnSendScanReqV2(prAdapter);
                }
            break;

        default:
            ASSERT(0);
            break;

                }
    }
    while (fgIsTransition);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief        Generate CMD_ID_SCAN_REQ command
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnSendScanReq (
    IN P_ADAPTER_T prAdapter
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    CMD_SCAN_REQ rCmdScanReq;
    UINT_32 i;

    ASSERT(prAdapter);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

            // send command packet for scan
            kalMemZero(&rCmdScanReq, sizeof(CMD_SCAN_REQ));

            rCmdScanReq.ucSeqNum        = prScanParam->ucSeqNum;
            rCmdScanReq.ucNetworkType   = (UINT_8)prScanParam->eNetTypeIndex;
            rCmdScanReq.ucScanType      = (UINT_8)prScanParam->eScanType;
            rCmdScanReq.ucSSIDType      = prScanParam->ucSSIDType;

    if(prScanParam->ucSSIDNum == 1) {
                COPY_SSID(rCmdScanReq.aucSSID,
                        rCmdScanReq.ucSSIDLength,
                prScanParam->aucSpecifiedSSID[0],
                prScanParam->ucSpecifiedSSIDLen[0]);
            }

            rCmdScanReq.ucChannelType   = (UINT_8)prScanParam->eScanChannel;

            if (prScanParam->eScanChannel == SCAN_CHANNEL_SPECIFIED) {
                /* P2P would use:
                  * 1. Specified Listen Channel of passive scan for LISTEN state.
                  * 2. Specified Listen Channel of Target Device of active scan for SEARCH state. (Target != NULL)
                  */
                rCmdScanReq.ucChannelListNum    = prScanParam->ucChannelListNum;

                for(i = 0 ; i < rCmdScanReq.ucChannelListNum ; i++) {
                    rCmdScanReq.arChannelList[i].ucBand =
                            (UINT_8) prScanParam->arChnlInfoList[i].eBand;

                    rCmdScanReq.arChannelList[i].ucChannelNum =
                        (UINT_8)prScanParam->arChnlInfoList[i].ucChannelNum;
                }
            }

#if CFG_ENABLE_WIFI_DIRECT
            if(prAdapter->fgIsP2PRegistered) {
                rCmdScanReq.u2ChannelDwellTime = prScanParam->u2PassiveListenInterval;
            }
#endif

            if(prScanParam->u2IELen <= MAX_IE_LENGTH) {
                rCmdScanReq.u2IELen = prScanParam->u2IELen;
            }
            else {
                rCmdScanReq.u2IELen = MAX_IE_LENGTH;
            }

            if (prScanParam->u2IELen) {
                kalMemCopy(rCmdScanReq.aucIE,
                        prScanParam->aucIE,
                        sizeof(UINT_8) * rCmdScanReq.u2IELen);
            }

            wlanSendSetQueryCmd(prAdapter,
                    CMD_ID_SCAN_REQ,
                    TRUE,
                    FALSE,
                    FALSE,
                    NULL,
                    NULL,
                    OFFSET_OF(CMD_SCAN_REQ, aucIE) + rCmdScanReq.u2IELen,
                    (PUINT_8)&rCmdScanReq,
                    NULL,
                    0);
}


/*----------------------------------------------------------------------------*/
/*!
* \brief        Generate CMD_ID_SCAN_REQ_V2 command
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnSendScanReqV2 (
    IN P_ADAPTER_T prAdapter
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    CMD_SCAN_REQ_V2 rCmdScanReq;
    UINT_32 i;

    ASSERT(prAdapter);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

    // send command packet for scan
    kalMemZero(&rCmdScanReq, sizeof(CMD_SCAN_REQ_V2));

    rCmdScanReq.ucSeqNum        = prScanParam->ucSeqNum;
    rCmdScanReq.ucNetworkType   = (UINT_8)prScanParam->eNetTypeIndex;
    rCmdScanReq.ucScanType      = (UINT_8)prScanParam->eScanType;
    rCmdScanReq.ucSSIDType      = prScanParam->ucSSIDType;

    for (i = 0 ; i < prScanParam->ucSSIDNum; i++) {
        COPY_SSID(rCmdScanReq.arSSID[i].aucSsid,
                rCmdScanReq.arSSID[i].u4SsidLen,
                prScanParam->aucSpecifiedSSID[i],
                prScanParam->ucSpecifiedSSIDLen[i]);
    }

    rCmdScanReq.u2ProbeDelayTime    = (UINT_8)prScanParam->u2ProbeDelayTime;
    rCmdScanReq.ucChannelType       = (UINT_8)prScanParam->eScanChannel;

    if (prScanParam->eScanChannel == SCAN_CHANNEL_SPECIFIED) {
        /* P2P would use:
         * 1. Specified Listen Channel of passive scan for LISTEN state.
         * 2. Specified Listen Channel of Target Device of active scan for SEARCH state. (Target != NULL)
         */
        rCmdScanReq.ucChannelListNum    = prScanParam->ucChannelListNum;

        for(i = 0 ; i < rCmdScanReq.ucChannelListNum ; i++) {
            rCmdScanReq.arChannelList[i].ucBand =
                (UINT_8) prScanParam->arChnlInfoList[i].eBand;

            rCmdScanReq.arChannelList[i].ucChannelNum =
                (UINT_8)prScanParam->arChnlInfoList[i].ucChannelNum;
        }
    }

#if CFG_ENABLE_WIFI_DIRECT
    if(prAdapter->fgIsP2PRegistered) {
        rCmdScanReq.u2ChannelDwellTime = prScanParam->u2PassiveListenInterval;
    }
#endif

    if(prScanParam->u2IELen <= MAX_IE_LENGTH) {
        rCmdScanReq.u2IELen = prScanParam->u2IELen;
    }
    else {
        rCmdScanReq.u2IELen = MAX_IE_LENGTH;
        }

    if (prScanParam->u2IELen) {
        kalMemCopy(rCmdScanReq.aucIE,
                prScanParam->aucIE,
                sizeof(UINT_8) * rCmdScanReq.u2IELen);
    }

    wlanSendSetQueryCmd(prAdapter,
            CMD_ID_SCAN_REQ_V2,
            TRUE,
            FALSE,
            FALSE,
            NULL,
            NULL,
            OFFSET_OF(CMD_SCAN_REQ_V2, aucIE) + rCmdScanReq.u2IELen,
            (PUINT_8)&rCmdScanReq,
            NULL,
            0);

}


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnFsmMsgStart (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;

    ASSERT(prMsgHdr);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;


    if (prScanInfo->eCurrentState == SCAN_STATE_IDLE) {
        if(prMsgHdr->eMsgId == MID_AIS_SCN_SCAN_REQ
                || prMsgHdr->eMsgId == MID_BOW_SCN_SCAN_REQ
                || prMsgHdr->eMsgId == MID_P2P_SCN_SCAN_REQ
                || prMsgHdr->eMsgId == MID_RLM_SCN_SCAN_REQ) {
            scnFsmHandleScanMsg(prAdapter, (P_MSG_SCN_SCAN_REQ)prMsgHdr);
        }
        else if(prMsgHdr->eMsgId == MID_AIS_SCN_SCAN_REQ_V2
                || prMsgHdr->eMsgId == MID_BOW_SCN_SCAN_REQ_V2
                || prMsgHdr->eMsgId == MID_P2P_SCN_SCAN_REQ_V2
                || prMsgHdr->eMsgId == MID_RLM_SCN_SCAN_REQ_V2) {
            scnFsmHandleScanMsgV2(prAdapter, (P_MSG_SCN_SCAN_REQ_V2)prMsgHdr);
            }
            else {
            // should not deliver to this function
            ASSERT(0);
        }

        cnmMemFree(prAdapter, prMsgHdr);
        scnFsmSteps(prAdapter, SCAN_STATE_SCANNING);
    }
    else {
        LINK_INSERT_TAIL(&prScanInfo->rPendingMsgList, &prMsgHdr->rLinkEntry);
    }

    return;
}



/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnFsmMsgAbort (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    )
{
    P_MSG_SCN_SCAN_CANCEL prScanCancel;
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    CMD_SCAN_CANCEL rCmdScanCancel;

    ASSERT(prMsgHdr);

    prScanCancel = (P_MSG_SCN_SCAN_CANCEL)prMsgHdr;
    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

    if (prScanInfo->eCurrentState != SCAN_STATE_IDLE) {
        if(prScanCancel->ucSeqNum == prScanParam->ucSeqNum &&
                prScanCancel->ucNetTypeIndex == (UINT_8)prScanParam->eNetTypeIndex) {
            /* send cancel message to firmware domain */
            rCmdScanCancel.ucSeqNum = prScanParam->ucSeqNum;

#if CFG_ENABLE_WIFI_DIRECT
            if(prAdapter->fgIsP2PRegistered) {
                rCmdScanCancel.ucIsExtChannel = (UINT_8) prScanCancel->fgIsChannelExt;
            }
            else {
                rCmdScanCancel.ucIsExtChannel = (UINT_8) FALSE;
            }
#endif

            wlanSendSetQueryCmd(prAdapter,
                    CMD_ID_SCAN_CANCEL,
                    TRUE,
                    FALSE,
                    FALSE,
                    NULL,
                    NULL,
                    sizeof(CMD_SCAN_CANCEL),
                    (PUINT_8)&rCmdScanCancel,
                    NULL,
                    0);

            /* generate scan-done event for caller */
            scnFsmGenerateScanDoneMsg(prAdapter,
                    prScanParam->ucSeqNum,
                    (UINT_8)prScanParam->eNetTypeIndex,
                    SCAN_STATUS_CANCELLED);

            /* switch to next pending scan */
            scnFsmSteps(prAdapter, SCAN_STATE_IDLE);
        }
        else {
            scnFsmRemovePendingMsg(prAdapter, prScanCancel->ucSeqNum, prScanCancel->ucNetTypeIndex);
        }
    }

    cnmMemFree(prAdapter, prMsgHdr);

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief            Scan Message Parsing (Legacy)
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnFsmHandleScanMsg (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_SCN_SCAN_REQ prScanReqMsg
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    UINT_32 i;

    ASSERT(prAdapter);
    ASSERT(prScanReqMsg);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

    prScanParam->eScanType      = prScanReqMsg->eScanType;
    prScanParam->eNetTypeIndex  = (ENUM_NETWORK_TYPE_INDEX_T)prScanReqMsg->ucNetTypeIndex;
    prScanParam->ucSSIDType     = prScanReqMsg->ucSSIDType;
    if (prScanParam->ucSSIDType & (SCAN_REQ_SSID_SPECIFIED | SCAN_REQ_SSID_P2P_WILDCARD)) {
        prScanParam->ucSSIDNum = 1;

        COPY_SSID(prScanParam->aucSpecifiedSSID[0],
                prScanParam->ucSpecifiedSSIDLen[0],
                prScanReqMsg->aucSSID,
                prScanReqMsg->ucSSIDLength);

        // reset SSID length to zero for rest array entries
        for(i = 1 ; i < SCN_SSID_MAX_NUM ; i++) {
            prScanParam->ucSpecifiedSSIDLen[i] = 0;
        }
    }
    else {
        prScanParam->ucSSIDNum = 0;

        for(i = 0 ; i < SCN_SSID_MAX_NUM ; i++) {
            prScanParam->ucSpecifiedSSIDLen[i] = 0;
        }
    }

    prScanParam->u2ProbeDelayTime   = 0;
    prScanParam->eScanChannel   = prScanReqMsg->eScanChannel;
    if(prScanParam->eScanChannel == SCAN_CHANNEL_SPECIFIED) {
        if(prScanReqMsg->ucChannelListNum <= MAXIMUM_OPERATION_CHANNEL_LIST) {
            prScanParam->ucChannelListNum   = prScanReqMsg->ucChannelListNum;
        }
        else {
            prScanParam->ucChannelListNum   = MAXIMUM_OPERATION_CHANNEL_LIST;
        }

        kalMemCopy(prScanParam->arChnlInfoList,
                prScanReqMsg->arChnlInfoList,
                sizeof(RF_CHANNEL_INFO_T) * prScanParam->ucChannelListNum);
    }

    if(prScanReqMsg->u2IELen <= MAX_IE_LENGTH) {
        prScanParam->u2IELen    = prScanReqMsg->u2IELen;
    }
    else {
        prScanParam->u2IELen    = MAX_IE_LENGTH;
    }

    if(prScanParam->u2IELen) {
        kalMemCopy(prScanParam->aucIE, prScanReqMsg->aucIE, prScanParam->u2IELen);
    }

#if CFG_ENABLE_WIFI_DIRECT
    if(prAdapter->fgIsP2PRegistered) {
        prScanParam->u2PassiveListenInterval = prScanReqMsg->u2ChannelDwellTime;
    }
#endif
    prScanParam->ucSeqNum       = prScanReqMsg->ucSeqNum;

    if(prScanReqMsg->rMsgHdr.eMsgId == MID_RLM_SCN_SCAN_REQ) {
        prScanParam->fgIsObssScan   = TRUE;
    }
    else {
        prScanParam->fgIsObssScan   = FALSE;
    }

    prScanParam->fgIsScanV2 = FALSE;

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief            Scan Message Parsing - V2 with multiple SSID support
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnFsmHandleScanMsgV2 (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_SCN_SCAN_REQ_V2 prScanReqMsg
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    UINT_32 i;

    ASSERT(prAdapter);
    ASSERT(prScanReqMsg);
    ASSERT(prScanReqMsg->ucSSIDNum <= SCN_SSID_MAX_NUM);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

    prScanParam->eScanType      = prScanReqMsg->eScanType;
    prScanParam->eNetTypeIndex  = (ENUM_NETWORK_TYPE_INDEX_T)prScanReqMsg->ucNetTypeIndex;
    prScanParam->ucSSIDType     = prScanReqMsg->ucSSIDType;
    prScanParam->ucSSIDNum      = prScanReqMsg->ucSSIDNum;

    for(i = 0 ; i < prScanReqMsg->ucSSIDNum ; i++) {
        COPY_SSID(prScanParam->aucSpecifiedSSID[i],
                prScanParam->ucSpecifiedSSIDLen[i],
                prScanReqMsg->prSsid[i].aucSsid,
                (UINT_8)prScanReqMsg->prSsid[i].u4SsidLen);
    }

    prScanParam->u2ProbeDelayTime   = prScanReqMsg->u2ProbeDelay;
    prScanParam->eScanChannel       = prScanReqMsg->eScanChannel;
    if(prScanParam->eScanChannel == SCAN_CHANNEL_SPECIFIED) {
        if(prScanReqMsg->ucChannelListNum <= MAXIMUM_OPERATION_CHANNEL_LIST) {
            prScanParam->ucChannelListNum   = prScanReqMsg->ucChannelListNum;
        }
        else {
            prScanParam->ucChannelListNum   = MAXIMUM_OPERATION_CHANNEL_LIST;
        }

        kalMemCopy(prScanParam->arChnlInfoList,
                prScanReqMsg->arChnlInfoList,
                sizeof(RF_CHANNEL_INFO_T) * prScanParam->ucChannelListNum);
    }

    if(prScanReqMsg->u2IELen <= MAX_IE_LENGTH) {
        prScanParam->u2IELen    = prScanReqMsg->u2IELen;
    }
    else {
        prScanParam->u2IELen    = MAX_IE_LENGTH;
    }

    if(prScanParam->u2IELen) {
        kalMemCopy(prScanParam->aucIE, prScanReqMsg->aucIE, prScanParam->u2IELen);
    }

#if CFG_ENABLE_WIFI_DIRECT
    if(prAdapter->fgIsP2PRegistered) {
        prScanParam->u2PassiveListenInterval = prScanReqMsg->u2ChannelDwellTime;
    }
#endif
    prScanParam->ucSeqNum       = prScanReqMsg->ucSeqNum;

    if(prScanReqMsg->rMsgHdr.eMsgId == MID_RLM_SCN_SCAN_REQ) {
        prScanParam->fgIsObssScan   = TRUE;
    }
    else {
        prScanParam->fgIsObssScan   = FALSE;
    }

    prScanParam->fgIsScanV2 = TRUE;

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief            Remove pending scan request
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnFsmRemovePendingMsg (
    IN P_ADAPTER_T  prAdapter,
    IN UINT_8       ucSeqNum,
    IN UINT_8       ucNetTypeIndex
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    P_MSG_HDR_T prPendingMsgHdr, prPendingMsgHdrNext, prRemoveMsgHdr = NULL;
    P_LINK_ENTRY_T prRemoveLinkEntry = NULL;

    ASSERT(prAdapter);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

            /* traverse through rPendingMsgList for removal */
            LINK_FOR_EACH_ENTRY_SAFE(prPendingMsgHdr,
                                        prPendingMsgHdrNext,
                                        &(prScanInfo->rPendingMsgList),
                                        rLinkEntry,
                                        MSG_HDR_T) {
        if(prPendingMsgHdr->eMsgId == MID_AIS_SCN_SCAN_REQ
                || prPendingMsgHdr->eMsgId == MID_BOW_SCN_SCAN_REQ
                || prPendingMsgHdr->eMsgId == MID_P2P_SCN_SCAN_REQ
                || prPendingMsgHdr->eMsgId == MID_RLM_SCN_SCAN_REQ) {
                P_MSG_SCN_SCAN_REQ prScanReqMsg = (P_MSG_SCN_SCAN_REQ)prPendingMsgHdr;

            if(ucSeqNum == prScanReqMsg->ucSeqNum &&
                    ucNetTypeIndex == prScanReqMsg->ucNetTypeIndex) {
                prRemoveLinkEntry = &(prScanReqMsg->rMsgHdr.rLinkEntry);
                prRemoveMsgHdr = prPendingMsgHdr;
            }
        }
        else if(prPendingMsgHdr->eMsgId == MID_AIS_SCN_SCAN_REQ_V2
                || prPendingMsgHdr->eMsgId == MID_BOW_SCN_SCAN_REQ_V2
                || prPendingMsgHdr->eMsgId == MID_P2P_SCN_SCAN_REQ_V2
                || prPendingMsgHdr->eMsgId == MID_RLM_SCN_SCAN_REQ_V2) {
            P_MSG_SCN_SCAN_REQ_V2 prScanReqMsgV2 = (P_MSG_SCN_SCAN_REQ_V2)prPendingMsgHdr;

            if(ucSeqNum == prScanReqMsgV2->ucSeqNum &&
                    ucNetTypeIndex == prScanReqMsgV2->ucNetTypeIndex) {
                prRemoveLinkEntry = &(prScanReqMsgV2->rMsgHdr.rLinkEntry);
                prRemoveMsgHdr = prPendingMsgHdr;
            }
        }

        if(prRemoveLinkEntry) {
                    /* generate scan-done event for caller */
                    scnFsmGenerateScanDoneMsg(prAdapter,
                    ucSeqNum,
                    ucNetTypeIndex,
                            SCAN_STATUS_CANCELLED);

                    /* remove from pending list */
            LINK_REMOVE_KNOWN_ENTRY(&(prScanInfo->rPendingMsgList), prRemoveLinkEntry);
            cnmMemFree(prAdapter, prRemoveMsgHdr);

                    break;
                }
            }

    return;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnEventScanDone (
    IN P_ADAPTER_T          prAdapter,
    IN P_EVENT_SCAN_DONE    prScanDone
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

    // buffer empty channel information
    if(prScanParam->eScanChannel == SCAN_CHANNEL_FULL 
            || prScanParam->eScanChannel == SCAN_CHANNEL_2G4) {
        if(prScanDone->ucSparseChannelValid) {
            prScanInfo->fgIsSparseChannelValid      = TRUE;
            prScanInfo->rSparseChannel.eBand        = (ENUM_BAND_T)prScanDone->rSparseChannel.ucBand;
            prScanInfo->rSparseChannel.ucChannelNum = prScanDone->rSparseChannel.ucChannelNum;
        }
        else {
            prScanInfo->fgIsSparseChannelValid      = FALSE;
        }
    }

    if(prScanInfo->eCurrentState == SCAN_STATE_SCANNING &&
            prScanDone->ucSeqNum == prScanParam->ucSeqNum) {
        /* generate scan-done event for caller */
        scnFsmGenerateScanDoneMsg(prAdapter,
                prScanParam->ucSeqNum,
                (UINT_8)prScanParam->eNetTypeIndex,
                SCAN_STATUS_DONE);

        /* switch to next pending scan */
        scnFsmSteps(prAdapter, SCAN_STATE_IDLE);
    }
    else {
        DBGLOG(SCN, LOUD, ("Unexpected SCAN-DONE event: SeqNum = %d, Current State = %d\n",
                 prScanDone->ucSeqNum,
                 prScanInfo->eCurrentState));
    }

    return;
} /* end of scnEventScanDone */


/*----------------------------------------------------------------------------*/
/*!
* \brief
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
VOID
scnFsmGenerateScanDoneMsg (
    IN P_ADAPTER_T          prAdapter,
    IN UINT_8               ucSeqNum,
    IN UINT_8               ucNetTypeIndex,
    IN ENUM_SCAN_STATUS     eScanStatus
    )
{
    P_SCAN_INFO_T prScanInfo;
    P_SCAN_PARAM_T prScanParam;
    P_MSG_SCN_SCAN_DONE prScanDoneMsg;

    ASSERT(prAdapter);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);
    prScanParam = &prScanInfo->rScanParam;

    prScanDoneMsg = (P_MSG_SCN_SCAN_DONE)cnmMemAlloc(prAdapter, RAM_TYPE_MSG, sizeof(MSG_SCN_SCAN_DONE));
    if (!prScanDoneMsg) {
        ASSERT(0); // Can't indicate SCAN FSM Complete
        return;
    }

    if(prScanParam->fgIsObssScan == TRUE) {
        prScanDoneMsg->rMsgHdr.eMsgId = MID_SCN_RLM_SCAN_DONE;
    }
    else {
        switch((ENUM_NETWORK_TYPE_INDEX_T)ucNetTypeIndex) {
        case NETWORK_TYPE_AIS_INDEX:
            prScanDoneMsg->rMsgHdr.eMsgId = MID_SCN_AIS_SCAN_DONE;
            break;

#if CFG_ENABLE_WIFI_DIRECT
        case NETWORK_TYPE_P2P_INDEX:
            prScanDoneMsg->rMsgHdr.eMsgId = MID_SCN_P2P_SCAN_DONE;
            break;
#endif

#if CFG_ENABLE_BT_OVER_WIFI
        case NETWORK_TYPE_BOW_INDEX:
            prScanDoneMsg->rMsgHdr.eMsgId = MID_SCN_BOW_SCAN_DONE;
            break;
#endif

        default:
            DBGLOG(SCN, LOUD, ("Unexpected Network Type: %d\n", ucNetTypeIndex));
            ASSERT(0);
            break;
        }
    }

    prScanDoneMsg->ucSeqNum         = ucSeqNum;
    prScanDoneMsg->ucNetTypeIndex   = ucNetTypeIndex;
    prScanDoneMsg->eScanStatus      = eScanStatus;

    mboxSendMsg(prAdapter,
            MBOX_ID_0,
            (P_MSG_HDR_T) prScanDoneMsg,
            MSG_SEND_METHOD_BUF);

} /* end of scnFsmGenerateScanDoneMsg() */


/*----------------------------------------------------------------------------*/
/*!
* \brief        Query for most sparse channel 
*
* \param[in]
*
* \return none
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
scnQuerySparseChannel (
    IN P_ADAPTER_T      prAdapter,
    P_ENUM_BAND_T       prSparseBand,
    PUINT_8             pucSparseChannel
    )
{
    P_SCAN_INFO_T prScanInfo;

    ASSERT(prAdapter);

    prScanInfo = &(prAdapter->rWifiVar.rScanInfo);

    if(prScanInfo->fgIsSparseChannelValid == TRUE) {
        if(prSparseBand) {
            *prSparseBand = prScanInfo->rSparseChannel.eBand;
        }

        if(pucSparseChannel) {
            *pucSparseChannel = prScanInfo->rSparseChannel.ucChannelNum;
        }

        return TRUE;
    }
    else {
        return FALSE;
    }
}

