/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/mgmt/ais_fsm.h#1 $
*/

/*! \file   ais_fsm.h
    \brief  Declaration of functions and finite state machine for AIS Module.

    Declaration of functions and finite state machine for AIS Module.
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
** $Log: ais_fsm.h $
 *
 * 11 22 2011 cp.wu
 * [WCXRP00001120] [MT6620 Wi-Fi][Driver] Modify roaming to AIS state transition from synchronous to asynchronous approach to avoid incomplete state termination
 * 1. change RDD related compile option brace position.
 * 2. when roaming is triggered, ask AIS to transit immediately only when AIS is in Normal TR state without join timeout timer ticking
 * 3. otherwise, insert AIS_REQUEST into pending request queue
 *
 * 04 25 2011 cp.wu
 * [WCXRP00000676] [MT6620 Wi-Fi][Driver] AIS to reduce request channel period from 5 seconds to 2 seconds
 * channel interval for joining is shortened to 2 seconds to avoid interruption of concurrent operating network.
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 02 26 2011 tsaiyuan.hsu
 * [WCXRP00000391] [MT6620 Wi-Fi][FW] Add Roaming Support
 * not send disassoc or deauth to leaving AP so as to improve performace of roaming.
 *
 * 02 22 2011 cp.wu
 * [WCXRP00000487] [MT6620 Wi-Fi][Driver][AIS] Serve scan and connect request with a queue-based approach to improve response time for scanning request
 * handle SCAN and RECONNECT with a FIFO approach.
 *
 * 01 27 2011 tsaiyuan.hsu
 * [WCXRP00000392] [MT6620 Wi-Fi][Driver] Add Roaming Support
 * add roaming fsm
 * 1. not support 11r, only use strength of signal to determine roaming.
 * 2. not enable CFG_SUPPORT_ROAMING until completion of full test.
 * 3. in 6620, adopt work-around to avoid sign extension problem of cck of hw
 * 4. assume that change of link quality in smooth way.
 *
 * 01 14 2011 cp.wu
 * [WCXRP00000359] [MT6620 Wi-Fi][Driver] add an extra state to ensure DEAUTH frame is always sent
 * Add an extra state to guarantee DEAUTH frame is sent then connect to new BSS.
 * This change is due to WAPI AP needs DEAUTH frame as a necessary step in handshaking protocol.
 *
 * 11 25 2010 cp.wu
 * [WCXRP00000208] [MT6620 Wi-Fi][Driver] Add scanning with specified SSID to AIS FSM
 * add scanning with specified SSID facility to AIS-FSM
 *
 * 11 01 2010 cp.wu
 * [WCXRP00000056] [MT6620 Wi-Fi][Driver] NVRAM implementation with Version Check[WCXRP00000150] [MT6620 Wi-Fi][Driver] Add implementation for querying current TX rate from firmware auto rate module
 * 1) Query link speed (TX rate) from firmware directly with buffering mechanism to reduce overhead
 * 2) Remove CNM CH-RECOVER event handling
 * 3) cfg read/write API renamed with kal prefix for unified naming rules.
 *
 * 09 06 2010 cp.wu
 * NULL
 * 1) initialize for correct parameter even for disassociation.
 * 2) AIS-FSM should have a limit on trials to build connection
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 08 25 2010 cp.wu
 * NULL
 * [AIS-FSM] IBSS no longer needs to acquire channel for beaconing, RLM/CNM will handle the channel switching when BSS information is updated
 *
 * 08 12 2010 kevin.huang
 * NULL
 * Refine bssProcessProbeRequest() and bssSendBeaconProbeResponse()
 *
 * 08 12 2010 cp.wu
 * NULL
 * [AIS-FSM] honor registry setting for adhoc running mode. (A/B/G)
 *
 * 08 03 2010 cp.wu
 * NULL
 * surpress compilation warning.
 *
 * 07 30 2010 cp.wu
 * NULL
 * 1) BoW wrapper: use definitions instead of hard-coded constant for error code
 * 2) AIS-FSM: eliminate use of desired RF parameters, use prTargetBssDesc instead
 * 3) add handling for RX_PKT_DESTINATION_HOST_WITH_FORWARD for GO-broadcast frames
 *
 * 07 26 2010 cp.wu
 * 
 * AIS-FSM: when scan request is coming in the 1st 5 seconds of channel privilege period, just pend it til 5-sec. period finishes
 *
 * 07 26 2010 cp.wu
 * 
 * AIS-FSM FIX: return channel privilege even when the privilege is not granted yet
 * QM: qmGetFrameAction() won't assert when corresponding STA-REC index is not found
 *
 * 07 23 2010 cp.wu
 * 
 * add AIS-FSM handling for beacon timeout event.
 *
 * 07 21 2010 cp.wu
 * 
 * separate AIS-FSM states into different cases of channel request.
 *
 * 07 21 2010 cp.wu
 * 
 * 1) change BG_SCAN to ONLINE_SCAN for consistent term
 * 2) only clear scanning result when scan is permitted to do
 *
 * 07 19 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * Add Ad-Hoc support to AIS-FSM
 *
 * 07 14 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration.
 * Refine AIS-FSM by divided into more states
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
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * AIS-FSM integration with CNM channel request messages
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * implementation of DRV-SCN and related mailbox message handling.
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add buildable & linkable ais_fsm.c
 * 
 * related reference are still waiting to be resolved
 *
 * 06 09 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add definitions for module migration.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add aa_fsm.h, ais_fsm.h, bss.h, mib.h and scan.h.
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support 
 * Add Power Management - Legacy PS-POLL support.
 *
 * 04 23 2010 wh.su
 * [BORA00000605][WIFISYS] Phase3 Integration 
 * reduce the backgroud ssid idle time min and max value
 *
 * 04 19 2010 kevin.huang
 * [BORA00000714][WIFISYS][New Feature]Beacon Timeout Support
 * Add Beacon Timeout Support
 *  *  and will send Null frame to diagnose connection
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 *
 *  * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 02 26 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Remove CFG_TEST_VIRTUAL_CMD and add support of Driver STA_RECORD_T activation
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Support dynamic channel selection
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * 01 11 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add Deauth and Disassoc Handler
 *
 * 01 07 2010 kevin.huang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add Media disconnect indication and related postpone functions
 *
 * Dec 3 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add aisFsmRunEventJoinComplete()
 *
 * Nov 25 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add Virtual CMD & RESP for testing CMD PATH
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * add aisFsmInitializeConnectionSettings()
 *
 * Nov 20 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add CFG_TEST_MGMT_FSM for aisFsmTest()
 *
 * Nov 18 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add function prototype of aisFsmInit()
 *
 * Nov 16 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
*/

