/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __DRV_CONF_H__
#define __DRV_CONF_H__
#include "autoconf.h"
#include "hal_ic_cfg.h"

#if defined(PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)

	#error "Shall be Linux or Windows, but not both!\n"

#endif

#if defined(CONFIG_MCC_MODE) && (!defined(CONFIG_CONCURRENT_MODE))

	#error "Enable CONCURRENT_MODE before enable MCC MODE\n"

#endif

#if defined(CONFIG_MCC_MODE) && defined(CONFIG_BT_COEXIST)

	#error "Disable BT COEXIST before enable MCC MODE\n"

#endif

#if defined(CONFIG_MCC_MODE) && defined(CONFIG_TDLS)

	#error "Disable TDLS before enable MCC MODE\n"

#endif

#if defined(CONFIG_RTW_80211R) && !defined(CONFIG_LAYER2_ROAMING)

	#error "Enable CONFIG_LAYER2_ROAMING before enable CONFIG_RTW_80211R\n"

#endif

/* Older Android kernel doesn't has CONFIG_ANDROID defined,
 * add this to force CONFIG_ANDROID defined */
#ifdef CONFIG_PLATFORM_ANDROID
	#ifndef CONFIG_ANDROID
		#define CONFIG_ANDROID
	#endif
#endif

#ifdef CONFIG_ANDROID
	/* Some Android build will restart the UI while non-printable ascii is passed
	* between java and c/c++ layer (JNI). We force CONFIG_VALIDATE_SSID
	* for Android here. If you are sure there is no risk on your system about this,
	* mask this macro define to support non-printable ascii ssid.
	* #define CONFIG_VALIDATE_SSID */

	/* Android expect dbm as the rx signal strength unit */
	#define CONFIG_SIGNAL_DISPLAY_DBM
#endif

/*
#if defined(CONFIG_HAS_EARLYSUSPEND) && defined(CONFIG_RESUME_IN_WORKQUEUE)
	#warning "You have CONFIG_HAS_EARLYSUSPEND enabled in your system, we disable CONFIG_RESUME_IN_WORKQUEUE automatically"
	#undef CONFIG_RESUME_IN_WORKQUEUE
#endif

#if defined(CONFIG_ANDROID_POWER) && defined(CONFIG_RESUME_IN_WORKQUEUE)
	#warning "You have CONFIG_ANDROID_POWER enabled in your system, we disable CONFIG_RESUME_IN_WORKQUEUE automatically"
	#undef CONFIG_RESUME_IN_WORKQUEUE
#endif
*/

#ifdef CONFIG_RESUME_IN_WORKQUEUE /* this can be removed, because there is no case for this... */
	#if !defined(CONFIG_WAKELOCK) && !defined(CONFIG_ANDROID_POWER)
		#error "enable CONFIG_RESUME_IN_WORKQUEUE without CONFIG_WAKELOCK or CONFIG_ANDROID_POWER will suffer from the danger of wifi's unfunctionality..."
		#error "If you still want to enable CONFIG_RESUME_IN_WORKQUEUE in this case, mask this preprossor checking and GOOD LUCK..."
	#endif
#endif

/* About USB VENDOR REQ */
#if defined(CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC) && !defined(CONFIG_USB_VENDOR_REQ_MUTEX)
	#warning "define CONFIG_USB_VENDOR_REQ_MUTEX for CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC automatically"
	#define CONFIG_USB_VENDOR_REQ_MUTEX
#endif
#if defined(CONFIG_VENDOR_REQ_RETRY) &&  !defined(CONFIG_USB_VENDOR_REQ_MUTEX)
	#warning "define CONFIG_USB_VENDOR_REQ_MUTEX for CONFIG_VENDOR_REQ_RETRY automatically"
	#define CONFIG_USB_VENDOR_REQ_MUTEX
#endif

