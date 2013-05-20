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
#define CMB_STUB_LOG_INFO(fmt, arg...) printk(KERN_INFO fmt, ##arg)
#define CMB_STUB_LOG_WARN(fmt, arg...) printk(KERN_WARNING fmt, ##arg)
#define CMB_STUB_LOG_DBG(fmt, arg...) printk(KERN_DEBUG fmt, ##arg)


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <linux/kernel.h>
#include <linux/module.h>
//#include <cust_gpio_usage.h>
//#include <mach/mt6573_pll.h> /* clr_device_working_ability, MT65XX_PDN_PERI_UART3, DEEP_IDLE_STATE, MT65XX_PDN_PERI_MSDC2 */
//#include <mach/mt6575_dcm.h>
// TODO: [FixMe][GeorgeKuo] keep prototype unchanged temporarily. Replace it
// when integrate MT6628 & ALPS & other built-in modules, such as AUDIO.
//#include <mach/mt_combo.h>

#include <mach/mtk_wcn_cmb_stub.h>

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

static wmt_aif_ctrl_cb cmb_stub_aif_ctrl_cb = NULL;
static wmt_func_ctrl_cb cmb_stub_func_ctrl_cb = NULL;
static CMB_STUB_AIF_X cmb_stub_aif_stat = CMB_STUB_AIF_0;

/* A temp translation table between COMBO_AUDIO_STATE_X and CMB_STUB_AIF_X.
 * This is used for ALPS backward compatible ONLY!!! Remove this table, related
 * functions, and type definition after modifying other kernel built-in modules,
 * such as AUDIO. [FixMe][GeorgeKuo]
 */
static CMB_STUB_AIF_X audio2aif[] = {
    [COMBO_AUDIO_STATE_0] = CMB_STUB_AIF_0,
    [COMBO_AUDIO_STATE_1] = CMB_STUB_AIF_1,
    [COMBO_AUDIO_STATE_2] = CMB_STUB_AIF_2,
    [COMBO_AUDIO_STATE_3] = CMB_STUB_AIF_3,
};

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*!
 * \brief A registration function for WMT-PLAT to register itself to CMB-STUB.
 *
 * An MTK-WCN-CMB-STUB registration function provided to WMT-PLAT to register
 * itself and related callback functions when driver being loaded into kernel.
 *
 * \param p_stub_cb a pointer carrying CMB_STUB_CB information
 *
 * \retval 0 operation success
 * \retval -1 invalid parameters
 */
int
mtk_wcn_cmb_stub_reg (P_CMB_STUB_CB p_stub_cb)
{
    if ( (!p_stub_cb )
        || (p_stub_cb->size != sizeof(CMB_STUB_CB)) ) {
        CMB_STUB_LOG_WARN( "[cmb_stub] invalid p_stub_cb:0x%p size(%d)\n",
            p_stub_cb, (p_stub_cb) ? p_stub_cb->size: 0);
        return -1;
    }

    CMB_STUB_LOG_DBG( "[cmb_stub] registered, p_stub_cb:0x%p size(%d)\n",
        p_stub_cb, p_stub_cb->size);

    cmb_stub_aif_ctrl_cb = p_stub_cb->aif_ctrl_cb;
    cmb_stub_func_ctrl_cb = p_stub_cb->func_ctrl_cb;

    return 0;
}
EXPORT_SYMBOL(mtk_wcn_cmb_stub_reg);

/*!
 * \brief A unregistration function for WMT-PLAT to unregister from CMB-STUB.
 *
 * An MTK-WCN-CMB-STUB unregistration function provided to WMT-PLAT to
 * unregister itself and clear callback function references.
 *
 * \retval 0 operation success
 */
int
mtk_wcn_cmb_stub_unreg (void)
{
    cmb_stub_aif_ctrl_cb = NULL;
    cmb_stub_func_ctrl_cb = NULL;

    CMB_STUB_LOG_INFO("[cmb_stub] unregistered \n"); /* KERN_DEBUG */

    return 0;
}
EXPORT_SYMBOL(mtk_wcn_cmb_stub_unreg);