#ifndef _AIS_FSM_H
#define _AIS_FSM_H

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
#define AIS_BG_SCAN_INTERVAL_MIN_SEC        2 //30 // exponential to 960
#define AIS_BG_SCAN_INTERVAL_MAX_SEC        2 //960 // 16min

#define AIS_DELAY_TIME_OF_DISCONNECT_SEC    10

#define AIS_IBSS_ALONE_TIMEOUT_SEC          20 // seconds

#define AIS_BEACON_TIMEOUT_COUNT_ADHOC      30
#define AIS_BEACON_TIMEOUT_COUNT_INFRA      10
#define AIS_BEACON_TIMEOUT_GUARD_TIME_SEC   1 /* Second */

#define AIS_BEACON_MAX_TIMEOUT_TU           100
#define AIS_BEACON_MIN_TIMEOUT_TU           5
#define AIS_BEACON_MAX_TIMEOUT_VALID        TRUE
#define AIS_BEACON_MIN_TIMEOUT_VALID        TRUE

#define AIS_BMC_MAX_TIMEOUT_TU              100
#define AIS_BMC_MIN_TIMEOUT_TU              5
#define AIS_BMC_MAX_TIMEOUT_VALID           TRUE
#define AIS_BMC_MIN_TIMEOUT_VALID           TRUE

#define AIS_JOIN_CH_GRANT_THRESHOLD         10
#define AIS_JOIN_CH_REQUEST_INTERVAL        2000

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_AIS_STATE_T {
    AIS_STATE_IDLE = 0,
    AIS_STATE_SEARCH,
    AIS_STATE_SCAN,
    AIS_STATE_ONLINE_SCAN,
    AIS_STATE_LOOKING_FOR,
    AIS_STATE_WAIT_FOR_NEXT_SCAN,
    AIS_STATE_REQ_CHANNEL_JOIN,
    AIS_STATE_JOIN,
    AIS_STATE_IBSS_ALONE,
    AIS_STATE_IBSS_MERGE,
    AIS_STATE_NORMAL_TR,
    AIS_STATE_DISCONNECTING,
    AIS_STATE_NUM
} ENUM_AIS_STATE_T;


typedef struct _MSG_AIS_ABORT_T {
    MSG_HDR_T           rMsgHdr;        /* Must be the first member */
    UINT_8              ucReasonOfDisconnect;
    BOOLEAN             fgDelayIndication;
} MSG_AIS_ABORT_T, *P_MSG_AIS_ABORT_T;


