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
#define DFT_TAG "[WMT-CMB-HW]"


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal.h"
#include "mtk_wcn_cmb_hw.h"
#include "wmt_plat.h"
#include "wmt_exp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define DFT_RTC_STABLE_TIME 100
#define DFT_LDO_STABLE_TIME 100
#define DFT_RST_STABLE_TIME 30
#define DFT_OFF_STABLE_TIME 10
#define DFT_ON_STABLE_TIME 30

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

PWR_SEQ_TIME gPwrSeqTime;




/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/



/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

INT32
mtk_wcn_cmb_hw_pwr_off (VOID)
{
    INT32 iRet = 0;
    WMT_INFO_FUNC("CMB-HW, hw_pwr_off start\n");

    /*1. disable irq --> should be done when do wmt-ic swDeinit period*/
    // TODO:[FixMe][GeorgeKuo] clarify this

    /*2. set bgf eint/all eint to deinit state, namely input low state*/
    iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_DEINIT);
    iRet += wmt_plat_gpio_ctrl(PIN_BGF_EINT, PIN_STA_DEINIT);
    WMT_INFO_FUNC("CMB-HW, BGF_EINT IRQ unregistered and set BGF_EINT GPIO to correct state!\n");
    /* 2.1 set ALL_EINT pin to correct state even it is not used currently */
    iRet += wmt_plat_eirq_ctrl(PIN_ALL_EINT, PIN_STA_EINT_DIS);
    WMT_INFO_FUNC("CMB-HW, ALL_EINT IRQ unregistered and disabled\n");
    iRet += wmt_plat_gpio_ctrl(PIN_ALL_EINT, PIN_STA_DEINIT);
    /* 2.2 deinit gps sync */
    iRet += wmt_plat_gpio_ctrl(PIN_GPS_SYNC, PIN_STA_DEINIT);

    /*3. set audio interface to CMB_STUB_AIF_0, BT PCM OFF, I2S OFF*/
    iRet += wmt_plat_audio_ctrl(CMB_STUB_AIF_0, CMB_STUB_AIF_CTRL_DIS);

    /*4. set control gpio into deinit state, namely input low state*/
    iRet += wmt_plat_gpio_ctrl(PIN_SDIO_GRP, PIN_STA_DEINIT);
    iRet += wmt_plat_gpio_ctrl(PIN_RST, PIN_STA_OUT_L);
    iRet += wmt_plat_gpio_ctrl(PIN_PMU, PIN_STA_OUT_L);

    /*5. set uart tx/rx into deinit state, namely input low state*/
    iRet += wmt_plat_gpio_ctrl(PIN_UART_GRP, PIN_STA_DEINIT);

    /* 6. Last, LDO output low */
    iRet += wmt_plat_gpio_ctrl(PIN_LDO, PIN_STA_OUT_L);
    
    /*7. deinit gps_lna*/
    iRet += wmt_plat_gpio_ctrl(PIN_GPS_LNA, PIN_STA_DEINIT);
    
    WMT_INFO_FUNC("CMB-HW, hw_pwr_off finish\n");
    return iRet;
}

INT32
mtk_wcn_cmb_hw_pwr_on (VOID)
{
    static UINT32 _pwr_first_time = 1;
    INT32 iRet = 0;

    WMT_INFO_FUNC("CMB-HW, hw_pwr_on start\n");
#if 0 //IRQ should in inact state before power on, so this step is not needed
    /* disable interrupt firstly */
    iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_DIS);
    iRet += wmt_plat_eirq_ctrl(PIN_ALL_EINT, PIN_STA_EINT_DIS);
