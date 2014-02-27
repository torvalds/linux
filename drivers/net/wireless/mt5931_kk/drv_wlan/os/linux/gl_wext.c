/*
** $Id: //Department/DaVinci/BRANCHES/MT662X_593X_WIFI_DRIVER_V2_3/os/linux/gl_wext.c#1 $
*/

/*! \file gl_wext.c
    \brief  ioctl() (mostly Linux Wireless Extensions) routines for STA driver.
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
** $Log: gl_wext.c $
** 
** 08 24 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** .
** 
** 08 24 2012 cp.wu
** [WCXRP00001269] [MT6620 Wi-Fi][Driver] cfg80211 porting merge back to DaVinci
** cfg80211 support merge back from ALPS.JB to DaVinci - MT6620 Driver v2.3 branch.
 *
 * 06 13 2012 yuche.tsai
 * NULL
 * Update maintrunk driver.
 * Add support for driver compose assoc request frame.
 *
 * 01 16 2012 wh.su
 * [WCXRP00001170] [MT6620 Wi-Fi][Driver] Adding the related code for set/get band ioctl
 * Adding the template code for set / get band IOCTL (with ICS supplicant_6)..
 *
 * 01 05 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the related ioctl / wlan oid function to set the Tx power cfg.
 *
 * 01 02 2012 wh.su
 * [WCXRP00001153] [MT6620 Wi-Fi][Driver] Adding the get_ch_list and set_tx_power proto type function
 * Adding the proto type function for set_int set_tx_power and get int get_ch_list.
 *
 * 11 10 2011 cp.wu
 * [WCXRP00001098] [MT6620 Wi-Fi][Driver] Replace printk by DBG LOG macros in linux porting layer
 * 1. eliminaite direct calls to printk in porting layer.
 * 2. replaced by DBGLOG, which would be XLOG on ALPS platforms.
 *
 * 10 12 2011 wh.su
 * [WCXRP00001036] [MT6620 Wi-Fi][Driver][FW] Adding the 802.11w code for MFP
 * adding the 802.11w related function and define .
 *
 * 09 23 2011 tsaiyuan.hsu
 * [WCXRP00000979] [MT6620 Wi-Fi][DRV]] stop attempting to connect to config AP after D3 state
 * avoid entering D3 state after deep sleep.
 *
 * 07 28 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings
 * Add BWCS cmd and event.
 *
 * 07 27 2011 wh.su
 * [WCXRP00000877] [MT6620 Wi-Fi][Driver] Remove the netif_carry_ok check for avoid the wpa_supplicant fail to query the ap address
 * Remove the netif check while query bssid and ssid
 *
 * 07 26 2011 chinglan.wang
 * NULL
 * [MT6620][WiFi Driver] Do not include the WSC IE in the association info packet when not do the wps connection..
 *
 * 07 18 2011 chinghwa.yu
 * [WCXRP00000063] Update BCM CoEx design and settings[WCXRP00000612] [MT6620 Wi-Fi] [FW] CSD update SWRDD algorithm
 * Add CMD/Event for RDD and BWCS.
 *
 * 05 17 2011 eddie.chen
 * [WCXRP00000603] [MT6620 Wi-Fi][DRV] Fix Klocwork warning
 * Initilize the vairlabes.
 *
 * 05 11 2011 jeffrey.chang
 * [WCXRP00000718] [MT6620 Wi-Fi] modify the behavior of setting tx power
 * modify set_tx_pow ioctl
 *
 * 03 29 2011 terry.wu
 * [WCXRP00000610] [MT 6620 Wi-Fi][Driver] Fix klocwork waring
 * [MT6620 Wi-Fi][Driver] Fix klocwork warning. Add Null pointer check on wext_get_essid. Limit the upper bound of essid storage array.
 *
 * 03 21 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * improve portability for awareness of early version of linux kernel and wireless extension.
 *
 * 03 17 2011 chinglan.wang
 * [WCXRP00000570] [MT6620 Wi-Fi][Driver] Add Wi-Fi Protected Setup v2.0 feature
 * .
 *
 * 03 07 2011 terry.wu
 * [WCXRP00000521] [MT6620 Wi-Fi][Driver] Remove non-standard debug message
 * Toggle non-standard debug messages to comments.
 *
 * 02 21 2011 wh.su
 * [WCXRP00000483] [MT6620 Wi-Fi][Driver] Check the kalIoctl return value before doing the memory copy at linux get essid
 * fixed the potential error to do a larget memory copy while wlanoid get essid not actually running.
 *
 * 02 08 2011 george.huang
 * [WCXRP00000422] [MT6620 Wi-Fi][Driver] support query power mode OID handler
 * Support querying power mode OID.
 *
 * 01 29 2011 wh.su
 * [WCXRP00000408] [MT6620 Wi-Fi][Driver] Not doing memory alloc while ioctl set ie with length 0
 * not doing mem alloc. while set ie length already 0
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Remove debug text.
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Adjust OID order.
 *
 * 01 20 2011 eddie.chen
 * [WCXRP00000374] [MT6620 Wi-Fi][DRV] SW debug control
 * Add Oid for sw control debug command
 *
 * 01 11 2011 chinglan.wang
 * NULL
 * Modify to reslove the CR :[ALPS00028994] Use WEP security to connect Marvell 11N AP.  Connection establish successfully.
 * Use the WPS function to connect AP, the privacy bit always is set to 1. .
 *
 * 01 07 2011 cm.chang
 * [WCXRP00000336] [MT6620 Wi-Fi][Driver] Add test mode commands in normal phone operation
 * Add a new compiling option to control if MCR read/write is permitted
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease physically continous memory demands
 * separate kalMemAlloc() into virtually-continous and physically-continous types
 * to ease slab system pressure
 *
 * 01 04 2011 cp.wu
 * [WCXRP00000338] [MT6620 Wi-Fi][Driver] Separate kalMemAlloc into kmalloc and vmalloc implementations to ease physically continous memory demands
 * separate kalMemAlloc() into virtually-continous and physically-continous type to ease slab system pressure
 *
 * 12 31 2010 cm.chang
 * [WCXRP00000336] [MT6620 Wi-Fi][Driver] Add test mode commands in normal phone operation
 * Add some iwpriv commands to support test mode operation
 *
 * 12 15 2010 george.huang
 * [WCXRP00000152] [MT6620 Wi-Fi] AP mode power saving function
 * Support set PS profile and set WMM-PS related iwpriv.
 *
 * 12 15 2010 george.huang
 * [WCXRP00000152] [MT6620 Wi-Fi] AP mode power saving function
 * Allow change PS profile function (throught wext_set_power()).
 *
 * 12 14 2010 jeffrey.chang
 * [WCXRP00000262] [MT6620 Wi-Fi][Driver] modify the scan request ioctl to handle hidden SSID
 * handle hidden SSID
 *
 * 12 13 2010 chinglan.wang
 * NULL
 * Add WPS 1.0 feature flag to enable the WPS 1.0 function.
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * Fix compiling error
 *
 * 12 07 2010 cm.chang
 * [WCXRP00000238] MT6620 Wi-Fi][Driver][FW] Support regulation domain setting from NVRAM and supplicant
 * 1. Country code is from NVRAM or supplicant
 * 2. Change band definition in CMD/EVENT.
 *
 * 11 30 2010 cp.wu
 * [WCXRP00000213] [MT6620 Wi-Fi][Driver] Implement scanning with specified SSID for wpa_supplicant with ap_scan=1
 * .
 *
 * 11 08 2010 wh.su
 * [WCXRP00000171] [MT6620 Wi-Fi][Driver] Add message check code same behavior as mt5921
 * add the message check code from mt5921.
 *
 * 10 19 2010 jeffrey.chang
 * [WCXRP00000121] [MT6620 Wi-Fi][Driver] Temporarily disable set power mode ioctl which may cause 6620 to enter power saving
 * Temporarily disable set power mode ioctl which may cause MT6620 to enter power saving
 *
 * 10 18 2010 jeffrey.chang
 * [WCXRP00000116] [MT6620 Wi-Fi][Driver] Refine the set_scan ioctl to resolve the Android UI hanging issue
 * refine the scan ioctl to prevent hanging of Android UI
 *
 * 10 01 2010 wh.su
 * [WCXRP00000067] [MT6620 Wi-Fi][Driver] Support the android+ WAPI function
 * add the scan result with wapi ie.
 *
 * 09 30 2010 wh.su
 * [WCXRP00000072] [MT6620 Wi-Fi][Driver] Fix TKIP Counter Measure EAPoL callback register issue
 * fixed the wapi ie assigned issue.
 *
 * 09 27 2010 wh.su
 * NULL
 * [WCXRP00000067][MT6620 Wi-Fi][Driver] Support the android+ WAPI function.
 *
 * 09 21 2010 kevin.huang
 * [WCXRP00000052] [MT6620 Wi-Fi][Driver] Eliminate Linux Compile Warning
 * Eliminate Linux Compile Warning
 *
 * 09 09 2010 cp.wu
 * NULL
 * add WPS/WPA/RSN IE for Wi-Fi Direct scanning result.
 *
 * 09 06 2010 cp.wu
 * NULL
 * Androi/Linux: return current operating channel information
 *
 * 09 01 2010 wh.su
 * NULL
 * adding the wapi support for integration test.
 *
 * 08 02 2010 jeffrey.chang
 * NULL
 * enable remove key ioctl
 *
 * 08 02 2010 jeffrey.chang
 * NULL
 * 1) modify tx service thread to avoid busy looping
 * 2) add spin lock declartion for linux build
 *
 * 07 28 2010 jeffrey.chang
 * NULL
 * 1) enable encyption ioctls
 * 2) temporarily disable remove keys ioctl to prevent  TX1 busy
 *
 * 07 28 2010 jeffrey.chang
 * NULL
 * 1) remove unused spinlocks
 * 2) enable encyption ioctls
 * 3) fix scan ioctl which may cause supplicant to hang
 *
 * 07 19 2010 jeffrey.chang
 *
 * add kal api for scanning done
 *
 * 07 19 2010 jeffrey.chang
 *
 * for linux driver migration
 *
 * 07 19 2010 jeffrey.chang
 *
 * Linux port modification
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base
 * [MT6620 5931] Create driver base
 *
 * 05 28 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * remove unused macro and debug messages
 *
 * 05 14 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * Add dissassoication support for wpa supplicant
 *
 * 04 22 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 *
 * 1) modify rx path code for supporting Wi-Fi direct
 * 2) modify config.h since Linux dont need to consider retaining packet
 *
 * 04 21 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * add for private ioctl support
 *
 * 04 19 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * Add ioctl of power management
 *
 * 04 19 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * remove debug message
 *
 * 04 14 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * 1) prGlueInfo->pvInformationBuffer and prGlueInfo->u4InformationBufferLength are no longer used
 *  * 2) fix ioctl
 *
 * 04 12 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * remove debug messages for pre-release
 *
 * 04 07 2010 cp.wu
 * [WPD00001943]Create WiFi test driver framework on WinXP
 * rWlanInfo should be placed at adapter rather than glue due to most operations
 *  *  *  *  *  *  *  * are done in adapter layer.
 *
 * 04 02 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * fix ioctl type
 *
 * 04 01 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * enable pmksa cache operation
 *
 * 03 31 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * fix ioctl which may cause cmdinfo memory leak
 *
 * 03 31 2010 wh.su
 * [WPD00003816][MT6620 Wi-Fi] Adding the security support
 * modify the wapi related code for new driver's design.
 *
 * 03 30 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * emulate NDIS Pending OID facility
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port
 * initial import for Linux port
**  \main\maintrunk.MT5921\38 2009-10-08 10:33:22 GMT mtk01090
**  Avoid accessing private data of net_device directly. Replace with netdev_priv(). Add more checking for input parameters and pointers.
**  \main\maintrunk.MT5921\37 2009-09-29 16:49:48 GMT mtk01090
**  Remove unused variables
**  \main\maintrunk.MT5921\36 2009-09-28 20:19:11 GMT mtk01090
**  Add private ioctl to carry OID structures. Restructure public/private ioctl interfaces to Linux kernel.
**  \main\maintrunk.MT5921\35 2009-09-03 11:42:30 GMT mtk01088
**  adding the wapi ioctl support
**  \main\maintrunk.MT5921\34 2009-08-18 22:56:50 GMT mtk01090
**  Add Linux SDIO (with mmc core) support.
**  Add Linux 2.6.21, 2.6.25, 2.6.26.
**  Fix compile warning in Linux.
**  \main\maintrunk.MT5921\33 2009-05-14 22:43:47 GMT mtk01089
**  fix compiling warning
**  \main\maintrunk.MT5921\32 2009-05-07 22:26:18 GMT mtk01089
**  Add mandatory and private IO control for Linux BWCS
**  \main\maintrunk.MT5921\31 2009-02-07 15:11:14 GMT mtk01088
**  fixed the compiling error
**  \main\maintrunk.MT5921\30 2009-02-07 14:46:51 GMT mtk01088
**  add the privacy setting from linux supplicant ap selection
**  \main\maintrunk.MT5921\29 2008-11-19 15:18:50 GMT mtk01088
**  fixed the compling error
**  \main\maintrunk.MT5921\28 2008-11-19 11:56:18 GMT mtk01088
**  rename some variable with pre-fix to avoid the misunderstanding
**  \main\maintrunk.MT5921\27 2008-08-29 16:59:43 GMT mtk01088
**  fixed compiling error
**  \main\maintrunk.MT5921\26 2008-08-29 14:55:53 GMT mtk01088
**  adjust the code for meet the coding style, and add assert check
**  \main\maintrunk.MT5921\25 2008-06-02 11:15:19 GMT mtk01461
**  Update after wlanoidSetPowerMode changed
**  \main\maintrunk.MT5921\24 2008-05-30 15:13:12 GMT mtk01084
**  rename wlanoid
**  \main\maintrunk.MT5921\23 2008-03-28 10:40:28 GMT mtk01461
**  Add set desired rate in Linux STD IOCTL
**  \main\maintrunk.MT5921\22 2008-03-18 10:31:24 GMT mtk01088
**  add pmkid ioctl and indicate
**  \main\maintrunk.MT5921\21 2008-03-11 15:21:24 GMT mtk01461
**  \main\maintrunk.MT5921\20 2008-03-11 14:50:55 GMT mtk01461
**  Refine WPS related priv ioctl for unified interface
**
**  \main\maintrunk.MT5921\19 2008-03-06 16:30:41 GMT mtk01088
**  move the configuration code from set essid function,
**  remove the non-used code
**  \main\maintrunk.MT5921\18 2008-02-21 15:47:09 GMT mtk01461
**  Fix CR[489]
**  \main\maintrunk.MT5921\17 2008-02-12 23:38:31 GMT mtk01461
**  Add Set Frequency & Channel oid support for Linux
**  \main\maintrunk.MT5921\16 2008-01-24 12:07:34 GMT mtk01461
**  \main\maintrunk.MT5921\15 2008-01-24 12:00:10 GMT mtk01461
**  Modify the wext_essid for set up correct information for IBSS, and fix the wrong input ptr for prAdapter
**  \main\maintrunk.MT5921\14 2007-12-06 09:30:12 GMT mtk01425
**  1. Branch Test
**  \main\maintrunk.MT5921\13 2007-12-04 18:07:59 GMT mtk01461
**  fix typo
**  \main\maintrunk.MT5921\12 2007-11-30 17:10:21 GMT mtk01425
**  1. Fix compiling erros
**
**  \main\maintrunk.MT5921\11 2007-11-27 10:43:22 GMT mtk01425
**  1. Add WMM-PS setting
**  \main\maintrunk.MT5921\10 2007-11-06 20:33:32 GMT mtk01088
**  fixed the compiler error
**  \main\maintrunk.MT5921\9 2007-11-06 19:33:15 GMT mtk01088
**  add WPS code
**  \main\maintrunk.MT5921\8 2007-10-30 12:00:44 GMT MTK01425
**  1. Update wlanQueryInformation
*/

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "gl_os.h"

#include "config.h"
#include "wlan_oid.h"

#include "gl_wext.h"
#include "gl_wext_priv.h"

#include "precomp.h"

#if CFG_SUPPORT_WAPI
#include "gl_sec.h"
#endif

/* compatibility to wireless extensions */
#ifdef WIRELESS_EXT

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
const long channel_freq[] = {
        2412, 2417, 2422, 2427, 2432, 2437, 2442,
        2447, 2452, 2457, 2462, 2467, 2472, 2484
};

#define     MAP_CHANNEL_ID_TO_KHZ(ch, khz)  {               \
                switch (ch)                                 \
                {                                           \
                    case 1:     khz = 2412000;   break;     \
                    case 2:     khz = 2417000;   break;     \
                    case 3:     khz = 2422000;   break;     \
                    case 4:     khz = 2427000;   break;     \
                    case 5:     khz = 2432000;   break;     \
                    case 6:     khz = 2437000;   break;     \
                    case 7:     khz = 2442000;   break;     \
                    case 8:     khz = 2447000;   break;     \
                    case 9:     khz = 2452000;   break;     \
                    case 10:    khz = 2457000;   break;     \
                    case 11:    khz = 2462000;   break;     \
                    case 12:    khz = 2467000;   break;     \
                    case 13:    khz = 2472000;   break;     \
                    case 14:    khz = 2484000;   break;     \
                    case 36:  /* UNII */  khz = 5180000;   break;     \
                    case 40:  /* UNII */  khz = 5200000;   break;     \
                    case 44:  /* UNII */  khz = 5220000;   break;     \
                    case 48:  /* UNII */  khz = 5240000;   break;     \
                    case 52:  /* UNII */  khz = 5260000;   break;     \
                    case 56:  /* UNII */  khz = 5280000;   break;     \
                    case 60:  /* UNII */  khz = 5300000;   break;     \
                    case 64:  /* UNII */  khz = 5320000;   break;     \
                    case 149: /* UNII */  khz = 5745000;   break;     \
                    case 153: /* UNII */  khz = 5765000;   break;     \
                    case 157: /* UNII */  khz = 5785000;   break;     \
                    case 161: /* UNII */  khz = 5805000;   break;     \
                    case 165: /* UNII */  khz = 5825000;   break;     \
                    case 100: /* HiperLAN2 */  khz = 5500000;   break;     \
                    case 104: /* HiperLAN2 */  khz = 5520000;   break;     \
                    case 108: /* HiperLAN2 */  khz = 5540000;   break;     \
                    case 112: /* HiperLAN2 */  khz = 5560000;   break;     \
                    case 116: /* HiperLAN2 */  khz = 5580000;   break;     \
                    case 120: /* HiperLAN2 */  khz = 5600000;   break;     \
                    case 124: /* HiperLAN2 */  khz = 5620000;   break;     \
                    case 128: /* HiperLAN2 */  khz = 5640000;   break;     \
                    case 132: /* HiperLAN2 */  khz = 5660000;   break;     \
                    case 136: /* HiperLAN2 */  khz = 5680000;   break;     \
                    case 140: /* HiperLAN2 */  khz = 5700000;   break;     \
                    case 34:  /* Japan MMAC */   khz = 5170000;   break;   \
                    case 38:  /* Japan MMAC */   khz = 5190000;   break;   \
                    case 42:  /* Japan MMAC */   khz = 5210000;   break;   \
                    case 46:  /* Japan MMAC */   khz = 5230000;   break;   \
                    case 184: /* Japan */   khz = 4920000;   break;   \
                    case 188: /* Japan */   khz = 4940000;   break;   \
                    case 192: /* Japan */   khz = 4960000;   break;   \
                    case 196: /* Japan */   khz = 4980000;   break;   \
                    case 208: /* Japan, means J08 */   khz = 5040000;   break;   \
                    case 212: /* Japan, means J12 */   khz = 5060000;   break;   \
                    case 216: /* Japan, means J16 */   khz = 5080000;   break;   \
                    default:    khz = 2412000;   break;     \
                }                                           \
            }


