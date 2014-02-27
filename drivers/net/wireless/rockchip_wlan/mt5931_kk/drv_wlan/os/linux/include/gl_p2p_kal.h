/*
** $Id: //Department/DaVinci/TRUNK/WiFi_P2P_Driver/os/linux/include/gl_p2p_kal.h#2 $
*/

/*! \file   gl_p2p_kal.h
    \brief  Declaration of KAL functions for Wi-Fi Direct support
            - kal*() which is provided by GLUE Layer.

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
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
** $Log: gl_p2p_kal.h $
** 
** 08 24 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** .
 *
 * 07 17 2012 yuche.tsai
 * NULL
 * Compile no error before trial run.
 *
 * 10 18 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * New 2.1 branch

 *
 * 08 15 2011 yuche.tsai
 * [WCXRP00000919] [Volunteer Patch][WiFi Direct][Driver] Invitation New Feature.
 * Add group BSSID in invitation request indication.
 * The BSSID is used for APP to decide the configure method.
 *
 * 08 09 2011 yuche.tsai
 * [WCXRP00000919] [Volunteer Patch][WiFi Direct][Driver] Invitation New Feature.
 * Invitation Feature add on.
 *
 * 03 19 2011 terry.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 p2p driver release based on label "MT6620_WIFI_P2P_DRIVER_V2_0_2100_0319_2011" from main trunk.
 *
 * 03 07 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * rename the define to anti_pviracy.
 *
 * 03 05 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * add the code to get the check rsponse and indicate to app.
 *
 * 03 02 2011 wh.su
 * [WCXRP00000506] [MT6620 Wi-Fi][Driver][FW] Add Security check related code
 * Add Security check related code.
 *
 * 12 22 2010 cp.wu
 * [WCXRP00000283] [MT6620 Wi-Fi][Driver][Wi-Fi Direct] Implementation of interface for supporting Wi-Fi Direct Service Discovery
 * 1. header file restructure for more clear module isolation
 * 2. add function interface definition for implementing Service Discovery callbacks
 *
*/

#ifndef _GL_P2P_KAL_H
#define _GL_P2P_KAL_H


/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "config.h"
#include "gl_typedef.h"
#include "gl_os.h"
#include "wlan_lib.h"
#include "wlan_oid.h"
#include "wlan_p2p.h"
#include "gl_kal.h"
#include "gl_wext_priv.h"
#include "gl_p2p_ioctl.h"
#include "nic/p2p.h"


#if DBG
    extern int allocatedMemSize;
#endif

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
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

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/* Service Discovery */
VOID
kalP2PIndicateSDRequest(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN PARAM_MAC_ADDRESS    rPeerAddr,
    IN UINT_8 ucSeqNum
    );

void
kalP2PIndicateSDResponse(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN PARAM_MAC_ADDRESS    rPeerAddr,
    IN UINT_8 ucSeqNum
    );

VOID
kalP2PIndicateTXDone(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN UINT_8               ucSeqNum,
    IN UINT_8               ucStatus
    );

/*----------------------------------------------------------------------------*/
/* Wi-Fi Direct handling                                                      */
/*----------------------------------------------------------------------------*/
ENUM_PARAM_MEDIA_STATE_T
kalP2PGetState (
    IN P_GLUE_INFO_T        prGlueInfo
    );

VOID
kalP2PSetState (
    IN P_GLUE_INFO_T            prGlueInfo,
    IN ENUM_PARAM_MEDIA_STATE_T eState,
    IN PARAM_MAC_ADDRESS        rPeerAddr,
    IN UINT_8                   ucRole
    );

VOID
kalP2PUpdateAssocInfo(
    IN P_GLUE_INFO_T    prGlueInfo,
    IN PUINT_8          pucFrameBody,
    IN UINT_32          u4FrameBodyLen,
    IN BOOLEAN          fgReassocRequest
    );

UINT_32
kalP2PGetFreqInKHz(
    IN P_GLUE_INFO_T prGlueInfo
    );

UINT_8
kalP2PGetRole(
    IN P_GLUE_INFO_T    prGlueInfo
    );

VOID
kalP2PSetRole(
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucResult,
    IN PUINT_8          pucSSID,
    IN UINT_8           ucSSIDLen,
    IN UINT_8           ucRole
    );

VOID
kalP2PSetCipher(
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_32          u4Cipher
    );

BOOLEAN
kalP2PGetCipher(
    IN P_GLUE_INFO_T    prGlueInfo
    );

BOOLEAN
kalP2PGetTkipCipher(
    IN P_GLUE_INFO_T    prGlueInfo
    );


BOOLEAN
kalP2PGetCcmpCipher(
    IN P_GLUE_INFO_T    prGlueInfo
    );


VOID
kalP2PSetWscMode (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucWscMode
    );

UINT_8
kalP2PGetWscMode(
    IN P_GLUE_INFO_T    prGlueInfo
    );