#endif
    /*set all control and eint gpio to init state, namely input low mode*/
    iRet += wmt_plat_gpio_ctrl(PIN_LDO, PIN_STA_INIT);
    iRet += wmt_plat_gpio_ctrl(PIN_PMU, PIN_STA_INIT);
    iRet += wmt_plat_gpio_ctrl(PIN_RST, PIN_STA_INIT);
    iRet += wmt_plat_gpio_ctrl(PIN_SDIO_GRP, PIN_STA_INIT);
    iRet += wmt_plat_gpio_ctrl(PIN_BGF_EINT, PIN_STA_INIT);
    iRet += wmt_plat_gpio_ctrl(PIN_ALL_EINT, PIN_STA_INIT);
    iRet += wmt_plat_gpio_ctrl(PIN_GPS_SYNC, PIN_STA_INIT);
    iRet += wmt_plat_gpio_ctrl(PIN_GPS_LNA, PIN_STA_INIT);
    // wmt_plat_gpio_ctrl(PIN_WIFI_EINT, PIN_STA_INIT); /* WIFI_EINT is controlled by SDIO host driver */
    // TODO: [FixMe][George]:WIFI_EINT is used in common SDIO

    /*1. pull high LDO to supply power to chip*/
    iRet += wmt_plat_gpio_ctrl(PIN_LDO, PIN_STA_OUT_H);
    osal_msleep(gPwrSeqTime.ldoStableTime);

    /* 2. export RTC clock to chip*/
    if (_pwr_first_time) {
        /* rtc clock should be output all the time, so no need to enable output again*/
        iRet += wmt_plat_gpio_ctrl(PIN_RTC, PIN_STA_INIT);
        osal_msleep(gPwrSeqTime.rtcStableTime);
        WMT_INFO_FUNC("CMB-HW, rtc clock exported\n");
    }

    /*3. set UART Tx/Rx to UART mode*/
    iRet += wmt_plat_gpio_ctrl(PIN_UART_GRP, PIN_STA_INIT);

    /*4. PMU->output low, RST->output low, sleep off stable time*/
    iRet += wmt_plat_gpio_ctrl(PIN_PMU, PIN_STA_OUT_L);
    iRet += wmt_plat_gpio_ctrl(PIN_RST, PIN_STA_OUT_L);
    osal_msleep(gPwrSeqTime.offStableTime);

    /*5. PMU->output high, sleep rst stable time*/
    iRet += wmt_plat_gpio_ctrl(PIN_PMU, PIN_STA_OUT_H);
    osal_msleep(gPwrSeqTime.rstStableTime);

    /*6. RST->output high, sleep on stable time*/
    iRet += wmt_plat_gpio_ctrl(PIN_RST, PIN_STA_OUT_H);
    osal_msleep(gPwrSeqTime.onStableTime);

    /*7. set audio interface to CMB_STUB_AIF_1, BT PCM ON, I2S OFF*/
    /* BT PCM bus default mode. Real control is done by audio */
    iRet += wmt_plat_audio_ctrl(CMB_STUB_AIF_1, CMB_STUB_AIF_CTRL_DIS);

    /*8. set EINT< -ommited-> move this to WMT-IC module, where common sdio interface will be identified and do proper operation*/
    // TODO: [FixMe][GeorgeKuo] double check if BGF_INT is implemented ok
    iRet += wmt_plat_gpio_ctrl(PIN_BGF_EINT, PIN_STA_MUX);
    iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_INIT);
    iRet += wmt_plat_eirq_ctrl(PIN_BGF_EINT, PIN_STA_EINT_DIS);
    WMT_INFO_FUNC("CMB-HW, BGF_EINT IRQ registered and disabled \n");

    /* 8.1 set ALL_EINT pin to correct state even it is not used currently */
    iRet += wmt_plat_gpio_ctrl(PIN_ALL_EINT, PIN_STA_MUX);
    iRet += wmt_plat_eirq_ctrl(PIN_ALL_EINT, PIN_STA_INIT);
    iRet += wmt_plat_eirq_ctrl(PIN_ALL_EINT, PIN_STA_EINT_DIS);
    WMT_INFO_FUNC("CMB-HW, hw_pwr_on finish (%d)\n", iRet);

    _pwr_first_time = 0;
    return iRet;

}