#define NUM_CHANNELS (sizeof(channel_freq) / sizeof(channel_freq[0]))

#define MAX_SSID_LEN    32


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/
/* NOTE: name in iwpriv_args only have 16 bytes */
static const struct iw_priv_args rIwPrivTable[] = {
    {IOCTL_SET_INT,             IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0,   ""},
    {IOCTL_GET_INT,             0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   ""},
    {IOCTL_SET_INT,             IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 3, 0,   ""},
    {IOCTL_GET_INT,             0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 3,   ""},
    {IOCTL_SET_INT,             IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0,   ""},

    {IOCTL_GET_INT,             IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   ""},
    {IOCTL_GET_INT,             IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   ""},

    {IOCTL_SET_INTS,            IW_PRIV_TYPE_INT | 4, 0,                        ""},
    {IOCTL_GET_INT,             0, IW_PRIV_TYPE_INT | 50,                       ""},
    {IOCTL_GET_INT,             0, IW_PRIV_TYPE_CHAR | 16,                      ""},

    /* added for set_oid and get_oid */
    {IOCTL_SET_STRUCT,          256,                                    0, ""},
    {IOCTL_GET_STRUCT,          0,                                      256, ""},

    /* sub-ioctl definitions */
#if 0
    {PRIV_CMD_REG_DOMAIN,       IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0,   "set_reg_domain" },
    {PRIV_CMD_REG_DOMAIN,       0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   "get_reg_domain" },
#endif

#if CFG_TCP_IP_CHKSUM_OFFLOAD
    {PRIV_CMD_CSUM_OFFLOAD,     IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0,   "set_tcp_csum" },
#endif /* CFG_TCP_IP_CHKSUM_OFFLOAD */

    {PRIV_CMD_POWER_MODE,       IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0,   "set_power_mode" },
    {PRIV_CMD_POWER_MODE,       0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   "get_power_mode" },

    {PRIV_CMD_WMM_PS,           IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 3, 0,   "set_wmm_ps" },

    {PRIV_CMD_TEST_MODE,        IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0,   "set_test_mode" },
    {PRIV_CMD_TEST_CMD,         IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0,   "set_test_cmd" },
    {PRIV_CMD_TEST_CMD,         IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   "get_test_result" },
#if CFG_SUPPORT_PRIV_MCR_RW
    {PRIV_CMD_ACCESS_MCR,       IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0,   "set_mcr" },
    {PRIV_CMD_ACCESS_MCR,       IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   "get_mcr" },
#endif
    {PRIV_CMD_SW_CTRL,          IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0,   "set_sw_ctrl" },
    {PRIV_CMD_SW_CTRL,          IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   "get_sw_ctrl" },

#if CFG_SUPPORT_BCM && CFG_SUPPORT_BCM_BWCS
    {PRIV_CUSTOM_BWCS_CMD,              IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0, "set_bwcs"},
    /* GET STRUCT sub-ioctls commands */
    {PRIV_CUSTOM_BWCS_CMD,              IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, "get_bwcs"},
#endif

    /* SET STRUCT sub-ioctls commands */
    {PRIV_CMD_OID,              256, 0, "set_oid"},
    /* GET STRUCT sub-ioctls commands */
    {PRIV_CMD_OID,              0, 256, "get_oid"},

    {PRIV_CMD_BAND_CONFIG,      IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1, 0,   "set_band" },
    {PRIV_CMD_BAND_CONFIG,      0, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   "get_band" },

    {PRIV_CMD_SET_TX_POWER,     IW_PRIV_TYPE_INT | 4, 0,                        "set_txpower" },
    {PRIV_CMD_GET_CH_LIST,      0, IW_PRIV_TYPE_INT | 50,                       "get_ch_list" },
    {PRIV_CMD_DUMP_MEM,         IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,   "get_mem" },

#if CFG_ENABLE_WIFI_DIRECT
    {PRIV_CMD_P2P_MODE,         IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0,   "set_p2p_mode" },
#endif
#if CFG_SUPPORT_BUILD_DATE_CODE
    {PRIV_CMD_GET_BUILD_DATE_CODE,      0, IW_PRIV_TYPE_CHAR | 16,              "get_date_code" },
#endif    
};

static const iw_handler rIwPrivHandler[] = {
    [IOCTL_SET_INT - SIOCIWFIRSTPRIV] = priv_set_int,
    [IOCTL_GET_INT - SIOCIWFIRSTPRIV] = priv_get_int,
    [IOCTL_SET_ADDRESS - SIOCIWFIRSTPRIV] = NULL,
    [IOCTL_GET_ADDRESS - SIOCIWFIRSTPRIV] = NULL,
    [IOCTL_SET_STR - SIOCIWFIRSTPRIV] = NULL,
    [IOCTL_GET_STR - SIOCIWFIRSTPRIV] = NULL,
    [IOCTL_SET_KEY - SIOCIWFIRSTPRIV] = NULL,
    [IOCTL_GET_KEY - SIOCIWFIRSTPRIV] = NULL,
    [IOCTL_SET_STRUCT - SIOCIWFIRSTPRIV] = priv_set_struct,
    [IOCTL_GET_STRUCT - SIOCIWFIRSTPRIV] = priv_get_struct,
    [IOCTL_SET_STRUCT_FOR_EM - SIOCIWFIRSTPRIV] = priv_set_struct,
    [IOCTL_SET_INTS - SIOCIWFIRSTPRIV] = priv_set_ints,
    [IOCTL_GET_INTS - SIOCIWFIRSTPRIV] = priv_get_ints,
};

const struct iw_handler_def wext_handler_def = {
    .num_standard   = 0,
    .num_private = (__u16)sizeof(rIwPrivHandler)/sizeof(iw_handler),
    .num_private_args = (__u16)sizeof(rIwPrivTable)/sizeof(struct iw_priv_args),
    .standard   = (iw_handler *) NULL,
    .private = rIwPrivHandler,
    .private_args = rIwPrivTable,
    .get_wireless_stats = wext_get_wireless_stats,
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
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* \brief Find the desired WPA/RSN Information Element according to desiredElemID.
*
* \param[in] pucIEStart IE starting address.
* \param[in] i4TotalIeLen Total length of all the IE.
* \param[in] ucDesiredElemId Desired element ID.
* \param[out] ppucDesiredIE Pointer to the desired IE.
*
* \retval TRUE Find the desired IE.
* \retval FALSE Desired IE not found.
*
* \note
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wextSrchDesiredWPAIE (
    IN  PUINT_8         pucIEStart,
    IN  INT_32          i4TotalIeLen,
    IN  UINT_8          ucDesiredElemId,
    OUT PUINT_8         *ppucDesiredIE
    )
{
    INT_32 i4InfoElemLen;

    ASSERT(pucIEStart);
    ASSERT(ppucDesiredIE);

    while (i4TotalIeLen >= 2) {
        i4InfoElemLen = (INT_32) pucIEStart[1] + 2;

        if (pucIEStart[0] == ucDesiredElemId && i4InfoElemLen <= i4TotalIeLen) {
            if (ucDesiredElemId != 0xDD) {
                /* Non 0xDD, OK! */
                *ppucDesiredIE = &pucIEStart[0];
                return TRUE;
            }
            else {
                /* EID == 0xDD, check WPA IE */
                if (pucIEStart[1] >= 4) {
                    if (memcmp(&pucIEStart[2], "\x00\x50\xf2\x01", 4) == 0) {
                        *ppucDesiredIE = &pucIEStart[0];
                        return TRUE;
                    }
                } /* check WPA IE length */
            } /* check EID == 0xDD */
        } /* check desired EID */

        /* Select next information element. */
        i4TotalIeLen -= i4InfoElemLen;
        pucIEStart += i4InfoElemLen;
    }

    return FALSE;
} /* parseSearchDesiredWPAIE */


#if CFG_SUPPORT_WAPI
/*----------------------------------------------------------------------------*/
/*!
* \brief Find the desired WAPI Information Element .
*
* \param[in] pucIEStart IE starting address.
* \param[in] i4TotalIeLen Total length of all the IE.
* \param[out] ppucDesiredIE Pointer to the desired IE.
*
* \retval TRUE Find the desired IE.
* \retval FALSE Desired IE not found.
*
* \note
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wextSrchDesiredWAPIIE (
    IN  PUINT_8         pucIEStart,
    IN  INT_32          i4TotalIeLen,
    OUT PUINT_8         *ppucDesiredIE
    )
{
    INT_32 i4InfoElemLen;

    ASSERT(pucIEStart);
    ASSERT(ppucDesiredIE);

    while (i4TotalIeLen >= 2) {
        i4InfoElemLen = (INT_32) pucIEStart[1] + 2;

        if (pucIEStart[0] == ELEM_ID_WAPI && i4InfoElemLen <= i4TotalIeLen) {
            *ppucDesiredIE = &pucIEStart[0];
            return TRUE;
        } /* check desired EID */

        /* Select next information element. */
        i4TotalIeLen -= i4InfoElemLen;
        pucIEStart += i4InfoElemLen;
    }

    return FALSE;
} /* wextSrchDesiredWAPIIE */
#endif


#if CFG_SUPPORT_WPS
/*----------------------------------------------------------------------------*/
/*!
* \brief Find the desired WPS Information Element according to desiredElemID.
*
* \param[in] pucIEStart IE starting address.
* \param[in] i4TotalIeLen Total length of all the IE.
* \param[in] ucDesiredElemId Desired element ID.
* \param[out] ppucDesiredIE Pointer to the desired IE.
*
* \retval TRUE Find the desired IE.
* \retval FALSE Desired IE not found.
*
* \note
*/
/*----------------------------------------------------------------------------*/
BOOLEAN
wextSrchDesiredWPSIE (
    IN PUINT_8 pucIEStart,
    IN INT_32 i4TotalIeLen,
    IN UINT_8 ucDesiredElemId,
    OUT PUINT_8 *ppucDesiredIE)
{
    INT_32 i4InfoElemLen;

    ASSERT(pucIEStart);
    ASSERT(ppucDesiredIE);

    while (i4TotalIeLen >= 2) {
        i4InfoElemLen = (INT_32) pucIEStart[1] + 2;

        if (pucIEStart[0] == ucDesiredElemId && i4InfoElemLen <= i4TotalIeLen) {
            if (ucDesiredElemId != 0xDD) {
                /* Non 0xDD, OK! */
                *ppucDesiredIE = &pucIEStart[0];
                return TRUE;
            }
            else {
                /* EID == 0xDD, check WPS IE */
                if (pucIEStart[1] >= 4) {
                    if (memcmp(&pucIEStart[2], "\x00\x50\xf2\x04", 4) == 0) {
                        *ppucDesiredIE = &pucIEStart[0];
                        return TRUE;
                    }
                } /* check WPS IE length */
            } /* check EID == 0xDD */
        } /* check desired EID */

        /* Select next information element. */
        i4TotalIeLen -= i4InfoElemLen;
        pucIEStart += i4InfoElemLen;
    }

    return FALSE;
} /* parseSearchDesiredWPSIE */
#endif


/*----------------------------------------------------------------------------*/
/*!
* \brief Get the name of the protocol used on the air.
*
* \param[in]  prDev Net device requested.
* \param[in]  prIwrInfo NULL.
* \param[out] pcName Buffer to store protocol name string
* \param[in]  pcExtra NULL.
*
* \retval 0 For success.
*
* \note If netif_carrier_ok, protocol name is returned;
*       otherwise, "disconnected" is returned.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_name (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT char *pcName,
    IN char *pcExtra
    )
{
    ENUM_PARAM_NETWORK_TYPE_T  eNetWorkType;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(pcName);
    if (FALSE == GLUE_CHK_PR2(prNetDev, pcName)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    if (netif_carrier_ok(prNetDev)) {

        rStatus = kalIoctl(prGlueInfo,
            wlanoidQueryNetworkTypeInUse,
            &eNetWorkType,
            sizeof(eNetWorkType),
            TRUE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);

        switch(eNetWorkType) {
        case PARAM_NETWORK_TYPE_DS:
            strcpy(pcName, "IEEE 802.11b");
            break;
        case PARAM_NETWORK_TYPE_OFDM24:
            strcpy(pcName, "IEEE 802.11bgn");
            break;
        case PARAM_NETWORK_TYPE_AUTOMODE:
        case PARAM_NETWORK_TYPE_OFDM5:
            strcpy(pcName, "IEEE 802.11abgn");
            break;
        case PARAM_NETWORK_TYPE_FH:
        default:
            strcpy(pcName, "IEEE 802.11");
            break;
        }
    }
    else {
        strcpy(pcName, "Disconnected");
    }

    return 0;
} /* wext_get_name */

/*----------------------------------------------------------------------------*/
/*!
* \brief To set the operating channel in the wireless device.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL
* \param[in] prFreq Buffer to store frequency information
* \param[in] pcExtra NULL
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If infrastructure mode is not NET NET_TYPE_IBSS.
* \retval -EINVAL Invalid channel frequency.
*
* \note If infrastructure mode is IBSS, new channel frequency is set to device.
*      The range of channel number depends on different regulatory domain.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_freq (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN struct iw_freq *prIwFreq,
    IN char *pcExtra
    )
{

#if 0
    UINT_32 u4ChnlFreq; /* Store channel or frequency information */

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prIwFreq);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prIwFreq)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    /*
    printk("set m:%d, e:%d, i:%d, flags:%d\n",
        prIwFreq->m, prIwFreq->e, prIwFreq->i, prIwFreq->flags);
    */

    /* If setting by frequency, convert to a channel */
    if ((prIwFreq->e == 1) &&
        (prIwFreq->m >= (int) 2.412e8) &&
        (prIwFreq->m <= (int) 2.484e8)) {

        /* Change to KHz format */
        u4ChnlFreq = (UINT_32)(prIwFreq->m / (KILO / 10));

        rStatus = kalIoctl(prGlueInfo,
                           wlanoidSetFrequency,
                           &u4ChnlFreq,
                           sizeof(u4ChnlFreq),
                           FALSE,
                           FALSE,
                           FALSE,
                           &u4BufLen);

        if (WLAN_STATUS_SUCCESS != rStatus) {
            return -EINVAL;
        }
    }
    /* Setting by channel number */
    else if ((prIwFreq->m > KILO) || (prIwFreq->e > 0)) {
        return -EOPNOTSUPP;
    }
    else {
        /* Change to channel number format */
        u4ChnlFreq = (UINT_32)prIwFreq->m;

        rStatus = kalIoctl(prGlueInfo,
                           wlanoidSetChannel,
                           &u4ChnlFreq,
                           sizeof(u4ChnlFreq),
                           FALSE,
                           FALSE,
                           FALSE,
                           &u4BufLen);




        if (WLAN_STATUS_SUCCESS != rStatus) {
            return -EINVAL;
        }
    }

#endif

    return 0;

} /* wext_set_freq */


/*----------------------------------------------------------------------------*/
/*!
* \brief To get the operating channel in the wireless device.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prFreq Buffer to store frequency information.
* \param[in] pcExtra NULL.
*
* \retval 0 If netif_carrier_ok.
* \retval -ENOTCONN Otherwise
*
* \note If netif_carrier_ok, channel frequency information is stored in pFreq.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_freq (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct iw_freq *prIwFreq,
    IN char *pcExtra
    )
{
    UINT_32 u4Channel = 0;


    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prIwFreq);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prIwFreq)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    /* GeorgeKuo: TODO skip checking in IBSS mode */
    if (!netif_carrier_ok(prNetDev)) {
        return -ENOTCONN;
    }

    rStatus = kalIoctl(prGlueInfo,
         wlanoidQueryFrequency,
         &u4Channel,
         sizeof(u4Channel),
         TRUE,
         FALSE,
         FALSE,
         FALSE,
         &u4BufLen);

    prIwFreq->m = (int) u4Channel; /* freq in KHz */
    prIwFreq->e = 3;

    return 0;

} /* wext_get_freq */


/*----------------------------------------------------------------------------*/
/*!
* \brief To set operating mode.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] pu4Mode Pointer to new operation mode.
* \param[in] pcExtra NULL.
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If new mode is not supported.
*
* \note Device will run in new operation mode if it is valid.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_mode (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN unsigned int *pu4Mode,
    IN char *pcExtra
    )
{
    ENUM_PARAM_OP_MODE_T eOpMode;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(pu4Mode);
    if (FALSE == GLUE_CHK_PR2(prNetDev, pu4Mode)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    switch (*pu4Mode) {
    case IW_MODE_AUTO:
        eOpMode = NET_TYPE_AUTO_SWITCH;
        break;

    case IW_MODE_ADHOC:
        eOpMode = NET_TYPE_IBSS;
        break;

    case IW_MODE_INFRA:
        eOpMode = NET_TYPE_INFRA;
        break;

    default:
        DBGLOG(INIT, INFO, ("%s(): Set UNSUPPORTED Mode = %d.\n", __FUNCTION__, *pu4Mode));
        return -EOPNOTSUPP;
    }

    //printk("%s(): Set Mode = %d\n", __FUNCTION__, *pu4Mode);

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetInfrastructureMode,
        &eOpMode,
        sizeof(eOpMode),
        FALSE,
        FALSE,
        TRUE,
        FALSE,
        &u4BufLen);


    /* after set operation mode, key table are cleared */

    /* reset wpa info */
    prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_DISABLED;
    prGlueInfo->rWpaInfo.u4KeyMgmt = 0;
    prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_NONE;
    prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_NONE;
    prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_OPEN_SYSTEM;