/* stub functions for kernel to control audio path pin mux */
int mtk_wcn_cmb_stub_aif_ctrl (CMB_STUB_AIF_X state, CMB_STUB_AIF_CTRL ctrl)
{
    int ret;

    if ( (CMB_STUB_AIF_MAX <= state)
        || (CMB_STUB_AIF_CTRL_MAX <= ctrl) ) {

        CMB_STUB_LOG_WARN("[cmb_stub] aif_ctrl invalid (%d, %d)\n", state, ctrl);
        return -1;
    }

    /* avoid the early interrupt before we register the eirq_handler */
    if (cmb_stub_aif_ctrl_cb){
        ret = (*cmb_stub_aif_ctrl_cb)(state, ctrl);
        CMB_STUB_LOG_INFO( "[cmb_stub] aif_ctrl_cb state(%d->%d) ctrl(%d) ret(%d)\n",
            cmb_stub_aif_stat , state, ctrl, ret); /* KERN_DEBUG */

        cmb_stub_aif_stat = state;
        return ret;
    }
    else {
        CMB_STUB_LOG_WARN("[cmb_stub] aif_ctrl_cb null \n");
        return -2;
    }
}
EXPORT_SYMBOL(mtk_wcn_cmb_stub_aif_ctrl);

/* Use a temp translation table between COMBO_AUDIO_STATE_X and CMB_STUB_AIF_X
 * for ALPS backward compatible ONLY!!! Remove this table, related functions,
 * and type definition after modifying other kernel built-in modules, such as
 * AUDIO. [FixMe][GeorgeKuo]
 */
int
mt_combo_audio_ctrl_ex (COMBO_AUDIO_STATE state, u32 clt_ctrl)
{
    /* input sanity check */
    if (COMBO_AUDIO_STATE_MAX < state) {
        CMB_STUB_LOG_WARN("[cmb_stub] invalid COMBO_AUDIO_STATE(%d)\n", state);
        return -1;
    }

    return mtk_wcn_cmb_stub_aif_ctrl(audio2aif[state],
        (clt_ctrl) ? CMB_STUB_AIF_CTRL_EN : CMB_STUB_AIF_CTRL_DIS );
}
EXPORT_SYMBOL(mt_combo_audio_ctrl_ex);

void mtk_wcn_cmb_stub_func_ctrl (unsigned int type, unsigned int on) {
    if (cmb_stub_func_ctrl_cb) {
        (*cmb_stub_func_ctrl_cb)(type, on);
    }
    else {
        CMB_STUB_LOG_WARN("[cmb_stub] func_ctrl_cb null \n");
    }
}
EXPORT_SYMBOL(mtk_wcn_cmb_stub_func_ctrl);

/*platform-related APIs*/
//void clr_device_working_ability(UINT32 clockId, MT6573_STATE state);
//void set_device_working_ability(UINT32 clockId, MT6573_STATE state);

static int
_mt_combo_plt_do_deep_idle(COMBO_IF src, int enter) {
    int ret = -1;

    const char *combo_if_name[] =
    {   "COMBO_IF_UART",
        "COMBO_IF_MSDC"
    };

    if(src != COMBO_IF_UART && src!= COMBO_IF_MSDC){
        CMB_STUB_LOG_WARN("src = %d is error\n", src);
        return ret;
    }

    if(src >= 0 && src < COMBO_IF_MAX){
        CMB_STUB_LOG_INFO("src = %s, to enter deep idle? %d \n",
            combo_if_name[src],
            enter);
    }

    /*TODO: For Common SDIO configuration, we need to do some judgement between STP and WIFI
            to decide if the msdc will enter deep idle safely*/

    switch(src){
        case COMBO_IF_UART:
            if(enter == 0){
                //clr_device_working_ability(MT65XX_PDN_PERI_UART3, DEEP_IDLE_STATE);
                //disable_dpidle_by_bit(MT65XX_PDN_PERI_UART2);
            } else {
                //set_device_working_ability(MT65XX_PDN_PERI_UART3, DEEP_IDLE_STATE);
                //enable_dpidle_by_bit(MT65XX_PDN_PERI_UART2);
            }
            ret = 0;
            break;

        case COMBO_IF_MSDC:
            if(enter == 0){
                //clr_device_working_ability(MT65XX_PDN_PERI_MSDC2, DEEP_IDLE_STATE);
            } else {
                //set_device_working_ability(MT65XX_PDN_PERI_MSDC2, DEEP_IDLE_STATE);
            }
            ret = 0;
            break;

        default:
            ret = -1;
            break;
    }

    return ret;
}

int
mt_combo_plt_enter_deep_idle (
    COMBO_IF src
    ) {
    //return 0;
    // TODO: [FixMe][GeorgeKuo] handling this depends on common UART or common SDIO
    return _mt_combo_plt_do_deep_idle(src, 1);
}
EXPORT_SYMBOL(mt_combo_plt_enter_deep_idle);

int
mt_combo_plt_exit_deep_idle (
    COMBO_IF src
    ) {
    //return 0;
    // TODO: [FixMe][GeorgeKuo] handling this depends on common UART or common SDIO
    return _mt_combo_plt_do_deep_idle(src, 0);
}
EXPORT_SYMBOL(mt_combo_plt_exit_deep_idle);