#if !defined(CONFIG_AP_MODE) && defined(CONFIG_DFS_MASTER)
	#warning "undef CONFIG_DFS_MASTER because CONFIG_AP_MODE is not defined"
	#undef CONFIG_DFS_MASTER
#endif

#define RTW_SCAN_SPARSE_MIRACAST 1
#define RTW_SCAN_SPARSE_BG 0
#define RTW_SCAN_SPARSE_ROAMING_ACTIVE 1

#ifndef CONFIG_RTW_HIQ_FILTER
	#define CONFIG_RTW_HIQ_FILTER 1
#endif

#ifndef CONFIG_RTW_FORCE_IGI_LB
	#define CONFIG_RTW_FORCE_IGI_LB 0
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_EN
	#define CONFIG_RTW_ADAPTIVITY_EN 0
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_MODE
	#define CONFIG_RTW_ADAPTIVITY_MODE 0
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_DML
	#define CONFIG_RTW_ADAPTIVITY_DML 0
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_DC_BACKOFF
	#define CONFIG_RTW_ADAPTIVITY_DC_BACKOFF 2
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_TH_L2H_INI
	#define CONFIG_RTW_ADAPTIVITY_TH_L2H_INI 0
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_TH_EDCCA_HL_DIFF
	#define CONFIG_RTW_ADAPTIVITY_TH_EDCCA_HL_DIFF 0
#endif

#ifndef CONFIG_RTW_EXCL_CHS
	#define CONFIG_RTW_EXCL_CHS {0}
#endif

#ifndef CONFIG_RTW_DFS_REGION_DOMAIN
	#define CONFIG_RTW_DFS_REGION_DOMAIN 0
#endif

#ifndef CONFIG_TXPWR_BY_RATE_EN
#define CONFIG_TXPWR_BY_RATE_EN 2 /* by efuse */
#endif
#ifndef CONFIG_TXPWR_LIMIT_EN
#define CONFIG_TXPWR_LIMIT_EN 2 /* by efuse */
#endif

/* compatible with old fashion configuration */
#if defined(CONFIG_CALIBRATE_TX_POWER_BY_REGULATORY)
	#undef CONFIG_TXPWR_BY_RATE_EN
	#undef CONFIG_TXPWR_LIMIT_EN
	#define CONFIG_TXPWR_BY_RATE_EN 1
	#define CONFIG_TXPWR_LIMIT_EN 1
#elif defined(CONFIG_CALIBRATE_TX_POWER_TO_MAX)
	#undef CONFIG_TXPWR_BY_RATE_EN
	#undef CONFIG_TXPWR_LIMIT_EN
	#define CONFIG_TXPWR_BY_RATE_EN 1
	#define CONFIG_TXPWR_LIMIT_EN 0
#endif

#ifndef RTW_DEF_MODULE_REGULATORY_CERT
	#define RTW_DEF_MODULE_REGULATORY_CERT 0
#endif

#if RTW_DEF_MODULE_REGULATORY_CERT
	/* force enable TX power by rate and TX power limit */
	#undef CONFIG_TXPWR_BY_RATE_EN
	#undef CONFIG_TXPWR_LIMIT_EN
	#define CONFIG_TXPWR_BY_RATE_EN 1
	#define CONFIG_TXPWR_LIMIT_EN 1
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_2G_A
	#define CONFIG_RTW_TARGET_TX_PWR_2G_A {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_2G_B
	#define CONFIG_RTW_TARGET_TX_PWR_2G_B {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_2G_C
	#define CONFIG_RTW_TARGET_TX_PWR_2G_C {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_2G_D
	#define CONFIG_RTW_TARGET_TX_PWR_2G_D {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_5G_A
	#define CONFIG_RTW_TARGET_TX_PWR_5G_A {-1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_5G_B
	#define CONFIG_RTW_TARGET_TX_PWR_5G_B {-1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_5G_C
	#define CONFIG_RTW_TARGET_TX_PWR_5G_C {-1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_TARGET_TX_PWR_5G_D
	#define CONFIG_RTW_TARGET_TX_PWR_5G_D {-1, -1, -1, -1, -1, -1, -1, -1, -1}
