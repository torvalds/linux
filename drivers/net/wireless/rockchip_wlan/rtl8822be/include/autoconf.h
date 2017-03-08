/******************************************************************************
 *
 * Copyright(c) 2015 - 2016 Realtek Corporation. All rights reserved.
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
/***** temporarily flag *******/
#define CONFIG_SINGLE_IMG
/* #define CONFIG_DISABLE_ODM */

/***** temporarily flag *******/
/*
 * Public  General Config
 */
#define AUTOCONF_INCLUDED
#define DRV_NAME "rtl8822be"

#define CONFIG_PCI_HCI
#define CONFIG_PCIE_HCI
#define PLATFORM_LINUX

/*
 * Wi-Fi Functions Config
 */
#define CONFIG_80211N_HT
#define CONFIG_RECV_REORDERING_CTRL
#define CONFIG_80211AC_VHT
#define CONFIG_IEEE80211_BAND_5GHZ

/*#define CONFIG_IOCTL_CFG80211*/
#ifdef CONFIG_IOCTL_CFG80211
	/*#define RTW_USE_CFG80211_STA_EVENT*/ /* Indecate new sta asoc through cfg80211_new_sta */
	#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
	/*#define CONFIG_DEBUG_CFG80211*/
	/*#define CONFIG_DRV_ISSUE_PROV_REQ*/ /* IOT FOR S2 */
	#define CONFIG_SET_SCAN_DENY_TIMER
#endif

#define TX_AMSDU

/*
 * Internal  General Config
 */
/*#define CONFIG_PWRCTRL*/
/*#define CONFIG_H2CLBK*/
#define CONFIG_TRX_BD_ARCH	/* PCI only */
#define USING_RX_TAG
/*#define CONFIG_NAPI*/
/*#define CONFIG_GRO*/
#define CONFIG_C2H_PACKET_EN	/* necessary for 8822B */
#define CONFIG_EMBEDDED_FWIMG
/*#define CONFIG_FILE_FWIMG*/

/*#define CONFIG_XMIT_ACK*/
#ifdef CONFIG_XMIT_ACK
	#define CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif

/*#define CONFIG_TCP_CSUM_OFFLOAD_RX*/

/*#define CONFIG_DRVEXT_MODULE*/


/*#define CONFIG_DISABLE_MCS13TO15	1*/	/* Disable MSC13-15 rates for more stable TX throughput with some 5G APs */

#define BUF_DESC_ARCH		/* if defined, hardware follows Rx buffer descriptor architecture */

#define CONFIG_DFS	1

/*#define CONFIG_IPS*/
#ifdef CONFIG_IPS
	/*#define CONFIG_IPS_LEVEL_2*/	 /* enable this to set default IPS mode to IPS_LEVEL_2 */
#endif
/*#define SUPPORT_HW_RFOFF_DETECTED*/

#define CONFIG_HIGH_CHAN_SUPER_CALIBRATION
#define CONFIG_LPS

#if defined(CONFIG_LPS)
	/*#define CONFIG_LPS_LCLK*/	 /* 32K */
#endif


#ifdef CONFIG_LPS_LCLK
	#define CONFIG_XMIT_THREAD_MODE
#endif

/*#define CONFIG_ANTENNA_DIVERSITY*/

/*#define CONFIG_CONCURRENT_MODE*/
#ifdef CONFIG_CONCURRENT_MODE
	/*#define CONFIG_HWPORT_SWAP*/			/* Port0->Sec, Port1->Pri */
	#define CONFIG_RUNTIME_PORT_SWITCH
	/*#define DBG_RUNTIME_PORT_SWITCH*/
	#define CONFIG_SCAN_BACKOP
	/*#define CONFIG_ATMEL_RC_PATCH*/
	/*#define CONFIG_TSF_RESET_OFFLOAD*/			/* For 2 PORT TSF SYNC. */
#else /* NAPI doesn't support CONCURRENT mode currently*/
	#define CONFIG_NAPI
	#define CONFIG_GRO
#endif

#define CONFIG_AP_MODE
#ifdef CONFIG_AP_MODE
	/*#define CONFIG_INTERRUPT_BASED_TXBCN*/ /* Tx Beacon when driver BCN_OK ,BCN_ERR interrupt occurs */
	#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_INTERRUPT_BASED_TXBCN)
		#undef CONFIG_INTERRUPT_BASED_TXBCN
	#endif
	#ifdef CONFIG_INTERRUPT_BASED_TXBCN
		/*#define CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT*/
		#define CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
	#endif

	#define CONFIG_NATIVEAP_MLME
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME
	#endif
	#define CONFIG_FIND_BEST_CHANNEL
	/*#define CONFIG_AUTO_AP_MODE*/
#endif

#define CONFIG_P2P
#ifdef CONFIG_P2P
	/* The CONFIG_WFD is for supporting the Wi-Fi display */
	#define CONFIG_WFD

	#define CONFIG_P2P_REMOVE_GROUP_INFO

	/*#define CONFIG_DBG_P2P*/

	#define CONFIG_P2P_PS
	/*#define CONFIG_P2P_IPS*/
	#define CONFIG_P2P_OP_CHK_SOCIAL_CH
	#define CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT  /* replace CONFIG_P2P_CHK_INVITE_CH_LIST flag */
	#define CONFIG_P2P_INVITE_IOT
#endif