#if CFG_SUPPORT_802_11W
    prGlueInfo->rWpaInfo.u4Mfp = IW_AUTH_MFP_DISABLED;
#endif

    return 0;
} /* wext_set_mode */

/*----------------------------------------------------------------------------*/
/*!
* \brief To get operating mode.
*
* \param[in] prNetDev Net device requested.
* \param[in] prIwReqInfo NULL.
* \param[out] pu4Mode Buffer to store operating mode information.
* \param[in] pcExtra NULL.
*
* \retval 0 If data is valid.
* \retval -EINVAL Otherwise.
*
* \note If netif_carrier_ok, operating mode information is stored in pu4Mode.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_mode (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    OUT unsigned int *pu4Mode,
    IN char *pcExtra
    )
{
    ENUM_PARAM_OP_MODE_T eOpMode;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(pu4Mode);
    if (FALSE == GLUE_CHK_PR2(prNetDev, pu4Mode)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    rStatus = kalIoctl(prGlueInfo,
         wlanoidQueryInfrastructureMode,
         &eOpMode,
         sizeof(eOpMode),
         TRUE,
         FALSE,
         FALSE,
         FALSE,
         &u4BufLen);



    switch (eOpMode){
    case NET_TYPE_IBSS:
        *pu4Mode = IW_MODE_ADHOC;
        break;

    case NET_TYPE_INFRA:
        *pu4Mode = IW_MODE_INFRA;
        break;

    case NET_TYPE_AUTO_SWITCH:
        *pu4Mode = IW_MODE_AUTO;
        break;

    default:
        DBGLOG(INIT, INFO, ("%s(): Get UNKNOWN Mode.\n", __FUNCTION__));
        return -EINVAL;
    }

    return 0;
} /* wext_get_mode */

/*----------------------------------------------------------------------------*/
/*!
* \brief To get the valid range for each configurable STA setting value.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prData Pointer to iw_point structure, not used.
* \param[out] pcExtra Pointer to buffer which is allocated by caller of this
*                     function, wext_support_ioctl() or ioctl_standard_call() in
*                     wireless.c.
*
* \retval 0 If data is valid.
*
* \note The extra buffer (pcExtra) is filled with information from driver.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_range (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_point *prData,
    OUT char *pcExtra
    )
{
    struct iw_range *prRange = NULL;
    PARAM_RATES_EX aucSuppRate = {0}; /* data buffers */
    int i = 0;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(pcExtra);
    if (FALSE == GLUE_CHK_PR2(prNetDev, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    prRange = (struct iw_range *) pcExtra;

    memset(prRange, 0, sizeof(*prRange));
    prRange->throughput = 20000000;  /* 20Mbps */
    prRange->min_nwid = 0;   /* not used */
    prRange->max_nwid = 0;   /* not used */

    /* scan_capa not implemented */

    /* event_capa[6]: kernel + driver capabilities */
    prRange->event_capa[0] = (IW_EVENT_CAPA_K_0
        | IW_EVENT_CAPA_MASK(SIOCGIWAP)
        | IW_EVENT_CAPA_MASK(SIOCGIWSCAN)
        /* can't display meaningful string in iwlist
        | IW_EVENT_CAPA_MASK(SIOCGIWTXPOW)
        | IW_EVENT_CAPA_MASK(IWEVMICHAELMICFAILURE)
        | IW_EVENT_CAPA_MASK(IWEVASSOCREQIE)
        | IW_EVENT_CAPA_MASK(IWEVPMKIDCAND)
        */
        );
    prRange->event_capa[1] = IW_EVENT_CAPA_K_1;

    /* report 2.4G channel and frequency only */
    prRange->num_channels = (__u16) NUM_CHANNELS;
    prRange->num_frequency = (__u8) NUM_CHANNELS;
    for (i = 0; i < NUM_CHANNELS; i++) {
        /* iwlib takes this number as channel number */
        prRange->freq[i].i = i + 1;
        prRange->freq[i].m = channel_freq[i];
        prRange->freq[i].e = 6;  /* Values in table in MHz */
    }

    rStatus = kalIoctl(
         prGlueInfo,
         wlanoidQuerySupportedRates,
         &aucSuppRate,
         sizeof(aucSuppRate),
         TRUE,
         FALSE,
         FALSE,
         FALSE,
         &u4BufLen);



    for (i = 0; i < IW_MAX_BITRATES && i < PARAM_MAX_LEN_RATES_EX ; i++) {
        if (aucSuppRate[i] == 0) {
            break;
    }
        prRange->bitrate[i] = (aucSuppRate[i] & 0x7F) * 500000; /* 0.5Mbps */
    }
    prRange->num_bitrates = i;

    prRange->min_rts = 0;
    prRange->max_rts = 2347;
    prRange->min_frag = 256;
    prRange->max_frag = 2346;

    prRange->min_pmp = 0;    /* power management by driver */
    prRange->max_pmp = 0;    /* power management by driver */
    prRange->min_pmt = 0;    /* power management by driver */
    prRange->max_pmt = 0;    /* power management by driver */
    prRange->pmp_flags = IW_POWER_RELATIVE;    /* pm default flag */
    prRange->pmt_flags = IW_POWER_ON;    /* pm timeout flag */
    prRange->pm_capa = IW_POWER_ON;  /* power management by driver */

    prRange->encoding_size[0] = 5;   /* wep40 */
    prRange->encoding_size[1] = 16;   /* tkip */
    prRange->encoding_size[2] = 16;   /* ckip */
    prRange->encoding_size[3] = 16;   /* ccmp */
    prRange->encoding_size[4] = 13;  /* wep104 */
    prRange->encoding_size[5] = 16;  /* wep128 */
    prRange->num_encoding_sizes = 6;
    prRange->max_encoding_tokens = 6;    /* token? */

#if WIRELESS_EXT < 17
    prRange->txpower_capa = 0x0002; /* IW_TXPOW_RELATIVE */
#else
    prRange->txpower_capa = IW_TXPOW_RELATIVE;
#endif
    prRange->num_txpower = 5;
    prRange->txpower[0] = 0; /* minimum */
    prRange->txpower[1] = 25; /* 25% */
    prRange->txpower[2] = 50;    /* 50% */
    prRange->txpower[3] = 100;    /* 100% */

    prRange->we_version_compiled = WIRELESS_EXT;
    prRange->we_version_source = WIRELESS_EXT;

    prRange->retry_capa = IW_RETRY_LIMIT;
    prRange->retry_flags = IW_RETRY_LIMIT;
    prRange->min_retry = 7;
    prRange->max_retry = 7;
    prRange->r_time_flags = IW_RETRY_ON;
    prRange->min_r_time = 0;
    prRange->max_r_time = 0;

    /* signal strength and link quality */
    /* Just define range here, reporting value moved to wext_get_stats() */
    prRange->sensitivity = -83;  /* fixed value */
    prRange->max_qual.qual = 100;  /* max 100% */
    prRange->max_qual.level = (__u8)(0x100 - 0); /* max 0 dbm */
    prRange->max_qual.noise = (__u8)(0x100 - 0); /* max 0 dbm */

    /* enc_capa */
#if WIRELESS_EXT > 17
    prRange->enc_capa = IW_ENC_CAPA_WPA |
        IW_ENC_CAPA_WPA2 |
        IW_ENC_CAPA_CIPHER_TKIP |
        IW_ENC_CAPA_CIPHER_CCMP;
#endif

    /* min_pms; Minimal PM saving */
    /* max_pms; Maximal PM saving */
    /* pms_flags; How to decode max/min PM saving */

    /* modul_capa; IW_MODUL_* bit field */
    /* bitrate_capa; Types of bitrates supported */

    return 0;
} /* wext_get_range */


/*----------------------------------------------------------------------------*/
/*!
* \brief To set BSSID of AP to connect.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prAddr Pointer to struct sockaddr structure containing AP's BSSID.
* \param[in] pcExtra NULL.
*
* \retval 0 For success.
*
* \note Desired AP's BSSID is set to driver.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_ap (
    IN struct net_device *prDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct sockaddr *prAddr,
    IN char *pcExtra
    )
{
    return 0;
} /* wext_set_ap */


/*----------------------------------------------------------------------------*/
/*!
* \brief To get AP MAC address.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prAddr Pointer to struct sockaddr structure storing AP's BSSID.
* \param[in] pcExtra NULL.
*
* \retval 0 If netif_carrier_ok.
* \retval -ENOTCONN Otherwise.
*
* \note If netif_carrier_ok, AP's mac address is stored in pAddr->sa_data.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_ap (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct sockaddr *prAddr,
    IN char *pcExtra
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prAddr);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prAddr)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    //if (!netif_carrier_ok(prNetDev)) {
    //    return -ENOTCONN;
    //}

    if(prGlueInfo->eParamMediaStateIndicated == PARAM_MEDIA_STATE_DISCONNECTED){
        memset(prAddr, 0, 6);
        return 0;
    }

    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryBssid,
        prAddr->sa_data,
        ETH_ALEN,
        TRUE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);

    return 0;
} /* wext_get_ap */

/*----------------------------------------------------------------------------*/
/*!
* \brief To set mlme operation request.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prData Pointer of iw_point header.
* \param[in] pcExtra Pointer to iw_mlme structure mlme request information.
*
* \retval 0 For success.
* \retval -EOPNOTSUPP unsupported IW_MLME_ command.
* \retval -EINVAL Set MLME Fail, different bssid.
*
* \note Driver will start mlme operation if valid.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_mlme (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_point *prData,
    IN char *pcExtra
    )
{
    struct iw_mlme *prMlme = NULL;


    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(pcExtra);
    if (FALSE == GLUE_CHK_PR2(prNetDev, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    prMlme = (struct iw_mlme *)pcExtra;
    if (prMlme->cmd == IW_MLME_DEAUTH || prMlme->cmd == IW_MLME_DISASSOC) {
        if (!netif_carrier_ok(prNetDev)) {
            DBGLOG(INIT, INFO, ("[wifi] Set MLME Deauth/Disassoc, but netif_carrier_off\n"));
            return 0;
        }

        rStatus = kalIoctl(prGlueInfo,
            wlanoidSetDisassociate,
            NULL,
            0,
            FALSE,
            FALSE,
            TRUE,
            FALSE,
            &u4BufLen);
        return 0;
    }
    else {
        DBGLOG(INIT, INFO, ("[wifi] unsupported IW_MLME_ command :%d\n", prMlme->cmd));
        return -EOPNOTSUPP;
    }
} /* wext_set_mlme */

/*----------------------------------------------------------------------------*/
/*!
* \brief To issue scan request.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prData NULL.
* \param[in] pcExtra NULL.
*
* \retval 0 For success.
* \retval -EFAULT Tx power is off.
*
* \note Device will start scanning.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_scan (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN union iwreq_data *prData,
    IN char *pcExtra
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;
    int essid_len = 0;

    ASSERT(prNetDev);
    if (FALSE == GLUE_CHK_DEV(prNetDev)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

#if WIRELESS_EXT > 17
    /* retrieve SSID */
    if(prData) {
        essid_len = ((struct iw_scan_req *)(((struct iw_point*)prData)->pointer))->essid_len;
    }
#endif

    init_completion(&prGlueInfo->rScanComp);

    // TODO:  parse flags and issue different scan requests?

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetBssidListScan,
        pcExtra,
        essid_len,
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);

    //wait_for_completion_interruptible_timeout(&prGlueInfo->rScanComp, 2 * KAL_HZ);
    //kalIndicateStatusAndComplete(prGlueInfo, WLAN_STATUS_SCAN_COMPLETE, NULL, 0);


    return 0;
} /* wext_set_scan */


/*----------------------------------------------------------------------------*/
/*!
* \brief To write the ie to buffer
*
*/
/*----------------------------------------------------------------------------*/
static inline int snprintf_hex(char *buf, size_t buf_size, const u8 *data,
                    size_t len)
{
    size_t i;
    char *pos = buf, *end = buf + buf_size;
    int ret;

    if (buf_size == 0)
        return 0;

    for (i = 0; i < len; i++) {
        ret = snprintf(pos, end - pos, "%02x",
                  data[i]);
        if (ret < 0 || ret >= end - pos) {
            end[-1] = '\0';
            return pos - buf;
        }
        pos += ret;
    }
    end[-1] = '\0';
    return pos - buf;
}


