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

/*****************************************************************************
*  Copyright Statement:
*  --------------------
*  This software is protected by Copyright and the information contained
*  herein is confidential. The software may not be copied and the information
*  contained herein may not be used or disclosed except with the written
*  permission of MediaTek Inc. (C) 2008
*
*  BY OPENING THIS FILE, BUYER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
*  THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
*  RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO BUYER ON
*  AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
*  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
*  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
*  NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
*  SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
*  SUPPLIED WITH THE MEDIATEK SOFTWARE, AND BUYER AGREES TO LOOK ONLY TO SUCH
*  THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. MEDIATEK SHALL ALSO
*  NOT BE RESPONSIBLE FOR ANY MEDIATEK SOFTWARE RELEASES MADE TO BUYER'S
*  SPECIFICATION OR TO CONFORM TO A PARTICULAR STANDARD OR OPEN FORUM.
*
*  BUYER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND CUMULATIVE
*  LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
*  AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
*  OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY BUYER TO
*  MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
*
*  THE TRANSACTION CONTEMPLATED HEREUNDER SHALL BE CONSTRUED IN ACCORDANCE
*  WITH THE LAWS OF THE STATE OF CALIFORNIA, USA, EXCLUDING ITS CONFLICT OF
*  LAWS PRINCIPLES.  ANY DISPUTES, CONTROVERSIES OR CLAIMS ARISING THEREOF AND
*  RELATED THERETO SHALL BE SETTLED BY ARBITRATION IN SAN FRANCISCO, CA, UNDER
*  THE RULES OF THE INTERNATIONAL CHAMBER OF COMMERCE (ICC).
*
*****************************************************************************/
/*******************************************************************************
*                E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "osal_linux.h"
#include "core_exp.h"
#include "stp_exp.h"

/*******************************************************************************
*                          C O N S T A N T S
********************************************************************************
*/


/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/


/*******************************************************************************
*                        P U B L I C   D A T A
********************************************************************************
*/
/*******************************************************************************
*                       P R I V A T E   D A T A
********************************************************************************
*/
static MTK_WCN_STP_IF_TX stp_uart_if_tx = NULL;
static MTK_WCN_STP_IF_TX stp_sdio_if_tx = NULL;
static ENUM_STP_TX_IF_TYPE g_stp_if_type = STP_MAX_IF_TX;
static MTK_WCN_STP_IF_RX stp_if_rx = NULL;
static MTK_WCN_STP_EVENT_CB event_callback_tbl[MTKSTP_MAX_TASK_NUM] = {0x0};
static MTK_WCN_STP_EVENT_CB tx_event_callback_tbl[MTKSTP_MAX_TASK_NUM] = {0x0};

/******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
*******************************************************************************
*/

/*******************************************************************************
*                          F U N C T I O N S
********************************************************************************
*/

INT32 mtk_wcn_sys_if_rx(UINT8 *data, INT32 size)
{
    if (stp_if_rx == 0x0)
    {
        return (-1);
    }
    else
    {
        (*stp_if_rx)(data, size);
        return 0;
    }
}

static INT32 mtk_wcn_sys_if_tx (
    const UINT8 *data,
    const UINT32 size,
    UINT32 *written_size
    )
{

    if (STP_UART_IF_TX == g_stp_if_type) {
        return stp_uart_if_tx != NULL ? (*stp_uart_if_tx)(data, size, written_size) : -1;
    }
    else if (STP_SDIO_IF_TX == g_stp_if_type) {
        return stp_sdio_if_tx != NULL ? (*stp_sdio_if_tx)(data, size, written_size) : -1;
    }
    else {
        /*if (g_stp_if_type >= STP_MAX_IF_TX) */ /* George: remove ALWAYS TRUE condition */
        return (-1);
    }
}

static INT32 mtk_wcn_sys_event_set(UINT8 function_type)
{
    if ((function_type < MTKSTP_MAX_TASK_NUM) && (event_callback_tbl[function_type] != 0x0))
    {
        (*event_callback_tbl[function_type])();
    }
    else {
        /* FIXME: error handling */
        printk(KERN_INFO "[%s] STP set event fail. It seems the function is not active.\n", __func__);
    }

    return 0;
}