#endif

#ifndef CONFIG_RTW_AMPLIFIER_TYPE_2G
	#define CONFIG_RTW_AMPLIFIER_TYPE_2G 0
#endif

#ifndef CONFIG_RTW_AMPLIFIER_TYPE_5G
	#define CONFIG_RTW_AMPLIFIER_TYPE_5G 0
#endif

#ifndef CONFIG_RTW_RFE_TYPE
	#define CONFIG_RTW_RFE_TYPE 64
#endif

#ifndef CONFIG_RTW_GLNA_TYPE
	#define CONFIG_RTW_GLNA_TYPE 0
#endif

#ifndef CONFIG_RTW_PLL_REF_CLK_SEL
	#define CONFIG_RTW_PLL_REF_CLK_SEL 0x0F
#endif

#ifndef CONFIG_IFACE_NUMBER
	#ifdef CONFIG_CONCURRENT_MODE
		#define CONFIG_IFACE_NUMBER	2
	#else
		#define CONFIG_IFACE_NUMBER	1
	#endif
#endif

#ifndef CONFIG_CONCURRENT_MODE
	#if (CONFIG_IFACE_NUMBER > 1)
		#error "CONFIG_IFACE_NUMBER over 1,but CONFIG_CONCURRENT_MODE not defined"
	#endif
#endif

#if (CONFIG_IFACE_NUMBER == 0)
	#error "CONFIG_IFACE_NUMBER cound not equel to 0 !!"
#endif

#if (CONFIG_IFACE_NUMBER > 3)
	#error "Not support over 3 interfaces yet !!"
#endif

#if (CONFIG_IFACE_NUMBER > 8)	/*IFACE_ID_MAX*/
	#error "HW count not support over 8 interfaces !!"
#endif

#if (CONFIG_IFACE_NUMBER > 2)
	#define CONFIG_MI_WITH_MBSSID_CAM

	#ifdef CONFIG_MI_WITH_MBSSID_CAM
		#define CONFIG_MBSSID_CAM
		#if defined(CONFIG_RUNTIME_PORT_SWITCH)
			#undef CONFIG_RUNTIME_PORT_SWITCH
		#endif
	#endif

	#ifdef CONFIG_AP_MODE
		#define CONFIG_SWTIMER_BASED_TXBCN
		/*#define CONFIG_FW_BASED_BCN*/
	#endif
#endif

#define MACID_NUM_SW_LIMIT 32
#define SEC_CAM_ENT_NUM_SW_LIMIT 32

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A)
	#define CONFIG_IEEE80211_BAND_5GHZ
#endif

#if defined(CONFIG_WOWLAN) && (defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C))
	#define CONFIG_WOW_PATTERN_HW_CAM
#endif

/*
	Mark CONFIG_DEAUTH_BEFORE_CONNECT by Arvin 2015/07/20
	If the failure of Wi-Fi connection is due to some irregular disconnection behavior (like unplug dongle,
	power down etc.) in last time, we can unmark this flag to avoid some unpredictable response from AP.
*/
/*#define CONFIG_DEAUTH_BEFORE_CONNECT */

/*#define CONFIG_WEXT_DONT_JOIN_BYSSID	*/
/* #include <rtl871x_byteorder.h> */


/*#define CONFIG_DOSCAN_IN_BUSYTRAFFIC	*/

/*Don't release SDIO irq in suspend/resume procedure*/
#define CONFIG_RTW_SDIO_KEEP_IRQ	0

/*
 * Add by Lucas@2016/02/15
 * For RX Aggregation
 */
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_USB_RX_AGGREGATION)
	#define RTW_RX_AGGREGATION
#endif /* CONFIG_SDIO_HCI || CONFIG_USB_RX_AGGREGATION */

#endif /* __DRV_CONF_H__ */