/*----------------------------------------------------------------------------*/
/*!
* \brief To get scan results, transform results from driver's format to WE's.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prData Pointer to iw_point structure, pData->length is the size of
*               pcExtra buffer before used, and is updated after filling scan
*               results.
* \param[out] pcExtra Pointer to buffer which is allocated by caller of this
*                     function, wext_support_ioctl() or ioctl_standard_call() in
*                     wireless.c.
*
* \retval 0 For success.
* \retval -ENOMEM If dynamic memory allocation fail.
* \retval -E2BIG Invalid length.
*
* \note Scan results is filled into pcExtra buffer, data size is updated in
*       pData->length.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_scan (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN OUT struct iw_point *prData,
    IN char *pcExtra
    )
{
    UINT_32 i = 0;
    UINT_32 j = 0;
    P_PARAM_BSSID_LIST_EX_T prList = NULL;
    P_PARAM_BSSID_EX_T prBss = NULL;
    P_PARAM_VARIABLE_IE_T prDesiredIE = NULL;
    struct iw_event iwEvent;    /* local iw_event buffer */

    /* write pointer of extra buffer */
    char *pcCur = NULL;
    /* pointer to the end of  last full entry in extra buffer */
    char *pcValidEntryEnd = NULL;
    char *pcEnd = NULL; /* end of extra buffer */

    UINT_32 u4AllocBufLen = 0;

    /* arrange rate information */
    UINT_32 u4HighestRate = 0;
    char aucRatesBuf[64];
    UINT_32 u4BufIndex;

    /* return value */
    int ret = 0;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prData);
    ASSERT(pcExtra);
    if (FALSE == GLUE_CHK_PR3(prNetDev, prData, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    /* Initialize local variables */
    pcCur = pcExtra;
    pcValidEntryEnd = pcExtra;
    pcEnd = pcExtra + prData->length; /* end of extra buffer */

    /* Allocate another query buffer with the same size of extra buffer */
    u4AllocBufLen = prData->length;
    prList = kalMemAlloc(u4AllocBufLen, VIR_MEM_TYPE);
    if (prList == NULL) {
        DBGLOG(INIT, INFO, ("[wifi] no memory for scan list:%d\n", prData->length));
        ret = -ENOMEM;
        goto error;
    }
    prList->u4NumberOfItems = 0;

    /* wait scan done */
    //printk ("wait for scan results\n");
    //wait_for_completion_interruptible_timeout(&prGlueInfo->rScanComp, 4 * KAL_HZ);

    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryBssidList,
        prList,
        u4AllocBufLen,
        TRUE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);

    if (rStatus == WLAN_STATUS_INVALID_LENGTH) {
        /* Buffer length is not large enough. */
        //printk(KERN_INFO "[wifi] buf:%d result:%ld\n", pData->length, u4BufLen);

#if WIRELESS_EXT >= 17
        /* This feature is supported in WE-17 or above, limited by iwlist.
        ** Return -E2BIG and iwlist will request again with a larger buffer.
        */
        ret = -E2BIG;
        /* Update length to give application a hint on result length */
        prData->length = (__u16)u4BufLen;
        goto error;
#else
        /* Realloc a larger query buffer here, but don't write too much to extra
        ** buffer when filling it later.
        */
        kalMemFree(prList, VIR_MEM_TYPE, u4AllocBufLen);

        u4AllocBufLen = u4BufLen;
        prList = kalMemAlloc(u4AllocBufLen, VIR_MEM_TYPE);
        if (prList == NULL) {
            DBGLOG(INIT, INFO, ("[wifi] no memory for larger scan list :%ld\n", u4BufLen));
            ret = -ENOMEM;
            goto error;
        }
        prList->NumberOfItems = 0;

        rStatus = kalIoctl(prGlueInfo,
            wlanoidQueryBssidList,
            prList,
            u4AllocBufLen,
            TRUE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);

        if (rStatus == WLAN_STATUS_INVALID_LENGTH) {
            DBGLOG(INIT, INFO, ("[wifi] larger buf:%d result:%ld\n", u4AllocBufLen, u4BufLen));
            ret = -E2BIG;
            prData->length = (__u16)u4BufLen;
            goto error;
        }
#endif /* WIRELESS_EXT >= 17 */

    }


    if (prList->u4NumberOfItems > CFG_MAX_NUM_BSS_LIST) {
        DBGLOG(INIT, INFO, ("[wifi] strange scan result count:%ld\n",
            prList->u4NumberOfItems));
        goto error;
    }

    /* Copy required data from pList to pcExtra */
    prBss = &prList->arBssid[0];    /* set to the first entry */
    for (i = 0; i < prList->u4NumberOfItems; ++i) {
        /* BSSID */
        iwEvent.cmd = SIOCGIWAP;
        iwEvent.len = IW_EV_ADDR_LEN;
        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        iwEvent.u.ap_addr.sa_family = ARPHRD_ETHER;
        memcpy(iwEvent.u.ap_addr.sa_data, prBss->arMacAddress, ETH_ALEN);
        memcpy(pcCur, &iwEvent, IW_EV_ADDR_LEN);
        pcCur += IW_EV_ADDR_LEN;

        /* SSID */
        iwEvent.cmd = SIOCGIWESSID;
        /* Modification to user space pointer(essid.pointer) is not needed. */
        iwEvent.u.essid.length = (__u16)prBss->rSsid.u4SsidLen;
        iwEvent.len = IW_EV_POINT_LEN + iwEvent.u.essid.length;

        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        iwEvent.u.essid.flags = 1;
        iwEvent.u.essid.pointer = NULL;

#if WIRELESS_EXT <= 18
        memcpy(pcCur, &iwEvent, iwEvent.len);
#else
        memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
        memcpy(pcCur + IW_EV_LCP_LEN,
            &iwEvent.u.data.length,
            sizeof(struct  iw_point) - IW_EV_POINT_OFF);
#endif
        memcpy(pcCur + IW_EV_POINT_LEN, prBss->rSsid.aucSsid, iwEvent.u.essid.length);
        pcCur += iwEvent.len;
        /* Frequency */
        iwEvent.cmd = SIOCGIWFREQ;
        iwEvent.len = IW_EV_FREQ_LEN;
        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        iwEvent.u.freq.m = prBss->rConfiguration.u4DSConfig;
        iwEvent.u.freq.e = 3;   /* (in KHz) */
        iwEvent.u.freq.i = 0;
        memcpy(pcCur, &iwEvent, IW_EV_FREQ_LEN);
        pcCur += IW_EV_FREQ_LEN;

        /* Operation Mode */
        iwEvent.cmd = SIOCGIWMODE;
        iwEvent.len = IW_EV_UINT_LEN;
        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        if (prBss->eOpMode == NET_TYPE_IBSS) {
            iwEvent.u.mode = IW_MODE_ADHOC;
        }
        else if (prBss->eOpMode == NET_TYPE_INFRA) {
            iwEvent.u.mode = IW_MODE_INFRA;
        }
        else {
            iwEvent.u.mode = IW_MODE_AUTO;
        }
        memcpy(pcCur, &iwEvent, IW_EV_UINT_LEN);
        pcCur += IW_EV_UINT_LEN;

        /* Quality */
        iwEvent.cmd = IWEVQUAL;
        iwEvent.len = IW_EV_QUAL_LEN;
        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        iwEvent.u.qual.qual = 0; /* Quality not available now */
        /* -100 < Rssi < -10, normalized by adding 0x100 */
        iwEvent.u.qual.level = 0x100 + prBss->rRssi;
        iwEvent.u.qual.noise = 0; /* Noise not available now */
        iwEvent.u.qual.updated = IW_QUAL_QUAL_INVALID | IW_QUAL_LEVEL_UPDATED \
            | IW_QUAL_NOISE_INVALID;
        memcpy(pcCur, &iwEvent, IW_EV_QUAL_LEN);
        pcCur += IW_EV_QUAL_LEN;

        /* Security Mode*/
        iwEvent.cmd = SIOCGIWENCODE;
        iwEvent.len = IW_EV_POINT_LEN;
        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        iwEvent.u.data.pointer = NULL;
        iwEvent.u.data.flags = 0;
        iwEvent.u.data.length = 0;
        if(!prBss->u4Privacy) {
            iwEvent.u.data.flags |=  IW_ENCODE_DISABLED;
        }
#if WIRELESS_EXT <= 18
        memcpy(pcCur, &iwEvent, IW_EV_POINT_LEN);
#else
        memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
        memcpy(pcCur + IW_EV_LCP_LEN,
            &iwEvent.u.data.length,
            sizeof(struct  iw_point) - IW_EV_POINT_OFF);
#endif
        pcCur += IW_EV_POINT_LEN;

        /* rearrange rate information */
        u4BufIndex = sprintf(aucRatesBuf, "Rates (Mb/s):");
        u4HighestRate = 0;
        for (j = 0; j < PARAM_MAX_LEN_RATES_EX; ++j) {
            UINT_8 curRate = prBss->rSupportedRates[j] & 0x7F;
            if (curRate == 0) {
                break;
            }

            if (curRate > u4HighestRate) {
                u4HighestRate = curRate;
            }

            if (curRate == RATE_5_5M) {
                u4BufIndex += sprintf(aucRatesBuf + u4BufIndex, " 5.5");
            }
            else {
                u4BufIndex += sprintf(aucRatesBuf + u4BufIndex, " %d", curRate / 2);
            }
    #if DBG
            if (u4BufIndex > sizeof(aucRatesBuf)) {
                //printk("rate info too long\n");
                break;
            }
    #endif
        }
        /* Report Highest Rates */
        iwEvent.cmd = SIOCGIWRATE;
        iwEvent.len = IW_EV_PARAM_LEN;
        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        iwEvent.u.bitrate.value = u4HighestRate * 500000;
        iwEvent.u.bitrate.fixed = 0;
        iwEvent.u.bitrate.disabled = 0;
        iwEvent.u.bitrate.flags = 0;
        memcpy(pcCur, &iwEvent, iwEvent.len);
        pcCur += iwEvent.len;

    #if WIRELESS_EXT >= 15  /* IWEVCUSTOM is available in WE-15 or above */
        /* Report Residual Rates */
        iwEvent.cmd = IWEVCUSTOM;
        iwEvent.u.data.length = u4BufIndex;
        iwEvent.len = IW_EV_POINT_LEN + iwEvent.u.data.length;
        if ((pcCur + iwEvent.len) > pcEnd)
            break;
        iwEvent.u.data.flags = 0;
     #if WIRELESS_EXT <= 18
        memcpy(pcCur, &iwEvent, IW_EV_POINT_LEN);
     #else
        memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
        memcpy(pcCur + IW_EV_LCP_LEN,
            &iwEvent.u.data.length,
            sizeof(struct  iw_point) - IW_EV_POINT_OFF);
     #endif
        memcpy(pcCur + IW_EV_POINT_LEN, aucRatesBuf, u4BufIndex);
        pcCur += iwEvent.len;
    #endif /* WIRELESS_EXT >= 15 */


    if (wextSrchDesiredWPAIE(&prBss->aucIEs[sizeof(PARAM_FIXED_IEs)],
             prBss->u4IELength - sizeof(PARAM_FIXED_IEs),
             0xDD,
             (PUINT_8 *)&prDesiredIE)) {
            iwEvent.cmd = IWEVGENIE;
            iwEvent.u.data.flags = 1;
            iwEvent.u.data.length = 2 + (__u16)prDesiredIE->ucLength;
            iwEvent.len = IW_EV_POINT_LEN + iwEvent.u.data.length;
            if ((pcCur + iwEvent.len) > pcEnd)
                break;
#if WIRELESS_EXT <= 18
            memcpy(pcCur, &iwEvent, IW_EV_POINT_LEN);
#else
            memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
            memcpy(pcCur + IW_EV_LCP_LEN,
                   &iwEvent.u.data.length,
                   sizeof(struct  iw_point) - IW_EV_POINT_OFF);
#endif
            memcpy(pcCur + IW_EV_POINT_LEN, prDesiredIE, 2 + prDesiredIE->ucLength);
            pcCur += iwEvent.len;
    }

#if CFG_SUPPORT_WPS  /* search WPS IE (0xDD, 221, OUI: 0x0050f204 ) */
    if (wextSrchDesiredWPSIE(&prBss->aucIEs[sizeof(PARAM_FIXED_IEs)],
              prBss->u4IELength - sizeof(PARAM_FIXED_IEs),
              0xDD,
              (PUINT_8 *)&prDesiredIE)) {
                iwEvent.cmd = IWEVGENIE;
                iwEvent.u.data.flags = 1;
                iwEvent.u.data.length = 2 + (__u16)prDesiredIE->ucLength;
                iwEvent.len = IW_EV_POINT_LEN + iwEvent.u.data.length;
            if ((pcCur + iwEvent.len) > pcEnd)
                break;
#if WIRELESS_EXT <= 18
            memcpy(pcCur, &iwEvent, IW_EV_POINT_LEN);
#else
            memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
            memcpy(pcCur + IW_EV_LCP_LEN,
                &iwEvent.u.data.length,
                sizeof(struct  iw_point) - IW_EV_POINT_OFF);
#endif
                memcpy(pcCur + IW_EV_POINT_LEN, prDesiredIE, 2 + prDesiredIE->ucLength);
                pcCur += iwEvent.len;
            }
#endif


        /* Search RSN IE (0x30, 48). pBss->IEs starts from timestamp. */
        /* pBss->IEs starts from timestamp */
        if (wextSrchDesiredWPAIE(&prBss->aucIEs[sizeof(PARAM_FIXED_IEs)],
                prBss->u4IELength -sizeof(PARAM_FIXED_IEs),
                0x30,
                (PUINT_8 *)&prDesiredIE)) {

                iwEvent.cmd = IWEVGENIE;
                iwEvent.u.data.flags = 1;
                iwEvent.u.data.length = 2 + (__u16)prDesiredIE->ucLength;
                iwEvent.len = IW_EV_POINT_LEN + iwEvent.u.data.length;
            if ((pcCur + iwEvent.len) > pcEnd)
                break;
#if WIRELESS_EXT <= 18
            memcpy(pcCur, &iwEvent, IW_EV_POINT_LEN);
#else
            memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
            memcpy(pcCur + IW_EV_LCP_LEN,
                &iwEvent.u.data.length,
                sizeof(struct  iw_point) - IW_EV_POINT_OFF);
#endif
            memcpy(pcCur + IW_EV_POINT_LEN, prDesiredIE, 2 + prDesiredIE->ucLength);
            pcCur += iwEvent.len;
        }

#if CFG_SUPPORT_WAPI /* Android+ */
        if (wextSrchDesiredWAPIIE(&prBss->aucIEs[sizeof(PARAM_FIXED_IEs)],
                prBss->u4IELength -sizeof(PARAM_FIXED_IEs),
                (PUINT_8 *)&prDesiredIE)) {

#if 0
                iwEvent.cmd = IWEVGENIE;
                iwEvent.u.data.flags = 1;
                iwEvent.u.data.length = 2 + (__u16)prDesiredIE->ucLength;
                iwEvent.len = IW_EV_POINT_LEN + iwEvent.u.data.length;
            if ((pcCur + iwEvent.len) > pcEnd)
                break;
#if WIRELESS_EXT <= 18
            memcpy(pcCur, &iwEvent, IW_EV_POINT_LEN);
#else
            memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
            memcpy(pcCur + IW_EV_LCP_LEN,
                &iwEvent.u.data.length,
                sizeof(struct  iw_point) - IW_EV_POINT_OFF);
#endif
            memcpy(pcCur + IW_EV_POINT_LEN, prDesiredIE, 2 + prDesiredIE->ucLength);
            pcCur += iwEvent.len;
#else
           iwEvent.cmd = IWEVCUSTOM;
           iwEvent.u.data.length = (2 + prDesiredIE->ucLength) * 2 + 8 /* wapi_ie= */;
           iwEvent.len = IW_EV_POINT_LEN + iwEvent.u.data.length;
           if ((pcCur + iwEvent.len) > pcEnd)
               break;
           iwEvent.u.data.flags = 1;

           memcpy(pcCur, &iwEvent, IW_EV_LCP_LEN);
           memcpy(pcCur + IW_EV_LCP_LEN,
                      &iwEvent.u.data.length,
                      sizeof(struct  iw_point) - IW_EV_POINT_OFF);

           pcCur += (IW_EV_POINT_LEN);

           pcCur += sprintf(pcCur, "wapi_ie=");

           snprintf_hex(pcCur, pcEnd - pcCur, (UINT_8 *)prDesiredIE, prDesiredIE->ucLength + 2);

           pcCur += (2 + prDesiredIE->ucLength) * 2 /* iwEvent.len */;
#endif
        }
#endif
        /* Complete an entry. Update end of valid entry */
        pcValidEntryEnd = pcCur;
        /* Extract next bss */
        prBss = (P_PARAM_BSSID_EX_T)((char *)prBss + prBss->u4Length);
    }

    /* Update valid data length for caller function and upper layer
     * applications.
     */
    prData->length = (pcValidEntryEnd - pcExtra);
    //printk(KERN_INFO "[wifi] buf:%d result:%ld\n", pData->length, u4BufLen);

    //kalIndicateStatusAndComplete(prGlueInfo, WLAN_STATUS_SCAN_COMPLETE, NULL, 0);

error:
    /* free local query buffer */
    if (prList) {
        kalMemFree(prList, VIR_MEM_TYPE, u4AllocBufLen);
    }

    return ret;
} /* wext_get_scan */

/*----------------------------------------------------------------------------*/
/*!
* \brief To set desired network name ESSID.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prEssid Pointer of iw_point header.
* \param[in] pcExtra Pointer to buffer srtoring essid string.
*
* \retval 0 If netif_carrier_ok.
* \retval -E2BIG Essid string length is too big.
* \retval -EINVAL pcExtra is null pointer.
* \retval -EFAULT Driver fail to set new essid.
*
* \note If string lengh is ok, device will try connecting to the new network.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_essid (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_point *prEssid,
    IN char *pcExtra
    )
{
    PARAM_SSID_T rNewSsid;
    UINT_32 cipher;
    ENUM_PARAM_ENCRYPTION_STATUS_T eEncStatus;
    ENUM_PARAM_AUTH_MODE_T eAuthMode;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prEssid);
    ASSERT(pcExtra);
    if (FALSE == GLUE_CHK_PR3(prNetDev, prEssid, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    if (prEssid->length > IW_ESSID_MAX_SIZE) {
        return -E2BIG;
    }


    /* set auth mode */
    if (prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_DISABLED) {
        eAuthMode = (prGlueInfo->rWpaInfo.u4AuthAlg == IW_AUTH_ALG_OPEN_SYSTEM) ?
            AUTH_MODE_OPEN : AUTH_MODE_AUTO_SWITCH;
        //printk(KERN_INFO "IW_AUTH_WPA_VERSION_DISABLED->Param_AuthMode%s\n",
        //    (eAuthMode == AUTH_MODE_OPEN) ? "Open" : "Shared");
    }
    else {
        /* set auth mode */
        switch(prGlueInfo->rWpaInfo.u4KeyMgmt) {
            case IW_AUTH_KEY_MGMT_802_1X:
                eAuthMode =
                    (prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_WPA) ?
                    AUTH_MODE_WPA : AUTH_MODE_WPA2;
                //printk("IW_AUTH_KEY_MGMT_802_1X->AUTH_MODE_WPA%s\n",
                //    (eAuthMode == AUTH_MODE_WPA) ? "" : "2");
                break;
            case IW_AUTH_KEY_MGMT_PSK:
                eAuthMode =
                    (prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_WPA) ?
                    AUTH_MODE_WPA_PSK: AUTH_MODE_WPA2_PSK;
                //printk("IW_AUTH_KEY_MGMT_PSK->AUTH_MODE_WPA%sPSK\n",
                //    (eAuthMode == AUTH_MODE_WPA_PSK) ? "" : "2");
                break;
#if CFG_SUPPORT_WAPI /* Android+ */
            case IW_AUTH_KEY_MGMT_WAPI_PSK:
                break;
            case IW_AUTH_KEY_MGMT_WAPI_CERT:
                break;
#endif

//#if defined (IW_AUTH_KEY_MGMT_WPA_NONE)
//            case IW_AUTH_KEY_MGMT_WPA_NONE:
//                eAuthMode = AUTH_MODE_WPA_NONE;
//                //printk("IW_AUTH_KEY_MGMT_WPA_NONE->AUTH_MODE_WPA_NONE\n");
//                break;
//#endif
#if CFG_SUPPORT_802_11W
            case IW_AUTH_KEY_MGMT_802_1X_SHA256:
                eAuthMode = AUTH_MODE_WPA2;
                break;
            case IW_AUTH_KEY_MGMT_PSK_SHA256:
                eAuthMode = AUTH_MODE_WPA2_PSK;
                break;
#endif
            default:
                //printk(KERN_INFO DRV_NAME"strange IW_AUTH_KEY_MGMT : %ld set auto switch\n",
                //    prGlueInfo->rWpaInfo.u4KeyMgmt);
                eAuthMode = AUTH_MODE_AUTO_SWITCH;
                break;
        }
    }


    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetAuthMode,
            &eAuthMode,
            sizeof(eAuthMode),
            FALSE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);

    /* set encryption status */
    cipher = prGlueInfo->rWpaInfo.u4CipherGroup |
        prGlueInfo->rWpaInfo.u4CipherPairwise;
    if (cipher & IW_AUTH_CIPHER_CCMP) {
        //printk("IW_AUTH_CIPHER_CCMP->ENUM_ENCRYPTION3_ENABLED\n");
        eEncStatus = ENUM_ENCRYPTION3_ENABLED;
    }
    else if (cipher & IW_AUTH_CIPHER_TKIP) {
        //printk("IW_AUTH_CIPHER_TKIP->ENUM_ENCRYPTION2_ENABLED\n");
        eEncStatus = ENUM_ENCRYPTION2_ENABLED;
    }
    else if (cipher & (IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_WEP40)) {
        //printk("IW_AUTH_CIPHER_WEPx->ENUM_ENCRYPTION1_ENABLED\n");
        eEncStatus = ENUM_ENCRYPTION1_ENABLED;
    }
    else if (cipher & IW_AUTH_CIPHER_NONE){
        //printk("IW_AUTH_CIPHER_NONE->ENUM_ENCRYPTION_DISABLED\n");
        if (prGlueInfo->rWpaInfo.fgPrivacyInvoke)
            eEncStatus = ENUM_ENCRYPTION1_ENABLED;
        else
            eEncStatus = ENUM_ENCRYPTION_DISABLED;
    }
    else {
        //printk("unknown IW_AUTH_CIPHER->Param_EncryptionDisabled\n");
        eEncStatus = ENUM_ENCRYPTION_DISABLED;
    }

    rStatus = kalIoctl(prGlueInfo,
            wlanoidSetEncryptionStatus,
            &eEncStatus,
            sizeof(eEncStatus),
            FALSE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);

#if WIRELESS_EXT < 21
    /* GeorgeKuo: a length error bug exists in (WE < 21) cases, kernel before
     ** 2.6.19. Cut the trailing '\0'.
     */
    rNewSsid.u4SsidLen = (prEssid->length) ? prEssid->length - 1 : 0;
#else
    rNewSsid.u4SsidLen = prEssid->length;
#endif
    kalMemCopy(rNewSsid.aucSsid, pcExtra, rNewSsid.u4SsidLen);

    /*
    rNewSsid.aucSsid[rNewSsid.u4SsidLen] = '\0';
    printk("set ssid(%lu): %s\n", rNewSsid.u4SsidLen, rNewSsid.aucSsid);
       */

    if (kalIoctl(prGlueInfo,
                wlanoidSetSsid,
                (PVOID) &rNewSsid,
                sizeof(PARAM_SSID_T),
                FALSE,
                FALSE,
                TRUE,
                FALSE,
                &u4BufLen) != WLAN_STATUS_SUCCESS) {
        //printk(KERN_WARNING "Fail to set ssid\n");
        return -EFAULT;
    }


    return 0;
} /* wext_set_essid */

