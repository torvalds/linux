/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/include/pwr_mgt.h#1 $
*/

/*! \file   "pwr_mgt.h"
    \brief  In this file we define the STATE and EVENT for Power Management FSM.

    The SCAN FSM is responsible for performing SCAN behavior when the Arbiter enter
    ARB_STATE_SCAN. The STATE and EVENT for SCAN FSM are defined here with detail
    description.
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
** $Log: pwr_mgt.h $
 *
 * 07 09 2010 george.huang
 * 
 * [WPD00001556] Migrate PM variables from FW to driver: for composing QoS Info
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 04 20 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * don't need SPIN_LOCK_PWR_CTRL anymore, it will raise IRQL 
 * and cause SdBusSubmitRequest running at DISPATCH_LEVEL as well.

 *
 * 03 25 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP 
 * firmware download load adress & start address are now configured from config.h
 *  *  * due to the different configurations on FPGA and ASIC
**  \main\maintrunk.MT6620WiFiDriver_Prj\7 2009-12-10 16:39:10 GMT mtk02752
**  disable PM macros temporally
**  \main\maintrunk.MT6620WiFiDriver_Prj\6 2009-10-29 19:48:37 GMT mtk01084
**  temp remove power management macro
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-04-08 16:51:11 GMT mtk01084
**  update for power management control macro
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-04-03 14:59:58 GMT mtk01426
**  Add #if CFG_HIF_LOOPBACK_PRETEST
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-03-23 16:53:10 GMT mtk01084
**  modify ACQUIRE_POWER_CONTROL_FROM_PM() and RECLAIM_POWER_CONTROL_TO_PM() macro
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-19 18:32:47 GMT mtk01084
**  update for basic power management functions
**  \main\maintrunk.MT6620WiFiDriver_Prj\1 2009-03-19 15:05:20 GMT mtk01084
**  Initial version
**
*/

#ifndef _PWR_MGT_H
#define _PWR_MGT_H
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
#define PM_UAPSD_AC0                        (BIT(0))
#define PM_UAPSD_AC1                        (BIT(1))
#define PM_UAPSD_AC2                        (BIT(2))
#define PM_UAPSD_AC3                        (BIT(3))

#define PM_UAPSD_ALL                        (PM_UAPSD_AC0 | PM_UAPSD_AC1 | PM_UAPSD_AC2 | PM_UAPSD_AC3)
#define PM_UAPSD_NONE                       0


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef struct _PM_PROFILE_SETUP_INFO_T {
    /* Profile setup */
    UINT_8                  ucBmpDeliveryAC;        /* 0: AC_BE, 1: AC_BK, 2: AC_VI, 3: AC_VO */
    UINT_8                  ucBmpTriggerAC;        /* 0: AC_BE, 1: AC_BK, 2: AC_VI, 3: AC_VO */

    UINT_8      ucUapsdSp;          /* Number of triggered packets in UAPSD */
    
} PM_PROFILE_SETUP_INFO_T, *P_PM_PROFILE_SETUP_INFO_T;


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

#if !CFG_ENABLE_FULL_PM
    #define ACQUIRE_POWER_CONTROL_FROM_PM(_prAdapter)
    #define RECLAIM_POWER_CONTROL_TO_PM(_prAdapter, _fgEnableGINT_in_IST)
#else
    #define ACQUIRE_POWER_CONTROL_FROM_PM(_prAdapter) \
    { \
        if (_prAdapter->fgIsFwOwn) { \
            nicpmSetDriverOwn(_prAdapter); \
        } \
        /* Increase Block to Enter Low Power Semaphore count */ \
        GLUE_INC_REF_CNT(_prAdapter->u4PwrCtrlBlockCnt); \
    }

    #define RECLAIM_POWER_CONTROL_TO_PM(_prAdapter, _fgEnableGINT_in_IST) \
    { \
        ASSERT(_prAdapter->u4PwrCtrlBlockCnt != 0); \
        /* Decrease Block to Enter Low Power Semaphore count */ \
        GLUE_DEC_REF_CNT(_prAdapter->u4PwrCtrlBlockCnt); \
        if (_prAdapter->fgWiFiInSleepyState && (_prAdapter->u4PwrCtrlBlockCnt == 0)) { \
            nicpmSetFWOwn(_prAdapter, _fgEnableGINT_in_IST); \
        } \
    }
#endif


/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _PWR_MGT_H */

