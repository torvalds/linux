/******************************************************************************
 *
 * Copyright(c) 2007 - 2019 Realtek Corporation.
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

/* 
 * RTW_BUSY_DENY_SCAN control if scan would be denied by busy traffic.
 * When this defined, BUSY_TRAFFIC_SCAN_DENY_PERIOD would be used to judge if 
 * scan request coming from scan UI. Scan request from scan UI would be
 * exception and never be denied by busy traffic.
 */
#define RTW_BUSY_DENY_SCAN

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
		#undef CONFIG_ROAMING_FLAG
        	#define CONFIG_ROAMING_FLAG	0x7
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

/* Default enable single wiphy if driver ver >= 5.9 */
#define RTW_SINGLE_WIPHY

#ifdef CONFIG_RTW_ANDROID

	#include <linux/version.h>
	
	#ifndef CONFIG_IOCTL_CFG80211
	#define CONFIG_IOCTL_CFG80211
	#endif
	
	#ifndef RTW_USE_CFG80211_STA_EVENT
	#define RTW_USE_CFG80211_STA_EVENT
	#endif

	#if (CONFIG_RTW_ANDROID > 4)
	#ifndef CONFIG_RADIO_WORK
	#define CONFIG_RADIO_WORK
	#endif
	#endif

	#if (CONFIG_RTW_ANDROID <= 7)
		#ifdef RTW_SINGLE_WIPHY
		#undef RTW_SINGLE_WIPHY
		#endif
	#endif

	#if (CONFIG_RTW_ANDROID >= 8)
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,0))
		#ifndef CONFIG_RTW_WIFI_HAL
		#define CONFIG_RTW_WIFI_HAL
		#endif
		#else
 		#error "Linux kernel version is too old\n"
		#endif
	#endif

	#ifdef CONFIG_RTW_WIFI_HAL
	#ifndef CONFIG_RTW_WIFI_HAL_DEBUG
	//#define CONFIG_RTW_WIFI_HAL_DEBUG
	#endif
	#ifndef CONFIG_RTW_CFGVENDOR_LLSTATS
	#define CONFIG_RTW_CFGVENDOR_LLSTATS
	#endif
	#if (CONFIG_RTW_ANDROID < 11)
	#ifndef CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	#define CONFIG_RTW_CFGVENDOR_RANDOM_MAC_OUI
	#endif
	#endif
	#ifndef CONFIG_RTW_CFGVENDOR_RSSIMONITOR
	#define CONFIG_RTW_CFGVENDOR_RSSIMONITOR
	#endif
	#ifndef CONFIG_RTW_CFGVENDOR_WIFI_LOGGER
	#define CONFIG_RTW_CFGVENDOR_WIFI_LOGGER
	#endif
	#if (CONFIG_RTW_ANDROID >= 10)
	#ifndef CONFIG_RTW_CFGVENDOR_WIFI_OFFLOAD
	//#define CONFIG_RTW_CFGVENDOR_WIFI_OFFLOAD
	#endif
	#ifndef CONFIG_RTW_HOSTAPD_ACS
	#define CONFIG_RTW_HOSTAPD_ACS
	#endif
	#ifndef CONFIG_KERNEL_PATCH_EXTERNAL_AUTH
	#define CONFIG_KERNEL_PATCH_EXTERNAL_AUTH
	#endif
	#ifndef CONFIG_RTW_ABORT_SCAN
	#define CONFIG_RTW_ABORT_SCAN
	#endif
	#endif
	#endif // CONFIG_RTW_WIFI_HAL


	/* Some Android build will restart the UI while non-printable ascii is passed
	* between java and c/c++ layer (JNI). We force CONFIG_VALIDATE_SSID
	* for Android here. If you are sure there is no risk on your system about this,
	* mask this macro define to support non-printable ascii ssid.
	* #define CONFIG_VALIDATE_SSID */

	/* Android expect dbm as the rx signal strength unit */
	#define CONFIG_SIGNAL_DISPLAY_DBM
#endif // CONFIG_RTW_ANDROID

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

#ifdef CONFIG_WIFI_MONITOR
	/*	#define CONFIG_MONITOR_MODE_XMIT	*/
#endif

#ifdef CONFIG_CUSTOMER_ALIBABA_GENERAL
	#ifndef CONFIG_WIFI_MONITOR
		#define CONFIG_WIFI_MONITOR
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

#ifndef CONFIG_RTW_DATA_BMC_TO_UC
#define CONFIG_RTW_DATA_BMC_TO_UC 0
#endif