INT32
mtk_wcn_cmb_hw_rst (VOID)
{
    INT32 iRet = 0;
    WMT_INFO_FUNC("CMB-HW, hw_rst start, eirq should be disabled before this step\n");

    /*1. PMU->output low, RST->output low, sleep off stable time*/
    iRet += wmt_plat_gpio_ctrl(PIN_PMU, PIN_STA_OUT_L);
    iRet += wmt_plat_gpio_ctrl(PIN_RST, PIN_STA_OUT_L);
    osal_msleep(gPwrSeqTime.offStableTime);

    /*2. PMU->output high, sleep rst stable time*/
    iRet += wmt_plat_gpio_ctrl(PIN_PMU, PIN_STA_OUT_H);
    osal_msleep(gPwrSeqTime.rstStableTime);

    /*3. RST->output high, sleep on stable time*/
    iRet += wmt_plat_gpio_ctrl(PIN_RST, PIN_STA_OUT_H);
    osal_msleep(gPwrSeqTime.onStableTime);
    WMT_INFO_FUNC("CMB-HW, hw_rst finish, eirq should be enabled after this step\n");
    return 0;
}

static VOID
mtk_wcn_cmb_hw_dmp_seq (VOID)
{
    PUINT32 pTimeSlot = (PUINT32)&gPwrSeqTime;
    WMT_INFO_FUNC("combo chip power on sequence time, RTC (%d), LDO (%d), RST(%d), OFF(%d), ON(%d)\n",
        pTimeSlot[0], /**pTimeSlot++,*/
        pTimeSlot[1],
        pTimeSlot[2],
        pTimeSlot[3],
        pTimeSlot[4]
        );
    return;
}

INT32
mtk_wcn_cmb_hw_init (
    P_PWR_SEQ_TIME pPwrSeqTime
    )
{
    if (NULL != pPwrSeqTime            && 
        pPwrSeqTime->ldoStableTime > 0 &&
        pPwrSeqTime->rtcStableTime > 0 &&
        pPwrSeqTime->offStableTime > DFT_OFF_STABLE_TIME &&
        pPwrSeqTime->onStableTime  > DFT_ON_STABLE_TIME  &&
        pPwrSeqTime->rstStableTime > DFT_RST_STABLE_TIME
    ) {
        /*memcpy may be more performance*/
        WMT_DBG_FUNC("setting hw init sequence parameters\n");
        osal_memcpy(&gPwrSeqTime, pPwrSeqTime, osal_sizeof(gPwrSeqTime));
    }
    else {    
        WMT_WARN_FUNC("invalid pPwrSeqTime parameter, use default hw init sequence parameters\n");
        gPwrSeqTime.ldoStableTime = DFT_LDO_STABLE_TIME;
        gPwrSeqTime.offStableTime = DFT_OFF_STABLE_TIME;
        gPwrSeqTime.onStableTime = DFT_ON_STABLE_TIME;
        gPwrSeqTime.rstStableTime = DFT_RST_STABLE_TIME;
        gPwrSeqTime.rtcStableTime = DFT_RTC_STABLE_TIME;
    }
    mtk_wcn_cmb_hw_dmp_seq();
    return 0;
}

INT32
mtk_wcn_cmb_hw_deinit (VOID)
{

    WMT_WARN_FUNC("mtk_wcn_cmb_hw_deinit start, set to default hw init sequence parameters\n");
    gPwrSeqTime.ldoStableTime = DFT_LDO_STABLE_TIME;
    gPwrSeqTime.offStableTime = DFT_OFF_STABLE_TIME;
    gPwrSeqTime.onStableTime = DFT_ON_STABLE_TIME;
    gPwrSeqTime.rstStableTime = DFT_RST_STABLE_TIME;
    gPwrSeqTime.rtcStableTime = DFT_RTC_STABLE_TIME;
    WMT_WARN_FUNC("mtk_wcn_cmb_hw_deinit finish\n");
    return 0;
}

