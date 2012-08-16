/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */


/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
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

#ifndef _WMT_EXP_H_
#define _WMT_EXP_H_

#include <mach/mtk_wcn_cmb_stub.h>
#include "osal.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#if 0
/* moved from wmt_lib.h */
#ifdef CONFIG_MTK_COMBO_COMM_PS
/* Enable common i/f power saving handling support */
/* FixMe SDIO power saving is to be integrated */
#define CFG_WMT_PS_SUPPORT 1
#else
#define CFG_WMT_PS_SUPPORT 0
#endif
#endif

#define CFG_WMT_PS_SUPPORT 1

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#if 1 /* moved from wmt_lib.h */
#ifndef DFT_TAG
#define DFT_TAG         "[WMT-DFT]"
#endif

#define WMT_LOUD_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_LOUD) { osal_loud_print(DFT_TAG "[L]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_DBG_FUNC(fmt, arg...)    if (gWmtDbgLvl >= WMT_LOG_DBG) { osal_dbg_print(DFT_TAG "[D]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_INFO_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_INFO) { osal_info_print(DFT_TAG "[I]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_WARN_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_WARN) { osal_warn_print(DFT_TAG "[W]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_ERR_FUNC(fmt, arg...)    if (gWmtDbgLvl >= WMT_LOG_ERR) { osal_err_print(DFT_TAG "[E]%s(%d):"  fmt, __FUNCTION__ , __LINE__, ##arg);}
#define WMT_TRC_FUNC(f)              if (gWmtDbgLvl >= WMT_LOG_DBG) { osal_dbg_print(DFT_TAG "<%s> <%d>\n", __FUNCTION__, __LINE__);}
#endif

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#if 1 /* moved from wmt_lib.h */
extern UINT32 gWmtDbgLvl ;
#endif

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#if 1 /* moved from wmt_lib.h */
#define WMT_LOG_LOUD    4
#define WMT_LOG_DBG     3
#define WMT_LOG_INFO    2
#define WMT_LOG_WARN    1
#define WMT_LOG_ERR     0
#endif

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_WMTDRV_TYPE_T {
    WMTDRV_TYPE_BT = 0,
    WMTDRV_TYPE_FM = 1,
    WMTDRV_TYPE_GPS = 2,
    WMTDRV_TYPE_WIFI = 3,
    WMTDRV_TYPE_WMT = 4,
    WMTDRV_TYPE_STP = 5,
    WMTDRV_TYPE_SDIO1 = 6,
    WMTDRV_TYPE_SDIO2 = 7,
    WMTDRV_TYPE_LPBK = 8,
    WMTDRV_TYPE_MAX
} ENUM_WMTDRV_TYPE_T, *P_ENUM_WMTDRV_TYPE_T;

// TODO: [ChangeFeature][GeorgeKuo] Reconsider usage of this type
// TODO: how do we extend for new chip and newer revision?
// TODO: This way is hard to extend
typedef enum _ENUM_WMTHWVER_TYPE_T{
    WMTHWVER_MT6620_E1 = 0x0,
    WMTHWVER_MT6620_E2 = 0x1,
    WMTHWVER_MT6620_E3 = 0x2,
    WMTHWVER_MT6620_E4 = 0x3,
    WMTHWVER_MT6620_E5 = 0x4,
    WMTHWVER_MT6620_E6 = 0x5,
    WMTHWVER_MT6620_E7 = 0x6,
    WMTHWVER_MT6620_MAX,
    WMTHWVER_INVALID = 0xff
} ENUM_WMTHWVER_TYPE_T, *P_ENUM_WMTHWVER_TYPE_T;

typedef enum _ENUM_WMTDSNS_TYPE_T{
    WMTDSNS_FM_DISABLE = 0,
    WMTDSNS_FM_ENABLE = 1,
    WMTDSNS_MAX
} ENUM_WMTDSNS_TYPE_T, *P_ENUM_WMTDSNS_TYPE_T;