/*----------------------------------------------------------------------------*/
/*!
* \brief To get current network name ESSID.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prEssid Pointer to iw_point structure containing essid information.
* \param[out] pcExtra Pointer to buffer srtoring essid string.
*
* \retval 0 If netif_carrier_ok.
* \retval -ENOTCONN Otherwise.
*
* \note If netif_carrier_ok, network essid is stored in pcExtra.
*/
/*----------------------------------------------------------------------------*/
//static PARAM_SSID_T ssid;
static int
wext_get_essid (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_point *prEssid,
    OUT char *pcExtra
    )
{
    //PARAM_SSID_T ssid;

    P_PARAM_SSID_T prSsid;
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prEssid);
    ASSERT(pcExtra);

    if (FALSE == GLUE_CHK_PR3(prNetDev, prEssid, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    //if (!netif_carrier_ok(prNetDev)) {
    //    return -ENOTCONN;
    //}

    prSsid = kalMemAlloc(sizeof(PARAM_SSID_T), VIR_MEM_TYPE);

    if(!prSsid) {
        return -ENOMEM;
    }

    rStatus = kalIoctl(prGlueInfo,
        wlanoidQuerySsid,
        prSsid,
        sizeof(PARAM_SSID_T),
        TRUE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);

    if ((rStatus == WLAN_STATUS_SUCCESS) && (prSsid->u4SsidLen <= MAX_SSID_LEN)) {
        kalMemCopy(pcExtra, prSsid->aucSsid, prSsid->u4SsidLen);
        prEssid->length = prSsid->u4SsidLen;
        prEssid->flags = 1;
    }

    kalMemFree(prSsid, VIR_MEM_TYPE, sizeof(PARAM_SSID_T));

    return 0;
} /* wext_get_essid */


#if 0

/*----------------------------------------------------------------------------*/
/*!
* \brief To set tx desired bit rate. Three cases here
*        iwconfig wlan0 auto -> Set to origianl supported rate set.
*        iwconfig wlan0 18M -> Imply "fixed" case, set to 18Mbps as desired rate.
*        iwconfig wlan0 18M auto -> Set to auto rate lower and equal to 18Mbps
*
* \param[in] prNetDev       Pointer to the net_device handler.
* \param[in] prIwReqInfo    Pointer to the Request Info.
* \param[in] prRate         Pointer to the Rate Parameter.
* \param[in] pcExtra        Pointer to the extra buffer.
*
* \retval 0         Update desired rate.
* \retval -EINVAL   Wrong parameter
*/
/*----------------------------------------------------------------------------*/
int
wext_set_rate (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwReqInfo,
    IN struct iw_param *prRate,
    IN char *pcExtra
    )
{
    PARAM_RATES_EX aucSuppRate = {0};
    PARAM_RATES_EX aucNewRate = {0};
    UINT_32 u4NewRateLen = 0;
    UINT_32 i;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prRate);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prRate)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    /*
    printk("value = %d, fixed = %d, disable = %d, flags = %d\n",
        prRate->value, prRate->fixed, prRate->disabled, prRate->flags);
    */

    rStatus = wlanQueryInformation(
        prGlueInfo->prAdapter,
        wlanoidQuerySupportedRates,
        &aucSuppRate,
        sizeof(aucSuppRate),
        &u4BufLen);

    /* Case: AUTO */
    if (prRate->value < 0)  {
        if (prRate->fixed == 0) {
            /* iwconfig wlan0 rate auto */

            /* set full supported rate to device */
            /* printk("wlanoidQuerySupportedRates():u4BufLen = %ld\n", u4BufLen); */
            rStatus = wlanSetInformation(
                prGlueInfo->prAdapter,
                wlanoidSetDesiredRates,
                &aucSuppRate,
                sizeof(aucSuppRate),
                &u4BufLen);
            return 0;
        }
        else {
            /* iwconfig wlan0 rate fixed */

            /* fix rate to what? DO NOTHING */
            return -EINVAL;
        }
    }


    aucNewRate[0] = prRate->value / 500000; /* In unit of 500k */

    for (i = 0; i < PARAM_MAX_LEN_RATES_EX; i++) {
        /* check the given value is supported */
        if (aucSuppRate[i] == 0) {
            break;
        }

        if (aucNewRate[0] == aucSuppRate[i]) {
            u4NewRateLen = 1;
            break;
        }
    }

    if (u4NewRateLen == 0) {
        /* the given value is not supported */
        /* return error or use given rate as upper bound? */
        return -EINVAL;
    }

    if (prRate->fixed == 0) {
        /* add all rates lower than desired rate */
        for (i = 0; i < PARAM_MAX_LEN_RATES_EX; ++i) {
            if (aucSuppRate[i] == 0) {
                break;
            }

            if (aucSuppRate[i] < aucNewRate[0]) {
                aucNewRate[u4NewRateLen++] = aucSuppRate[i];
            }
        }
    }

    rStatus = wlanSetInformation(
        prGlueInfo->prAdapter,
        wlanoidSetDesiredRates,
        &aucNewRate,
        sizeof(aucNewRate),
        &u4BufLen);
    return 0;
} /* wext_set_rate */

#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief To get current tx bit rate.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prRate Pointer to iw_param structure to store current tx rate.
* \param[in] pcExtra NULL.
*
* \retval 0 If netif_carrier_ok.
* \retval -ENOTCONN Otherwise.
*
* \note If netif_carrier_ok, current tx rate is stored in pRate.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_rate (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct iw_param *prRate,
    IN char *pcExtra
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;
    UINT_32 u4Rate = 0;

    ASSERT(prNetDev);
    ASSERT(prRate);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prRate)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    if (!netif_carrier_ok(prNetDev)) {
        return -ENOTCONN;
    }


    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryLinkSpeed,
        &u4Rate,
        sizeof(u4Rate),
        TRUE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }

    prRate->value = u4Rate * 100;   /* u4Rate is in unit of 100bps */
    prRate->fixed = 0;

    return 0;
} /* wext_get_rate */


/*----------------------------------------------------------------------------*/
/*!
* \brief To set RTS/CTS theshold.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prRts Pointer to iw_param structure containing rts threshold.
* \param[in] pcExtra NULL.
*
* \retval 0 For success.
* \retval -EINVAL Given value is out of range.
*
* \note If given value is valid, device will follow the new setting.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_rts (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_param *prRts,
    IN char *pcExtra
    )
{
    PARAM_RTS_THRESHOLD u4RtsThresh;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prRts);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prRts)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    if (prRts->disabled == 1) {
        u4RtsThresh = 2347;
    }
    else if (prRts->value < 0 || prRts->value > 2347) {
        return -EINVAL;
    }
    else {
        u4RtsThresh = (PARAM_RTS_THRESHOLD)prRts->value;
    }

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetRtsThreshold,
        &u4RtsThresh,
        sizeof(u4RtsThresh),
        FALSE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);



    prRts->value = (typeof(prRts->value ))u4RtsThresh;
    prRts->disabled = (prRts->value > 2347) ? 1 : 0;
    prRts->fixed = 1;

    return 0;
} /* wext_set_rts */

/*----------------------------------------------------------------------------*/
/*!
* \brief To get RTS/CTS theshold.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prRts Pointer to iw_param structure containing rts threshold.
* \param[in] pcExtra NULL.
*
* \retval 0 Success.
*
* \note RTS threshold is stored in pRts.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_rts (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct iw_param *prRts,
    IN char *pcExtra
    )
{
    PARAM_RTS_THRESHOLD u4RtsThresh;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prRts);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prRts)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryRtsThreshold,
        &u4RtsThresh,
        sizeof(u4RtsThresh),
        TRUE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);



    prRts->value = (typeof(prRts->value ))u4RtsThresh;
    prRts->disabled = (prRts->value > 2347 || prRts->value < 0) ? 1 : 0;
    prRts->fixed = 1;

    return 0;
} /* wext_get_rts */

/*----------------------------------------------------------------------------*/
/*!
* \brief To get fragmentation threshold.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prFrag Pointer to iw_param structure containing frag threshold.
* \param[in] pcExtra NULL.
*
* \retval 0 Success.
*
* \note RTS threshold is stored in pFrag. Fragmentation is disabled.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_frag (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct iw_param *prFrag,
    IN char *pcExtra
    )
{
    ASSERT(prFrag);

    prFrag->value = 2346;
    prFrag->fixed = 1;
    prFrag->disabled = 1;
    return 0;
} /* wext_get_frag */

#if 1
/*----------------------------------------------------------------------------*/
/*!
* \brief To set TX power, or enable/disable the radio.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prTxPow Pointer to iw_param structure containing tx power setting.
* \param[in] pcExtra NULL.
*
* \retval 0 Success.
*
* \note Tx power is stored in pTxPow. iwconfig wlan0 txpow on/off are used
*       to enable/disable the radio.
*/
/*----------------------------------------------------------------------------*/

static int
wext_set_txpow (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_param *prTxPow,
    IN char *pcExtra
    )
{
    int ret = 0;
    //PARAM_DEVICE_POWER_STATE ePowerState;
    ENUM_ACPI_STATE_T ePowerState;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prTxPow);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prTxPow)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    if(prTxPow->disabled){
        /* <1> disconnect */
        rStatus = kalIoctl(prGlueInfo,
                wlanoidSetDisassociate,
                NULL,
                0,
                FALSE,
                FALSE,
                TRUE,
                FALSE,
                &u4BufLen);
        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(INIT, INFO, ("######set disassoc failed\n"));
        } else {
            DBGLOG(INIT, INFO, ("######set assoc ok\n"));
        }

        /* <2> mark to power state flag*/
        ePowerState = ACPI_STATE_D0;
        DBGLOG(INIT, INFO, ("set to acpi d3(0)\n"));
        wlanSetAcpiState(prGlueInfo->prAdapter, ePowerState);

    }
    else {
        ePowerState = ACPI_STATE_D0;
        DBGLOG(INIT, INFO, ("set to acpi d0\n"));
        wlanSetAcpiState(prGlueInfo->prAdapter, ePowerState);
    }

    prGlueInfo->ePowerState = ePowerState;

    return ret;
} /* wext_set_txpow */


#endif

/*----------------------------------------------------------------------------*/
/*!
* \brief To get TX power.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prTxPow Pointer to iw_param structure containing tx power setting.
* \param[in] pcExtra NULL.
*
* \retval 0 Success.
*
* \note Tx power is stored in pTxPow.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_txpow (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct iw_param *prTxPow,
    IN char *pcExtra
    )
{
    //PARAM_DEVICE_POWER_STATE ePowerState;

    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(prNetDev);
    ASSERT(prTxPow);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prTxPow)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    /* GeorgeKuo: wlanoidQueryAcpiDevicePowerState() reports capability, not
     * current state. Use GLUE_INFO_T to store state.
    */
    //ePowerState = prGlueInfo->ePowerState;

    /* TxPow parameters: Fixed at relative 100% */
#if WIRELESS_EXT < 17
    prTxPow->flags = 0x0002; /* IW_TXPOW_RELATIVE */
#else
    prTxPow->flags = IW_TXPOW_RELATIVE;
#endif
    prTxPow->value = 100;
    prTxPow->fixed = 1;
    //prTxPow->disabled = (ePowerState != ParamDeviceStateD3) ? FALSE : TRUE;
    prTxPow->disabled = TRUE;

    return 0;
} /* wext_get_txpow */


/*----------------------------------------------------------------------------*/
/*!
* \brief To get encryption cipher and key.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prEnc Pointer to iw_point structure containing securiry information.
* \param[in] pcExtra Buffer to store key content.
*
* \retval 0 Success.
*
* \note Securiry information is stored in pEnc except key content.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_encode (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct iw_point *prEnc,
    IN char *pcExtra
    )
{
#if 1
    //ENUM_ENCRYPTION_STATUS_T eEncMode;
    ENUM_PARAM_ENCRYPTION_STATUS_T eEncMode;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prEnc);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prEnc)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));


    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryEncryptionStatus,
        &eEncMode,
        sizeof(eEncMode),
        TRUE,
        FALSE,
        FALSE,
        FALSE,
        &u4BufLen);



    switch(eEncMode) {
    case ENUM_WEP_DISABLED:
        prEnc->flags = IW_ENCODE_DISABLED;
        break;
    case ENUM_WEP_ENABLED:
        prEnc->flags = IW_ENCODE_ENABLED;
        break;
    case ENUM_WEP_KEY_ABSENT:
        prEnc->flags = IW_ENCODE_ENABLED | IW_ENCODE_NOKEY;
        break;
    default:
        prEnc->flags = IW_ENCODE_ENABLED;
        break;
    }

    /* Cipher, Key Content, Key ID can't be queried */
    prEnc->flags |= IW_ENCODE_NOKEY;
#endif
    return 0;
} /* wext_get_encode */



/*----------------------------------------------------------------------------*/
/*!
* \brief To set encryption cipher and key.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prEnc Pointer to iw_point structure containing securiry information.
* \param[in] pcExtra Pointer to key string buffer.
*
* \retval 0 Success.
* \retval -EINVAL Key ID error for WEP.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note Securiry information is stored in pEnc.
*/
/*----------------------------------------------------------------------------*/
static UINT_8 wepBuf[48];

static int
wext_set_encode (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_point *prEnc,
    IN char *pcExtra
    )
{
#if 1
    ENUM_PARAM_ENCRYPTION_STATUS_T eEncStatus;
    ENUM_PARAM_AUTH_MODE_T eAuthMode;
    //UINT_8 wepBuf[48];
    P_PARAM_WEP_T prWepKey = (P_PARAM_WEP_T) wepBuf;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prEnc);
    ASSERT(pcExtra);
    if (FALSE == GLUE_CHK_PR3(prNetDev, prEnc, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    /* reset to default mode */
    prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_DISABLED;
    prGlueInfo->rWpaInfo.u4KeyMgmt = 0;
    prGlueInfo->rWpaInfo.u4CipherPairwise = IW_AUTH_CIPHER_NONE;
    prGlueInfo->rWpaInfo.u4CipherGroup = IW_AUTH_CIPHER_NONE;
    prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_OPEN_SYSTEM;
#if CFG_SUPPORT_802_11W
    prGlueInfo->rWpaInfo.u4Mfp = IW_AUTH_MFP_DISABLED;
#endif

    /* iwconfig wlan0 key off */
    if ( (prEnc->flags & IW_ENCODE_MODE) == IW_ENCODE_DISABLED ) {
        eAuthMode = AUTH_MODE_OPEN;

        rStatus = kalIoctl(prGlueInfo,
            wlanoidSetAuthMode,
            &eAuthMode,
            sizeof(eAuthMode),
            FALSE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);

        eEncStatus = ENUM_ENCRYPTION_DISABLED;

        rStatus = kalIoctl(prGlueInfo,
            wlanoidSetEncryptionStatus,
            &eEncStatus,
            sizeof(eEncStatus),
            FALSE,
            FALSE,
            FALSE,
            FALSE,
            &u4BufLen);

        return 0;
    }

    /* iwconfig wlan0 key 0123456789 */
    /* iwconfig wlan0 key s:abcde */
    /* iwconfig wlan0 key 0123456789 [1] */
    /* iwconfig wlan0 key 01234567890123456789012345 [1] */
    /* check key size for WEP */
    if (prEnc->length == 5 || prEnc->length == 13 || prEnc->length == 16) {
        /* prepare PARAM_WEP key structure */
        prWepKey->u4KeyIndex = (prEnc->flags & IW_ENCODE_INDEX) ?
            (prEnc->flags & IW_ENCODE_INDEX) -1 : 0;
        if (prWepKey->u4KeyIndex > 3) {
            /* key id is out of range */
            return -EINVAL;
        }
        prWepKey->u4KeyIndex |= 0x80000000;
        prWepKey->u4Length = 12 + prEnc->length;
        prWepKey->u4KeyLength = prEnc->length;
        kalMemCopy(prWepKey->aucKeyMaterial, pcExtra, prEnc->length);


        rStatus = kalIoctl(prGlueInfo,
                     wlanoidSetAddWep,
                     prWepKey,
                     prWepKey->u4Length,
                     FALSE,
                     FALSE,
                     TRUE,
                     FALSE,
                     &u4BufLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(INIT, INFO, ("wlanoidSetAddWep fail 0x%lx\n", rStatus));
            return -EFAULT;
        }

        /* change to auto switch */
        prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_SHARED_KEY |
            IW_AUTH_ALG_OPEN_SYSTEM;
        eAuthMode = AUTH_MODE_AUTO_SWITCH;

        rStatus = kalIoctl(prGlueInfo,
                     wlanoidSetAuthMode,
                     &eAuthMode,
                     sizeof(eAuthMode),
                     FALSE,
                     FALSE,
                     FALSE,
                     FALSE,
                     &u4BufLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            //printk(KERN_INFO DRV_NAME"wlanoidSetAuthMode fail 0x%lx\n", rStatus);
            return -EFAULT;
        }

        prGlueInfo->rWpaInfo.u4CipherPairwise =
            IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_WEP40;
        prGlueInfo->rWpaInfo.u4CipherGroup =
            IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_WEP40;

        eEncStatus = ENUM_WEP_ENABLED;


        rStatus = kalIoctl(prGlueInfo,
                     wlanoidSetEncryptionStatus,
                     &eEncStatus,
                     sizeof(ENUM_PARAM_ENCRYPTION_STATUS_T),
                     FALSE,
                     FALSE,
                     FALSE,
                     FALSE,
                     &u4BufLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            //printk(KERN_INFO DRV_NAME"wlanoidSetEncryptionStatus fail 0x%lx\n", rStatus);
            return -EFAULT;
        }

        return 0;
    }
#endif
    return -EOPNOTSUPP;
} /* wext_set_encode */


/*----------------------------------------------------------------------------*/
/*!
* \brief To set power management.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prPower Pointer to iw_param structure containing tx power setting.
* \param[in] pcExtra NULL.
*
* \retval 0 Success.
*
* \note New Power Management Mode is set to driver.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_power (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_param *prPower,
    IN char *pcExtra
    )
{
#if 1
    PARAM_POWER_MODE ePowerMode;
    INT_32 i4PowerValue;

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prPower);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prPower)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    //printk(KERN_INFO "wext_set_power value(%d) disabled(%d) flag(0x%x)\n",
    //  prPower->value, prPower->disabled, prPower->flags);

    if(prPower->disabled){
        ePowerMode = Param_PowerModeCAM;
    }
    else {
        i4PowerValue = prPower->value;
#if WIRELESS_EXT < 21
        i4PowerValue /= 1000000;
#endif
        if (i4PowerValue == 0) {
            ePowerMode = Param_PowerModeCAM;
        } else if (i4PowerValue == 1) {
            ePowerMode = Param_PowerModeMAX_PSP;
        } else if (i4PowerValue == 2) {
            ePowerMode = Param_PowerModeFast_PSP;
        }
        else {
            DBGLOG(INIT, INFO, ("%s(): unsupported power management mode value = %d.\n",
                __FUNCTION__,
                prPower->value));

            return -EINVAL;
    }
    }


    rStatus = kalIoctl(prGlueInfo,
        wlanoidSet802dot11PowerSaveProfile,
        &ePowerMode,
        sizeof(ePowerMode),
        FALSE,
        FALSE,
        TRUE,
        FALSE,
        &u4BufLen);

    if (rStatus != WLAN_STATUS_SUCCESS) {
        //printk(KERN_INFO DRV_NAME"wlanoidSet802dot11PowerSaveProfile fail 0x%lx\n", rStatus);
        return -EFAULT;
    }

#endif
    return 0;
} /* wext_set_power */


/*----------------------------------------------------------------------------*/
/*!
* \brief To get power management.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[out] prPower Pointer to iw_param structure containing tx power setting.
* \param[in] pcExtra NULL.
*
* \retval 0 Success.
*
* \note Power management mode is stored in pTxPow->value.
*/
/*----------------------------------------------------------------------------*/
static int
wext_get_power (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    OUT struct iw_param *prPower,
    IN char *pcExtra
    )
{

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;
    PARAM_POWER_MODE ePowerMode = Param_PowerModeCAM;

    ASSERT(prNetDev);
    ASSERT(prPower);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prPower)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

#if 0
#if defined(_HIF_SDIO)
    rStatus = sdio_io_ctrl(prGlueInfo,
        wlanoidQuery802dot11PowerSaveProfile,
        &ePowerMode,
        sizeof(ePowerMode),
        TRUE,
        TRUE,
        &u4BufLen);
#else
    rStatus = wlanQueryInformation(prGlueInfo->prAdapter,
        wlanoidQuery802dot11PowerSaveProfile,
        &ePowerMode,
        sizeof(ePowerMode),
        &u4BufLen);
#endif
#else
    rStatus = wlanQueryInformation(prGlueInfo->prAdapter,
        wlanoidQuery802dot11PowerSaveProfile,
        &ePowerMode,
        sizeof(ePowerMode),
        &u4BufLen);
#endif

    if (rStatus != WLAN_STATUS_SUCCESS) {
        return -EFAULT;
    }

    prPower->value = 0;
    prPower->disabled = 1;

    if (Param_PowerModeCAM == ePowerMode) {
        prPower->value = 0;
        prPower->disabled = 1;
    }
    else if (Param_PowerModeMAX_PSP == ePowerMode ) {
        prPower->value = 1;
        prPower->disabled = 0;
    }
    else if (Param_PowerModeFast_PSP == ePowerMode ) {
        prPower->value = 2;
        prPower->disabled = 0;
    }

    prPower->flags = IW_POWER_PERIOD | IW_POWER_RELATIVE;
#if WIRELESS_EXT < 21
    prPower->value *= 1000000;
#endif

    //printk(KERN_INFO "wext_get_power value(%d) disabled(%d) flag(0x%x)\n",
    //    prPower->value, prPower->disabled, prPower->flags);

    return 0;
} /* wext_get_power */

