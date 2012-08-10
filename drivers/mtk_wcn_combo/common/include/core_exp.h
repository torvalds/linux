
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

#ifndef _CORE_EXP_H_
#define _CORE_EXP_H_

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#define CFG_WMT_DBG_SUPPORT (1)

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal.h"
#include "wmt_exp.h"


/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define MAX_WIFI_ON_TIME (5500) // in ms?
#define WMT_LIB_RX_TIMEOUT (2000) // in ms?

#define WMT_PWRON_RTY_DFT (2)
#define MAX_RETRY_TIME_DUE_TO_RX_TIMEOUT (WMT_PWRON_RTY_DFT * WMT_LIB_RX_TIMEOUT)
#define MAX_EACH_FUNC_ON_WHEN_CHIP_POWER_ON_ALREADY (WMT_LIB_RX_TIMEOUT) /*each WMT command*/
#define MAX_FUNC_ON_TIME (MAX_WIFI_ON_TIME + MAX_RETRY_TIME_DUE_TO_RX_TIMEOUT + MAX_EACH_FUNC_ON_WHEN_CHIP_POWER_ON_ALREADY * 3)

#define MAX_EACH_FUNC_OFF (WMT_LIB_RX_TIMEOUT + 1000) /*1000->WMT_LIB_RX_TIMEOUT + 1000, logical judgement*/
#define MAX_FUNC_OFF_TIME (MAX_EACH_FUNC_OFF * 4)

#define MAX_EACH_WMT_CMD (WMT_LIB_RX_TIMEOUT + 1000)

#define OP_FUNCTION_ACTIVE (0)


#define STATUS_OP_INVALID (0)
#define STATUS_FUNCTION_INVALID (1)

#define STATUS_FUNCTION_ACTIVE (31)
#define STATUS_FUNCTION_INACTIVE (32)

#define defaultPatchName "mt66xx_patch_hdr.bin"
#define BCNT_PATCH_BUF_HEADROOM (8)
#define DWCNT_HIF_CONF (4)
#define DWCNT_STRAP_CONF (4)
#define DWCNT_RESERVED    (8)
#define DWCNT_CTRL_DATA  (16)

/*******************************************************************************
*                               M A C R O S
********************************************************************************
*/


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_WMT_OPID_T {
    WMT_OPID_HIF_CONF = 0,
    WMT_OPID_PWR_ON = 1,
    WMT_OPID_PWR_OFF = 2,
    WMT_OPID_FUNC_ON = 3,
    WMT_OPID_FUNC_OFF = 4,
    WMT_OPID_REG_RW =  5, // TODO:[ChangeFeature][George] is this OP obsoleted?
    WMT_OPID_EXIT = 6,
    WMT_OPID_PWR_SV = 7,
    WMT_OPID_DSNS = 8,
    WMT_OPID_LPBK = 9,
    WMT_OPID_CMD_TEST = 10,
    WMT_OPID_HW_RST = 11,
    WMT_OPID_SW_RST = 12,
    WMT_OPID_BAUD_RST = 13,
    WMT_OPID_STP_RST = 14,
    WMT_OPID_THERM_CTRL = 15,
    WMT_OPID_EFUSE_RW = 16,
    WMT_OPID_GPIO_CTRL = 17,
    WMT_OPID_SDIO_CTRL = 18,
    WMT_OPID_MAX
} ENUM_WMT_OPID_T, *P_ENUM_WMT_OPID_T;

typedef enum _ENUM_WMT_UART_FC_T
{
    WMT_UART_NO_FC = 0,
    WMT_UART_MTK_SW_FC = 1,
    WMT_UART_LUX_SW_FC = 2,
    WMT_UART_HW_FC = 3,
    WMT_UART_MAX
} ENUM_WMT_UART_FC_T, *P_ENUM_UART_FC_T;


typedef OSAL_OP_DAT WMT_OP;
typedef P_OSAL_OP_DAT P_WMT_OP;


typedef INT32 (*IF_TX)(const UINT8 *data, const UINT32 size, UINT32 *written_size);
/* event/signal */
typedef INT32 (*EVENT_SET)(UINT8 function_type);
typedef INT32 (*EVENT_TX_RESUME)(UINT8 winspace);
typedef INT32 (*FUNCTION_STATUS)(UINT8 type, UINT8 op);


typedef struct
{
    /* common interface */
    IF_TX           cb_if_tx;
    /* event/signal */
    EVENT_SET       cb_event_set;
    EVENT_TX_RESUME cb_event_tx_resume;
    FUNCTION_STATUS cb_check_funciton_status;
}mtkstp_callback;

typedef struct _WMT_HIF_CONF {
    UINT32 hifType; // HIF Type
    UINT32 uartFcCtrl; // UART FC config
    UINT32 au4HifConf[DWCNT_HIF_CONF]; // HIF Config
    UINT32 au4StrapConf[DWCNT_STRAP_CONF]; // Strap Config
} WMT_HIF_CONF, *P_WMT_HIF_CONF;


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

extern INT32 wmt_lib_init(VOID);
extern INT32 wmt_lib_deinit(VOID);
extern P_OSAL_OP wmt_lib_get_free_op (VOID);
extern MTK_WCN_BOOL wmt_lib_put_act_op (P_OSAL_OP pOp);
extern INT32 wmt_lib_host_awake_get(VOID);
extern INT32 wmt_lib_host_awake_put(VOID);
extern VOID wmt_lib_disable_psm_monitor(VOID);
extern VOID wmt_lib_enable_psm_monitor(VOID);
extern MTK_WCN_BOOL wmt_lib_is_therm_ctrl_support (VOID);
extern MTK_WCN_BOOL wmt_lib_is_dsns_ctrl_support (VOID);
extern INT32 wmt_lib_msgcb_reg (ENUM_WMTDRV_TYPE_T eType,PF_WMT_CB pCb);

extern INT32 wmt_lib_msgcb_unreg (ENUM_WMTDRV_TYPE_T eType);

extern VOID wmt_lib_ps_set_sdio_psop (PF_WMT_SDIO_PSOP own_cb);
extern ENUM_WMTHWVER_TYPE_T wmt_lib_get_hwver (VOID);
extern VOID wmt_lib_flush_rx(VOID);
extern INT32 wmt_lib_trigger_cmd_signal (INT32 result);
extern UCHAR *wmt_lib_get_cmd(VOID);
extern P_OSAL_EVENT wmt_lib_get_cmd_event(VOID);
extern MTK_WCN_BOOL wmt_lib_get_cmd_status(VOID);
extern INT32 wmt_lib_set_patch_name(UCHAR *cPatchName);

extern INT32 wmt_lib_set_hif(ULONG hifconf);
extern P_WMT_HIF_CONF wmt_lib_get_hif(VOID);


extern INT32  stp_drv_init(VOID);
extern VOID stp_drv_exit(VOID);
extern INT32 mtk_wcn_stp_init(const mtkstp_callback * const cb_func);
extern INT32 mtk_wcn_stp_deinit(VOID);
extern VOID mtk_wcn_stp_flush_rx_queue(UINT32 type);
extern INT32 mtk_wcn_stp_send_data_raw(const UINT8 *buffer, const UINT32 length, const UINT8 type);


extern INT32 wmt_dbg_proc_read(CHAR *page, CHAR **start, LONG off, INT32 count, INT32 *eof, VOID *data);
extern INT32 wmt_dbg_proc_write(CHAR *buffer);


#endif

