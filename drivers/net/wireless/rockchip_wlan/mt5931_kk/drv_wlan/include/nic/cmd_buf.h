/*
** $Id:
*/

/*! \file   "cmd_buf.h"
    \brief  In this file we define the structure for Command Packet.

		In this file we define the structure for Command Packet and the control unit
    of MGMT Memory Pool.
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
** $Log: cmd_buf.h $
 *
 * 10 04 2010 cp.wu
 * [WCXRP00000077] [MT6620 Wi-Fi][Driver][FW] Eliminate use of ENUM_NETWORK_TYPE_T and replaced by ENUM_NETWORK_TYPE_INDEX_T only
 * remove ENUM_NETWORK_TYPE_T definitions
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 07 13 2010 cp.wu
 * 
 * 1) MMPDUs are now sent to MT6620 by CMD queue for keeping strict order of 1X/MMPDU/CMD packets
 * 2) integrate with qmGetFrameAction() for deciding which MMPDU/1X could pass checking for sending
 * 2) enhance CMD_INFO_T descriptor number from 10 to 32 to avoid descriptor underflow under concurrent network operation
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 01 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver 
 * Enable change log
*/

#ifndef _CMD_BUF_H
#define _CMD_BUF_H

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

typedef enum _COMMAND_TYPE {
    COMMAND_TYPE_GENERAL_IOCTL,
    COMMAND_TYPE_NETWORK_IOCTL,
    COMMAND_TYPE_SECURITY_FRAME,
    COMMAND_TYPE_MANAGEMENT_FRAME,
    COMMAND_TYPE_NUM
} COMMAND_TYPE, *P_COMMAND_TYPE;

typedef VOID (*PFN_CMD_DONE_HANDLER) (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo,
    IN PUINT_8      pucEventBuf
    );

typedef VOID (*PFN_CMD_TIMEOUT_HANDLER) (
    IN P_ADAPTER_T  prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    );

struct _CMD_INFO_T {
    QUE_ENTRY_T rQueEntry;

    COMMAND_TYPE    eCmdType;

    UINT_16     u2InfoBufLen;   /* This is actual CMD buffer length */
    PUINT_8     pucInfoBuffer;  /* May pointer to structure in prAdapter */
    P_NATIVE_PACKET prPacket;   /* only valid when it's a security frame */

    ENUM_NETWORK_TYPE_INDEX_T eNetworkType;
    UINT_8      ucStaRecIndex;  /* only valid when it's a security frame */

    PFN_CMD_DONE_HANDLER        pfCmdDoneHandler;
    PFN_CMD_TIMEOUT_HANDLER     pfCmdTimeoutHandler;

    BOOLEAN     fgIsOid; /* Used to check if we need indicate */

    UINT_8      ucCID;
    BOOLEAN     fgSetQuery;
    BOOLEAN     fgNeedResp;
    BOOLEAN     fgDriverDomainMCR; /* Access Driver Domain MCR, for CMD_ID_ACCESS_REG only */
    UINT_8      ucCmdSeqNum;
    UINT_32     u4SetInfoLen; /* Indicate how many byte we read for Set OID */

    /* information indicating by OID/ioctl */
    PVOID       pvInformationBuffer;
    UINT_32     u4InformationBufferLength;

    /* private data */
    UINT_32     u4PrivateData;
};


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
cmdBufInitialize (
    IN P_ADAPTER_T prAdapter
    );

P_CMD_INFO_T
cmdBufAllocateCmdInfo (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32 u4Length
    );

VOID
cmdBufFreeCmdInfo (
    IN P_ADAPTER_T prAdapter,
    IN P_CMD_INFO_T prCmdInfo
    );

/*----------------------------------------------------------------------------*/
/* Routines for CMDs                                                          */
/*----------------------------------------------------------------------------*/
WLAN_STATUS
wlanSendSetQueryCmd (
    IN P_ADAPTER_T  prAdapter,
    UINT_8          ucCID,
    BOOLEAN         fgSetQuery,
    BOOLEAN         fgNeedResp,
    BOOLEAN         fgIsOid,
    PFN_CMD_DONE_HANDLER pfCmdDoneHandler,
    PFN_CMD_TIMEOUT_HANDLER pfCmdTimeoutHandler,
    UINT_32         u4SetQueryInfoLen,
    PUINT_8         pucInfoBuffer,
    OUT PVOID       pvSetQueryBuffer,
    IN UINT_32      u4SetQueryBufferLen
    );


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _CMD_BUF_H */