/*----------------------------------------------------------------------------*/
/*!
* \brief To set authentication parameters.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] rpAuth Pointer to iw_param structure containing authentication information.
* \param[in] pcExtra Pointer to key string buffer.
*
* \retval 0 Success.
* \retval -EINVAL Key ID error for WEP.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note Securiry information is stored in pEnc.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_auth (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_param *prAuth,
    IN char *pcExtra
    )
{
    P_GLUE_INFO_T prGlueInfo = NULL;

    ASSERT(prNetDev);
    ASSERT(prAuth);
    if (FALSE == GLUE_CHK_PR2(prNetDev, prAuth)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    /* Save information to glue info and process later when ssid is set. */
    switch(prAuth->flags & IW_AUTH_INDEX) {
        case IW_AUTH_WPA_VERSION:
#if CFG_SUPPORT_WAPI
            if (wlanQueryWapiMode(prGlueInfo->prAdapter)){
                prGlueInfo->rWpaInfo.u4WpaVersion = IW_AUTH_WPA_VERSION_DISABLED;
                prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_OPEN_SYSTEM;
            }
            else {
                prGlueInfo->rWpaInfo.u4WpaVersion = prAuth->value;
            }
#else
            prGlueInfo->rWpaInfo.u4WpaVersion = prAuth->value;
#endif
            break;

        case IW_AUTH_CIPHER_PAIRWISE:
            prGlueInfo->rWpaInfo.u4CipherPairwise = prAuth->value;
            break;

        case IW_AUTH_CIPHER_GROUP:
            prGlueInfo->rWpaInfo.u4CipherGroup = prAuth->value;
            break;

        case IW_AUTH_KEY_MGMT:
            prGlueInfo->rWpaInfo.u4KeyMgmt = prAuth->value;
#if CFG_SUPPORT_WAPI
            if (prGlueInfo->rWpaInfo.u4KeyMgmt == IW_AUTH_KEY_MGMT_WAPI_PSK ||
                prGlueInfo->rWpaInfo.u4KeyMgmt == IW_AUTH_KEY_MGMT_WAPI_CERT)  {
                    UINT_32 u4BufLen;
                    WLAN_STATUS rStatus;

                    rStatus = kalIoctl(prGlueInfo,
                            wlanoidSetWapiMode,
                            &prAuth->value,
                            sizeof(UINT_32),
                            FALSE,
                            FALSE,
                            TRUE,
                            FALSE,
                            &u4BufLen);
                DBGLOG(INIT, INFO, ("IW_AUTH_WAPI_ENABLED :%d\n", prAuth->value));
            }
#endif
            if (prGlueInfo->rWpaInfo.u4KeyMgmt == IW_AUTH_KEY_MGMT_WPS)
                prGlueInfo->fgWpsActive = TRUE;
            else
                prGlueInfo->fgWpsActive = FALSE;
            break;

        case IW_AUTH_80211_AUTH_ALG:
            prGlueInfo->rWpaInfo.u4AuthAlg = prAuth->value;
            break;

        case IW_AUTH_PRIVACY_INVOKED:
            prGlueInfo->rWpaInfo.fgPrivacyInvoke = prAuth->value;
            break;
#if CFG_SUPPORT_802_11W
        case IW_AUTH_MFP:
            //printk("wext_set_auth IW_AUTH_MFP=%d\n", prAuth->value);
            prGlueInfo->rWpaInfo.u4Mfp = prAuth->value;
            break;
#endif
#if CFG_SUPPORT_WAPI
        case IW_AUTH_WAPI_ENABLED:
            {
                UINT_32 u4BufLen;
                WLAN_STATUS rStatus;

                rStatus = kalIoctl(prGlueInfo,
                        wlanoidSetWapiMode,
                        &prAuth->value,
                        sizeof(UINT_32),
                        FALSE,
                        FALSE,
                        TRUE,
                        FALSE,
                        &u4BufLen);
            }
            DBGLOG(INIT, INFO, ("IW_AUTH_WAPI_ENABLED :%d\n", prAuth->value));
            break;
#endif
        default:
            /*
            printk(KERN_INFO "[wifi] unsupported IW_AUTH_INDEX :%d\n", prAuth->flags);
            */
            break;
    }
    return 0;
} /* wext_set_auth */


/*----------------------------------------------------------------------------*/
/*!
* \brief To set encryption cipher and key.
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] prEnc Pointer to iw_point structure containing securiry information.
* \param[in] pcExtra Pointer to key string buffer.
*
* \retval 0 Success.
* \retval -EINVAL Key ID error for WEP.
* \retval -EFAULT Setting parameters to driver fail.
* \retval -EOPNOTSUPP Key size not supported.
*
* \note Securiry information is stored in pEnc.
*/
/*----------------------------------------------------------------------------*/
#if CFG_SUPPORT_WAPI
    UINT_8 keyStructBuf[320];   /* add/remove key shared buffer */
#else
    UINT_8 keyStructBuf[100];   /* add/remove key shared buffer */
#endif

static int
wext_set_encode_ext (
    IN struct net_device *prNetDev,
    IN struct iw_request_info *prIwrInfo,
    IN struct iw_point *prEnc,
    IN char *pcExtra
    )
{
    P_PARAM_REMOVE_KEY_T prRemoveKey = (P_PARAM_REMOVE_KEY_T) keyStructBuf;
    P_PARAM_KEY_T prKey = (P_PARAM_KEY_T) keyStructBuf;


    P_PARAM_WEP_T prWepKey = (P_PARAM_WEP_T) wepBuf;

    struct iw_encode_ext *prIWEncExt = (struct iw_encode_ext *) pcExtra;

    ENUM_PARAM_ENCRYPTION_STATUS_T eEncStatus;
    ENUM_PARAM_AUTH_MODE_T eAuthMode;
    //ENUM_PARAM_OP_MODE_T eOpMode = NET_TYPE_AUTO_SWITCH;

#if CFG_SUPPORT_WAPI
    P_PARAM_WPI_KEY_T prWpiKey = (P_PARAM_WPI_KEY_T) keyStructBuf;
#endif

    P_GLUE_INFO_T prGlueInfo = NULL;
    WLAN_STATUS rStatus = WLAN_STATUS_SUCCESS;
    UINT_32 u4BufLen = 0;

    ASSERT(prNetDev);
    ASSERT(prEnc);
    if (FALSE == GLUE_CHK_PR3(prNetDev, prEnc, pcExtra)) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    memset(keyStructBuf, 0, sizeof(keyStructBuf));

#if CFG_SUPPORT_WAPI
    if (prIWEncExt->alg == IW_ENCODE_ALG_SMS4) {
        if (prEnc->flags & IW_ENCODE_DISABLED) {
            //printk(KERN_INFO "[wapi] IW_ENCODE_DISABLED\n");
            return 0;
        }
        /* KeyID */
        prWpiKey->ucKeyID = (prEnc->flags & IW_ENCODE_INDEX);
        prWpiKey->ucKeyID --;
        if (prWpiKey->ucKeyID > 1) {
            /* key id is out of range */
            //printk(KERN_INFO "[wapi] add key error: key_id invalid %d\n", prWpiKey->ucKeyID);
            return -EINVAL;
        }

        if (prIWEncExt->key_len != 32) {
            /* key length not valid */
            //printk(KERN_INFO "[wapi] add key error: key_len invalid %d\n", prIWEncExt->key_len);
            return -EINVAL;
        }

        //printk(KERN_INFO "[wapi] %d ext_flags %d\n", prEnc->flags, prIWEncExt->ext_flags);

        if (prIWEncExt->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
            prWpiKey->eKeyType = ENUM_WPI_GROUP_KEY;
            prWpiKey->eDirection = ENUM_WPI_RX;
        }
        else if (prIWEncExt->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
            prWpiKey->eKeyType = ENUM_WPI_PAIRWISE_KEY;
            prWpiKey->eDirection = ENUM_WPI_RX_TX;
        }

        /* PN */
        memcpy(prWpiKey->aucPN, prIWEncExt->tx_seq, IW_ENCODE_SEQ_MAX_SIZE * 2);

        /* BSSID */
        memcpy(prWpiKey->aucAddrIndex, prIWEncExt->addr.sa_data, 6);

        memcpy(prWpiKey->aucWPIEK, prIWEncExt->key, 16);
        prWpiKey->u4LenWPIEK = 16;

        memcpy(prWpiKey->aucWPICK, &prIWEncExt->key[16], 16);
        prWpiKey->u4LenWPICK = 16;

        rStatus = kalIoctl(prGlueInfo,
                     wlanoidSetWapiKey,
                     prWpiKey,
                     sizeof(PARAM_WPI_KEY_T),
                     FALSE,
                     FALSE,
                     TRUE,
                     FALSE,
                     &u4BufLen);

        if (rStatus != WLAN_STATUS_SUCCESS) {
            //printk(KERN_INFO "[wapi] add key error:%lx\n", rStatus);
        }

    }
    else
#endif
    {

    if ( (prEnc->flags & IW_ENCODE_MODE) == IW_ENCODE_DISABLED) {
        prRemoveKey->u4Length = sizeof(*prRemoveKey);
        memcpy(prRemoveKey->arBSSID, prIWEncExt->addr.sa_data, 6);
        /*
        printk("IW_ENCODE_DISABLED: ID:%d, Addr:[" MACSTR "]\n",
            prRemoveKey->KeyIndex, MAC2STR(prRemoveKey->BSSID));
        */

        rStatus = kalIoctl(prGlueInfo,
                     wlanoidSetRemoveKey,
                     prRemoveKey,
                     prRemoveKey->u4Length,
                     FALSE,
                     FALSE,
                     TRUE,
                     FALSE,
                     &u4BufLen);


        if (rStatus != WLAN_STATUS_SUCCESS) {
            DBGLOG(INIT, INFO, ("remove key error:%lx\n", rStatus));
        }
        return 0;
    }

    //return 0;
    //printk ("alg %x\n", prIWEncExt->alg);

    switch (prIWEncExt->alg) {
        case IW_ENCODE_ALG_NONE:
            break;
        case IW_ENCODE_ALG_WEP:
            /* iwconfig wlan0 key 0123456789 */
            /* iwconfig wlan0 key s:abcde */
            /* iwconfig wlan0 key 0123456789 [1] */
            /* iwconfig wlan0 key 01234567890123456789012345 [1] */
            /* check key size for WEP */
            if (prIWEncExt->key_len == 5 || prIWEncExt->key_len == 13 || prIWEncExt->key_len == 16) {
                /* prepare PARAM_WEP key structure */
                prWepKey->u4KeyIndex = (prEnc->flags & IW_ENCODE_INDEX) ?
                    (prEnc->flags & IW_ENCODE_INDEX) -1 : 0;
                if (prWepKey->u4KeyIndex > 3) {
                    /* key id is out of range */
                    return -EINVAL;
                }
                prWepKey->u4KeyIndex |= 0x80000000;
                prWepKey->u4Length = 12 + prIWEncExt->key_len;
                prWepKey->u4KeyLength = prIWEncExt->key_len;
                //kalMemCopy(prWepKey->aucKeyMaterial, pcExtra, prIWEncExt->key_len);
                kalMemCopy(prWepKey->aucKeyMaterial, prIWEncExt->key, prIWEncExt->key_len);


                rStatus = kalIoctl(prGlueInfo,
                        wlanoidSetAddWep,
                        prWepKey,
                        prWepKey->u4Length,
                        FALSE,
                        FALSE,
                        TRUE,
                        FALSE,
                        &u4BufLen);

                if (rStatus != WLAN_STATUS_SUCCESS) {
                    DBGLOG(INIT, INFO, ("wlanoidSetAddWep fail 0x%lx\n", rStatus));
                    return -EFAULT;
                }

                /* change to auto switch */
                prGlueInfo->rWpaInfo.u4AuthAlg = IW_AUTH_ALG_SHARED_KEY |
                    IW_AUTH_ALG_OPEN_SYSTEM;
                eAuthMode = AUTH_MODE_AUTO_SWITCH;

                rStatus = kalIoctl(prGlueInfo,
                        wlanoidSetAuthMode,
                        &eAuthMode,
                        sizeof(eAuthMode),
                        FALSE,
                        FALSE,
                        FALSE,
                        FALSE,
                        &u4BufLen);

                if (rStatus != WLAN_STATUS_SUCCESS) {
                    DBGLOG(INIT, INFO, ("wlanoidSetAuthMode fail 0x%lx\n", rStatus));
                    return -EFAULT;
                }

                prGlueInfo->rWpaInfo.u4CipherPairwise =
                    IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_WEP40;
                prGlueInfo->rWpaInfo.u4CipherGroup =
                    IW_AUTH_CIPHER_WEP104 | IW_AUTH_CIPHER_WEP40;

                eEncStatus = ENUM_WEP_ENABLED;


                rStatus = kalIoctl(prGlueInfo,
                        wlanoidSetEncryptionStatus,
                        &eEncStatus,
                        sizeof(ENUM_PARAM_ENCRYPTION_STATUS_T),
                        FALSE,
                        FALSE,
                        FALSE,
                        FALSE,
                        &u4BufLen);

                if (rStatus != WLAN_STATUS_SUCCESS) {
                    DBGLOG(INIT, INFO, ("wlanoidSetEncryptionStatus fail 0x%lx\n", rStatus));
                    return -EFAULT;
                }

            } else {
                DBGLOG(INIT, INFO, ("key length %x\n", prIWEncExt->key_len));
                DBGLOG(INIT, INFO, ("key error\n"));
            }

            break;
        case IW_ENCODE_ALG_TKIP:
        case IW_ENCODE_ALG_CCMP:
#if CFG_SUPPORT_802_11W
        case IW_ENCODE_ALG_AES_CMAC:
#endif
            {

                /* KeyID */
                prKey->u4KeyIndex = (prEnc->flags & IW_ENCODE_INDEX) ?
                    (prEnc->flags & IW_ENCODE_INDEX) -1: 0;
#if CFG_SUPPORT_802_11W
                if (prKey->u4KeyIndex > 5)
#else
                if (prKey->u4KeyIndex > 3)
#endif
                {
                    DBGLOG(INIT, INFO, ("key index error:0x%lx\n", prKey->u4KeyIndex));
                    /* key id is out of range */
                    return -EINVAL;
                }

                /* bit(31) and bit(30) are shared by pKey and pRemoveKey */
                /* Tx Key Bit(31)*/
                if (prIWEncExt->ext_flags & IW_ENCODE_EXT_SET_TX_KEY) {
                    prKey->u4KeyIndex |= 0x1UL << 31;
                }

                /* Pairwise Key Bit(30) */
                if (prIWEncExt->ext_flags & IW_ENCODE_EXT_GROUP_KEY) {
                    /* group key */
                }
                else {
                    /* pairwise key */
                    prKey->u4KeyIndex |= 0x1UL << 30;
                }

            }
            /* Rx SC Bit(29) */
            if (prIWEncExt->ext_flags & IW_ENCODE_EXT_RX_SEQ_VALID) {
                prKey->u4KeyIndex |= 0x1UL << 29;
                memcpy(&prKey->rKeyRSC, prIWEncExt->rx_seq, IW_ENCODE_SEQ_MAX_SIZE);
            }

            /* BSSID */
            memcpy(prKey->arBSSID, prIWEncExt->addr.sa_data, 6);

            /* switch tx/rx MIC key for sta */
            if (prIWEncExt->alg == IW_ENCODE_ALG_TKIP && prIWEncExt->key_len == 32) {
                memcpy(prKey->aucKeyMaterial, prIWEncExt->key, 16);
                memcpy(((PUINT_8)prKey->aucKeyMaterial) + 16, prIWEncExt->key + 24, 8);
                memcpy((prKey->aucKeyMaterial) + 24, prIWEncExt->key + 16, 8);
            }
            else {
                memcpy(prKey->aucKeyMaterial, prIWEncExt->key, prIWEncExt->key_len);
            }

            prKey->u4KeyLength = prIWEncExt->key_len;
            prKey->u4Length = ((UINT_32)&(((P_PARAM_KEY_T)0)->aucKeyMaterial)) + prKey->u4KeyLength;


            rStatus = kalIoctl(prGlueInfo,
                    wlanoidSetAddKey,
                    prKey,
                    prKey->u4Length,
                    FALSE,
                    FALSE,
                    TRUE,
                    FALSE,
                    &u4BufLen);

            if (rStatus != WLAN_STATUS_SUCCESS) {
                DBGLOG(INIT, INFO, ("add key error:%lx\n", rStatus));
                return -EFAULT;
            }
            break;
        }
    }

    return 0;
} /* wext_set_encode_ext */


