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

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-EXP]"


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"
#include <linux/module.h>
#include <wmt_exp.h>
#include "core_exp.h"



/*******************************************************************************
*                              C O N S T A N T S
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

static MTK_WCN_BOOL
mtk_wcn_wmt_func_ctrl (
    ENUM_WMTDRV_TYPE_T type,
    ENUM_WMT_OPID_T opId
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

static MTK_WCN_BOOL
mtk_wcn_wmt_func_ctrl (
    ENUM_WMTDRV_TYPE_T type,
    ENUM_WMT_OPID_T opId
    )
{
    P_OSAL_OP pOp;
    MTK_WCN_BOOL bRet;
    P_OSAL_SIGNAL pSignal;

    pOp = wmt_lib_get_free_op();
    if (!pOp) {
        WMT_WARN_FUNC("get_free_lxop fail\n");
        return MTK_WCN_BOOL_FALSE;
    }

    pSignal = &pOp->signal;

    pOp->op.opId = opId;
    pOp->op.au4OpData[0] = type;
    pSignal->timeoutValue= (WMT_OPID_FUNC_ON == pOp->op.opId) ? MAX_FUNC_ON_TIME : MAX_FUNC_OFF_TIME;

    WMT_INFO_FUNC("OPID(%d) type(%d) start\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);

    /*do not check return value, we will do this either way*/
    wmt_lib_host_awake_get();
    /*wake up chip first*/
    wmt_lib_disable_psm_monitor();
    bRet = wmt_lib_put_act_op(pOp);
    wmt_lib_enable_psm_monitor();
    wmt_lib_host_awake_put();

    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_WARN_FUNC("OPID(%d) type(%d) fail\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);
    }
    else {
        WMT_INFO_FUNC("OPID(%d) type(%d) ok\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);
    }
    return bRet;
}

MTK_WCN_BOOL
mtk_wcn_wmt_func_off (
    ENUM_WMTDRV_TYPE_T type
    )
{
    return mtk_wcn_wmt_func_ctrl(type, WMT_OPID_FUNC_OFF);
}
EXPORT_SYMBOL(mtk_wcn_wmt_func_off);

MTK_WCN_BOOL
mtk_wcn_wmt_func_on (
    ENUM_WMTDRV_TYPE_T type
    )
{
    return mtk_wcn_wmt_func_ctrl(type, WMT_OPID_FUNC_ON);
}
EXPORT_SYMBOL(mtk_wcn_wmt_func_on);

/*
return value:
enable/disable thermal sensor function: true(1)/false(0)
read thermal sensor function:thermal value

*/
INT8
mtk_wcn_wmt_therm_ctrl (
    ENUM_WMTTHERM_TYPE_T eType
    )
{
    P_OSAL_OP pOp;
    P_WMT_OP pOpData;
    MTK_WCN_BOOL bRet;
    P_OSAL_SIGNAL pSignal;

    /*parameter validation check*/
    if( WMTTHERM_MAX < eType || WMTTHERM_ENABLE > eType){
        WMT_ERR_FUNC("invalid thermal control command (%d)\n", eType);
        return MTK_WCN_BOOL_FALSE;
    }

    /*check if chip support thermal control function or not*/
    bRet = wmt_lib_is_therm_ctrl_support();
    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_ERR_FUNC("thermal ctrl function not supported\n");
        return MTK_WCN_BOOL_FALSE;
    }

    pOp = wmt_lib_get_free_op();
    if (!pOp) {
        WMT_WARN_FUNC("get_free_lxop fail \n");
        return MTK_WCN_BOOL_FALSE;
    }

    pSignal = &pOp->signal;
    pOpData = &pOp->op;
    pOpData->opId = WMT_OPID_THERM_CTRL;
    /*parameter fill*/
    pOpData->au4OpData[0] = eType;
    pSignal->timeoutValue = MAX_EACH_WMT_CMD;

    WMT_INFO_FUNC("OPID(%d) type(%d) start\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);

    wmt_lib_disable_psm_monitor();
    bRet = wmt_lib_put_act_op(pOp);
    wmt_lib_enable_psm_monitor();

    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_WARN_FUNC("OPID(%d) type(%d) fail\n\n",
            pOpData->opId,
            pOpData->au4OpData[0]);
        /*0xFF means read error occurs*/
        pOpData->au4OpData[1] = (eType == WMTTHERM_READ) ? 0xFF : MTK_WCN_BOOL_FALSE;/*will return to function driver*/
    }
    else {
        WMT_INFO_FUNC("OPID(%d) type(%d) return(%d) ok\n\n",
            pOpData->opId,
            pOpData->au4OpData[0],
            pOpData->au4OpData[1]);
    }
    /*return value will be put to lxop->op.au4OpData[1]*/
    WMT_DBG_FUNC("therm ctrl type(%d), iRet(0x%08x) \n", eType, pOpData->au4OpData[1]);
    return (INT8)pOpData->au4OpData[1];
}
EXPORT_SYMBOL(mtk_wcn_wmt_therm_ctrl);

