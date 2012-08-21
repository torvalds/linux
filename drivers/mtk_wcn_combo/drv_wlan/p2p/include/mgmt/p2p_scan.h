/*
** $Id: //Department/DaVinci/BRANCHES/WIFI_P2P_DRIVER_V2_2/include/mgmt/p2p_scan.h#2 $
*/

/*! \file   "scan.h"
    \brief

*/


/*******************************************************************************
* Copyright (c) 2010 MediaTek Inc.
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

#ifndef _P2P_SCAN_H
#define _P2P_SCAN_H

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


WLAN_STATUS
scanSendDeviceDiscoverEvent (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc,
    IN P_SW_RFB_T prSwRfb
    );

P_P2P_DEVICE_DESC_T
scanSearchTargetP2pDesc(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 aucDeviceID[],
    IN PP_BSS_DESC_T pprBssDesc
    );

P_P2P_DEVICE_DESC_T
scanFindP2pDeviceDesc(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc,
    IN UINT_8 aucMacAddr[],
    IN BOOLEAN fgIsDeviceAddr,
    IN BOOLEAN fgAddIfNoFound
    );

P_P2P_DEVICE_DESC_T
scanGetP2pDeviceDesc(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    );

VOID
scnEventReturnChannel(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucScnSeqNum
    );

BOOLEAN
scanUpdateP2pDeviceDesc (
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_DESC_T prBssDesc
    );

VOID
scanP2pProcessBeaconAndProbeResp(
    IN P_ADAPTER_T prAdapter,
    IN P_SW_RFB_T prSwRfb,
    IN P_WLAN_STATUS prStatus,
    IN P_BSS_DESC_T prBssDesc,
    IN P_WLAN_BEACON_FRAME_T prWlanBeaconFrame
    );

VOID
scanRemoveAllP2pBssDesc(
    P_ADAPTER_T prAdapter
    );

VOID
scanRemoveP2pBssDesc(
    P_ADAPTER_T prAdapter,
    P_BSS_DESC_T prBssDesc
    );


P_BSS_DESC_T
scanP2pSearchDesc(
    IN P_ADAPTER_T prAdapter,
    IN P_BSS_INFO_T prP2pBssInfo,
    IN P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo
    );



#endif