#ifdef CONFIG_AP_MODE
	#define CONFIG_LIMITED_AP_NUM 1

	#ifndef CONFIG_RTW_AP_DATA_BMC_TO_UC
	#define CONFIG_RTW_AP_DATA_BMC_TO_UC 1
	#endif
	#if CONFIG_RTW_AP_DATA_BMC_TO_UC
	#undef CONFIG_RTW_DATA_BMC_TO_UC
	#define CONFIG_RTW_DATA_BMC_TO_UC 1
	#endif
	#ifndef CONFIG_RTW_AP_SRC_B2U_FLAGS
	#define CONFIG_RTW_AP_SRC_B2U_FLAGS 0x8 /* see RTW_AP_B2U_XXX */
	#endif
	#ifndef CONFIG_RTW_AP_FWD_B2U_FLAGS
	#define CONFIG_RTW_AP_FWD_B2U_FLAGS 0x8 /* see RTW_AP_B2U_XXX */
	#endif
#endif

#ifdef CONFIG_RTW_MULTI_AP
	#ifndef CONFIG_AP_MODE
	#error "enable CONFIG_RTW_MULTI_AP without CONFIG_AP_MODE"
	#endif
	#ifndef CONFIG_RTW_WDS
	#define CONFIG_RTW_WDS
	#endif
	#ifndef CONFIG_RTW_UNASOC_STA_MODE_OF_STYPE
	#define CONFIG_RTW_UNASOC_STA_MODE_OF_STYPE {2, 1} /* BMC:2 for all, NMY_UC:1 for interested target */
	#endif
	#ifndef CONFIG_RTW_NLRTW
	#define CONFIG_RTW_NLRTW
	#endif
	#ifndef CONFIG_RTW_WNM
	#define CONFIG_RTW_WNM
	#endif
	#ifndef CONFIG_RTW_80211K
	#define CONFIG_RTW_80211K
	#endif
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
	#if CONFIG_RTW_MESH_DATA_BMC_TO_UC
	#undef CONFIG_RTW_DATA_BMC_TO_UC
	#define CONFIG_RTW_DATA_BMC_TO_UC 1
	#endif
	#ifndef CONFIG_RTW_MSRC_B2U_FLAGS
	#define CONFIG_RTW_MSRC_B2U_FLAGS 0x0 /* see RTW_MESH_B2U_XXX */
	#endif
	#ifndef CONFIG_RTW_MFWD_B2U_FLAGS
	#define CONFIG_RTW_MFWD_B2U_FLAGS 0x2 /* see RTW_MESH_B2U_XXX */
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

#ifndef CONFIG_IEEE80211_BAND_5GHZ
	#if defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8821C) \
		|| defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8822C) \
		|| defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8814B) || defined(CONFIG_RTL8723F)
	#define CONFIG_IEEE80211_BAND_5GHZ 1
	#else
	#define CONFIG_IEEE80211_BAND_5GHZ 0
	#endif
#endif

#ifndef CONFIG_DFS
#define CONFIG_DFS 1
#endif

#if CONFIG_IEEE80211_BAND_5GHZ && CONFIG_DFS && defined(CONFIG_AP_MODE)
	#if !defined(CONFIG_DFS_SLAVE_WITH_RADAR_DETECT)
	#define CONFIG_DFS_SLAVE_WITH_RADAR_DETECT 0
	#endif
	#if !defined(CONFIG_DFS_MASTER) || CONFIG_DFS_SLAVE_WITH_RADAR_DETECT
	#define CONFIG_DFS_MASTER
	#endif
	#if defined(CONFIG_DFS_MASTER) && !defined(CONFIG_RTW_DFS_REGION_DOMAIN)
	#define CONFIG_RTW_DFS_REGION_DOMAIN 0
	#endif
#else
	#undef CONFIG_DFS_MASTER
	#undef CONFIG_RTW_DFS_REGION_DOMAIN
	#define CONFIG_RTW_DFS_REGION_DOMAIN 0
	#undef CONFIG_DFS_SLAVE_WITH_RADAR_DETECT
	#define CONFIG_DFS_SLAVE_WITH_RADAR_DETECT 0
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
	#ifdef CONFIG_REGD_SRC_FROM_OS
	#error "CONFIG_REGD_SRC_FROM_OS is not supported when enable RTW_DEF_MODULE_REGULATORY_CERT"
	#endif
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

