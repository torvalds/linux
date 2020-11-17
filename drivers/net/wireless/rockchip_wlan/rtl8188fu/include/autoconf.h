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
/*
 * Public General Configure
 */
#define AUTOCONF_INCLUDED

#define RTL871X_MODULE_NAME "8188FU"
#define DRV_NAME "rtl8188fu"

#ifndef CONFIG_RTL8188F
#define CONFIG_RTL8188F
#endif
#define CONFIG_USB_HCI

#define PLATFORM_LINUX

#define RTL8188F_USB_MAC_LOOPBACK 0

/*
 * Wi-Fi Functions Configure
 */
#define CONFIG_80211N_HT
#define CONFIG_RECV_REORDERING_CTRL

/* #define CONFIG_IOCTL_CFG80211 */		/* Set from Makefile */
#ifdef CONFIG_IOCTL_CFG80211
	/*
	 * Indicate new sta asoc through cfg80211_new_sta
	 * If kernel version >= 3.2 or
	 * version < 3.2 but already apply cfg80211 patch,
	 * RTW_USE_CFG80211_STA_EVENT must be defined!
	 */
	/* #define RTW_USE_CFG80211_STA_EVENT */ /* Indicate new sta asoc through cfg80211_new_sta */
	#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER

	/* #define CONFIG_DEBUG_CFG80211 */
	/* #define CONFIG_DRV_ISSUE_PROV_REQ */ /* IOT FOR S2 */

	#define CONFIG_SET_SCAN_DENY_TIMER
#endif

#define CONFIG_AP_MODE
#ifdef CONFIG_AP_MODE
	#define CONFIG_NATIVEAP_MLME
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME
	#endif
	/* #define CONFIG_FIND_BEST_CHANNEL */
	/* #define CONFIG_AUTO_AP_MODE */
#endif

#define CONFIG_P2P
#ifdef CONFIG_P2P
	/*
	 * Added by Albert 20110812
	 * The CONFIG_WFD is for supporting the Wi-Fi display
	 */
	#define CONFIG_WFD

	#define CONFIG_P2P_REMOVE_GROUP_INFO

	/* #define CONFIG_DBG_P2P */
	#define CONFIG_P2P_PS
	#define CONFIG_P2P_OP_CHK_SOCIAL_CH
	#define CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT  /* replace CONFIG_P2P_CHK_INVITE_CH_LIST flag */
	/*#define CONFIG_P2P_INVITE_IOT*/
#endif

/*
 * Added by Kurt 20110511
 */
#ifdef CONFIG_TDLS
	#define CONFIG_TDLS_DRIVER_SETUP
/*
	#ifndef CONFIG_WFD
		#define CONFIG_WFD
	#endif
	#define CONFIG_TDLS_AUTOSETUP
*/
	#define CONFIG_TDLS_AUTOCHECKALIVE
	#define CONFIG_TDLS_CH_SW	/* Enable this flag only when we confirm that TDLS CH SW is supported in FW */
#endif

/* #define CONFIG_CONCURRENT_MODE */	/* Set from Makefile */
#ifdef CONFIG_CONCURRENT_MODE
	#define CONFIG_TSF_RESET_OFFLOAD			/* For 2 PORT TSF SYNC. */
	#define CONFIG_RUNTIME_PORT_SWITCH

	/* #define DBG_RUNTIME_PORT_SWITCH */
#endif /* CONFIG_CONCURRENT_MODE */

#define CONFIG_LAYER2_ROAMING
#define CONFIG_LAYER2_ROAMING_RESUME

/*
 * Hareware/Firmware Related Configure
 */
/* #define CONFIG_ANTENNA_DIVERSITY	 */
/* #define SUPPORT_HW_RFOFF_DETECTED */

#define CONFIG_RTW_LED
#ifdef CONFIG_RTW_LED
	#define CONFIG_RTW_SW_LED
	#ifdef CONFIG_RTW_SW_LED
		/* define CONFIG_RTW_LED_HANDLED_BY_CMD_THREAD */
	#endif
#endif /* CONFIG_RTW_LED */

#define CONFIG_XMIT_ACK
#ifdef CONFIG_XMIT_ACK
	#define CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif

#define CONFIG_RF_POWER_TRIM

#define DISABLE_BB_RF	0

#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable, */

/*
 * Interface Related Configure
 */
#define CONFIG_USB_TX_AGGREGATION
#define CONFIG_USB_RX_AGGREGATION


#define CONFIG_GLOBAL_UI_PID

#define CONFIG_OUT_EP_WIFI_MODE

#define ENABLE_USB_DROP_INCORRECT_OUT

/* #define CONFIG_SUPPORT_USB_INT */
#ifdef CONFIG_SUPPORT_USB_INT
/* #define CONFIG_USB_INTERRUPT_IN_PIPE */
#endif

/* #define CONFIG_REDUCE_USB_TX_INT	*/ /* Trade-off: Improve performance, but may cause TX URBs blocked by USB Host/Bus driver on few platforms. */

/*
 * CONFIG_USE_USB_BUFFER_ALLOC_XX uses Linux USB Buffer alloc API and is for Linux platform only now!
 */
/* #define CONFIG_USE_USB_BUFFER_ALLOC_TX	*/ /* Trade-off: For TX path, improve stability on some platforms, but may cause performance degrade on other platforms. */
/* #define CONFIG_USE_USB_BUFFER_ALLOC_RX	*/ /* For RX path */

/*
 * USB VENDOR REQ BUFFER ALLOCATION METHOD
 * if not set we'll use function local variable (stack memory)
 */
/* #define CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE */
#define CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC
#define CONFIG_USB_VENDOR_REQ_MUTEX
#define CONFIG_VENDOR_REQ_RETRY
/* #define CONFIG_USB_SUPPORT_ASYNC_VDN_REQ */


