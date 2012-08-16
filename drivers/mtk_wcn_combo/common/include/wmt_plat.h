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

#ifndef _WMT_PLAT_H_
#define _WMT_PLAT_H_
#include "osal_typedef.h"
#include "osal.h"
#include <mach/mtk_wcn_cmb_stub.h>
#include "mtk_wcn_cmb_hw.h"


/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#if 0 /* [GeorgeKuo] remove COMBO_AUDIO FLAG */
#define COMBO_AUDIO_BT_MASK (0x1UL)
#define COMBO_AUDIO_BT_PCM_ON (0x1UL << 0)
#define COMBO_AUDIO_BT_PCM_OFF (0x0UL << 0)

#define COMBO_AUDIO_FM_MASK (0x2UL)
#define COMBO_AUDIO_FM_LINEIN (0x0UL << 1)
#define COMBO_AUDIO_FM_I2S (0x1UL << 1)

#define COMBO_AUDIO_PIN_MASK     (0x4UL)
#define COMBO_AUDIO_PIN_SHARE    (0x1UL << 2)
#define COMBO_AUDIO_PIN_SEPARATE (0x0UL << 2)
#endif

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

typedef enum _ENUM_FUNC_STATE_{
    FUNC_OFF = 0,
    FUNC_ON = 1,
    FUNC_RST = 2,
    FUNC_CTRL_MAX,
} ENUM_FUNC_STATE, *P_ENUM_FUNC_STATE;

typedef enum _ENUM_PIN_ID_{
    PIN_LDO = 0,
    PIN_PMU = 1,
    PIN_RTC = 2,
    PIN_RST = 3,
    PIN_BGF_EINT = 4,
    PIN_WIFI_EINT = 5,
    PIN_ALL_EINT = 6,
    PIN_UART_GRP = 7,
    PIN_PCM_GRP = 8,
    PIN_I2S_GRP = 9,
    PIN_SDIO_GRP = 10,
    PIN_GPS_SYNC = 11,
    PIN_GPS_LNA = 12,
    PIN_ID_MAX
} ENUM_PIN_ID, *P_ENUM_PIN_ID;

typedef enum _ENUM_PIN_STATE_{
    PIN_STA_INIT = 0,
    PIN_STA_OUT_L = 1,
    PIN_STA_OUT_H = 2,
    PIN_STA_IN_L = 3,
    PIN_STA_MUX = 4,
    PIN_STA_EINT_EN = 5,
    PIN_STA_EINT_DIS = 6,
    PIN_STA_DEINIT = 7,
    PIN_STA_MAX
} ENUM_PIN_STATE, *P_ENUM_PIN_STATE;

typedef enum _CMB_IF_TYPE_{
    CMB_IF_UART = 0,
    CMB_IF_WIFI_SDIO = 1,
    CMB_IF_BGF_SDIO = 2,
    CMB_IF_BGWF_SDIO = 3,
    CMB_IF_TYPE_MAX
} CMB_IF_TYPE, *P_CMB_IF_TYPE;

typedef INT32 (*fp_set_pin)(ENUM_PIN_STATE);

typedef enum _ENUM_WL_OP_{
    WL_OP_GET = 0,
    WL_OP_PUT = 1,
    WL_OP_MAX
} ENUM_WL_OP, *P_ENUM_WL_OP;


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
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

INT32
wmt_plat_init (P_PWR_SEQ_TIME pPwrSeqTime);

INT32
wmt_plat_deinit (VOID);

#if 0
INT32
wmt_plat_irq_ctrl (
    ENUM_FUNC_STATE state
    );
#endif

INT32
wmt_plat_pwr_ctrl (
    ENUM_FUNC_STATE state
    );

INT32
wmt_plat_ps_ctrl (
    ENUM_FUNC_STATE state
    );

INT32
wmt_plat_gpio_ctrl (
    ENUM_PIN_ID id,
    ENUM_PIN_STATE state
    );

INT32
wmt_plat_eirq_ctrl (
    ENUM_PIN_ID id,
    ENUM_PIN_STATE state
    );

INT32
wmt_plat_audio_ctrl (
    CMB_STUB_AIF_X state,
    CMB_STUB_AIF_CTRL ctrl
    );

INT32
wmt_plat_sdio_ctrl (
    UINT32 sdioPortNum,
    ENUM_FUNC_STATE on
    );


INT32
wmt_plat_wake_lock_ctrl(
    ENUM_WL_OP opId
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WMT_PLAT_H_ */