#ifndef CONFIG_RTW_REGD_SRC
#define CONFIG_RTW_REGD_SRC 1 /* 0:RTK_PRIV, 1:OS */
#endif

#define CONFIG_IOCTL_WEXT

#ifdef CONFIG_RTW_IPCAM_APPLICATION
	#undef CONFIG_TXPWR_BY_RATE_EN
	#define CONFIG_TXPWR_BY_RATE_EN 1
	#define CONFIG_RTW_CUSTOMIZE_BEEDCA		0x0000431C
	#define CONFIG_RTW_CUSTOMIZE_BWMODE		0x00
	#define CONFIG_RTW_CUSTOMIZE_RLSTA		0x30
	#define CONFIG_CHECK_SPECIFIC_IE_CONTENT
	#ifdef CONFIG_CUSTOMER_EZVIZ_CHIME2
		#undef CONFIG_ACTIVE_KEEP_ALIVE_CHECK
	#endif
#if defined(CONFIG_RTL8192E) || defined(CONFIG_RTL8192F) || defined(CONFIG_RTL8822B)
	#define CONFIG_RTW_TX_NPATH_EN		/*	mutually incompatible with STBC_TX & Beamformer	*/
#endif
#endif
/* #define CONFIG_RTW_TOKEN_BASED_XMIT */
#ifdef CONFIG_RTW_TOKEN_BASED_XMIT
	#define NR_TBTX_SLOT			4
	#define NR_MAXSTA_INSLOT		5
	#define TBTX_TX_DURATION		30
	
	#define MAX_TXPAUSE_DURATION	(TBTX_TX_DURATION*NR_TBTX_SLOT)
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

#ifndef CONFIG_RTW_ANTENNA_GAIN
#define CONFIG_RTW_ANTENNA_GAIN 0x7FFF /* == UNSPECIFIED_MBM */
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

#if defined(CONFIG_RTL8188E) || defined(CONFIG_RTL8192E) || defined(CONFIG_RTL8188F) || \
defined(CONFIG_RTL8188GTV) || defined(CONFIG_RTL8192F) || \
defined(CONFIG_RTL8812A) || defined(CONFIG_RTL8821A) || defined(CONFIG_RTL8710B) || \
defined(CONFIG_RTL8723B) || defined(CONFIG_RTL8703B) || defined(CONFIG_RTL8723D)
#define CONFIG_HWMPCAP_GEN1
#elif defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8822C) || \
defined(CONFIG_RTL8723F) /*|| defined(CONFIG_RTL8814A)*/
#define CONFIG_HWMPCAP_GEN2
#elif defined(CONFIG_RTL8814B) /*Address CAM - 128*/
#define CONFIG_HWMPCAP_GEN3
#endif

#if defined(CONFIG_HWMPCAP_GEN1) && (CONFIG_IFACE_NUMBER > 2) 
	#ifdef CONFIG_POWER_SAVING
	/*#warning "Disable PS when CONFIG_IFACE_NUMBER > 2"*/
	#undef CONFIG_POWER_SAVING
	#endif

	#ifdef CONFIG_WOWLAN
	#error "This IC can't support MI and WoWLan at the same time"
	#endif
#endif

#if defined(CONFIG_HWMPCAP_GEN1) && (CONFIG_IFACE_NUMBER > 3)
        #error " This IC can't support over 3 interfaces !!"
#endif

#if (CONFIG_IFACE_NUMBER > 4)
	#error "Not support over 4 interfaces yet !!"
#endif

#if (CONFIG_IFACE_NUMBER > 8)	/*IFACE_ID_MAX*/
	#error "HW count not support over 8 interfaces !!"
#endif

