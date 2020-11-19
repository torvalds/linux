/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __DRV_CONF_H__
#define __DRV_CONF_H__
#include "autoconf.h"
#include "hal_ic_cfg.h"

#define CONFIG_RSSI_PRIORITY
#ifdef CONFIG_RTW_REPEATER_SON
	#ifndef CONFIG_AP
		#define CONFIG_AP
	#endif
	#ifndef CONFIG_CONCURRENT_MODE
		#define CONFIG_CONCURRENT_MODE
	#endif
	#ifndef CONFIG_BR_EXT
		#define CONFIG_BR_EXT
	#endif
	#ifndef CONFIG_RTW_REPEATER_SON_ID
		#define CONFIG_RTW_REPEATER_SON_ID			0x02040608
	#endif
	//#define CONFIG_RTW_REPEATER_SON_ROOT
	#ifndef CONFIG_RTW_REPEATER_SON_ROOT
		#define CONFIG_LAYER2_ROAMING_ACTIVE
	#endif
	#undef CONFIG_POWER_SAVING
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

#if defined(CONFIG_DFS_SLAVE_WITH_RADAR_DETECT) && !defined(CONFIG_DFS_MASTER)
	#define CONFIG_DFS_MASTER
#endif

#if !defined(CONFIG_AP_MODE) && defined(CONFIG_DFS_MASTER)
	#error "enable CONFIG_DFS_MASTER without CONFIG_AP_MODE"
#endif

#ifdef CONFIG_WIFI_MONITOR
	/*	#define CONFIG_MONITOR_MODE_XMIT	*/
#endif

#ifdef CONFIG_CUSTOMER_ALIBABA_GENERAL
	#ifndef CONFIG_WIFI_MONITOR
		#define CONFIG_WIFI_MONITOR
	#endif
	#ifndef CONFIG_MONITOR_MODE_XMIT
		#define CONFIG_MONITOR_MODE_XMIT
	#endif
	#ifdef CONFIG_POWER_SAVING
		#undef CONFIG_POWER_SAVING
	#endif
#endif

#ifdef CONFIG_CUSTOMER01_SMART_ANTENNA
	#ifdef CONFIG_POWER_SAVING
		#undef CONFIG_POWER_SAVING
	#endif
	#ifdef CONFIG_BEAMFORMING
		#undef CONFIG_BEAMFORMING
	#endif
#endif

#ifdef CONFIG_AP_MODE
	#define CONFIG_TX_MCAST2UNI /* AP mode support IP multicast->unicast */
#endif

#ifdef CONFIG_RTW_MESH
	#ifndef CONFIG_RTW_MESH_ACNODE_PREVENT
	#define CONFIG_RTW_MESH_ACNODE_PREVENT 1
	#endif

	#ifndef CONFIG_RTW_MESH_OFFCH_CAND
	#define CONFIG_RTW_MESH_OFFCH_CAND 1
	#endif

	#ifndef CONFIG_RTW_MESH_PEER_BLACKLIST
	#define CONFIG_RTW_MESH_PEER_BLACKLIST 1
	#endif

	#ifndef CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	#define CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST 1
	#endif
	#ifndef CONFIG_RTW_MESH_CTO_MGATE_CARRIER
	#define CONFIG_RTW_MESH_CTO_MGATE_CARRIER CONFIG_RTW_MESH_CTO_MGATE_BLACKLIST
	#endif

	#ifndef CONFIG_RTW_MPM_TX_IES_SYNC_BSS
	#define CONFIG_RTW_MPM_TX_IES_SYNC_BSS 1
	#endif
	#if CONFIG_RTW_MPM_TX_IES_SYNC_BSS
		#ifndef CONFIG_RTW_MESH_AEK
		#define CONFIG_RTW_MESH_AEK
		#endif
	#endif

	#ifndef CONFIG_RTW_MESH_DATA_BMC_TO_UC
	#define CONFIG_RTW_MESH_DATA_BMC_TO_UC 1
	#endif
#endif

#if !defined(CONFIG_SCAN_BACKOP) && defined(CONFIG_AP_MODE)
#define CONFIG_SCAN_BACKOP
#endif

#define RTW_SCAN_SPARSE_MIRACAST 1
#define RTW_SCAN_SPARSE_BG 0
#define RTW_SCAN_SPARSE_ROAMING_ACTIVE 1

