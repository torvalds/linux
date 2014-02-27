/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/wlan_bow.h#1 $
*/

/*! \file   "wlan_bow.h"
    \brief This file contains the declairations of 802.11 PAL 
           command processing routines for 
           MediaTek Inc. 802.11 Wireless LAN Adapters.
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
** $Log: wlan_bow.h $
 *
 * 05 25 2011 terry.wu
 * [WCXRP00000735] [MT6620 Wi-Fi][BoW][FW/Driver] Protect BoW connection establishment
 * Add BoW Cancel Scan Request and Turn On deactive network function.
 *
 * 05 23 2011 terry.wu
 * [WCXRP00000735] [MT6620 Wi-Fi][BoW][FW/Driver] Protect BoW connection establishment
 * Add some BoW error handling.
 *
 * 05 21 2011 terry.wu
 * [WCXRP00000735] [MT6620 Wi-Fi][BoW][FW/Driver] Protect BoW connection establishment
 * Protect BoW connection establishment.
 *
 * 05 17 2011 terry.wu
 * [WCXRP00000730] [MT6620 Wi-Fi][BoW] Send deauth while disconnecting
 * Send deauth while disconnecting BoW link.
 *
 * 05 06 2011 terry.wu
 * [WCXRP00000707] [MT6620 Wi-Fi][Driver] Fix BoW Multiple Physical Link connect/disconnect issue
 * Fix BoW Multiple Physical Link connect/disconnect issue.
 *
 * 04 15 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Add BOW short range mode.
 *
 * 03 27 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Support multiple physical link.
 *
 * 03 10 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Add BOW table.
 *
 * 02 16 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Add bowNotifyAllLinkDisconnected  interface and change channel grant procedure for bow starting..
 *
 * 02 15 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Update bowString and channel grant.
 *
 * 01 11 2011 chinghwa.yu
 * [WCXRP00000065] Update BoW design and settings
 * Update BOW Activity Report structure and bug fix.
 *
 * 09 27 2010 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000065] Update BoW design and settings
 * Update BCM/BoW design and settings.
 *
 * 09 14 2010 chinghwa.yu
 * NULL
 * Add bowRunEventAAAComplete.
 *
 * 08 24 2010 chinghwa.yu
 * NULL
 * Update BOW for the 1st time.
 *
 * 07 30 2010 cp.wu
 * NULL
 * 1) BoW wrapper: use definitions instead of hard-coded constant for error code
 * 2) AIS-FSM: eliminate use of desired RF parameters, use prTargetBssDesc instead
 * 3) add handling for RX_PKT_DESTINATION_HOST_WITH_FORWARD for GO-broadcast frames
 *
 * 07 15 2010 cp.wu
 * 
 * sync. bluetooth-over-Wi-Fi interface to driver interface document v0.2.6.
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 05 17 2010 cp.wu
 * [WPD00003831][MT6620 Wi-Fi] Add framework for Wi-Fi Direct support 
 * 1) add timeout handler mechanism for pending command packets
 * 2) add p2p add/removal key
 *
 * 05 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * 1) all BT physical handles shares the same RSSI/Link Quality.
 * 2) simplify BT command composing
 *
 * 04 28 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * change prefix for data structure used to communicate with 802.11 PAL
 * to avoid ambiguous naming with firmware interface
 *
 * 04 27 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * add multiple physical link support
 *
 * 04 13 2010 cp.wu
 * [WPD00003823][MT6620 Wi-Fi] Add Bluetooth-over-Wi-Fi support 
 * add framework for BT-over-Wi-Fi support.
 *  *  *  *  *  *  *  *  *  *  *  * 1) prPendingCmdInfo is replaced by queue for multiple handler capability
 *  *  *  *  *  *  *  *  *  *  *  * 2) command sequence number is now increased atomically 
 *  *  *  *  *  *  *  *  *  *  *  * 3) private data could be hold and taken use for other purpose
**
*/

#ifndef _WLAN_BOW_H
#define _WLAN_BOW_H

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "nic/bow.h"
#include "nic/cmd_buf.h"

#if CFG_ENABLE_BT_OVER_WIFI
/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define BOWCMD_STATUS_SUCCESS       0
#define BOWCMD_STATUS_FAILURE       1
#define BOWCMD_STATUS_UNACCEPTED    2
#define BOWCMD_STATUS_INVALID       3
#define BOWCMD_STATUS_TIMEOUT       4

