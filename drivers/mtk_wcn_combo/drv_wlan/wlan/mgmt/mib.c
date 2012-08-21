/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/mgmt/mib.c#1 $
*/

/*! \file   "mib.c"
    \brief  This file includes the mib default vale and functions.
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
** $Log: mib.c $
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 07 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add mib.c.
 *
 * 02 04 2010 kevin.huang
 * [BORA00000603][WIFISYS] [New Feature] AAA Module Support 
 * Add AAA Module Support, Revise Net Type to Net Type Index for array lookup
 *
 * Nov 23 2009 mtk01461
 * [BORA00000018] Integrate WIFI part into BORA for the 1st time
 * 
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "precomp.h"

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
NON_HT_PHY_ATTRIBUTE_T rNonHTPhyAttributes[] = {
    {RATE_SET_HR_DSSS,      TRUE,       FALSE   },  /* For PHY_TYPE_HR_DSSS_INDEX(0) */
    {RATE_SET_ERP,          TRUE,       TRUE    },  /* For PHY_TYPE_ERP_INDEX(1) */
    {RATE_SET_ERP_P2P,      TRUE,       TRUE    },  /* For PHY_TYPE_ERP_P2P_INDEX(2) */
    {RATE_SET_OFDM,         FALSE,      FALSE   },  /* For PHY_TYPE_OFDM_INDEX(3) */
};

NON_HT_ADHOC_MODE_ATTRIBUTE_T rNonHTAdHocModeAttributes[AD_HOC_MODE_NUM] = {
    {PHY_TYPE_HR_DSSS_INDEX,    BASIC_RATE_SET_HR_DSSS},        /* For AD_HOC_MODE_11B(0) */
    {PHY_TYPE_ERP_INDEX,        BASIC_RATE_SET_HR_DSSS_ERP},    /* For AD_HOC_MODE_MIXED_11BG(1) */
    {PHY_TYPE_ERP_INDEX,        BASIC_RATE_SET_ERP},            /* For AD_HOC_MODE_11G(2) */
    {PHY_TYPE_OFDM_INDEX,       BASIC_RATE_SET_OFDM},           /* For AD_HOC_MODE_11A(3) */
};

NON_HT_AP_MODE_ATTRIBUTE_T rNonHTApModeAttributes[AP_MODE_NUM] = {
    {PHY_TYPE_HR_DSSS_INDEX,    BASIC_RATE_SET_HR_DSSS},        /* For AP_MODE_11B(0) */
    {PHY_TYPE_ERP_INDEX,        BASIC_RATE_SET_HR_DSSS_ERP},    /* For AP_MODE_MIXED_11BG(1) */
    {PHY_TYPE_ERP_INDEX,        BASIC_RATE_SET_ERP},            /* For AP_MODE_11G(2) */
    {PHY_TYPE_ERP_P2P_INDEX,    BASIC_RATE_SET_ERP_P2P},        /* For AP_MODE_11G_P2P(3) */
    {PHY_TYPE_OFDM_INDEX,       BASIC_RATE_SET_OFDM},           /* For AP_MODE_11A(4) */
};

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

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/


