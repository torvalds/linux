/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/mgmt/cnm.h#1 $
*/

/*! \file   "cnm.h"
    \brief
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
** $Log: cnm.h $
 *
 * 06 23 2011 cp.wu
 * [WCXRP00000798] [MT6620 Wi-Fi][Firmware] Follow-ups for WAPI frequency offset workaround in firmware SCN module
 * follow-ups for frequency-shifted WAPI AP support
 *
 * 04 12 2011 cm.chang
 * [WCXRP00000634] [MT6620 Wi-Fi][Driver][FW] 2nd BSS will not support 40MHz bandwidth for concurrency
 * .
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 10 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Add some functions to let AIS/Tethering or AIS/BOW be the same channel
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Provide function to decide if BSS can be activated or not
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000239] MT6620 Wi-Fi][Driver][FW] Merge concurrent branch back to maintrunk
 * 1. BSSINFO include RLM parameter
 * 2. free all sta records when network is disconnected
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 07 19 2010 cm.chang
 *
 * Set RLM parameters and enable CNM channel manager
 *
 * 07 13 2010 cm.chang
 *
 * Rename MSG_CH_RELEASE_T to MSG_CH_ABORT_T
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 08 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Rename MID_MNY_CNM_CH_RELEASE to MID_MNY_CNM_CH_ABORT
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Need bandwidth info when requesting channel privilege
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * Modify CNM message handler for new flow
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support
 * Add Power Management - Legacy PS-POLL support.
 *
 * 05 05 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add a new function to send abort message
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 02 08 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support partial part about cmd basic configuration
 *
 * Nov 18 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add prototype of cnmFsmEventInit()
 *
 * Nov 2 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
**
*/

#ifndef _CNM_H
#define _CNM_H

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
*                             D A T A   T Y P E S
********************************************************************************
*/

typedef enum _ENUM_CH_REQ_TYPE_T {
    CH_REQ_TYPE_JOIN,
    CH_REQ_TYPE_P2P_LISTEN,

    CH_REQ_TYPE_NUM
} ENUM_CH_REQ_TYPE_T, *P_ENUM_CH_REQ_TYPE_T;

typedef struct _MSG_CH_REQ_T {
    MSG_HDR_T           rMsgHdr;    /* Must be the first member */
    UINT_8              ucNetTypeIndex;
    UINT_8              ucTokenID;
    UINT_8              ucPrimaryChannel;
    ENUM_CHNL_EXT_T     eRfSco;
    ENUM_BAND_T         eRfBand;
    ENUM_CH_REQ_TYPE_T  eReqType;
    UINT_32             u4MaxInterval;  /* In unit of ms */
    UINT_8              aucBSSID[6];
    UINT_8              aucReserved[2];
} MSG_CH_REQ_T, *P_MSG_CH_REQ_T;

typedef struct _MSG_CH_ABORT_T {
    MSG_HDR_T           rMsgHdr;    /* Must be the first member */
    UINT_8              ucNetTypeIndex;
    UINT_8              ucTokenID;
} MSG_CH_ABORT_T, *P_MSG_CH_ABORT_T;

typedef struct _MSG_CH_GRANT_T {
    MSG_HDR_T           rMsgHdr;    /* Must be the first member */
    UINT_8              ucNetTypeIndex;
    UINT_8              ucTokenID;
    UINT_8              ucPrimaryChannel;
    ENUM_CHNL_EXT_T     eRfSco;
    ENUM_BAND_T         eRfBand;
    ENUM_CH_REQ_TYPE_T  eReqType;
    UINT_32             u4GrantInterval;    /* In unit of ms */
} MSG_CH_GRANT_T, *P_MSG_CH_GRANT_T;

typedef struct _MSG_CH_REOCVER_T {
    MSG_HDR_T           rMsgHdr;    /* Must be the first member */
    UINT_8              ucNetTypeIndex;
    UINT_8              ucTokenID;
    UINT_8              ucPrimaryChannel;
    ENUM_CHNL_EXT_T     eRfSco;
    ENUM_BAND_T         eRfBand;
    ENUM_CH_REQ_TYPE_T  eReqType;
} MSG_CH_RECOVER_T, *P_MSG_CH_RECOVER_T;


