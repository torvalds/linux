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
 * Public General Config
 */
#define AUTOCONF_INCLUDED

#define RTL871X_MODULE_NAME "8189FS"
#define DRV_NAME "rtl8189fs"

#ifndef CONFIG_RTL8188F
#define CONFIG_RTL8188F
#endif
#define CONFIG_SDIO_HCI

#define PLATFORM_LINUX


/*
 * Wi-Fi Functions Config
 */
#define CONFIG_80211N_HT
#define CONFIG_RECV_REORDERING_CTRL

/* #define CONFIG_IOCTL_CFG80211 */		/* Set from Makefile */
#ifdef CONFIG_IOCTL_CFG80211
	/*
	 * Indecate new sta asoc through cfg80211_new_sta
	 * If kernel version >= 3.2 or
	 * version < 3.2 but already apply cfg80211 patch,
	 * RTW_USE_CFG80211_STA_EVENT must be defiend!
	 */
	/* #define RTW_USE_CFG80211_STA_EVENT */ /* Indecate new sta asoc through cfg80211_new_sta */
	#ifndef CONFIG_PLATFORM_INTEL_BYT
	#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
	#endif /* !CONFIG_PLATFORM_INTEL_BYT */
	/* #define CONFIG_DEBUG_CFG80211 */
	#define CONFIG_SET_SCAN_DENY_TIMER
#endif

#define CONFIG_AP_MODE
#ifdef CONFIG_AP_MODE
	#define CONFIG_NATIVEAP_MLME
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME
	#endif
	/* #define CONFIG_FIND_BEST_CHANNEL */
#endif

#define CONFIG_P2P
#ifdef CONFIG_P2P
	/* Added by Albert 20110812 */
	/* The CONFIG_WFD is for supporting the Wi-Fi display */
	#define CONFIG_WFD

	#define CONFIG_P2P_REMOVE_GROUP_INFO

	/* #define CONFIG_DBG_P2P */
	#define CONFIG_P2P_PS
	#define CONFIG_P2P_OP_CHK_SOCIAL_CH
	#define CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT  /* replace CONFIG_P2P_CHK_INVITE_CH_LIST flag */
	/*#define CONFIG_P2P_INVITE_IOT*/
#endif

/* Added by Kurt 20110511 */
#ifdef CONFIG_TDLS
	#define CONFIG_TDLS_DRIVER_SETUP
/*	#ifndef CONFIG_WFD */
/*		#define CONFIG_WFD */
/*	#endif */
/*	#define CONFIG_TDLS_AUTOSETUP */
	#define CONFIG_TDLS_AUTOCHECKALIVE
	/* #define CONFIG_TDLS_CH_SW */	/* Enable this flag only when we confirm that TDLS CH SW is supported in FW */
#endif

/* #define CONFIG_CONCURRENT_MODE */	/* Set from Makefile */
#ifdef CONFIG_CONCURRENT_MODE
	#define CONFIG_RUNTIME_PORT_SWITCH
	/* #define DBG_RUNTIME_PORT_SWITCH */


	#ifndef CONFIG_RUNTIME_PORT_SWITCH
		#define CONFIG_TSF_RESET_OFFLOAD			/* For 2 PORT TSF SYNC. */
	#endif
#endif /* CONFIG_CONCURRENT_MODE */

#define CONFIG_LAYER2_ROAMING
#define CONFIG_LAYER2_ROAMING_RESUME

/*
 * Hareware/Firmware Related Config
 */
/* #define CONFIG_BT_COEXIST */	/* Set from Makefile */
/* #define CONFIG_ANTENNA_DIVERSITY */
/* #define SUPPORT_HW_RFOFF_DETECTED */

/*#define CONFIG_RTW_LED*/
#ifdef CONFIG_RTW_LED
	/*#define CONFIG_RTW_SW_LED*/
#endif /* CONFIG_RTW_LED */

#define CONFIG_XMIT_ACK
#ifdef CONFIG_XMIT_ACK
	#define CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif

#define CONFIG_RF_POWER_TRIM

#define DISABLE_BB_RF	0

#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable, */

/*
 * Interface Related Config
 */
#define CONFIG_SDIO_CHK_HCI_RESUME
#define CONFIG_TX_AGGREGATION
#define SDIO_FREE_XMIT_BUF_SEMA
#define CONFIG_SDIO_RX_COPY
#define CONFIG_XMIT_THREAD_MODE
#define CONFIG_SDIO_TX_ENABLE_AVAL_INT