/* Added by Kurt 20110511 */
#ifdef CONFIG_TDLS
	#define CONFIG_TDLS_DRIVER_SETUP
#if 0
	#ifndef CONFIG_WFD
		#define CONFIG_WFD
	#endif
	#define CONFIG_TDLS_AUTOSETUP
#endif
	#define CONFIG_TDLS_AUTOCHECKALIVE
	#define CONFIG_TDLS_CH_SW		/* Enable "CONFIG_TDLS_CH_SW" by default, however limit it to only work in wifi logo test mode but not in normal mode currently */
#endif

#define CONFIG_SKB_COPY	/* for amsdu */

/*#define CONFIG_LED*/
#ifdef CONFIG_LED
	/*#define CONFIG_SW_LED*/
	#ifdef CONFIG_SW_LED
		/*#define CONFIG_LED_HANDLED_BY_CMD_THREAD*/
	#endif
#endif /* CONFIG_LED */

#define CONFIG_GLOBAL_UI_PID

#define CONFIG_LAYER2_ROAMING
#define CONFIG_LAYER2_ROAMING_RESUME
/*#define CONFIG_ADAPTOR_INFO_CACHING_FILE*/ /* now just applied on 8192cu only, should make it general...*/
/*#define CONFIG_RESUME_IN_WORKQUEUE*/
/*#define CONFIG_SET_SCAN_DENY_TIMER*/
#define CONFIG_LONG_DELAY_ISSUE
#define CONFIG_NEW_SIGNAL_STAT_PROCESS
/*#define CONFIG_SIGNAL_DISPLAY_DBM*/ /* display RX signal with dbm */
#ifdef CONFIG_SIGNAL_DISPLAY_DBM
/*#define CONFIG_BACKGROUND_NOISE_MONITOR*/
#endif

#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable, */

#define CONFIG_TX_MCAST2UNI		/* Support IP multicast->unicast*/
/*#define CONFIG_CHECK_AC_LIFETIME 1*/	/* Check packet lifetime of 4 ACs. */

/*
 * Software feature Related Config
 */
#define RTW_HALMAC			/* Use HALMAC architecture, necessary for 8822B */
/* #define CONFIG_RF4CE_COEXIST */	/* ZigBee coexist support */

/*
 * Interface  Related Config
 */

/*
 * HAL  Related Config
 */

#define RTL8192E_RX_PACKET_INCLUDE_CRC	0
#define CONFIG_RX_PACKET_APPEND_FCS
#define SUPPORTED_BLOCK_IO
#define RTL8188E_FW_DOWNLOAD_ENABLE

/*#define CONFIG_ONLY_ONE_OUT_EP_TO_LOW	0*/

#define CONFIG_OUT_EP_WIFI_MODE	0

#define ENABLE_USB_DROP_INCORRECT_OUT


#define DISABLE_BB_RF	0

#ifdef CONFIG_GPIO_WAKEUP
	#ifndef WAKEUP_GPIO_IDX
		#define WAKEUP_GPIO_IDX 14
	#endif
#endif

/*#define RTL8191C_FPGA_NETWORKTYPE_ADHOC 0*/

#ifdef CONFIG_MP_INCLUDED
	#define MP_DRIVER 1
	#define CONFIG_MP_IWPRIV_SUPPORT	1
#else
	#define MP_DRIVER 0
#endif

/* Use cmd frame to issue beacon. Use a fixed buffer for beacon. */
#define CONFIG_BCN_ICF

/*
 * Platform  Related Config
 */


#ifdef CONFIG_BT_COEXIST
	/* for ODM and outsrc BT-Coex */
	#ifndef CONFIG_LPS
		#define CONFIG_LPS	/* download reserved page to FW */
	#endif
#endif /* !CONFIG_BT_COEXIST */


#ifdef CONFIG_USB_TX_AGGREGATION
/*#define CONFIG_TX_EARLY_MODE*/
#endif

#ifdef CONFIG_TX_EARLY_MODE
#define	RTL8192E_EARLY_MODE_PKT_NUM_10	0
#endif

/* Try to handle the Beacon error found in some types of TP-LINK APs */
#define CONFIG_ATTEMPT_TO_FIX_AP_BEACON_ERROR

#define CONFIG_80211D

#define CONFIG_BEAMFORMING
#ifdef CONFIG_BEAMFORMING
#define BEAMFORMING_SUPPORT 0	/* Use driver self mechanism, not phydm */
#define RTW_BEAMFORMING_VERSION_2
#endif /* CONFIG_BEAMFORMING */

/*
 * Debug Related Config
 */
#define DBG	1
/*#define CONFIG_DEBUG_RTL871X*/ /* RT_TRACE, RT_PRINT_DATA, _func_enter_, _func_exit_ */

#define CONFIG_PROC_DEBUG

#define CONFIG_DBG_COUNTER

#define DBG_CONFIG_ERROR_DETECT
/*
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
#define DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED "jeff-ap"
*/
#define DBG_RX_SIGNAL_DISPLAY_RAW_DATA
#if 0
#define DBG_NOISE_MONITOR

#define DBG_TX_POWER_IDX

#define DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
#define DBG_ROAMING_TEST

#define DBG_HAL_INIT_PROFILING

#define DBG_MEMORY_LEAK	1

/* TX use 1 urb */
#define CONFIG_SINGLE_XMIT_BUF
/* RX use 1 urb */
#define CONFIG_SINGLE_RECV_BUF
#endif