ENUM_WMTHWVER_TYPE_T
mtk_wcn_wmt_hwver_get (VOID)
{
    // TODO: [ChangeFeature][GeorgeKuo] Reconsider usage of this type
    // TODO: how do we extend for new chip and newer revision?
    // TODO: This way is hard to extend
    return wmt_lib_get_hwver();
}
EXPORT_SYMBOL(mtk_wcn_wmt_hwver_get);

MTK_WCN_BOOL
mtk_wcn_wmt_dsns_ctrl (
    ENUM_WMTDSNS_TYPE_T eType
    )
{
    P_OSAL_OP pOp;
    P_WMT_OP pOpData;
    MTK_WCN_BOOL bRet;
    P_OSAL_SIGNAL pSignal;

    if (WMTDSNS_MAX <= eType) {
        WMT_ERR_FUNC("invalid desense control command (%d)\n", eType);
        return MTK_WCN_BOOL_FALSE;
    }

    /*check if chip support thermal control function or not*/
    bRet = wmt_lib_is_dsns_ctrl_support();
    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_ERR_FUNC("thermal ctrl function not supported\n");
        return MTK_WCN_BOOL_FALSE;
    }

    pOp = wmt_lib_get_free_op();
    if (!pOp) {
        WMT_WARN_FUNC("get_free_lxop fail \n");
        return MTK_WCN_BOOL_FALSE;
    }

    pSignal = &pOp->signal;
    pOpData = &pOp->op;
    pOpData->opId = WMT_OPID_DSNS;
    pSignal->timeoutValue = MAX_EACH_WMT_CMD;
    /*parameter fill*/
    if (WMTDSNS_FM_DISABLE == eType) {
        pOpData->au4OpData[0] = WMTDRV_TYPE_FM;
        pOpData->au4OpData[1] = 0x0;
    }
    else { /* input sanity had been verified *//*if (eType == WMTDSNS_FM_ENABLE)*/
        pOpData->au4OpData[0] = WMTDRV_TYPE_FM;
        pOpData->au4OpData[1] = 0x1;
    }

    WMT_INFO_FUNC("OPID(%d) type(%d) start\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);

    wmt_lib_disable_psm_monitor();
    bRet = wmt_lib_put_act_op(pOp);
    wmt_lib_enable_psm_monitor();

    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_WARN_FUNC("OPID(%d) type(%d) fail\n\n",
            pOpData->opId,
            pOpData->au4OpData[0]);
    }
    else {
        WMT_INFO_FUNC("OPID(%d) type(%d) ok\n\n",
            pOpData->opId,
            pOpData->au4OpData[0]);
    }

    return bRet;
}
EXPORT_SYMBOL(mtk_wcn_wmt_dsns_ctrl);

INT32
mtk_wcn_wmt_msgcb_reg (
    ENUM_WMTDRV_TYPE_T eType,
    PF_WMT_CB pCb
    )
{
    return (INT32)wmt_lib_msgcb_reg(eType, pCb);
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_reg);

INT32
mtk_wcn_wmt_msgcb_unreg (
    ENUM_WMTDRV_TYPE_T eType
    )
{
    return (INT32)wmt_lib_msgcb_unreg(eType);
}
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_unreg);

INT32
mtk_wcn_stp_wmt_sdio_op_reg (
    PF_WMT_SDIO_PSOP own_cb
    )
{
    wmt_lib_ps_set_sdio_psop(own_cb);
    return 0;
}
EXPORT_SYMBOL(mtk_wcn_stp_wmt_sdio_op_reg);