/*
 * Others
 */
#define CONFIG_SKB_COPY	/* for amsdu */

/* #define CONFIG_EASY_REPLACEMENT */

/* #define CONFIG_ADAPTOR_INFO_CACHING_FILE */ /* now just applied on 8192cu only, should make it general... */

/* #define CONFIG_RESUME_IN_WORKQUEUE */

/* #define CONFIG_SET_SCAN_DENY_TIMER */

#define CONFIG_NEW_SIGNAL_STAT_PROCESS

/* #define CONFIG_SIGNAL_DISPLAY_DBM */ /* display RX signal with dbm */
#ifdef CONFIG_SIGNAL_DISPLAY_DBM
/* #define CONFIG_BACKGROUND_NOISE_MONITOR */
#endif

#define CONFIG_EMBEDDED_FWIMG

#ifdef CONFIG_EMBEDDED_FWIMG
	#define	LOAD_FW_HEADER_FROM_DRIVER
#endif
/* #define CONFIG_FILE_FWIMG */

#define CONFIG_LONG_DELAY_ISSUE


/*
 * Auto Configure Section
 */
#ifdef CONFIG_MINIMAL_MEMORY_USAGE
	#undef CONFIG_USB_TX_AGGREGATION
	#undef CONFIG_USB_RX_AGGREGATION
#endif

#ifdef CONFIG_MP_INCLUDED
	#define MP_DRIVER	1
	#define CONFIG_MP_IWPRIV_SUPPORT
	/* #undef CONFIG_USB_TX_AGGREGATION */
	/* #undef CONFIG_USB_RX_AGGREGATION */
#else /* !CONFIG_MP_INCLUDED */
	#define MP_DRIVER	0
#endif /* !CONFIG_MP_INCLUDED */

#ifdef CONFIG_POWER_SAVING
#define CONFIG_IPS
#define CONFIG_LPS

#ifdef CONFIG_IPS
/* #define CONFIG_IPS_LEVEL_2	1  */ /*enable this to set default IPS mode to IPS_LEVEL_2 */
#endif

#if defined(CONFIG_LPS)
	#define CONFIG_LPS_LCLK
#endif

#ifdef CONFIG_LPS_LCLK
	/* #define CONFIG_XMIT_THREAD_MODE */
	#ifndef CONFIG_SUPPORT_USB_INT
		#define LPS_RPWM_WAIT_MS 300
		#define CONFIG_DETECT_CPWM_BY_POLLING
	#endif
#endif
#endif /*CONFIG_POWER_SAVING*/

#ifdef CONFIG_BT_COEXIST
	/* for ODM and outsrc BT-Coex */
	#ifndef CONFIG_LPS
		#define CONFIG_LPS	/* download reserved page to FW */
	#endif
#endif /* CONFIG_BT_COEXIST */

#ifdef CONFIG_WOWLAN
	#define CONFIG_GTK_OL
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_GPIO_WAKEUP
	#ifndef WAKEUP_GPIO_IDX
		#define WAKEUP_GPIO_IDX	0	/* WIFI Chip Side */
	#endif /* !WAKEUP_GPIO_IDX */
#endif /* CONFIG_GPIO_WAKEUP */

#ifdef CONFIG_AP_MODE
	/* #define CONFIG_INTERRUPT_BASED_TXBCN */ /* Tx Beacon when driver BCN_OK ,BCN_ERR interrupt occurs */
	#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_INTERRUPT_BASED_TXBCN)
		#undef CONFIG_INTERRUPT_BASED_TXBCN
	#endif
	#ifdef CONFIG_INTERRUPT_BASED_TXBCN
		/* #define CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT */
		#define CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
	#endif
#endif /* CONFIG_AP_MODE */

#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX

#else
	#define CONFIG_PREALLOC_RECV_SKB
	#ifdef CONFIG_PREALLOC_RECV_SKB
		/* #define CONFIG_FIX_NR_BULKIN_BUFFER */ /* only use PREALLOC_RECV_SKB buffer, don't alloc skb at runtime */
	#endif
#endif

#ifdef CONFIG_USB_TX_AGGREGATION
/* #define CONFIG_TX_EARLY_MODE */
#endif

#ifdef CONFIG_TX_EARLY_MODE
#define RTL8188F_EARLY_MODE_PKT_NUM_10	0
#endif




/*
 * Debug Related Configure
 */
#ifdef CONFIG_RTW_DEBUG
	#define DBG	1	/* for ODM & BTCOEX debug */
	#define DBG_PHYDM_MORE 0
#else /* !CONFIG_RTW_DEBUG */
	#define DBG	0	/* for ODM & BTCOEX debug */
	#define DBG_PHYDM_MORE 0
#endif /* CONFIG_RTW_DEBUG */



#define CONFIG_PROC_DEBUG

/*
#define DBG_CONFIG_ERROR_DETECT
#define DBG_CONFIG_ERROR_DETECT_INT
#define DBG_CONFIG_ERROR_RESET

#define DBG_IO
#define DBG_DELAY_OS
#define DBG_MEM_ALLOC
#define DBG_IOCTL

#define DBG_TX
#define DBG_XMIT_BUF
#define DBG_XMIT_BUF_EXT
#define DBG_TX_DROP_FRAME

#define DBG_RX_DROP_FRAME
#define DBG_RX_SEQ
#define DBG_RX_SIGNAL_DISPLAY_PROCESSING
#define DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED "rtw-ap"

#define DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
#define DBG_ROAMING_TEST

#define DBG_HAL_INIT_PROFILING

#define DBG_MEMORY_LEAK
*/
#define	DBG_RX_DFRAME_RAW_DATA
