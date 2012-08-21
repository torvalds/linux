/*
** $Id: //Department/DaVinci/BRANCHES/WIFI_P2P_DRIVER_V2_2/include/mgmt/p2p_rlm.h#1 $
*/

/*! \file   "rlm.h"
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


#ifndef _P2P_RLM_H
#define _P2P_RLM_H


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


VOID
rlmBssInitForAP(
    P_ADAPTER_T  prAdapter,
    P_BSS_INFO_T prBssInfo
    );

BOOLEAN
rlmUpdateBwByChListForAP (
    P_ADAPTER_T         prAdapter,
    P_BSS_INFO_T        prBssInfo
    );

VOID
rlmUpdateParamsForAP (
    P_ADAPTER_T     prAdapter,
    P_BSS_INFO_T    prBssInfo,
    BOOLEAN         fgUpdateBeacon
    );

VOID
rlmFuncInitialChannelList(
    IN P_ADAPTER_T prAdapter
    );

VOID
rlmFuncCommonChannelList(
    IN P_ADAPTER_T prAdapter,
    IN P_CHANNEL_ENTRY_FIELD_T prChannelEntryII,
    IN UINT_8 ucChannelListSize
    );

UINT_8
rlmFuncFindOperatingClass(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucChannelNum
    );

BOOLEAN
rlmFuncFindAvailableChannel(
    IN P_ADAPTER_T prAdapter,
    IN UINT_8 ucCheckChnl,
    IN PUINT_8 pucSuggestChannel,
    IN BOOLEAN fgIsSocialChannel,
    IN BOOLEAN fgIsDefaultChannel
    );

ENUM_CHNL_EXT_T
rlmDecideScoForAP (
    P_ADAPTER_T     prAdapter,
    P_BSS_INFO_T    prBssInfo
    );

#endif