#ifndef CONFIG_TX_AC_LIFETIME
#define CONFIG_TX_AC_LIFETIME 1
#endif
#ifndef CONFIG_TX_ACLT_FLAGS
#define CONFIG_TX_ACLT_FLAGS 0x00
#endif
#ifndef CONFIG_TX_ACLT_CONF_DEFAULT
#define CONFIG_TX_ACLT_CONF_DEFAULT {0x0, 1024 * 1000, 1024 * 1000}
#endif
#ifndef CONFIG_TX_ACLT_CONF_AP_M2U
#define CONFIG_TX_ACLT_CONF_AP_M2U {0xF, 256 * 1000, 256 * 1000}
#endif
#ifndef CONFIG_TX_ACLT_CONF_MESH
#define CONFIG_TX_ACLT_CONF_MESH {0xF, 256 * 1000, 256 * 1000}
#endif

#ifndef CONFIG_RTW_HIQ_FILTER
	#define CONFIG_RTW_HIQ_FILTER 1
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_EN
	#define CONFIG_RTW_ADAPTIVITY_EN 0
#endif

#ifndef CONFIG_RTW_ADAPTIVITY_MODE
	#define CONFIG_RTW_ADAPTIVITY_MODE 0
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

#ifndef CONFIG_RTW_CHPLAN
#define CONFIG_RTW_CHPLAN 0xFF /* RTW_CHPLAN_UNSPECIFIED */
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

#if !CONFIG_TXPWR_LIMIT && CONFIG_TXPWR_LIMIT_EN
	#undef CONFIG_TXPWR_LIMIT
	#define CONFIG_TXPWR_LIMIT 1
#endif

#ifdef CONFIG_RTW_IPCAM_APPLICATION
	#undef CONFIG_TXPWR_BY_RATE_EN
	#define CONFIG_TXPWR_BY_RATE_EN 1
	#define CONFIG_RTW_CUSTOMIZE_BEEDCA		0x0000431C
	#define CONFIG_RTW_CUSTOMIZE_BWMODE		0x00
	#define CONFIG_RTW_CUSTOMIZE_RLSTA		0x7
#if defined(CONFIG_RTL8192E) || defined(CONFIG_RTL8192F) || defined(CONFIG_RTL8822B)
	#define CONFIG_RTW_TX_2PATH_EN		/*	mutually incompatible with STBC_TX & Beamformer	*/
#endif
#endif
/*#define CONFIG_EXTEND_LOWRATE_TXOP			*/

#ifndef CONFIG_RTW_RX_AMPDU_SZ_LIMIT_1SS
	#define CONFIG_RTW_RX_AMPDU_SZ_LIMIT_1SS {0xFF, 0xFF, 0xFF, 0xFF}
#endif
#ifndef CONFIG_RTW_RX_AMPDU_SZ_LIMIT_2SS
	#define CONFIG_RTW_RX_AMPDU_SZ_LIMIT_2SS {0xFF, 0xFF, 0xFF, 0xFF}
#endif
#ifndef CONFIG_RTW_RX_AMPDU_SZ_LIMIT_3SS
	#define CONFIG_RTW_RX_AMPDU_SZ_LIMIT_3SS {0xFF, 0xFF, 0xFF, 0xFF}
#endif
#ifndef CONFIG_RTW_RX_AMPDU_SZ_LIMIT_4SS
	#define CONFIG_RTW_RX_AMPDU_SZ_LIMIT_4SS {0xFF, 0xFF, 0xFF, 0xFF}
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
	#error "CONFIG_IFACE_NUMBER cound not be 0 !!"
#endif

#if (CONFIG_IFACE_NUMBER > 4)
	#error "Not support over 4 interfaces yet !!"
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
		#define CONFIG_SUPPORT_MULTI_BCN

		#define CONFIG_SWTIMER_BASED_TXBCN

		#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) /* || defined(CONFIG_RTL8822C)*/
		#define CONFIG_FW_HANDLE_TXBCN

		#ifdef CONFIG_FW_HANDLE_TXBCN
			#ifdef CONFIG_SWTIMER_BASED_TXBCN
				#undef CONFIG_SWTIMER_BASED_TXBCN
			#endif

			#define CONFIG_LIMITED_AP_NUM	4
		#endif
	#endif /*defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) */ /*|| defined(CONFIG_RTL8822C)*/
	#endif /*CONFIG_AP_MODE*/

	#if defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)
	#define CONFIG_CLIENT_PORT_CFG
	#define CONFIG_NEW_NETDEV_HDL
	#endif/*defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C)*/

#endif/*(CONFIG_IFACE_NUMBER > 2)*/

#define MACID_NUM_SW_LIMIT 32
#define SEC_CAM_ENT_NUM_SW_LIMIT 32

#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A)
	#define CONFIG_IEEE80211_BAND_5GHZ
#endif

#if defined(CONFIG_WOWLAN) && (defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822C))
	#define CONFIG_WOW_PATTERN_HW_CAM
#endif

