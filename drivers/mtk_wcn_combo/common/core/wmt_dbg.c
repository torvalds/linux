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
    \brief brief description

    Detailed descriptions here.

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

#include "wmt_dbg.h"
#include "wmt_core.h"
#include "wmt_lib.h"
#include "wmt_conf.h"
#include "psm_core.h"
#include "stp_core.h"

#if CFG_WMT_DBG_SUPPORT


#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-DEV]"



#define WMT_DBG_PROCNAME "driver/wmt_dbg"

static struct proc_dir_entry *gWmtDbgEntry = NULL;
COEX_BUF gCoexBuf;

static INT32 wmt_dbg_psm_ctrl(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_dsns_ctrl(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_hwver_get(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_inband_rst(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_chip_rst(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_func_ctrl(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_raed_chipid(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_wmt_dbg_level(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_stp_dbg_level(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_reg_read(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_reg_write(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_coex_test(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_assert_test(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_cmd_test_api(ENUM_WMTDRV_CMD_T cmd);
static INT32 wmt_dbg_rst_ctrl(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_ut_test(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_efuse_read(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_efuse_write(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_sdio_ctrl(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_stp_dbg_ctrl(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_stp_dbg_log_ctrl(INT32 par1, INT32 par2, INT32 par3);
static INT32 wmt_dbg_wmt_assert_ctrl(INT32 par1, INT32 par2, INT32 par3);





const static WMT_DEV_DBG_FUNC wmt_dev_dbg_func[] =
{
    [0] = wmt_dbg_psm_ctrl,
    [1] = wmt_dbg_psm_ctrl,
    [2] = wmt_dbg_dsns_ctrl,
    [3] = wmt_dbg_hwver_get,
    [4] = wmt_dbg_assert_test,
    [5] = wmt_dbg_inband_rst,
    [6] = wmt_dbg_chip_rst,
    [7] = wmt_dbg_func_ctrl,
    [8] = wmt_dbg_raed_chipid,
    [9] = wmt_dbg_wmt_dbg_level,
    [0xa] = wmt_dbg_stp_dbg_level,
    [0xb] = wmt_dbg_reg_read,
    [0xc] = wmt_dbg_reg_write,
    [0xd] = wmt_dbg_coex_test, 
    [0xe] = wmt_dbg_rst_ctrl,
    [0xf] = wmt_dbg_ut_test,
    [0x10] = wmt_dbg_efuse_read,
    [0x11] = wmt_dbg_efuse_write,
    [0x12] = wmt_dbg_sdio_ctrl,
    [0x13] = wmt_dbg_stp_dbg_ctrl,
    [0x14] = wmt_dbg_stp_dbg_log_ctrl,
    [0x15] = wmt_dbg_wmt_assert_ctrl,
};

INT32 wmt_dbg_psm_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
#if CFG_WMT_PS_SUPPORT
    if (0 == par2)
    {
        wmt_lib_ps_ctrl(0);
        WMT_INFO_FUNC("disable PSM\n");
    }
    else
    {
        par2 = (1 > par2 || 20000 < par2) ? STP_PSM_IDLE_TIME_SLEEP : par2;
        wmt_lib_ps_set_idle_time(par2);
        wmt_lib_ps_ctrl(1);
        WMT_INFO_FUNC("enable PSM, idle to sleep time = %d ms\n", par2);
    }
#else
    WMT_INFO_FUNC("WMT PS not supported\n");
#endif    
    return 0;
}

INT32 wmt_dbg_dsns_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
    if (WMTDSNS_FM_DISABLE <= par2 && WMTDSNS_MAX > par2 )
    {
        WMT_INFO_FUNC("DSNS type (%d)\n", par2);
        mtk_wcn_wmt_dsns_ctrl(par2);
    }
    else
    {
        WMT_WARN_FUNC("invalid DSNS type\n");
    }
    return 0;
}

INT32 wmt_dbg_hwver_get(INT32 par1, INT32 par2, INT32 par3)
{
    WMT_INFO_FUNC("query chip version\n");
    mtk_wcn_wmt_hwver_get();
    return 0;
}

INT32 wmt_dbg_assert_test(INT32 par1, INT32 par2, INT32 par3)
{
    if (0 == par3)
    {
    //par2 = 0:  send assert command
    //par2 != 0: send exception command
        return wmt_dbg_cmd_test_api(0 == par2 ? 0 : 1);
    }
    else
    {
        INT32 sec = 8;
        INT32 times = 0;
        times = par3;
        do{
            WMT_INFO_FUNC("Send Assert Command per 8 secs!!\n");
            wmt_dbg_cmd_test_api(0);
            osal_msleep(sec * 1000);
        }while(--times);
    }
    return 0;
}

INT32 wmt_dbg_cmd_test_api(ENUM_WMTDRV_CMD_T cmd)
{
    
    P_OSAL_OP pOp = NULL;
    MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
    P_OSAL_SIGNAL pSignal;
    
    pOp  = wmt_lib_get_free_op();
    if (!pOp ) {
        WMT_WARN_FUNC("get_free_lxop fail\n");
        return MTK_WCN_BOOL_FALSE;
    }

    pSignal = &pOp ->signal;

    pOp ->op.opId = WMT_OPID_CMD_TEST;
    
    pSignal->timeoutValue= MAX_EACH_WMT_CMD;
    /*this test command should be run with usb cable connected, so no host awake is needed*/
    //wmt_lib_host_awake_get();
    switch (cmd)
    {
        case WMTDRV_CMD_ASSERT:
            pOp->op.au4OpData[0] = 0;
        break;
        case WMTDRV_CMD_EXCEPTION:
            pOp->op.au4OpData[0] = 1;
        break;
        default:
            if (WMTDRV_CMD_COEXDBG_00 <= cmd && WMTDRV_CMD_COEXDBG_15 >= cmd)
            {
                pOp->op.au4OpData[0] = 2;
                pOp->op.au4OpData[1] = cmd - 2;
            }
            else
            {
                pOp->op.au4OpData[0] = 0xff;
                pOp->op.au4OpData[1] = 0xff;
            }
            pOp->op.au4OpData[2] = (ULONG)gCoexBuf.buffer;
            pOp->op.au4OpData[3] = osal_sizeof(gCoexBuf.buffer);
        break;
    }
    WMT_INFO_FUNC("CMD_TEST, opid(%d), par(%d, %d)\n", pOp->op.opId, pOp->op.au4OpData[0], pOp->op.au4OpData[1]);
    /*wake up chip first*/
    if (DISABLE_PSM_MONITOR()) {
        WMT_ERR_FUNC("wake up failed\n");
        wmt_lib_put_op_to_free_queue(pOp);
        return -1;
    }
    bRet = wmt_lib_put_act_op(pOp);
    ENABLE_PSM_MONITOR();
    if ((cmd != WMTDRV_CMD_ASSERT) && (cmd != WMTDRV_CMD_EXCEPTION))
    {
        if (MTK_WCN_BOOL_FALSE == bRet)
        {
            gCoexBuf.availSize = 0;
        }
        else
        {
            gCoexBuf.availSize = pOp->op.au4OpData[3];
            WMT_INFO_FUNC("gCoexBuf.availSize = %d\n", gCoexBuf.availSize);
        }
    }
    //wmt_lib_host_awake_put();
    WMT_INFO_FUNC("CMD_TEST, opid (%d), par(%d, %d), ret(%d), result(%s)\n", \
    pOp->op.opId, \
    pOp->op.au4OpData[0], \
    pOp->op.au4OpData[1], \
    bRet, \
    MTK_WCN_BOOL_FALSE == bRet ? "failed" : "succeed"\
    );
    
    return 0;
}

INT32 wmt_dbg_inband_rst(INT32 par1, INT32 par2, INT32 par3)
{
    if (0 == par2)
    {
        WMT_INFO_FUNC("inband reset test!!\n");
        mtk_wcn_stp_inband_reset();
       }
    else
    {
        WMT_INFO_FUNC("STP context reset in host side!!\n");
        mtk_wcn_stp_flush_context();
    }
    
    return 0;
}

INT32 wmt_dbg_chip_rst(INT32 par1, INT32 par2, INT32 par3)
{
    if (0 == par2)
    {
        if (mtk_wcn_stp_is_ready())
        {
            WMT_INFO_FUNC("whole chip reset test\n");
            wmt_lib_cmb_rst(WMTRSTSRC_RESET_TEST);
        }
        else
        {
            WMT_INFO_FUNC("STP not ready , not to launch whole chip reset test\n");
        }
    }
    else if (1 == par2)
    {
        WMT_INFO_FUNC("chip hardware reset test\n");    
        wmt_lib_hw_rst();
    }
    else
    {
        WMT_INFO_FUNC("chip software reset test\n");
        wmt_lib_sw_rst(1);
    }
    return 0;
}

INT32 wmt_dbg_func_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
    if (WMTDRV_TYPE_WMT > par2 || WMTDRV_TYPE_LPBK == par2)
    {
        if (0 == par3)
        {
            WMT_INFO_FUNC("function off test, type(%d)\n", par2);
            mtk_wcn_wmt_func_off(par2);
        }
        else
        {
            WMT_INFO_FUNC("function on test, type(%d)\n", par2);
            mtk_wcn_wmt_func_on(par2);
        }
    }
    else
    {
        WMT_INFO_FUNC("function ctrl test, invalid type(%d)\n", par2);
    }
    return 0;    
}

INT32 wmt_dbg_raed_chipid(INT32 par1, INT32 par2, INT32 par3)
{
    WMT_INFO_FUNC("chip version = %d\n", wmt_lib_get_icinfo(WMTCHIN_MAPPINGHWVER));
    return 0;
}

INT32 wmt_dbg_wmt_dbg_level(INT32 par1, INT32 par2, INT32 par3)
{
    par2 = (WMT_LOG_ERR <= par2 && WMT_LOG_LOUD >= par2) ? par2 : WMT_LOG_INFO;
    wmt_lib_dbg_level_set(par2);
    WMT_INFO_FUNC("set wmt log level to %d\n", par2);
    return 0;
}

INT32 wmt_dbg_stp_dbg_level(INT32 par1, INT32 par2, INT32 par3)
{
    par2 = (0 <= par2 && 4 >= par2) ? par2 : 2;
    mtk_wcn_stp_dbg_level(par2);
    WMT_INFO_FUNC("set stp log level to %d\n", par2);
    return 0;

}

INT32 wmt_dbg_reg_read(INT32 par1, INT32 par2, INT32 par3)
{
    //par2-->register address
    //par3-->register mask
    UINT32 value = 0x0;
    UINT32 iRet = -1;
#if 0    
    DISABLE_PSM_MONITOR();
    iRet = wmt_core_reg_rw_raw(0, par2, &value, par3);
    ENABLE_PSM_MONITOR();
#endif
    iRet = wmt_lib_reg_rw(0, par2, &value, par3);
    WMT_INFO_FUNC("read combo chip register (0x%08x) with mask (0x%08x) %s, value = 0x%08x\n", \
        par2, \
        par3, \
        iRet != 0 ? "failed" : "succeed", \
        iRet != 0 ?  -1: value\
        );
    return 0;
}

INT32 wmt_dbg_reg_write(INT32 par1, INT32 par2, INT32 par3)
{
    //par2-->register address    
    //par3-->value to set
    UINT32 iRet = -1;
    #if 0
    DISABLE_PSM_MONITOR();
    iRet = wmt_core_reg_rw_raw(1, par2, &par3, 0xffffffff);
    ENABLE_PSM_MONITOR();
    #endif
    iRet = wmt_lib_reg_rw(1, par2, &par3, 0xffffffff);
    WMT_INFO_FUNC("write combo chip register (0x%08x) with value (0x%08x) %s\n", \
        par2, \
        par3, \
        iRet != 0 ? "failed" : "succeed"\
        );
    return 0;
}

INT32 wmt_dbg_efuse_read(INT32 par1, INT32 par2, INT32 par3)
{
    //par2-->efuse address
    //par3-->register mask
    UINT32 value = 0x0;
    UINT32 iRet = -1;

    iRet = wmt_lib_efuse_rw(0, par2, &value, par3);
    WMT_INFO_FUNC("read combo chip efuse (0x%08x) with mask (0x%08x) %s, value = 0x%08x\n", \
        par2, \
        par3, \
        iRet != 0 ? "failed" : "succeed", \
        iRet != 0 ?  -1: value\
        );
    return 0;
}

INT32 wmt_dbg_efuse_write(INT32 par1, INT32 par2, INT32 par3)
{
    //par2-->efuse address    
    //par3-->value to set
    UINT32 iRet = -1;
    iRet = wmt_lib_efuse_rw(1, par2, &par3, 0xffffffff);
    WMT_INFO_FUNC("write combo chip efuse (0x%08x) with value (0x%08x) %s\n", \
        par2, \
        par3, \
        iRet != 0 ? "failed" : "succeed"\
        );
    return 0;
}


INT32 wmt_dbg_sdio_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
    INT32 iRet = -1;
    iRet = wmt_lib_sdio_ctrl (0 != par2 ? 1 : 0);
    WMT_INFO_FUNC("ctrl SDIO function %s\n", 0 == iRet ? "succeed" : "failed");
    return 0;
}


INT32 wmt_dbg_stp_dbg_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
    if (1 < par2)
    {
        mtk_wcn_stp_dbg_dump_package();
        return 0;
    }
    WMT_INFO_FUNC("%s stp debug function\n", 0 == par2 ? "disable" : "enable");
    if (0 == par2)
    {
        mtk_wcn_stp_dbg_disable();
    }
    else if (1 == par2)
    {
        mtk_wcn_stp_dbg_enable();
    }
    return 0;
}

INT32 wmt_dbg_stp_dbg_log_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
    mtk_wcn_stp_dbg_log_ctrl(0 != par2 ? 1 : 0);
    return 0;
}



INT32 wmt_dbg_wmt_assert_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
    mtk_wcn_stp_coredump_flag_ctrl(0 != par2 ? 1 : 0);
    return 0;
}


INT32 wmt_dbg_coex_test(INT32 par1, INT32 par2, INT32 par3)
{
    WMT_INFO_FUNC("coexistance test cmd!!\n");
    return wmt_dbg_cmd_test_api(par2 + WMTDRV_CMD_COEXDBG_00);
}

INT32 wmt_dbg_rst_ctrl(INT32 par1, INT32 par2, INT32 par3)
{
    WMT_INFO_FUNC("%s audo rst\n", 0 == par2 ? "disable" : "enable");
    mtk_wcn_stp_set_auto_rst(0 == par2 ? 0 : 1);
    return 0;
}

static INT32 wmt_dev_dbg_read(CHAR *page, CHAR **start, off_t off, INT32 count, INT32 *eof, void *data){
    INT32 len = 0;

    if(off > 0){
        len = 0;
    } else {
        /*len = sprintf(page, "%d\n", g_psm_enable);*/
        if ( gCoexBuf.availSize <= 0)
        {
            WMT_INFO_FUNC("no data available, please run echo 15 xx > /proc/driver/wmt_psm first\n");
            len = osal_sprintf(page, "no data available, please run echo 15 xx > /proc/driver/wmt_psm first\n");
        }
        else
        {
            INT32 i = 0;
            /*we do not check page buffer, because there are only 100 bytes in g_coex_buf, no reason page buffer is not enough, a bomb is placed here on unexpected condition*/
            for (i = 0; i < gCoexBuf.availSize; i++)
            {
                len += osal_sprintf(page + len, "0x%02x ", gCoexBuf.buffer[i]);
            }
            len += osal_sprintf(page + len, "\n");
        }
    }
    gCoexBuf.availSize = 0;
    return len;
}

INT32 wmt_dbg_ut_test(INT32 par1, INT32 par2, INT32 par3)
{

    INT32 i = 0;
    INT32 j = 0;
    INT32 iRet = 0;
   
    i = 20;          
    while((i--) > 0){
        WMT_INFO_FUNC("#### UT WMT and STP Function On/Off .... %d\n", i);
        j = 10;
        while((j--) > 0){
            WMT_INFO_FUNC("#### BT  On .... (%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_on(WMTDRV_TYPE_BT); 
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
            WMT_INFO_FUNC("#### GPS On .... (%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_on(WMTDRV_TYPE_GPS); 
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
            WMT_INFO_FUNC("#### FM  On .... (%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_on(WMTDRV_TYPE_FM);
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
            WMT_INFO_FUNC("#### WIFI On .... (%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_on(WMTDRV_TYPE_WIFI); 
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
            WMT_INFO_FUNC("#### BT  Off .... (%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_off(WMTDRV_TYPE_BT); 
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
            WMT_INFO_FUNC("#### GPS  Off ....(%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_off(WMTDRV_TYPE_GPS); 
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
            WMT_INFO_FUNC("#### FM  Off .... (%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_off(WMTDRV_TYPE_FM); 
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
            WMT_INFO_FUNC("#### WIFI  Off ....(%d, %d) \n", i, j);
            iRet = mtk_wcn_wmt_func_off(WMTDRV_TYPE_WIFI);           
            if(iRet == MTK_WCN_BOOL_FALSE){
                break;
            }
        }
        if(iRet == MTK_WCN_BOOL_FALSE){
                break;
        }          
    }
    if(iRet == MTK_WCN_BOOL_FALSE){
        WMT_INFO_FUNC("#### UT FAIL!!\n");
    } else {
        WMT_INFO_FUNC("#### UT PASS!!\n");
    }
    return iRet;        
}


static INT32 wmt_dev_dbg_write(struct file *file, const CHAR *buffer, ULONG count, void *data){
    
    CHAR buf[256];
    CHAR *pBuf;
    ULONG len = count;
    INT32 x = 0,y = 0, z=0;
    CHAR *pToken = NULL;
    CHAR *pDelimiter = " \t";

    WMT_INFO_FUNC("write parameter len = %d\n\r", (INT32)len);
    if(len >= osal_sizeof(buf)){
        WMT_ERR_FUNC("input handling fail!\n");
        len = osal_sizeof(buf) - 1;
        return -1;
    }    
    
    if(copy_from_user(buf, buffer, len)){
        return -EFAULT;
    }
    buf[len] = '\0';
    WMT_INFO_FUNC("write parameter data = %s\n\r", buf);

    pBuf = buf;
    pToken = osal_strsep(&pBuf, pDelimiter);
    x = NULL != pToken ? osal_strtol(pToken, NULL, 16) : 0; 

    pToken = osal_strsep(&pBuf, "\t\n ");
    if(pToken != NULL){
        y = osal_strtol(pToken, NULL, 16);
        WMT_INFO_FUNC("y = 0x%08x \n\r", y);
    } else {
        y = 3000;
         /*efuse, register read write default value*/
        if(0x11 == x || 0x12 == x || 0x13 == x) {
            y = 0x80000000;
        }
    }

    pToken = osal_strsep(&pBuf, "\t\n ");
    if(pToken != NULL){
        z = osal_strtol(pToken, NULL, 16);
    } else {
        z = 10;
        /*efuse, register read write default value*/
        if(0x11 == x || 0x12 == x || 0x13 == x) {
            z = 0xffffffff;
        }
    }
    
    WMT_INFO_FUNC("x(0x%08x), y(0x%08x), z(0x%08x)\n\r", x, y, z);

    if (osal_array_size(wmt_dev_dbg_func) > x && NULL != wmt_dev_dbg_func[x])
    {
        (*wmt_dev_dbg_func[x])(x, y, z);
    }
    else
    {
        WMT_WARN_FUNC("no handler defined for command id(0x%08x)\n\r", x);
    }
    return len;
}

INT32 wmt_dev_dbg_setup(VOID)
{
    gWmtDbgEntry = create_proc_entry(WMT_DBG_PROCNAME, 0664, NULL);
    if(gWmtDbgEntry == NULL){
        WMT_ERR_FUNC("Unable to create /proc entry\n\r");
        return -1;
    }
    gWmtDbgEntry->read_proc = wmt_dev_dbg_read;
    gWmtDbgEntry->write_proc = wmt_dev_dbg_write;
    return 0;
}

INT32 wmt_dev_dbg_remove(VOID)
{
    if (NULL != gWmtDbgEntry)
    {
        remove_proc_entry(WMT_DBG_PROCNAME, NULL);
    }
#if CFG_WMT_PS_SUPPORT
    wmt_lib_ps_deinit();
#endif
    return 0;
}
#endif