typedef struct _MSG_AIS_IBSS_PEER_FOUND_T {
    MSG_HDR_T       rMsgHdr;    /* Must be the first member */
    UINT_8          ucNetTypeIndex;
    BOOLEAN         fgIsMergeIn; /* TRUE: Merge In, FALSE: Merge Out */
    P_STA_RECORD_T  prStaRec;
} MSG_AIS_IBSS_PEER_FOUND_T, *P_MSG_AIS_IBSS_PEER_FOUND_T;

typedef enum _ENUM_AIS_REQUEST_TYPE_T {
    AIS_REQUEST_SCAN,
    AIS_REQUEST_RECONNECT,
    AIS_REQUEST_ROAMING_SEARCH,
    AIS_REQUEST_ROAMING_CONNECT,
    AIS_REQUEST_NUM
} ENUM_AIS_REQUEST_TYPE_T;

typedef struct _AIS_REQ_HDR_T {
    LINK_ENTRY_T            rLinkEntry;
    ENUM_AIS_REQUEST_TYPE_T eReqType;
} AIS_REQ_HDR_T, *P_AIS_REQ_HDR_T;


typedef struct _AIS_FSM_INFO_T {
    ENUM_AIS_STATE_T    ePreviousState;
    ENUM_AIS_STATE_T    eCurrentState;

    BOOLEAN             fgTryScan;

    BOOLEAN             fgIsInfraChannelFinished;
    BOOLEAN             fgIsChannelRequested;
    BOOLEAN             fgIsChannelGranted;

#if CFG_SUPPORT_ROAMING
    BOOLEAN             fgIsRoamingScanPending;
#endif /* CFG_SUPPORT_ROAMING */

    UINT_8              ucAvailableAuthTypes;       /* Used for AUTH_MODE_AUTO_SWITCH */

    P_BSS_DESC_T        prTargetBssDesc;            /* For destination */

    P_STA_RECORD_T      prTargetStaRec;             /* For JOIN Abort */

    UINT_32             u4SleepInterval;

    TIMER_T             rBGScanTimer;

    TIMER_T             rIbssAloneTimer;

    TIMER_T             rIndicationOfDisconnectTimer;

    TIMER_T             rJoinTimeoutTimer;

    UINT_8              ucSeqNumOfReqMsg;
    UINT_8              ucSeqNumOfChReq;
    UINT_8              ucSeqNumOfScanReq;

    UINT_32             u4ChGrantedInterval;

    UINT_8              ucConnTrialCount;

    UINT_8              ucScanSSIDLen;
    UINT_8              aucScanSSID[ELEM_MAX_LEN_SSID];

    /* Pending Request List */
    LINK_T              rPendingReqList;

} AIS_FSM_INFO_T, *P_AIS_FSM_INFO_T;


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
#define aisChangeMediaState(_prAdapter, _eNewMediaState) \
            (_prAdapter->rWifiVar.arBssInfo[NETWORK_TYPE_AIS_INDEX].eConnectionState = (_eNewMediaState));


/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
VOID
aisInitializeConnectionSettings (
    IN P_ADAPTER_T prAdapter,
    IN P_REG_INFO_T prRegInfo
    );