typedef enum _ENUM_WMTTHERM_TYPE_T{
    WMTTHERM_ZERO = 0,
    WMTTHERM_ENABLE = WMTTHERM_ZERO + 1,
    WMTTHERM_READ = WMTTHERM_ENABLE + 1,
    WMTTHERM_DISABLE = WMTTHERM_READ + 1,
    WMTTHERM_MAX
}ENUM_WMTTHERM_TYPE_T, *P_ENUM_WMTTHERM_TYPE_T;

typedef enum _ENUM_WMTMSG_TYPE_T {
    WMTMSG_TYPE_POWER_ON = 0,
    WMTMSG_TYPE_POWER_OFF = 1,
    WMTMSG_TYPE_RESET = 2,
    WMTMSG_TYPE_STP_RDY= 3,
    WMTMSG_TYPE_HW_FUNC_ON= 4,
    WMTMSG_TYPE_MAX
} ENUM_WMTMSG_TYPE_T, *P_ENUM_WMTMSG_TYPE_T;

typedef enum _ENUM_WMTRSTMSG_TYPE_T{
    WMTRSTMSG_RESET_START = 0x0,
    WMTRSTMSG_RESET_END = 0x1,
    WMTRSTMSG_RESET_MAX,
    WMTRSTMSG_RESET_INVALID = 0xff
} ENUM_WMTRSTMSG_TYPE_T, *P_ENUM_WMTRSTMSG_TYPE_T;

typedef void (*PF_WMT_CB)(
    ENUM_WMTDRV_TYPE_T, /* Source driver type */
    ENUM_WMTDRV_TYPE_T, /* Destination driver type */
    ENUM_WMTMSG_TYPE_T, /* Message type */
    VOID *, /* READ-ONLY buffer. Buffer is allocated and freed by WMT_drv. Client
            can't touch this buffer after this function return. */
    UINT32 /* Buffer size in unit of byte */
);

typedef enum _SDIO_PS_OP{
    OWN_SET = 0,
    OWN_CLR = 1
} SDIO_PS_OP;


typedef INT32 (*PF_WMT_SDIO_PSOP)(SDIO_PS_OP);

#if 1 /* moved from wmt_core.h */
typedef enum {
    WMT_SDIO_SLOT_INVALID = 0,
    WMT_SDIO_SLOT_SDIO1 = 1, /* Wi-Fi dedicated SDIO1*/
    WMT_SDIO_SLOT_SDIO2 = 2,
    WMT_SDIO_SLOT_MAX
} WMT_SDIO_SLOT_NUM;

typedef enum {
    WMT_SDIO_FUNC_STP = 0,
    WMT_SDIO_FUNC_WIFI = 1,
    WMT_SDIO_FUNC_MAX
} WMT_SDIO_FUNC_TYPE;
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
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*subsystem function ctrl APIs*/
extern MTK_WCN_BOOL
mtk_wcn_wmt_func_off (
    ENUM_WMTDRV_TYPE_T type
    );

extern MTK_WCN_BOOL
mtk_wcn_wmt_func_on (
    ENUM_WMTDRV_TYPE_T type
    );

extern MTK_WCN_BOOL mtk_wcn_wmt_dsns_ctrl (
    ENUM_WMTDSNS_TYPE_T eType
    );

extern INT32 mtk_wcn_wmt_msgcb_reg (
    ENUM_WMTDRV_TYPE_T eType,
    PF_WMT_CB pCb
    );

extern INT32 mtk_wcn_wmt_msgcb_unreg (
    ENUM_WMTDRV_TYPE_T eType
    );

extern INT32
mtk_wcn_stp_wmt_sdio_op_reg (
    PF_WMT_SDIO_PSOP own_cb
    );

/*
return value:
enable/disable thermal sensor function: true(1)/false(0)
read thermal sensor function: thermal value

*/
extern INT8
mtk_wcn_wmt_therm_ctrl (
    ENUM_WMTTHERM_TYPE_T eType
    );

extern ENUM_WMTHWVER_TYPE_T
mtk_wcn_wmt_hwver_get (VOID);

extern INT32 wmt_lib_set_aif (
    CMB_STUB_AIF_X aif,
    MTK_WCN_BOOL share
    ); /* set AUDIO interface options */

extern VOID
wmt_lib_ps_irq_cb(VOID);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WMT_EXP_H_ */