/*----------------------------------------------------------------------------*/
/*!
* \brief Set country code
*
* \param[in] prDev Net device requested.
* \param[in] prIwrInfo NULL.
* \param[in] pu4Mode Pointer to new operation mode.
* \param[in] pcExtra NULL.
*
* \retval 0 For success.
* \retval -EOPNOTSUPP If new mode is not supported.
*
* \note Device will run in new operation mode if it is valid.
*/
/*----------------------------------------------------------------------------*/
static int
wext_set_country (
    IN struct net_device *prNetDev,
    IN struct iwreq      *iwr
    )
{
    P_GLUE_INFO_T   prGlueInfo;
    WLAN_STATUS     rStatus;
    UINT_32         u4BufLen;
    UINT_8          aucCountry[2];

    ASSERT(prNetDev);

    /* iwr->u.data.pointer should be like "COUNTRY US", "COUNTRY EU"
     * and "COUNTRY JP"
     */
    if (FALSE == GLUE_CHK_PR2(prNetDev, iwr) ||
        !iwr->u.data.pointer || iwr->u.data.length < 10) {
        return -EINVAL;
    }
    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prNetDev));

    aucCountry[0] = *((PUINT_8)iwr->u.data.pointer + 8);
    aucCountry[1] = *((PUINT_8)iwr->u.data.pointer + 9);

    rStatus = kalIoctl(prGlueInfo,
        wlanoidSetCountryCode,
        &aucCountry[0],
        2,
        FALSE,
        FALSE,
        TRUE,
        FALSE,
        &u4BufLen);

    return 0;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief ioctl() (Linux Wireless Extensions) routines
*
* \param[in] prDev Net device requested.
* \param[in] ifr The ifreq structure for seeting the wireless extension.
* \param[in] i4Cmd The wireless extension ioctl command.
*
* \retval zero On success.
* \retval -EOPNOTSUPP If the cmd is not supported.
* \retval -EFAULT If copy_to_user goes wrong.
* \retval -EINVAL If any value's out of range.
*
* \note
*/
/*----------------------------------------------------------------------------*/
int
wext_support_ioctl (
    IN struct net_device *prDev,
    IN struct ifreq *prIfReq,
    IN int i4Cmd
    )
{
    /* prIfReq is verified in the caller function wlanDoIOCTL() */
    struct iwreq *iwr = (struct iwreq*)prIfReq;
    struct iw_request_info rIwReqInfo;
    int ret = 0;
    char *prExtraBuf = NULL;
    UINT_32 u4ExtraSize = 0;

    /* prDev is verified in the caller function wlanDoIOCTL() */

    //printk("%d CMD:0x%x\n", jiffies_to_msecs(jiffies), i4Cmd);

    /* Prepare the call */
    rIwReqInfo.cmd = (__u16)i4Cmd;
    rIwReqInfo.flags = 0;

    switch (i4Cmd) {
    case SIOCGIWNAME:   /* 0x8B01, get wireless protocol name */
        ret = wext_get_name(prDev, &rIwReqInfo, (char *)&iwr->u.name, NULL);
        break;

    /* case SIOCSIWNWID: 0x8B02, deprecated */
    /* case SIOCGIWNWID: 0x8B03, deprecated */

    case SIOCSIWFREQ:   /* 0x8B04, set channel */
        ret = wext_set_freq(prDev, NULL, &iwr->u.freq, NULL);
        break;

    case SIOCGIWFREQ:   /* 0x8B05, get channel */
        ret = wext_get_freq(prDev, NULL, &iwr->u.freq, NULL);
        break;

    case SIOCSIWMODE:   /* 0x8B06, set operation mode */
        ret = wext_set_mode(prDev, NULL, &iwr->u.mode, NULL);
        //ret = 0;
        break;

    case SIOCGIWMODE:   /* 0x8B07, get operation mode */
        ret = wext_get_mode(prDev, NULL, &iwr->u.mode, NULL);
        break;

    /* case SIOCSIWSENS: 0x8B08, unsupported */
    /* case SIOCGIWSENS: 0x8B09, unsupported */

    /* case SIOCSIWRANGE: 0x8B0A, unused */
    case SIOCGIWRANGE: /* 0x8B0B, get range of parameters */
        if (iwr->u.data.pointer != NULL) {
            /* Buffer size shoule be large enough */
            if (iwr->u.data.length < sizeof(struct iw_range)) {
                ret = -E2BIG;
                break;
            }

            prExtraBuf = kalMemAlloc(sizeof(struct iw_range), VIR_MEM_TYPE);
            if (!prExtraBuf) {
                ret = - ENOMEM;
                break;
            }

            /* reset all fields */
            memset(prExtraBuf, 0, sizeof(struct iw_range));
                iwr->u.data.length = sizeof(struct iw_range);

            ret = wext_get_range(prDev, NULL, &iwr->u.data, prExtraBuf);
            /* Push up to the caller */
            if (copy_to_user(iwr->u.data.pointer,
                    prExtraBuf,
                    iwr->u.data.length)) {
                    ret = -EFAULT;
                }

            kalMemFree(prExtraBuf, VIR_MEM_TYPE, sizeof(struct iw_range));
            prExtraBuf = NULL;
        }
        else {
            ret = -EINVAL;
        }
        break;

    case SIOCSIWPRIV: /* 0x8B0C, Country */
        ret = wext_set_country(prDev, iwr);
        break;

    /* case SIOCGIWPRIV: 0x8B0D, handled in wlan_do_ioctl() */
    /* caes SIOCSIWSTATS: 0x8B0E, unused */
    /* case SIOCGIWSTATS:
            get statistics, intercepted by wireless_process_ioctl() in wireless.c,
            redirected to dev_iwstats(), dev->get_wireless_stats().
    */
    /* case SIOCSIWSPY: 0x8B10, unsupported */
    /* case SIOCGIWSPY: 0x8B11, unsupported*/
    /* case SIOCSIWTHRSPY: 0x8B12, unsupported */
    /* case SIOCGIWTHRSPY: 0x8B13, unsupported*/

    case SIOCSIWAP: /* 0x8B14, set access point MAC addresses (BSSID) */
        if (iwr->u.ap_addr.sa_data[0] == 0 &&
            iwr->u.ap_addr.sa_data[1] == 0 &&
            iwr->u.ap_addr.sa_data[2] == 0 &&
            iwr->u.ap_addr.sa_data[3] == 0 &&
            iwr->u.ap_addr.sa_data[4] == 0 &&
            iwr->u.ap_addr.sa_data[5] == 0) {
            /* WPA Supplicant will set 000000000000 in
            ** wpa_driver_wext_deinit(), do nothing here or disassoc again?
            */
            ret = 0;
            break;
        }
        else {
            ret = wext_set_ap(prDev, NULL, &iwr->u.ap_addr, NULL);
        }
        break;

    case SIOCGIWAP: /* 0x8B15, get access point MAC addresses (BSSID) */
        ret = wext_get_ap(prDev, NULL, &iwr->u.ap_addr, NULL);
        break;

    case SIOCSIWMLME: /* 0x8B16, request MLME operation */
        /* Fixed length structure */
        if (iwr->u.data.length != sizeof(struct iw_mlme)) {
            DBGLOG(INIT, INFO, ("MLME buffer strange:%d\n", iwr->u.data.length));
            ret = -EINVAL;
            break;
        }

        if (!iwr->u.data.pointer) {
            ret = -EINVAL;
            break;
        }

        prExtraBuf = kalMemAlloc(sizeof(struct iw_mlme), VIR_MEM_TYPE);
        if (!prExtraBuf) {
            ret = - ENOMEM;
                break;
        }

        if (copy_from_user(prExtraBuf, iwr->u.data.pointer, sizeof(struct iw_mlme))) {
            ret = -EFAULT;
        }
        else {
            ret = wext_set_mlme(prDev, NULL, &(iwr->u.data), prExtraBuf);
        }

        kalMemFree(prExtraBuf, VIR_MEM_TYPE, sizeof(struct iw_mlme));
        prExtraBuf = NULL;
        break;

    /* case SIOCGIWAPLIST: 0x8B17, deprecated */
    case SIOCSIWSCAN: /* 0x8B18, scan request */
        if (iwr->u.data.pointer == NULL) {
            ret = wext_set_scan(prDev, NULL, NULL, NULL);
        }
#if WIRELESS_EXT > 17
		else if (iwr->u.data.length == sizeof(struct iw_scan_req)) {
            prExtraBuf = kalMemAlloc(MAX_SSID_LEN, VIR_MEM_TYPE);
            if (!prExtraBuf) {
                ret = -ENOMEM;
                break;
            }
            if (copy_from_user(prExtraBuf, ((struct iw_scan_req *) (iwr->u.data.pointer))->essid,
                ((struct iw_scan_req *) (iwr->u.data.pointer))->essid_len)) {
                ret = -EFAULT;
            } else {
                ret = wext_set_scan(prDev, NULL, (union iwreq_data *)  &(iwr->u.data), prExtraBuf);
            }

            kalMemFree(prExtraBuf, VIR_MEM_TYPE, MAX_SSID_LEN);
            prExtraBuf = NULL;
        }
#endif
		else {
            ret = -EINVAL;
        }
        break;
#if 1
    case SIOCGIWSCAN: /* 0x8B19, get scan results */
        if (!iwr->u.data.pointer|| !iwr->u.essid.pointer) {
            ret = -EINVAL;
            break;
        }

        u4ExtraSize = iwr->u.data.length;
        /* allocate the same size of kernel buffer to store scan results. */
        prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);
            if (!prExtraBuf) {
                ret = - ENOMEM;
                break;
            }

        /* iwr->u.data.length may be updated by wext_get_scan() */
            ret = wext_get_scan(prDev, NULL, &iwr->u.data, prExtraBuf);
            if (ret != 0) {
                if (ret == -E2BIG) {
                    DBGLOG(INIT, INFO, ("[wifi] wext_get_scan -E2BIG\n"));
                }
            }
            else {
            /* check updated length is valid */
                ASSERT(iwr->u.data.length <= u4ExtraSize);
                if (iwr->u.data.length > u4ExtraSize) {
                    DBGLOG(INIT, INFO, ("Updated result length is larger than allocated (%d > %ld)\n",
                        iwr->u.data.length, u4ExtraSize));
                    iwr->u.data.length = u4ExtraSize;
                }

                if (copy_to_user(iwr->u.data.pointer,
                        prExtraBuf,
                    iwr->u.data.length)) {
                    ret = -EFAULT;
                }
            }

        kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
        prExtraBuf = NULL;

        break;

#endif

#if 1
    case SIOCSIWESSID: /* 0x8B1A, set SSID (network name) */
        if (iwr->u.essid.length > IW_ESSID_MAX_SIZE) {
            ret = -E2BIG;
            break;
        }
        if (!iwr->u.essid.pointer) {
            ret = -EINVAL;
            break;
        }

        prExtraBuf = kalMemAlloc(IW_ESSID_MAX_SIZE + 4, VIR_MEM_TYPE);
        if (!prExtraBuf) {
            ret = - ENOMEM;
            break;
        }

        if (copy_from_user(prExtraBuf,
                iwr->u.essid.pointer,
                iwr->u.essid.length)) {
                ret = -EFAULT;
        }
        else {
        /* Add trailing '\0' for printk */
        //prExtraBuf[iwr->u.essid.length] = 0;
        //printk(KERN_INFO "wext_set_essid: %s (%d)\n", prExtraBuf, iwr->u.essid.length);
            ret = wext_set_essid(prDev, NULL, &iwr->u.essid, prExtraBuf);
            //printk ("set essid %d\n", ret);
        }

        kalMemFree(prExtraBuf, VIR_MEM_TYPE, IW_ESSID_MAX_SIZE + 4);
        prExtraBuf = NULL;
        break;

#endif

    case SIOCGIWESSID: /* 0x8B1B, get SSID */
        if (!iwr->u.essid.pointer) {
            ret = -EINVAL;
            break;
        }

        if (iwr->u.essid.length < IW_ESSID_MAX_SIZE) {
        DBGLOG(INIT, INFO, ("[wifi] iwr->u.essid.length:%d too small\n",
                iwr->u.essid.length));
            ret = -E2BIG;   /* let caller try larger buffer */
            break;
        }

        prExtraBuf = kalMemAlloc(IW_ESSID_MAX_SIZE, VIR_MEM_TYPE);
        if (!prExtraBuf) {
            ret = -ENOMEM;
            break;
        }

        /* iwr->u.essid.length is updated by wext_get_essid() */

        ret = wext_get_essid(prDev, NULL, &iwr->u.essid, prExtraBuf);
        if (ret == 0) {
            if (copy_to_user(iwr->u.essid.pointer, prExtraBuf, iwr->u.essid.length)) {
                ret = -EFAULT;
            }
        }

        kalMemFree(prExtraBuf, VIR_MEM_TYPE, IW_ESSID_MAX_SIZE);
        prExtraBuf = NULL;

        break;

    /* case SIOCSIWNICKN: 0x8B1C, not supported */
    /* case SIOCGIWNICKN: 0x8B1D, not supported */

    case SIOCSIWRATE: /* 0x8B20, set default bit rate (bps) */
        //ret = wext_set_rate(prDev, &rIwReqInfo, &iwr->u.bitrate, NULL);
        break;

    case SIOCGIWRATE: /* 0x8B21, get current bit rate (bps) */
        ret = wext_get_rate(prDev, NULL, &iwr->u.bitrate, NULL);
        break;

    case SIOCSIWRTS: /* 0x8B22, set rts/cts threshold */
        ret = wext_set_rts(prDev, NULL, &iwr->u.rts, NULL);
        break;

    case SIOCGIWRTS: /* 0x8B23, get rts/cts threshold */
        ret = wext_get_rts(prDev, NULL, &iwr->u.rts, NULL);
        break;

    /* case SIOCSIWFRAG: 0x8B24, unsupported */
    case SIOCGIWFRAG: /* 0x8B25, get frag threshold */
        ret = wext_get_frag(prDev, NULL, &iwr->u.frag, NULL);
        break;

    case SIOCSIWTXPOW: /* 0x8B26, set relative tx power (in %) */
        ret = wext_set_txpow(prDev, NULL, &iwr->u.txpower, NULL);
        break;

    case SIOCGIWTXPOW: /* 0x8B27, get relative tx power (in %) */
        ret = wext_get_txpow(prDev, NULL, &iwr->u.txpower, NULL);
        break;

    /* case SIOCSIWRETRY: 0x8B28, unsupported */
    /* case SIOCGIWRETRY: 0x8B29, unsupported */

#if 1
    case SIOCSIWENCODE: /* 0x8B2A, set encoding token & mode */
        /* Only DISABLED case has NULL pointer and length == 0 */
        if (iwr->u.encoding.pointer) {
            if (iwr->u.encoding.length > 16) {
                ret = -E2BIG;
                break;
            }

            u4ExtraSize = iwr->u.encoding.length;
            prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);
        if (!prExtraBuf) {
            ret = -ENOMEM;
            break;
        }

        if (copy_from_user(prExtraBuf,
                iwr->u.encoding.pointer,
                iwr->u.encoding.length)) {
                ret = -EFAULT;
            }
        }
        else if (iwr->u.encoding.length != 0) {
            ret = -EINVAL;
            break;
        }

        if (ret == 0) {
            ret = wext_set_encode(prDev, NULL, &iwr->u.encoding, prExtraBuf);
        }

        if (prExtraBuf) {
            kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
            prExtraBuf = NULL;
        }
        break;

    case SIOCGIWENCODE: /* 0x8B2B, get encoding token & mode */
        /* check pointer */
        ret = wext_get_encode(prDev, NULL, &iwr->u.encoding, NULL);
        break;

    case SIOCSIWPOWER: /* 0x8B2C, set power management */
        ret = wext_set_power(prDev, NULL, &iwr->u.power, NULL);
        break;

    case SIOCGIWPOWER: /* 0x8B2D, get power management */
        ret = wext_get_power(prDev, NULL, &iwr->u.power, NULL);
        break;

