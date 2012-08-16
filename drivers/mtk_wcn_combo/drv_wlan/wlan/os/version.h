/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_2/os/version.h#1 $
*/

/*! \file   "version.h"
    \brief  Driver's version definition

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
** $Log: version.h $
 *
 * 10 19 2011 yuche.tsai
 * [WCXRP00001045] [WiFi Direct][Driver] Check 2.1 branch.
 * Branch 2.1
 * Davinci Maintrunk Label: MT6620_WIFI_DRIVER_FW_TRUNK_MT6620E5_111019_0926.
 *
 * 03 18 2011 cp.wu
 * [WCXRP00000577] [MT6620 Wi-Fi][Driver][FW] Create V2.0 branch for firmware and driver
 * create V2.0 driver release based on label "MT6620_WIFI_DRIVER_V2_0_110318_1600" from main trunk
 *
 * 03 18 2011 chinglan.wang
 * NULL
 * Change the version number to v2.0.0.0.
 *
 * 02 11 2011 chinglan.wang
 * NULL
 * Change to the version 1.2.0.2.
 *
 * 02 10 2011 chinglan.wang
 * NULL
 * Change the version to 1.2.0.1.
 *
 * 02 08 2011 cp.wu
 * [WCXRP00000427] [MT6620 Wi-Fi][Driver] Modify veresion information to match with release revision number
 * change version number to v1.2.0.0 for preparing v1.2 software package release.
 *
 * 12 10 2010 kevin.huang
 * [WCXRP00000128] [MT6620 Wi-Fi][Driver] Add proc support to Android Driver for debug and driver status check
 * Add Linux Proc Support
 *
 * 10 07 2010 cp.wu
 * [WCXRP00000083] [MT5931][Driver][FW] Add necessary logic for MT5931 first connection
 * [WINDDK] build system changes for MT5931
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
**  \main\maintrunk.MT6620WiFiDriver_Prj\5 2009-12-14 14:10:55 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\4 2009-11-17 22:41:00 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\3 2009-11-13 16:20:33 GMT mtk01084
**  \main\maintrunk.MT6620WiFiDriver_Prj\2 2009-03-10 20:27:13 GMT mtk01426
**  Init for develop
**
*/

#ifndef _VERSION_H
#define _VERSION_H
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

#ifndef NIC_AUTHOR
#define NIC_AUTHOR      "NIC_AUTHOR"
#endif
#ifndef NIC_DESC
#define NIC_DESC        "NIC_DESC"
#endif

#ifndef NIC_NAME
    #if defined(MT6620)
        #define NIC_NAME            "MT6620"
        #define NIC_DEVICE_ID       "MT6620"
		#define NIC_DEVICE_ID_LOW   "mt6620"
    #elif defined(MT5931)
        #define NIC_NAME            "MT5931"
        #define NIC_DEVICE_ID       "MT5931"
		#define NIC_DEVICE_ID_LOW   "mt5931"
    #elif defined(MT6628)
        #define NIC_NAME            "MT6628"
        #define NIC_DEVICE_ID       "MT6628"
		#define NIC_DEVICE_ID_LOW   "mt6628"
    #endif
#endif

/* NIC driver information */
#define NIC_VENDOR                      "MediaTek Inc."
#define NIC_VENDOR_OUI                  {0x00, 0x0C, 0xE7}

#if defined(MT6620)
    #define NIC_PRODUCT_NAME                "MediaTek Inc. MT6620 Wireless LAN Adapter"
    #define NIC_DRIVER_NAME                 "MediaTek Inc. MT6620 Wireless LAN Adapter Driver"
#elif defined(MT5931)
    #define NIC_PRODUCT_NAME                "MediaTek Inc. MT5931 Wireless LAN Adapter"
    #define NIC_DRIVER_NAME                 "MediaTek Inc. MT5931 Wireless LAN Adapter Driver"
#elif defined(MT6628)
    #define NIC_PRODUCT_NAME                "MediaTek Inc. MT6628 Wireless LAN Adapter"
    #define NIC_DRIVER_NAME                 "MediaTek Inc. MT6628 Wireless LAN Adapter Driver"
#endif

/* Define our driver version */
#define NIC_DRIVER_MAJOR_VERSION        2
#define NIC_DRIVER_MINOR_VERSION        0
#define NIC_DRIVER_VERSION              2,0,0,9
#define NIC_DRIVER_VERSION_STRING       "2.0.0.9"


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


#endif /* _VERSION_H */

