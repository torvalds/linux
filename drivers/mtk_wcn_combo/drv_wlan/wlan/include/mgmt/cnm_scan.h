/*
** $Id: @(#)
*/

/*! \file   "cnm_scan.h"
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
** $Log: cnm_scan.h $
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 07 05 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * remove unused definitions.
 *
 * 07 01 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * implementation of DRV-SCN and related mailbox message handling.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * merge cnm_scan.h and hem_mbox.h
 *
 * 05 12 2010 kevin.huang
 * [BORA00000794][WIFISYS][New Feature]Power Management Support 
 * Add Power Management - Legacy PS-POLL support.
 *
 * 03 30 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time 
 * Support 2.4G OBSS scan
 *
 * 03 16 2010 kevin.huang
 * [BORA00000663][WIFISYS][New Feature] AdHoc Mode Support
 * Add AdHoc Mode
 *
 * 03 10 2010 kevin.huang
 * [BORA00000654][WIFISYS][New Feature] CNM Module - Ch Manager Support
 *
 *  *  *  * Add Channel Manager for arbitration of JOIN and SCAN Req
 *
 * 02 23 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add support scan channel 1~14 and update scan result's frequency infou1rwduu`wvpghlqg|n`slk+mpdkb
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * Nov 18 2009 mtk01104
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * Add function prototype of cnmScanInit()
 *
 * Nov 5 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 *
**
*/

#ifndef _CNM_SCAN_H
#define _CNM_SCAN_H

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
#define SCN_CHANNEL_DWELL_TIME_MIN_MSEC         12
#define SCN_CHANNEL_DWELL_TIME_EXT_MSEC         98

#define SCN_TOTAL_PROBEREQ_NUM_FOR_FULL         3
#define SCN_SPECIFIC_PROBEREQ_NUM_FOR_FULL      1

#define SCN_TOTAL_PROBEREQ_NUM_FOR_PARTIAL      2
#define SCN_SPECIFIC_PROBEREQ_NUM_FOR_PARTIAL   1


#define SCN_INTERLACED_CHANNEL_GROUPS_NUM       3   /* Used by partial scan */

#define SCN_PARTIAL_SCAN_NUM                    3

#define SCN_PARTIAL_SCAN_IDLE_MSEC              100

#define	MAXIMUM_OPERATION_CHANNEL_LIST	        32

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* The type of Scan Source */
typedef enum _ENUM_SCN_REQ_SOURCE_T {
    SCN_REQ_SOURCE_HEM = 0,
    SCN_REQ_SOURCE_NET_FSM,
    SCN_REQ_SOURCE_ROAMING, /* ROAMING Module is independent of AIS FSM */
    SCN_REQ_SOURCE_OBSS,    /* 2.4G OBSS scan */
    SCN_REQ_SOURCE_NUM
} ENUM_SCN_REQ_SOURCE_T, *P_ENUM_SCN_REQ_SOURCE_T;

typedef enum _ENUM_SCAN_PROFILE_T {
    SCAN_PROFILE_FULL = 0,
    SCAN_PROFILE_PARTIAL,
    SCAN_PROFILE_VOIP,
    SCAN_PROFILE_FULL_2G4,
    SCAN_PROFILE_NUM
} ENUM_SCAN_PROFILE_T, *P_ENUM_SCAN_PROFILE_T;

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

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#if 0
VOID
cnmScanInit (
    VOID
    );

VOID
cnmScanRunEventScanRequest (
    IN P_MSG_HDR_T prMsgHdr
    );

BOOLEAN
cnmScanRunEventScanAbort (
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
cnmScanProfileSelection (
    VOID
    );

VOID
cnmScanProcessStart (
    VOID
    );

VOID
cnmScanProcessStop (
    VOID
    );

VOID
cnmScanRunEventReqAISAbsDone (
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
cnmScanRunEventCancelAISAbsDone (
    IN P_MSG_HDR_T prMsgHdr
    );

VOID
cnmScanPartialScanTimeout (
    UINT_32 u4Param
    );

VOID
cnmScanRunEventScnFsmComplete (
    IN P_MSG_HDR_T prMsgHdr
    );
#endif



#endif /* _CNM_SCAN_H */