#if (CONFIG_IFACE_NUMBER > 2)
	#ifndef CONFIG_HWMPCAP_GEN3
		#define CONFIG_MI_WITH_MBSSID_CAM
	#endif

	#ifdef CONFIG_MI_WITH_MBSSID_CAM
		#define CONFIG_MBSSID_CAM
		#if defined(CONFIG_RUNTIME_PORT_SWITCH)
			#undef CONFIG_RUNTIME_PORT_SWITCH
		#endif
	#endif

	#ifdef CONFIG_AP_MODE
		#undef CONFIG_LIMITED_AP_NUM
		#define CONFIG_LIMITED_AP_NUM	2

		#define CONFIG_SUPPORT_MULTI_BCN

		#define CONFIG_SWTIMER_BASED_TXBCN

		#ifdef CONFIG_HWMPCAP_GEN2 /*CONFIG_RTL8822B/CONFIG_RTL8821C/CONFIG_RTL8822C*/
		#define CONFIG_FW_HANDLE_TXBCN

		#ifdef CONFIG_FW_HANDLE_TXBCN
			#ifdef CONFIG_SWTIMER_BASED_TXBCN
				#undef CONFIG_SWTIMER_BASED_TXBCN
			#endif
			#undef CONFIG_LIMITED_AP_NUM
			#define CONFIG_LIMITED_AP_NUM	4
		#endif

		#endif /*CONFIG_HWMPCAP_GEN2*/

		#ifdef CONFIG_HWMPCAP_GEN3
			#define CONFIG_PORT_BASED_TXBCN
			#undef CONFIG_SUPPORT_MULTI_BCN
			#undef CONFIG_SWTIMER_BASED_TXBCN
			#undef CONFIG_LIMITED_AP_NUM
			#define CONFIG_LIMITED_AP_NUM	4
			#ifdef CONFIG_PCI_HCI
			#define CONFIG_PORT_BASED_HIQ	/* 8814BU doesn't support */
			#endif
		#endif
	#endif /*CONFIG_AP_MODE*/

	#ifdef CONFIG_HWMPCAP_GEN2 /*CONFIG_RTL8822B/CONFIG_RTL8821C/CONFIG_RTL8822C*/
	#define CONFIG_CLIENT_PORT_CFG
	#define CONFIG_NEW_NETDEV_HDL
	#endif/*CONFIG_HWMPCAP_GEN2*/
#endif/*(CONFIG_IFACE_NUMBER > 2)*/

#if defined(CONFIG_MI_UNIQUE_MACADDR_BIT)
	#if !defined(CONFIG_MI_WITH_MBSSID_CAM)
		#error "CONFIG_MI_UNIQUE_MACADDR_BIT should not be used without multiple interface !!"
	#endif
	#if (CONFIG_MI_UNIQUE_MACADDR_BIT < 24) || ( 47 < CONFIG_MI_UNIQUE_MACADDR_BIT)
		#error "CONFIG_MI_UNIQUE_MACADDR_BIT should be the bit in NIC specific mac address(BIT[24:47] !!"
	#endif
#endif

#define MACID_NUM_SW_LIMIT 32
#define SEC_CAM_ENT_NUM_SW_LIMIT 32

#ifdef SEC_DEFAULT_KEY_SEARCH
	#if (CONFIG_IFACE_NUMBER >= 2)
		#error "Default Key Search only work with only one interface case!"
	#endif
#endif

#if defined(CONFIG_WOWLAN) && (defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C) || defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822C) || defined(CONFIG_RTL8814B))
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
	#ifndef CONFIG_RTW_ACS
		#define CONFIG_RTW_ACS
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

#ifndef RTW_LPS_1T1R
#define RTW_LPS_1T1R 0
#endif

#ifndef RTW_WOW_LPS_1T1R
#define RTW_WOW_LPS_1T1R 0
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

#ifdef CONFIG_WAR_OFFLOAD
#ifndef CONFIG_WOWLAN
	#error "WAR OFFLOAD is part of WOWLAN"
#endif
#endif

#if defined(CONFIG_OFFLOAD_MDNS_V4) || defined(CONFIG_OFFLOAD_MDNS_V6)
#ifndef CONFIG_WOWLAN
	#error "mDNS OFFLOAD is part of WOWLAN"
#endif
#ifndef CONFIG_WAR_OFFLOAD
	#define CONFIG_WAR_OFFLOAD
#endif
#endif

#define CONFIG_RTW_TPT_MODE 

#ifdef CONFIG_PCI_BCN_POLLING
#define CONFIG_BCN_ICF
#endif 

#ifndef CONFIG_RTW_MGMT_QUEUE
	#define CONFIG_RTW_MGMT_QUEUE
#endif

#ifndef CONFIG_PCI_MSI
#define CONFIG_RTW_PCI_MSI_DISABLE
#endif

#if defined(CONFIG_PCI_DYNAMIC_ASPM_L1_LATENCY) ||	\
    defined(CONFIG_PCI_DYNAMIC_ASPM_LINK_CTRL)
#define CONFIG_PCI_DYNAMIC_ASPM
#endif

#if 0
/* Debug related compiler flags */
#define DBG_THREAD_PID	/* Add thread pid to debug message prefix */
#define DBG_CPU_INFO	/* Add CPU info to debug message prefix */
#endif

#endif /* __DRV_CONF_H__ */
