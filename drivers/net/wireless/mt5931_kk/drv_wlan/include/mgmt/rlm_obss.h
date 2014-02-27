/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/mgmt/rlm_obss.h#1 $
*/

/*! \file   "rlm_obss.h"
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
** $Log: rlm_obss.h $
 *
 * 01 24 2011 cm.chang
 * [WCXRP00000384] [MT6620 Wi-Fi][Driver][FW] Handle 20/40 action frame in AP mode and stop ampdu timer when sta_rec is freed
 * Process received 20/40 coexistence action frame for AP mode
 *
 * 01 13 2011 cm.chang
 * [WCXRP00000358] [MT6620 Wi-Fi][Driver] Provide concurrent information for each module
 * Refine function when rcv a 20/40M public action frame
 *
 * 01 12 2011 cm.chang
 * [WCXRP00000354] [MT6620 Wi-Fi][Driver][FW] Follow NVRAM bandwidth setting
 * User-defined bandwidth is for 2.4G and 5G individually
 *
 * 08 24 2010 cm.chang
 * NULL
 * Support RLM initail channel of Ad-hoc, P2P and BOW
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 28 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * 1st draft code for RLM module
 *
 * 05 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Process 20/40 coexistence public action frame in AP mode
 *
 * 05 05 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * First draft support for 20/40M bandwidth for AP mode
 *
 * 04 07 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Add virtual test for OBSS scan
 *
 * 03 30 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support 2.4G OBSS scan
 *
 * 02 13 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support PCO in STA mode
 *
 * 02 12 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Use bss info array for concurrent handle
 *
 * 01 25 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support protection and bandwidth switch
*/

#ifndef _RLM_OBSS_H
#define _RLM_OBSS_H

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
#define CHNL_LIST_SZ_2G         14
#define CHNL_LIST_SZ_5G         14


#define CHNL_LEVEL0             0
#define CHNL_LEVEL1             1
#define CHNL_LEVEL2             2

#define AFFECTED_CHNL_OFFSET    5

#define OBSS_SCAN_MIN_INTERVAL  10      /* In unit of sec */

#define PUBLIC_ACTION_MAX_LEN   200     /* In unit of byte */

/* P2P GO only */
/* Define default OBSS Scan parameters (from MIB in spec.) */
#define dot11OBSSScanPassiveDwell                   20
#define dot11OBSSScanActiveDwell                    10
#define dot11OBSSScanPassiveTotalPerChannel         200
#define dot11OBSSScanActiveTotalPerChannel          20
#define dot11BSSWidthTriggerScanInterval            300     /* Unit: sec */
#define dot11BSSWidthChannelTransitionDelayFactor   5
#define dot11OBSSScanActivityThreshold              25

#define OBSS_20_40M_TIMEOUT     (dot11BSSWidthTriggerScanInterval + 10)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/* Control MAC PCO function */
typedef enum _ENUM_SYS_PCO_PHASE_T {
    SYS_PCO_PHASE_DISABLED = 0,
    SYS_PCO_PHASE_20M,
    SYS_PCO_PHASE_40M
} ENUM_SYS_PCO_PHASE_T, *P_ENUM_SYS_PCO_PHASE_T;

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
rlmObssInit (
    P_ADAPTER_T     prAdapter
    );

VOID
rlmObssScanDone (
    P_ADAPTER_T prAdapter,
    P_MSG_HDR_T prMsgHdr
    );

VOID
rlmObssTriggerScan (
    P_ADAPTER_T         prAdapter,
    P_BSS_INFO_T        prBssInfo
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _RLM_OBSS_H */