/* #define CONFIG_RECV_THREAD_MODE */
#ifdef CONFIG_RECV_THREAD_MODE
#define RTW_RECV_THREAD_HIGH_PRIORITY
#endif/*CONFIG_RECV_THREAD_MODE*/
/*
 * Others
 */
/* #define CONFIG_MAC_LOOPBACK_DRIVER */

#define CONFIG_SKB_COPY	/* for amsdu */

#define CONFIG_NEW_SIGNAL_STAT_PROCESS

#define CONFIG_EMBEDDED_FWIMG
#ifdef CONFIG_EMBEDDED_FWIMG
	#define	LOAD_FW_HEADER_FROM_DRIVER
#endif
/* #define CONFIG_FILE_FWIMG */

#define CONFIG_LONG_DELAY_ISSUE
/* #define CONFIG_PATCH_JOIN_WRONG_CHANNEL */


/*
 * Auto Config Section
 */
#ifdef CONFIG_MAC_LOOPBACK_DRIVER
	#undef CONFIG_IOCTL_CFG80211
	#undef CONFIG_AP_MODE
	#undef CONFIG_NATIVEAP_MLME
	#undef CONFIG_POWER_SAVING
	#undef CONFIG_BT_COEXIST
	#undef CONFIG_ANTENNA_DIVERSITY
	#undef SUPPORT_HW_RFOFF_DETECTED
#endif

#ifdef CONFIG_MP_INCLUDED
	#define MP_DRIVER	1
	#define CONFIG_MP_IWPRIV_SUPPORT
#else /* !CONFIG_MP_INCLUDED */
	#define MP_DRIVER	0
#endif /* !CONFIG_MP_INCLUDED */

#ifdef CONFIG_POWER_SAVING
	#define CONFIG_IPS
	#define CONFIG_LPS

	#if defined(CONFIG_LPS) && (defined(CONFIG_GSPI_HCI) || defined(CONFIG_SDIO_HCI))
		#define CONFIG_LPS_LCLK
	#endif

	#ifdef CONFIG_LPS
		#define CONFIG_CHECK_LEAVE_LPS
		/* #define CONFIG_LPS_SLOW_TRANSITION */
	#endif

	#ifdef CONFIG_LPS_LCLK
		#define CONFIG_DETECT_CPWM_BY_POLLING
		#define CONFIG_LPS_RPWM_TIMER
		#if defined(CONFIG_LPS_RPWM_TIMER) || defined(CONFIG_DETECT_CPWM_BY_POLLING)
			#define LPS_RPWM_WAIT_MS 300
		#endif
		#define CONFIG_LPS_LCLK_WD_TIMER /* Watch Dog timer in LPS LCLK */
	#endif

	#ifdef CONFIG_IPS
		#define CONFIG_IPS_CHECK_IN_WD /* Do IPS Check in WatchDog */
		/* #define CONFIG_SWLPS_IN_IPS */ /* Do SW LPS flow when entering and leaving IPS */
		/* #define CONFIG_FWLPS_IN_IPS */ /* issue H2C command to let FW do LPS when entering IPS */
	#endif
#endif /* CONFIG_POWER_SAVING */

#ifdef CONFIG_WOWLAN
	/* #define CONFIG_GTK_OL */
	/* #define CONFIG_ARP_KEEP_ALIVE */
#endif /* CONFIG_WOWLAN */

#ifdef CONFIG_GPIO_WAKEUP
	#ifndef WAKEUP_GPIO_IDX
		#define WAKEUP_GPIO_IDX	0
	#endif
#endif

/*
 * Debug Related Config
 */
#ifdef CONFIG_RTW_DEBUG
#define DBG	1	/* for ODM & BTCOEX debug */
#else /* !CONFIG_RTW_DEBUG */
#define DBG	0	/* for ODM & BTCOEX debug */
#endif /* CONFIG_RTW_DEBUG */

#define DBG_CONFIG_ERROR_DETECT
/* #define DBG_XMIT_BUF */
/* #define DBG_XMIT_BUF_EXT */
/* #define DBG_CHECK_FW_PS_STATE */
/* #define DBG_CHECK_FW_PS_STATE_H2C */
/* #define CONFIG_FW_C2H_DEBUG */
#define	DBG_RX_DFRAME_RAW_DATA
