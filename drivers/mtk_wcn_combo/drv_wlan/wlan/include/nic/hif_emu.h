/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/include/nic/hif_emu.h#1 $
*/
/*! \file   hif.h"
    \brief  Sdio specific structure for GLUE layer on WinXP

    Sdio specific structure for GLUE layer on WinXP
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
** $Log: hif_emu.h $
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-11-04 14:11:18 GMT mtk01084
**  add new test func
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-10-30 18:17:42 GMT mtk01084
**  modify return value
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-10-29 19:50:45 GMT mtk01084
**  add emu test cases
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-10-23 16:08:54 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-10-23 16:08:16 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\1 2009-10-13 21:27:05 GMT mtk01084
**
*/

#ifndef _HIF_EMU_H
#define _HIF_EMU_H

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
typedef enum
{
    HIF_TC_MBOX_LB = 0x100,
    HIF_TC_SW_INT,
    HIF_TC_COUNT_INCREASE,
    HIF_TC_COUNT_RESET,
    HIF_TC_TX_SINGLE_PACKET,
    HIF_TC_TX_AGG_PACKET,
    HIF_TC_TX_CLEAR_TC_COUNT,
    HIF_TC_TX_CHK_STATE,
    HIF_TC_RX_SINGLE_PACKET = 0x200,
    HIF_TC_RX_PACKET_LEN,
    HIF_TC_RX_PACKET_LEN_OVERFLOW,
    HIF_TC_RX_AGG_PACKET,
    HIF_TC_RX_CHK_STATE,
    HIF_TC_RX_SW_PKT_FORMAT,
    HIF_TC_RX_READ_HALF,
    HIF_TC_MIX_TX_RX_STRESS,
    HIF_TC_INTR_ENHANCE,
    HIF_TC_RX_ENHANCE_MODE,
    HIF_TC_TX_BURST,
    HIF_TC_RX_BURST,
} HIF_TEST_CASE;

#define HIF_TEST_CASE_START     BIT(16)


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
VOID
emuInit (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuStart (
    IN P_ADAPTER_T prAdapter
    );


BOOLEAN
emuInitChkCis (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuMailboxLoopback (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgReadClearChk
    );

BOOLEAN
emuSoftwareInterruptLoopback (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgIntrReadClear
    );

BOOLEAN
emuCheckTxCount (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN fgReadByIntrEnhanMode
    );

BOOLEAN
emuSendPacket1 (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32     u4PortId,
    IN BOOLEAN     fgUseEnhanceModeRead
    );

BOOLEAN
emuSendPacketAggN (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32     u4PortId,
    IN UINT_32     u4AggNum,
    IN UINT_32     u4LenStart,
    IN UINT_32     u4LenEnd,
    IN BOOLEAN     fgUseIntrEnhanceModeRead
    );

BOOLEAN
emuReadHalfRxPacket (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuLPown_ownback_stress (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32     u4LoopCount
    );

BOOLEAN
emuLPown_illegal_access (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuIntrEnhanceChk (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuResetTxCount (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuChkTxState (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuChkRxState (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuRxPacket1 (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32      u4PortId,
    IN UINT_32      u4RxLen,
    IN BOOLEAN fgEnIntrEnhanceMode,
    IN BOOLEAN fgEnRxEnhanceMode,
    IN BOOLEAN fgMBoxReadClearByRxEnhance
    );

BOOLEAN
emuRxPacketLenChk (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuRxPacketAggN (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32      u4PortId,
    IN BOOLEAN fgEnRxEnhanceMode,
    IN UINT_32 u4RxLen,
    IN UINT_32 u4AggNum,
    IN UINT_32 u4MaxReadAggNum//0: unlimited
    );

BOOLEAN
emuRxPacketSwHdrFormat (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32      u4PortId,
    IN UINT_32      u4RxLen,
    IN UINT_32      u4Num
    );

BOOLEAN
emuRxPacketLenOverflow (
    IN P_ADAPTER_T prAdapter
    );

BOOLEAN
emuTxPacketBurstInSwHdrFormat (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN      fgEnable
    );

BOOLEAN
emuRxPacketBurstInSwHdrFormat (
    IN P_ADAPTER_T prAdapter,
    IN BOOLEAN      fgEnable
    );

BOOLEAN
emuSendPacketAggNSwHdrFormat (
    IN P_ADAPTER_T prAdapter,
    IN UINT_32     u4PortId,
    IN UINT_32     u4LenStart
    );

#define RUN_TEST_CASE(_Fmt) \
    { \
        if (status == FALSE) { \
            break; \
        } \
        if (_Fmt == FALSE) { \
            status = FALSE; \
            break; \
        } \
    }
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _HIF_EMU_H */