VOID
aisFsmInit (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmUninit (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmStateInit_JOIN (
    IN P_ADAPTER_T prAdapter,
    P_BSS_DESC_T prBssDesc
    );

BOOLEAN
aisFsmStateInit_RetryJOIN (
    IN P_ADAPTER_T      prAdapter,
    IN P_STA_RECORD_T   prStaRec
    );

VOID
aisFsmStateInit_IBSS_ALONE (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmStateInit_IBSS_MERGE (
    IN P_ADAPTER_T prAdapter,
    P_BSS_DESC_T prBssDesc
    );

VOID
aisFsmStateAbort (
    IN P_ADAPTER_T prAdapter,
    UINT_8         ucReasonOfDisconnect,
    BOOLEAN        fgDelayIndication
    );

VOID
aisFsmStateAbort_JOIN (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmStateAbort_SCAN (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmStateAbort_NORMAL_TR (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmStateAbort_IBSS (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmSteps (
    IN P_ADAPTER_T prAdapter,
    ENUM_AIS_STATE_T eNextState
    );

/*----------------------------------------------------------------------------*/
/* Mailbox Message Handling                                                   */
/*----------------------------------------------------------------------------*/
VOID
aisFsmRunEventScanDone (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
aisFsmRunEventAbort (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
aisFsmRunEventJoinComplete (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
aisFsmRunEventFoundIBSSPeer (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

/*----------------------------------------------------------------------------*/
/* Handling for Ad-Hoc Network                                                */
/*----------------------------------------------------------------------------*/
VOID
aisFsmCreateIBSS (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisFsmMergeIBSS (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

/*----------------------------------------------------------------------------*/
/* Handling of Incoming Mailbox Message from CNM                              */
/*----------------------------------------------------------------------------*/
VOID
aisFsmRunEventChGrant (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );


/*----------------------------------------------------------------------------*/
/* Generating Outgoing Mailbox Message to CNM                                 */
/*----------------------------------------------------------------------------*/
VOID
aisFsmReleaseCh (
    IN P_ADAPTER_T prAdapter
    );


/*----------------------------------------------------------------------------*/
/* Event Indication                                                           */
/*----------------------------------------------------------------------------*/
VOID
aisIndicationOfMediaStateToHost (
    IN P_ADAPTER_T prAdapter,
    ENUM_PARAM_MEDIA_STATE_T eConnectionState,
    BOOLEAN fgDelayIndication
    );

VOID
aisPostponedEventOfDisconnTimeout (
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4Param
    );

VOID
aisUpdateBssInfoForJOIN (
    IN P_ADAPTER_T prAdapter,
    P_STA_RECORD_T prStaRec,
    P_SW_RFB_T prAssocRspSwRfb
    );

VOID
aisUpdateBssInfoForCreateIBSS (
    IN P_ADAPTER_T prAdapter
    );

VOID
aisUpdateBssInfoForMergeIBSS (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

BOOLEAN
aisValidateProbeReq (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    OUT PUINT_32 pu4ControlFlags
    );

/*----------------------------------------------------------------------------*/
/* Disconnection Handling                                                     */
/*----------------------------------------------------------------------------*/
VOID
aisFsmDisconnect (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN     fgDelayIndication
    );


/*----------------------------------------------------------------------------*/
/* Event Handling                                                             */
/*----------------------------------------------------------------------------*/
VOID
aisBssBeaconTimeout (
    IN P_ADAPTER_T prAdapter
    );

WLAN_STATUS
aisDeauthXmitComplete (
    IN P_ADAPTER_T              prAdapter,
    IN P_MSDU_INFO_T            prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T    rTxDoneStatus
    );

#if CFG_SUPPORT_ROAMING
VOID
aisFsmRunEventRoamingDiscovery (
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4ReqScan
    );
    
ENUM_AIS_STATE_T
aisFsmRoamingScanResultsUpdate (
    IN P_ADAPTER_T prAdapter    
    );

VOID
aisFsmRoamingDisconnectPrevAP (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prTargetStaRec
    );

VOID
aisUpdateBssInfoForRoamingAP (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN P_SW_RFB_T prAssocRspSwRfb
    );
#endif /*CFG_SUPPORT_ROAMING */

/*----------------------------------------------------------------------------*/
/* Timeout Handling                                                           */
/*----------------------------------------------------------------------------*/
VOID
aisFsmRunEventBGSleepTimeOut (
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4Param
    );

VOID
aisFsmRunEventIbssAloneTimeOut (
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4Param
    );

VOID
aisFsmRunEventJoinTimeout (
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4Param
    );

/*----------------------------------------------------------------------------*/
/* OID/IOCTL Handling                                                         */
/*----------------------------------------------------------------------------*/
VOID
aisFsmScanRequest (
    IN P_ADAPTER_T prAdapter,
    IN P_PARAM_SSID_T prSsid
    );

/*----------------------------------------------------------------------------*/
/* Internal State Checking                                                    */
/*----------------------------------------------------------------------------*/
BOOLEAN
aisFsmIsRequestPending (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_AIS_REQUEST_TYPE_T eReqType,
    IN BOOLEAN bRemove
    );

P_AIS_REQ_HDR_T
aisFsmGetNextRequest (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
aisFsmInsertRequest (
    IN P_ADAPTER_T prAdapter,
    IN ENUM_AIS_REQUEST_TYPE_T eReqType
    );

VOID
aisFsmFlushRequest (
    IN P_ADAPTER_T prAdapter
    );


#if defined(CFG_TEST_MGMT_FSM) && (CFG_TEST_MGMT_FSM != 0)
VOID
aisTest (
    VOID
    );
#endif /* CFG_TEST_MGMT_FSM */
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _AIS_FSM_H */




