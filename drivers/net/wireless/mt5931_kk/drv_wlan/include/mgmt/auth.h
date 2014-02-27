/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/mgmt/auth.h#1 $
*/

/*! \file  auth.h
    \brief This file contains the authentication REQ/RESP of
           IEEE 802.11 family for MediaTek 802.11 Wireless LAN Adapters.
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
** $Log: auth.h $
 *
 * 04 21 2011 terry.wu
 * [WCXRP00000674] [MT6620 Wi-Fi][Driver] Refine AAA authSendAuthFrame
 * Add network type parameter to authSendAuthFrame.
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 14 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add management dispatching function table.
 *
 * 06 11 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * auth.c is migrated.
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add buildable & linkable ais_fsm.c
 * 
 * related reference are still waiting to be resolved
 *
*/

#ifndef _AUTH_H
#define _AUTH_H

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

/*******************************************************************************
*                         D A T A   T Y P E S
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

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Routines in auth.c                                                         */
/*----------------------------------------------------------------------------*/
VOID
authAddIEChallengeText (
    IN P_ADAPTER_T prAdapter,
    IN OUT P_MSDU_INFO_T prMsduInfo
    );

#if !CFG_SUPPORT_AAA
WLAN_STATUS
authSendAuthFrame (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN UINT_16 u2TransactionSeqNum
    );
#else
WLAN_STATUS
authSendAuthFrame (
    IN P_ADAPTER_T prAdapter,
    IN P_STA_RECORD_T prStaRec,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_SW_RFB_T prFalseAuthSwRfb,
    IN UINT_16 u2TransactionSeqNum,
    IN UINT_16 u2StatusCode
    );
#endif /* CFG_SUPPORT_AAA */

WLAN_STATUS
authCheckTxAuthFrame (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN UINT_16 u2TransactionSeqNum
    );

WLAN_STATUS
authCheckRxAuthFrameTransSeq (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
authCheckRxAuthFrameStatus (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN UINT_16 u2TransactionSeqNum,
    OUT PUINT_16 pu2StatusCode
    );

VOID
authHandleIEChallengeText (
    P_ADAPTER_T prAdapter,
    P_SW_RFB_T prSwRfb,
    P_IE_HDR_T prIEHdr
    );

WLAN_STATUS
authProcessRxAuth2_Auth4Frame (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb
    );

WLAN_STATUS
authSendDeauthFrame (
    IN P_ADAPTER_T          prAdapter,
    IN P_STA_RECORD_T       prStaRec,
    IN P_SW_RFB_T           prClassErrSwRfb,
    IN UINT_16              u2ReasonCode,
    IN PFN_TX_DONE_HANDLER  pfTxDoneHandler
    );

WLAN_STATUS
authProcessRxDeauthFrame (
    IN P_SW_RFB_T prSwRfb,
    IN UINT_8 aucBSSID[],
    OUT PUINT_16 pu2ReasonCode
    );

WLAN_STATUS
authProcessRxAuth1Frame (
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN UINT_8 aucExpectedBSSID[],
    IN UINT_16 u2ExpectedAuthAlgNum,
    IN UINT_16 u2ExpectedTransSeqNum,
    OUT PUINT_16 pu2ReturnStatusCode
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _AUTH_H */