UINT_16
kalP2PCalWSC_IELen(
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucType
    );

VOID
kalP2PGenWSC_IE(
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_8           ucType,
    IN PUINT_8          pucBuffer
    );


VOID
kalP2PUpdateWSC_IE(
    IN P_GLUE_INFO_T prGlueInfo,
    IN UINT_8 ucType,
    IN PUINT_8 pucBuffer,
    IN UINT_16 u2BufferLength
    );



BOOLEAN
kalP2PIndicateFound(
    IN P_GLUE_INFO_T    prGlueInfo
    );

VOID
kalP2PIndicateConnReq(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN PUINT_8              pucDevName,
    IN INT_32               u4NameLength,
    IN PARAM_MAC_ADDRESS    rPeerAddr,
    IN UINT_8               ucDevType, /* 0: P2P Device / 1: GC / 2: GO */
    IN INT_32               i4ConfigMethod,
    IN INT_32               i4ActiveConfigMethod
    );

VOID
kalP2PInvitationStatus (
    IN P_GLUE_INFO_T prGlueInfo,
    IN UINT_32       u4InvStatus
    );

VOID
kalP2PInvitationIndication(
        IN P_GLUE_INFO_T prGlueInfo,
        IN P_P2P_DEVICE_DESC_T prP2pDevDesc,
        IN PUINT_8 pucSsid,
        IN UINT_8 ucSsidLen,
        IN UINT_8 ucOperatingChnl,
        IN UINT_8 ucInvitationType,
        IN PUINT_8 pucGroupBssid
        );


struct net_device*
kalP2PGetDevHdlr(
    P_GLUE_INFO_T prGlueInfo
    );

VOID
kalGetChnlList(
    IN P_GLUE_INFO_T        prGlueInfo,
    IN ENUM_BAND_T          eSpecificBand,
    IN UINT_8               ucMaxChannelNum,
    IN PUINT_8              pucNumOfChannel,
    IN P_RF_CHANNEL_INFO_T  paucChannelList
    );

#if CFG_SUPPORT_ANTI_PIRACY
VOID
kalP2PIndicateSecCheckRsp(
    IN P_GLUE_INFO_T prGlueInfo,
    IN PUINT_8       pucRsp,
    IN UINT_16       u2RspLen
    );
#endif

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

VOID
kalP2PIndicateChannelReady(
    IN P_GLUE_INFO_T prGlueInfo,
    IN UINT_64 u8SeqNum,
    IN UINT_32 u4ChannelNum,
    IN ENUM_BAND_T eBand,
    IN ENUM_CHNL_EXT_T eSco,
    IN UINT_32 u4Duration
    );

VOID
kalP2PIndicateScanDone(
    IN P_GLUE_INFO_T prGlueInfo,
    IN BOOLEAN fgIsAbort
    );

VOID
kalP2PIndicateBssInfo(
    IN P_GLUE_INFO_T prGlueInfo,
    IN PUINT_8 pucFrameBuf,
    IN UINT_32 u4BufLen,
    IN P_RF_CHANNEL_INFO_T prChannelInfo,
    IN INT_32 i4SignalStrength
    );

VOID
kalP2PIndicateRxMgmtFrame(
        IN P_GLUE_INFO_T prGlueInfo,
        IN P_SW_RFB_T prSwRfb
        );

VOID
kalP2PIndicateMgmtTxStatus(
        IN P_GLUE_INFO_T prGlueInfo,
        IN UINT_64 u8Cookie,
        IN BOOLEAN fgIsAck,
        IN PUINT_8 pucFrameBuf,
        IN UINT_32 u4FrameLen
        );

VOID
kalP2PIndicateChannelExpired(
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_P2P_CHNL_REQ_INFO_T prChnlReqInfo
    );

VOID
kalP2PGCIndicateConnectionStatus(
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_P2P_CONNECTION_REQ_INFO_T prP2pConnInfo,
    IN PUINT_8 pucRxIEBuf,
    IN UINT_16 u2RxIELen,
    IN UINT_16 u2StatusReason
    );


VOID
kalP2PGOStationUpdate(
    IN P_GLUE_INFO_T prGlueInfo,
    IN P_STA_RECORD_T prCliStaRec,
    IN BOOLEAN fgIsNew
    );

BOOLEAN
kalP2PSetBlackList (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN PARAM_MAC_ADDRESS rbssid,
    IN BOOLEAN fgIsblock
    );

BOOLEAN
kalP2PCmpBlackList (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN PARAM_MAC_ADDRESS rbssid
    );

VOID
kalP2PSetMaxClients (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_32 u4MaxClient
    );

BOOLEAN
kalP2PMaxClients (
    IN P_GLUE_INFO_T    prGlueInfo,
    IN UINT_32 u4NumClient
    );

#endif /* _GL_P2P_KAL_H */