#ifndef CONFIG_TSF_UPDATE_PAUSE_FACTOR
#define CONFIG_TSF_UPDATE_PAUSE_FACTOR 200
#endif

#ifndef CONFIG_TSF_UPDATE_RESTORE_FACTOR
#define CONFIG_TSF_UPDATE_RESTORE_FACTOR 5
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
/*#define CONFIG_PHDYM_FW_FIXRATE		*/	/*	Another way to fix tx rate	*/

/*Don't release SDIO irq in suspend/resume procedure*/
#define CONFIG_RTW_SDIO_KEEP_IRQ	0

/*
 * Add by Lucas@2016/02/15
 * For RX Aggregation
 */
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_USB_RX_AGGREGATION)
	#define RTW_RX_AGGREGATION
#endif /* CONFIG_SDIO_HCI || CONFIG_USB_RX_AGGREGATION */

#ifdef CONFIG_RTW_HOSTAPD_ACS
	#if defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8814A)
		#ifndef CONFIG_FIND_BEST_CHANNEL
			#define CONFIG_FIND_BEST_CHANNEL
		#endif
	#else
		#ifdef CONFIG_FIND_BEST_CHANNEL
			#undef CONFIG_FIND_BEST_CHANNEL
		#endif
		#ifndef CONFIG_RTW_ACS
			#define CONFIG_RTW_ACS
		#endif
		#ifndef CONFIG_BACKGROUND_NOISE_MONITOR
			#define CONFIG_BACKGROUND_NOISE_MONITOR
		#endif
	#endif
#endif

#ifdef CONFIG_RTW_80211K
	#ifndef CONFIG_RTW_ACS
		#define CONFIG_RTW_ACS
	#endif
#endif /*CONFIG_RTW_80211K*/

#ifdef DBG_CONFIG_ERROR_RESET
#ifndef CONFIG_IPS
#define CONFIG_IPS
#endif
#endif

/* IPS */
#ifndef RTW_IPS_MODE
	#if defined(CONFIG_IPS)
		#define RTW_IPS_MODE 1
	#else
		#define RTW_IPS_MODE 0
	#endif
#endif /* !RTW_IPS_MODE */

#if (RTW_IPS_MODE > 1 || RTW_IPS_MODE < 0)
	#error "The CONFIG_IPS_MODE value is wrong. Please follow HowTo_enable_the_power_saving_functionality.pdf.\n"
#endif

/* LPS */
#ifndef RTW_LPS_MODE
	#if defined(CONFIG_LPS_PG) || defined(CONFIG_LPS_PG_DDMA)
		#define RTW_LPS_MODE 3
	#elif defined(CONFIG_LPS_LCLK)
		#define RTW_LPS_MODE 2
	#elif defined(CONFIG_LPS)
		#define RTW_LPS_MODE 1
	#else
		#define RTW_LPS_MODE 0
	#endif 
#endif /* !RTW_LPS_MODE */

#if (RTW_LPS_MODE > 3 || RTW_LPS_MODE < 0)
	#error "The CONFIG_LPS_MODE value is wrong. Please follow HowTo_enable_the_power_saving_functionality.pdf.\n"
#endif

/* WOW LPS */
#ifndef RTW_WOW_LPS_MODE
	#if defined(CONFIG_LPS_PG) || defined(CONFIG_LPS_PG_DDMA)
		#define RTW_WOW_LPS_MODE 3
	#elif defined(CONFIG_LPS_LCLK)
		#define RTW_WOW_LPS_MODE 2
	#elif defined(CONFIG_LPS)
		#define RTW_WOW_LPS_MODE 1
	#else
		#define RTW_WOW_LPS_MODE 0
	#endif
#endif /* !RTW_WOW_LPS_MODE */

#if (RTW_WOW_LPS_MODE > 3 || RTW_WOW_LPS_MODE < 0)
	#error "The RTW_WOW_LPS_MODE value is wrong. Please follow HowTo_enable_the_power_saving_functionality.pdf.\n"
#endif

#ifdef RTW_REDUCE_SCAN_SWITCH_CH_TIME
#ifndef CONFIG_RTL8822B
	#error "Only 8822B support RTW_REDUCE_SCAN_SWITCH_CH_TIME"
#endif
	#ifndef RTW_CHANNEL_SWITCH_OFFLOAD
		#define RTW_CHANNEL_SWITCH_OFFLOAD
	#endif
#endif

#define CONFIG_RTW_TPT_MODE 

#ifdef CONFIG_PCI_BCN_POLLING
#define CONFIG_BCN_ICF
#endif 

#ifndef CONFIG_PCI_MSI
#define CONFIG_RTW_PCI_MSI_DISABLE
#endif

#endif /* __DRV_CONF_H__ */