#define BOW_WILDCARD_SSID               "AMP"
#define BOW_WILDCARD_SSID_LEN       3
#define BOW_SSID_LEN                            21

 /* 0: query, 1: setup, 2: destroy */
#define BOW_QUERY_CMD                   0
#define BOW_SETUP_CMD                   1
#define BOW_DESTROY_CMD               2

#define BOW_INITIATOR                   0
#define BOW_RESPONDER                  1

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

typedef struct _BOW_TABLE_T {
    UINT_8                      ucAcquireID;
    BOOLEAN                     fgIsValid;
    ENUM_BOW_DEVICE_STATE       eState;
    UINT_8                      aucPeerAddress[6];
    //UINT_8                      ucRole;
    //UINT_8                      ucChannelNum;
    UINT_16                     u2Reserved;
} BOW_TABLE_T, *P_BOW_TABLE_T;

typedef WLAN_STATUS (*PFN_BOW_CMD_HANDLE)(P_ADAPTER_T, P_AMPC_COMMAND);

typedef struct _BOW_CMD_T {
    UINT_8              uCmdID;
    PFN_BOW_CMD_HANDLE  pfCmdHandle;
} BOW_CMD_T, *P_BOW_CMD_T;

typedef struct _BOW_EVENT_ACTIVITY_REPORT_T {
	UINT_8	ucReason;
	UINT_8	aucReserved;
	UINT_8	aucPeerAddress[6];
} BOW_EVENT_ACTIVITY_REPORT_T, *P_BOW_EVENT_ACTIVITY_REPORT_T;

/*
ucReason:	0: success
	1: general failure
	2: too much time (> 2/3 second totally) requested for scheduling.
	Others: reserved.
*/

typedef struct _BOW_EVENT_SYNC_TSF_T {
    UINT_64     u4TsfTime;
    UINT_32     u4TsfSysTime;
    UINT_32     u4ScoTime;
    UINT_32     u4ScoSysTime;
    } BOW_EVENT_SYNC_TSF_T, *P_BOW_EVENT_SYNC_TSF_T;

typedef struct _BOW_ACTIVITY_REPORT_BODY_T {
    UINT_32		u4StartTime;
    UINT_32		u4Duration;
    UINT_32		u4Periodicity;
    } BOW_ACTIVITY_REPORT_BODY_T, *P_BOW_ACTIVITY_REPORT_BODY_T;

typedef struct _BOW_ACTIVITY_REPORT_T {
    UINT_8		aucPeerAddress[6];
    UINT_8		ucScheduleKnown;
    UINT_8		ucNumReports;
    BOW_ACTIVITY_REPORT_BODY_T arBowActivityReportBody[MAX_ACTIVITY_REPORT];
    } BOW_ACTIVITY_REPORT_T, *P_BOW_ACTIVITY_REPORT_T;

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*--------------------------------------------------------------*/
/* Firmware Command Packer                                      */
/*--------------------------------------------------------------*/
WLAN_STATUS
wlanoidSendSetQueryBowCmd (
    IN P_ADAPTER_T  prAdapter,
    UINT_8          ucCID,
    BOOLEAN         fgSetQuery,
    BOOLEAN         fgNeedResp,
    PFN_CMD_DONE_HANDLER pfCmdDoneHandler,
    PFN_CMD_TIMEOUT_HANDLER pfCmdTimeoutHandler,
    UINT_32         u4SetQueryInfoLen,
    PUINT_8         pucInfoBuffer,
    IN UINT_8       ucSeqNumber
    );


/*--------------------------------------------------------------*/
/* Command Dispatcher                                           */
/*--------------------------------------------------------------*/
WLAN_STATUS
wlanbowHandleCommand(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );


/*--------------------------------------------------------------*/
/* Routines to handle command                                   */
/*--------------------------------------------------------------*/
WLAN_STATUS
bowCmdGetMacStatus(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

WLAN_STATUS
bowCmdSetupConnection(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

WLAN_STATUS
bowCmdDestroyConnection(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

WLAN_STATUS
bowCmdSetPTK(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

WLAN_STATUS
bowCmdReadRSSI(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

WLAN_STATUS
bowCmdReadLinkQuality(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

WLAN_STATUS
bowCmdShortRangeMode(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

WLAN_STATUS
bowCmdGetChannelList(
    IN P_ADAPTER_T      prAdapter,
    IN P_AMPC_COMMAND   prCmd
    );

VOID
wlanbowCmdEventSetStatus(
    IN P_ADAPTER_T  prAdapter,
    IN P_AMPC_COMMAND   prCmd,
    IN UINT_8   ucEventBuf
    );

/*--------------------------------------------------------------*/
/* Callbacks for event indication                               */
/*--------------------------------------------------------------*/
VOID
wlanbowCmdEventSetCommon (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
wlanbowCmdEventLinkConnected (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
wlanbowCmdEventLinkDisconnected (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
wlanbowCmdEventSetSetupConnection (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
wlanbowCmdEventReadLinkQuality (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
wlanbowCmdEventReadRssi (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

VOID
wlanbowCmdTimeoutHandler (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    );

VOID
bowStopping(
    IN P_ADAPTER_T prAdapter);

VOID
bowStarting (
    IN P_ADAPTER_T prAdapter
    );

VOID
bowAssignSsid (
    IN PUINT_8 pucSsid,
    IN PUINT_8 pucSsidLen
    );

BOOLEAN
bowValidateProbeReq (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    OUT PUINT_32 pu4ControlFlags
    );

VOID
bowSendBeacon(
    IN P_ADAPTER_T prAdapter,
    UINT_32 u4Param
    );

VOID
bowResponderScan(
    IN P_ADAPTER_T prAdapter
    );

VOID
bowResponderScanDone(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
bowResponderCancelScan (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIsChannelExtention
    );

VOID
bowResponderJoin(
    IN P_ADAPTER_T prAdapter,
    P_BSS_DESC_T prBssDesc
    );

VOID
bowFsmRunEventJoinComplete(
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
bowIndicationOfMediaStateToHost(
    IN P_ADAPTER_T prAdapter,
    ENUM_PARAM_MEDIA_STATE_T eConnectionState,
    BOOLEAN fgDelayIndication
    );

VOID
bowRunEventAAATxFail(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

WLAN_STATUS
bowRunEventAAAComplete(
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec
    );

WLAN_STATUS
bowRunEventRxDeAuth (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN P_SW_RFB_T prSwRfb
    );

VOID
bowDisconnectLink (
    IN P_ADAPTER_T            prAdapter,
    IN P_MSDU_INFO_T          prMsduInfo,
    IN ENUM_TX_RESULT_CODE_T  rTxDoneStatus
    );

BOOLEAN
bowValidateAssocReq(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    OUT PUINT_16 pu2StatusCode
    );

BOOLEAN
bowValidateAuth(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN PP_STA_RECORD_T pprStaRec,
    OUT PUINT_16 pu2StatusCode
    );

VOID
bowRunEventChGrant (
    IN P_ADAPTER_T prAdapter,
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
bowRequestCh (
    IN P_ADAPTER_T prAdapter
    );

VOID
bowReleaseCh (
    IN P_ADAPTER_T prAdapter
    );

VOID
bowChGrantedTimeout(
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Param
    );

BOOLEAN
bowNotifyAllLinkDisconnected (
    IN P_ADAPTER_T     prAdapter
    );

BOOLEAN
bowCheckBowTableIfVaild(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8   aucPeerAddress[6]
    );

BOOLEAN
bowGetBowTableContent(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8                   ucBowTableIdx,
    OUT P_BOW_TABLE_T  prBowTable
    );

BOOLEAN
bowGetBowTableEntryByPeerAddress(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8   aucPeerAddress[6],
    OUT PUINT_8 pucBowTableIdx
    );

BOOLEAN
bowGetBowTableFreeEntry(
    IN P_ADAPTER_T prAdapter,
    OUT PUINT_8 pucBowTableIdx
    );

ENUM_BOW_DEVICE_STATE
bowGetBowTableState(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8   aucPeerAddress[6]
    );

BOOLEAN
bowSetBowTableState(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8   aucPeerAddress[6],
    IN ENUM_BOW_DEVICE_STATE eState
    );


BOOLEAN
bowSetBowTableContent(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8                    ucBowTableIdx,
    IN P_BOW_TABLE_T      prBowTable
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif
#endif /* _WLAN_BOW_H */