#if WIRELESS_EXT > 17
    case SIOCSIWGENIE: /* 0x8B30, set gen ie */
        if (iwr->u.data.pointer) {
            P_GLUE_INFO_T prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
            if (1 /* wlanQueryWapiMode(prGlueInfo->prAdapter) */) {
                /* Fixed length structure */
#if CFG_SUPPORT_WAPI
                if (iwr->u.data.length > 42 /* The max wapi ie buffer */) {
                    ret = -EINVAL;
                    break;
                }
#endif
                u4ExtraSize = iwr->u.data.length;
                if (u4ExtraSize) {
                    prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);
                    if (!prExtraBuf) {
                        ret = -ENOMEM;
                        break;
                    }
                    if (copy_from_user(prExtraBuf,
                        iwr->u.data.pointer,
                        iwr->u.data.length)) {
                        ret = -EFAULT;
                    }
                    else {
                        WLAN_STATUS    rStatus;
                        UINT_32        u4BufLen;
#if CFG_SUPPORT_WAPI
                        rStatus = kalIoctl(prGlueInfo,
                            wlanoidSetWapiAssocInfo,
                            prExtraBuf,
                            u4ExtraSize,
                            FALSE,
                            FALSE,
                            TRUE,
                            FALSE,
                            &u4BufLen);

                        if (rStatus != WLAN_STATUS_SUCCESS) {
                            //printk(KERN_INFO "[wapi] set wapi assoc info error:%lx\n", rStatus);
#endif
#if CFG_SUPPORT_WPS2
                            PUINT_8 prDesiredIE = NULL;
							if (wextSrchDesiredWPSIE(prExtraBuf,
										  u4ExtraSize,
										  0xDD,
										  (PUINT_8 *)&prDesiredIE)) {
                                rStatus = kalIoctl(prGlueInfo,
                                    wlanoidSetWSCAssocInfo,
                                    prDesiredIE,
                                    IE_SIZE(prDesiredIE),
                                    FALSE,
                                    FALSE,
                                    TRUE,
                                    FALSE,
                                    &u4BufLen);
                                if (rStatus != WLAN_STATUS_SUCCESS) {
								    //printk(KERN_INFO "[WSC] set WSC assoc info error:%lx\n", rStatus);
                                }
							}
#endif
#if CFG_SUPPORT_WAPI
                        }
#endif
                    }
                    kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
                    prExtraBuf = NULL;
                }
            }
        }
        break;

    case SIOCGIWGENIE: /* 0x8B31, get gen ie, unused */
        break;

#endif

    case SIOCSIWAUTH: /* 0x8B32, set auth mode params */
        ret = wext_set_auth(prDev, NULL, &iwr->u.param, NULL);
        break;

    /* case SIOCGIWAUTH: 0x8B33, unused? */
    case SIOCSIWENCODEEXT: /* 0x8B34, set extended encoding token & mode */
        if (iwr->u.encoding.pointer) {
            u4ExtraSize = iwr->u.encoding.length;
            prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);
            if (!prExtraBuf) {
                ret = -ENOMEM;
                break;
            }

            if (copy_from_user(prExtraBuf,
                    iwr->u.encoding.pointer,
                    iwr->u.encoding.length)) {
                ret = -EFAULT;
            }
        }
        else if (iwr->u.encoding.length != 0) {
            ret = -EINVAL;
            break;
        }

        if (ret == 0) {
            ret = wext_set_encode_ext(prDev, NULL, &iwr->u.encoding, prExtraBuf);
        }

        if (prExtraBuf) {
            kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
            prExtraBuf = NULL;
        }
        break;

    /* case SIOCGIWENCODEEXT: 0x8B35, unused? */

    case SIOCSIWPMKSA: /* 0x8B36, pmksa cache operation */
        #if 1
        if (iwr->u.data.pointer) {
            /* Fixed length structure */
            if (iwr->u.data.length != sizeof(struct iw_pmksa)) {
                ret = -EINVAL;
                break;
            }

            u4ExtraSize = sizeof(struct iw_pmksa);
            prExtraBuf = kalMemAlloc(u4ExtraSize, VIR_MEM_TYPE);
            if (!prExtraBuf) {
                ret = -ENOMEM;
                break;
            }

            if (copy_from_user(prExtraBuf,
                  iwr->u.data.pointer,
                  sizeof(struct iw_pmksa))) {
                ret = -EFAULT;
            }
            else {
                switch(((struct iw_pmksa *)prExtraBuf)->cmd) {
                case IW_PMKSA_ADD:
                    /*
                    printk(KERN_INFO "IW_PMKSA_ADD [" MACSTR "]\n",
                        MAC2STR(((struct iw_pmksa *)pExtraBuf)->bssid.sa_data));
                    */
                    {
                    P_GLUE_INFO_T prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
                    WLAN_STATUS    rStatus;
                    UINT_32        u4BufLen;
                    P_PARAM_PMKID_T  prPmkid;

                    prPmkid =(P_PARAM_PMKID_T)kalMemAlloc(8 + sizeof(PARAM_BSSID_INFO_T), VIR_MEM_TYPE);
                    if (!prPmkid) {
                        DBGLOG(INIT, INFO, ("Can not alloc memory for IW_PMKSA_ADD\n"));
                        ret = -ENOMEM;
                        break;
                    }

                    prPmkid->u4Length = 8 + sizeof(PARAM_BSSID_INFO_T);
                    prPmkid->u4BSSIDInfoCount = 1;
                    kalMemCopy(prPmkid->arBSSIDInfo->arBSSID,
                        ((struct iw_pmksa *)prExtraBuf)->bssid.sa_data,
                        6);
                    kalMemCopy(prPmkid->arBSSIDInfo->arPMKID,
                        ((struct iw_pmksa *)prExtraBuf)->pmkid,
                        IW_PMKID_LEN);

                    rStatus = kalIoctl(prGlueInfo,
                                 wlanoidSetPmkid,
                                 prPmkid,
                                 sizeof(PARAM_PMKID_T),
                                 FALSE,
                                 FALSE,
                                 TRUE,
                                 FALSE,
                                 &u4BufLen);

                    if (rStatus != WLAN_STATUS_SUCCESS) {
                        DBGLOG(INIT, INFO, ("add pmkid error:%lx\n", rStatus));
                    }
                    kalMemFree(prPmkid, VIR_MEM_TYPE, 8 + sizeof(PARAM_BSSID_INFO_T));
                    }
                    break;
                case IW_PMKSA_REMOVE:
                    /*
                    printk(KERN_INFO "IW_PMKSA_REMOVE [" MACSTR "]\n",
                        MAC2STR(((struct iw_pmksa *)buf)->bssid.sa_data));
                    */
                    break;
                case IW_PMKSA_FLUSH:
                    /*
                    printk(KERN_INFO "IW_PMKSA_FLUSH\n");
                    */
                    {
                    P_GLUE_INFO_T prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
                    WLAN_STATUS    rStatus;
                    UINT_32        u4BufLen;
                    P_PARAM_PMKID_T  prPmkid;

                    prPmkid =(P_PARAM_PMKID_T)kalMemAlloc(8, VIR_MEM_TYPE);
                    if (!prPmkid) {
                        DBGLOG(INIT, INFO, ("Can not alloc memory for IW_PMKSA_FLUSH\n"));
                        ret = -ENOMEM;
                        break;
                    }

                    prPmkid->u4Length = 8;
                    prPmkid->u4BSSIDInfoCount = 0;

                    rStatus = kalIoctl(prGlueInfo,
                                 wlanoidSetPmkid,
                                 prPmkid,
                                 sizeof(PARAM_PMKID_T),
                                 FALSE,
                                 FALSE,
                                 TRUE,
                                 FALSE,
                                 &u4BufLen);

                    if (rStatus != WLAN_STATUS_SUCCESS) {
                        DBGLOG(INIT, INFO, ("flush pmkid error:%lx\n", rStatus));
                    }
                    kalMemFree(prPmkid, VIR_MEM_TYPE, 8);
                    }
                    break;
                default:
                    DBGLOG(INIT, INFO, ("UNKNOWN iw_pmksa command:%d\n",
                        ((struct iw_pmksa *)prExtraBuf)->cmd));
                    ret = -EFAULT;
                    break;
                }
            }

            if (prExtraBuf) {
                kalMemFree(prExtraBuf, VIR_MEM_TYPE, u4ExtraSize);
                prExtraBuf = NULL;
            }
        }
        else if (iwr->u.data.length != 0) {
            ret = -EINVAL;
            break;
        }
        #endif
        break;

#endif

    default:
    /* printk(KERN_NOTICE "unsupported IOCTL: 0x%x\n", i4Cmd); */
        ret = -EOPNOTSUPP;
        break;
    }

    //printk("%ld CMD:0x%x ret:%d\n", jiffies_to_msecs(jiffies), i4Cmd, ret);

    return ret;
} /* wext_support_ioctl */



/*----------------------------------------------------------------------------*/
/*!
* \brief To send an event (RAW socket pacekt) to user process actively.
*
* \param[in] prGlueInfo Glue layer info.
* \param[in] u4cmd Whcih event command we want to indicate to user process.
* \param[in] pData Data buffer to be indicated.
* \param[in] dataLen Available data size in pData.
*
* \return (none)
*
* \note Event is indicated to upper layer if cmd is supported and data is valid.
*       Using of kernel symbol wireless_send_event(), which is defined in
*      <net/iw_handler.h> after WE-14 (2.4.20).
*/
/*----------------------------------------------------------------------------*/
void
wext_indicate_wext_event (
    IN P_GLUE_INFO_T prGlueInfo,
    IN unsigned int u4Cmd,
    IN unsigned char *pucData,
    IN unsigned int u4dataLen
    )
{
    union iwreq_data wrqu;
    unsigned char *pucExtraInfo = NULL;
#if WIRELESS_EXT >= 15
    unsigned char *pucDesiredIE = NULL;
    unsigned char aucExtraInfoBuf[200];
#endif
#if WIRELESS_EXT < 18
	int i;
#endif

    memset(&wrqu, 0, sizeof(wrqu));

    switch (u4Cmd) {
    case SIOCGIWTXPOW:
        memcpy(&wrqu.power, pucData, u4dataLen);
        break;
    case SIOCGIWSCAN:
        complete_all(&prGlueInfo->rScanComp);
        break;

    case SIOCGIWAP:
        if (pucData) {
            memcpy(&wrqu.ap_addr.sa_data, pucData, ETH_ALEN);
        }
        else {
            memset(&wrqu.ap_addr.sa_data, 0, ETH_ALEN);
        }
        break;

    case IWEVASSOCREQIE:
#if WIRELESS_EXT < 15
        /* under WE-15, no suitable Event can be used */
        goto skip_indicate_event;
#else
        /* do supplicant a favor, parse to the start of WPA/RSN IE */
        if (wextSrchDesiredWPAIE(pucData, u4dataLen, 0x30, &pucDesiredIE)) {
            /* RSN IE found */
        }
#if 0
        else if (wextSrchDesiredWPSIE(pucData, u4dataLen, 0xDD, &pucDesiredIE)) {
            /* WPS IE found */
        }
#endif
        else if (wextSrchDesiredWPAIE(pucData, u4dataLen, 0xDD, &pucDesiredIE)) {
            /* WPA IE found */
        }
#if CFG_SUPPORT_WAPI /* Android+ */
        else if (wextSrchDesiredWAPIIE(pucData, u4dataLen, &pucDesiredIE)) {
            //printk("wextSrchDesiredWAPIIE!!\n");
            /* WAPI IE found */
        }
#endif
        else {
            /* no WPA/RSN IE found, skip this event */
            goto skip_indicate_event;
        }

    #if WIRELESS_EXT < 18
        /* under WE-18, only IWEVCUSTOM can be used */
        u4Cmd = IWEVCUSTOM;
        pucExtraInfo = aucExtraInfoBuf;
        pucExtraInfo += sprintf(pucExtraInfo, "ASSOCINFO(ReqIEs=");
        /* printk(KERN_DEBUG "assoc info buffer size needed:%d\n", infoElemLen * 2 + 17); */
        /* translate binary string to hex string, requirement of IWEVCUSTOM */
        for (i = 0; i < pucDesiredIE[1] + 2 ; ++i) {
            pucExtraInfo += sprintf(pucExtraInfo, "%02x", pucDesiredIE[i]);
        }
        pucExtraInfo = aucExtraInfoBuf;
        wrqu.data.length = 17 + (pucDesiredIE[1] + 2) * 2;
    #else
         /* IWEVASSOCREQIE, indicate binary string */
        pucExtraInfo = pucDesiredIE;
        wrqu.data.length = pucDesiredIE[1] + 2;
    #endif
#endif  /* WIRELESS_EXT < 15 */
        break;

    case IWEVMICHAELMICFAILURE:
#if WIRELESS_EXT < 15
        /* under WE-15, no suitable Event can be used */
        goto skip_indicate_event;
#else
        if (pucData) {
            P_PARAM_AUTH_REQUEST_T pAuthReq = (P_PARAM_AUTH_REQUEST_T)pucData;
            /* under WE-18, only IWEVCUSTOM can be used */
            u4Cmd = IWEVCUSTOM;
            pucExtraInfo = aucExtraInfoBuf;
            pucExtraInfo += sprintf(pucExtraInfo,
                    "MLME-MICHAELMICFAILURE.indication ");
            pucExtraInfo += sprintf(pucExtraInfo,
                                "%s",
                                (pAuthReq->u4Flags == PARAM_AUTH_REQUEST_GROUP_ERROR) ?
                                "groupcast " : "unicast ");

            wrqu.data.length = pucExtraInfo - aucExtraInfoBuf;
            pucExtraInfo = aucExtraInfoBuf;
        }
#endif /* WIRELESS_EXT < 15 */
        break;

    case IWEVPMKIDCAND:
        if (prGlueInfo->rWpaInfo.u4WpaVersion == IW_AUTH_WPA_VERSION_WPA2 &&
            prGlueInfo->rWpaInfo.u4KeyMgmt == IW_AUTH_KEY_MGMT_802_1X) {

            /* only used in WPA2 */
#if WIRELESS_EXT >= 18
            P_PARAM_PMKID_CANDIDATE_T prPmkidCand = (P_PARAM_PMKID_CANDIDATE_T)pucData;

            struct  iw_pmkid_cand rPmkidCand;
            pucExtraInfo = aucExtraInfoBuf;

            rPmkidCand.flags = prPmkidCand->u4Flags;
            rPmkidCand.index = 0;
            kalMemCopy(rPmkidCand.bssid.sa_data, prPmkidCand->arBSSID, 6);

            kalMemCopy(pucExtraInfo, (PUINT_8)&rPmkidCand, sizeof(struct iw_pmkid_cand));
            wrqu.data.length = sizeof(struct iw_pmkid_cand);

            /* pmkid canadidate list is supported after WE-18 */
            /* indicate struct iw_pmkid_cand */
#else
            /* printk(KERN_INFO "IWEVPMKIDCAND event skipped, WE < 18\n"); */
            goto skip_indicate_event;
#endif
        }
        else {
            /* printk(KERN_INFO "IWEVPMKIDCAND event skipped, NOT WPA2\n"); */
            goto skip_indicate_event;
        }
        break;

    case IWEVCUSTOM:
        u4Cmd = IWEVCUSTOM;
        pucExtraInfo = aucExtraInfoBuf;
        kalMemCopy(pucExtraInfo, pucData, sizeof(PTA_IPC_T));
        wrqu.data.length = sizeof(PTA_IPC_T);
        break;

        default:
            /* printk(KERN_INFO "Unsupported wext event:%x\n", cmd); */
            goto skip_indicate_event;
    }

    /* Send event to user space */
    wireless_send_event(prGlueInfo->prDevHandler, u4Cmd, &wrqu, pucExtraInfo);

skip_indicate_event:
    return;
} /* wext_indicate_wext_event */


/*----------------------------------------------------------------------------*/
/*!
* \brief A method of struct net_device, to get the network interface statistical
*        information.
*
* Whenever an application needs to get statistics for the interface, this method
* is called. This happens, for example, when ifconfig or netstat -i is run.
*
* \param[in] pDev Pointer to struct net_device.
*
* \return net_device_stats buffer pointer.
*
*/
/*----------------------------------------------------------------------------*/
struct iw_statistics *
wext_get_wireless_stats (
    struct net_device *prDev
    )
{

    WLAN_STATUS rStatus = WLAN_STATUS_FAILURE;
    P_GLUE_INFO_T prGlueInfo = NULL;
    struct iw_statistics *pStats = NULL;
    INT_32 i4Rssi;
    UINT_32 bufLen = 0;


    prGlueInfo = *((P_GLUE_INFO_T *) netdev_priv(prDev));
     ASSERT(prGlueInfo);
    if (!prGlueInfo) {
        goto stat_out;
    }

    pStats = (struct iw_statistics *) (&(prGlueInfo->rIwStats));

    if (!prDev || !netif_carrier_ok(prDev)) {
        /* network not connected */
        goto stat_out;
    }

    rStatus = kalIoctl(prGlueInfo,
        wlanoidQueryRssi,
        &i4Rssi,
        sizeof(i4Rssi),
        TRUE,
        TRUE,
        TRUE,
        FALSE,
        &bufLen);

stat_out:
    return pStats;
} /* wlan_get_wireless_stats */

/*----------------------------------------------------------------------------*/
/*!
* \brief To report the private supported IOCTLs table to user space.
*
* \param[in] prNetDev Net device requested.
* \param[out] prIfReq Pointer to ifreq structure, content is copied back to
*                  user space buffer in gl_iwpriv_table.
*
* \retval 0 For success.
* \retval -E2BIG For user's buffer size is too small.
* \retval -EFAULT For fail.
*
*/
/*----------------------------------------------------------------------------*/
int
wext_get_priv (
    IN struct net_device *prNetDev,
    IN struct ifreq *prIfReq
    )
{
    /* prIfReq is verified in the caller function wlanDoIOCTL() */
    struct iwreq *prIwReq = (struct iwreq *)prIfReq;
    struct iw_point *prData= (struct iw_point *)&prIwReq->u.data;
    UINT_16 u2BufferSize = 0;

    u2BufferSize = prData->length;

    /* update our private table size */
    prData->length = (__u16)sizeof(rIwPrivTable)/sizeof(struct iw_priv_args);

    if (u2BufferSize < prData->length) {
        return -E2BIG;
    }

    if (prData->length) {
        if (copy_to_user(prData->pointer, rIwPrivTable, sizeof(rIwPrivTable))) {
            return -EFAULT;
        }
    }

    return 0;
} /* wext_get_priv */

#endif /* WIRELESS_EXT */