typedef struct _CNM_INFO_T {
    UINT_32     u4Reserved;
} CNM_INFO_T, *P_CNM_INFO_T;

#if CFG_ENABLE_WIFI_DIRECT
/* Moved from p2p_fsm.h */
typedef struct _DEVICE_TYPE_T {
    UINT_16     u2CategoryId;           /* Category ID */
    UINT_8      aucOui[4];              /* OUI */
    UINT_16     u2SubCategoryId;        /* Sub Category ID */
} __KAL_ATTRIB_PACKED__ DEVICE_TYPE_T, *P_DEVICE_TYPE_T;
#endif


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
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
VOID
cnmInit (
    P_ADAPTER_T prAdapter
    );

VOID
cnmUninit (
    P_ADAPTER_T     prAdapter
    );

VOID
cnmChMngrRequestPrivilege (
    P_ADAPTER_T prAdapter,
    P_MSG_HDR_T prMsgHdr
    );

VOID
cnmChMngrAbortPrivilege (
    P_ADAPTER_T prAdapter,
    P_MSG_HDR_T prMsgHdr
    );

VOID
cnmChMngrHandleChEvent (
    P_ADAPTER_T     prAdapter,
    P_WIFI_EVENT_T  prEvent
    );

BOOLEAN
cnmPreferredChannel (
    P_ADAPTER_T         prAdapter,
    P_ENUM_BAND_T       prBand,
    PUINT_8             pucPrimaryChannel,
    P_ENUM_CHNL_EXT_T   prBssSCO
    );

BOOLEAN
cnmAisInfraChannelFixed (
    P_ADAPTER_T         prAdapter,
    P_ENUM_BAND_T       prBand,
    PUINT_8             pucPrimaryChannel
    );

VOID
cnmAisInfraConnectNotify (
    P_ADAPTER_T         prAdapter
    );

BOOLEAN
cnmAisIbssIsPermitted (
    P_ADAPTER_T     prAdapter
    );

BOOLEAN
cnmP2PIsPermitted (
    P_ADAPTER_T     prAdapter
    );

BOOLEAN
cnmBowIsPermitted (
    P_ADAPTER_T     prAdapter
    );

BOOLEAN
cnmBss40mBwPermitted (
    P_ADAPTER_T                 prAdapter,
    ENUM_NETWORK_TYPE_INDEX_T   eNetTypeIdx
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#ifndef _lint
/* We don't have to call following function to inspect the data structure.
 * It will check automatically while at compile time.
 * We'll need this to guarantee the same member order in different structures
 * to simply handling effort in some functions.
 */
__KAL_INLINE__ VOID
cnmMsgDataTypeCheck (
    VOID
    )
{
    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,rMsgHdr) == 0);

    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,rMsgHdr) ==
        OFFSET_OF(MSG_CH_RECOVER_T,rMsgHdr));

    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,ucNetTypeIndex) ==
        OFFSET_OF(MSG_CH_RECOVER_T,ucNetTypeIndex));

    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,ucTokenID) ==
        OFFSET_OF(MSG_CH_RECOVER_T,ucTokenID));

    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,ucPrimaryChannel) ==
        OFFSET_OF(MSG_CH_RECOVER_T,ucPrimaryChannel));

    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,eRfSco) ==
        OFFSET_OF(MSG_CH_RECOVER_T,eRfSco));

    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,eRfBand) ==
        OFFSET_OF(MSG_CH_RECOVER_T,eRfBand));

    DATA_STRUC_INSPECTING_ASSERT(
        OFFSET_OF(MSG_CH_GRANT_T,eReqType) ==
        OFFSET_OF(MSG_CH_RECOVER_T,eReqType));

    return;
}
#endif /* _lint */

#endif /* _CNM_H */