static INT32 mtk_wcn_sys_event_tx_resume(UINT8 winspace)
{
    INT32 type = 0;

    for(type = 0 ;  type < MTKSTP_MAX_TASK_NUM ; type ++ )
    {
        if (tx_event_callback_tbl[type])
        {
            tx_event_callback_tbl[type]();
        }
    }

    return 0;
}

static INT32 mtk_wcn_sys_check_function_status(UINT8 type, UINT8 op){

    /*op == FUNCTION_ACTIVE, to check if funciton[type] is active ?*/
    if (!(type >= 0 && type < MTKSTP_MAX_TASK_NUM))
    {
        return STATUS_FUNCTION_INVALID;
    }

    if (op == OP_FUNCTION_ACTIVE)
    {
        if (event_callback_tbl[type] != 0x0)
        {
            return STATUS_FUNCTION_ACTIVE;
        }
        else
        {
            return STATUS_FUNCTION_INACTIVE;
        }
    }
    /*you can define more operation here ..., to queury function's status/information*/

    return STATUS_OP_INVALID;
}

INT32 mtk_wcn_stp_register_if_rx(MTK_WCN_STP_IF_RX func)
{
    stp_if_rx = func;

    return 0;
}

VOID mtk_wcn_stp_set_if_tx_type (
    ENUM_STP_TX_IF_TYPE stp_if_type
    )
{
    g_stp_if_type = stp_if_type;
    printk(KERN_INFO "[%s] set STP_IF_TX to %s.\n",
        __FUNCTION__,
        (STP_UART_IF_TX == stp_if_type)? "UART" : ((STP_SDIO_IF_TX == stp_if_type) ? "SDIO" : "NULL"));
}

INT32 mtk_wcn_stp_register_if_tx (
    ENUM_STP_TX_IF_TYPE stp_if,
    MTK_WCN_STP_IF_TX func
    )
{
    if (STP_UART_IF_TX == stp_if)
    {
        stp_uart_if_tx = func;
    }
    else if (STP_SDIO_IF_TX == stp_if)
    {
        stp_sdio_if_tx = func;
    }
    else
    {
        printk(KERN_WARNING "[%s] STP_IF_TX(%d) out of boundary.\n", __FUNCTION__, stp_if);
        return -1;
    }

    return 0;
}

INT32 mtk_wcn_stp_register_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func)
{
    if (type < MTKSTP_MAX_TASK_NUM)
    {
        event_callback_tbl[type] = func;

        /*clear rx queue*/
        //printk("Flush type = %d Rx Queue\n", type);
        mtk_wcn_stp_flush_rx_queue(type);
    }

    return 0;
}

INT32 mtk_wcn_stp_register_tx_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func)
{
    if (type < MTKSTP_MAX_TASK_NUM)
    {
        tx_event_callback_tbl[type] = func;
    }
    else
    {
        BUG_ON(0);
    }

    return 0;
}

INT32 stp_drv_init(VOID)
{
    mtkstp_callback cb =
    {
        .cb_if_tx           = mtk_wcn_sys_if_tx,
        .cb_event_set       = mtk_wcn_sys_event_set,
        .cb_event_tx_resume = mtk_wcn_sys_event_tx_resume,
        .cb_check_funciton_status = mtk_wcn_sys_check_function_status
    };

    return mtk_wcn_stp_init(&cb);
}

VOID stp_drv_exit(VOID)
{
    mtk_wcn_stp_deinit();

    return;
}

EXPORT_SYMBOL(mtk_wcn_stp_register_if_tx);
EXPORT_SYMBOL(mtk_wcn_stp_register_if_rx);
EXPORT_SYMBOL(mtk_wcn_stp_register_event_cb);
EXPORT_SYMBOL(mtk_wcn_stp_register_tx_event_cb);
EXPORT_SYMBOL(mtk_wcn_stp_parser_data);
EXPORT_SYMBOL(mtk_wcn_stp_send_data);
EXPORT_SYMBOL(mtk_wcn_stp_send_data_raw);
EXPORT_SYMBOL(mtk_wcn_stp_receive_data);
EXPORT_SYMBOL(mtk_wcn_stp_is_rxqueue_empty);
EXPORT_SYMBOL(mtk_wcn_stp_set_bluez);
EXPORT_SYMBOL(mtk_wcn_stp_is_ready);






