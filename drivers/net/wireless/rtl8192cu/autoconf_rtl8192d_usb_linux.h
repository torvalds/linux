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

/*
 * Public  General Config
 */
#define AUTOCONF_INCLUDED
#define RTL871X_MODULE_NAME "92DU"
#define DRV_NAME "rtl8192du"

#define CONFIG_USB_HCI	1

#define CONFIG_RTL8192D	1

#define PLATFORM_LINUX	1


/*
 * Internal  General Config
 */
//#define CONFIG_PWRCTRL	1
//#define CONFIG_H2CLBK	1

#define CONFIG_EMBEDDED_FWIMG	1
//#define CONFIG_FILE_FWIMG

#define CONFIG_R871X_TEST	1

#define CONFIG_80211N_HT	1

#define CONFIG_RECV_REORDERING_CTRL	1

//#define CONFIG_TCP_CSUM_OFFLOAD_RX	1

//#define CONFIG_DRVEXT_MODULE	1


#ifndef CONFIG_MP_INCLUDED
//#define CONFIG_IPS	1
#define CONFIG_LPS	1
//#define CONFIG_BT_COEXIST  	1
//#define SUPPORT_HW_RFOFF_DETECTED	1
#else
#define CONFIG_MP_IWPRIV_SUPPORT 1
#endif

#define CONFIG_AP_MODE 1
#define CONFIG_NATIVEAP_MLME 1

//	Added by Albert 20110314
#define CONFIG_P2P	1

#ifdef CONFIG_P2P
//	Added by Albert 20110812
//	The CONFIG_WFD is for supporting the Wi-Fi display
//#define CONFIG_WFD	1

//	Unmarked if there is low p2p scanned ratio; Kurt
//#define CONFIG_P2P_AGAINST_NOISE	1
#endif

//	Added by Kurt 20110511
//#define CONFIG_TDLS 1

#ifdef CONFIG_AP_MODE
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME	1
	#endif			
	#define CONFIG_FIND_BEST_CHANNEL	1
#endif

#define CONFIG_SKB_COPY	1//for amsdu

#define CONFIG_DFS	1

#define CONFIG_LED
#ifdef CONFIG_LED
#define CONFIG_SW_LED
#endif //CONFIG_LED


//#define CONFIG_LAYER2_ROAMING
//#define CONFIG_LAYER2_ROAMING_RESUME
//#define CONFIG_SET_SCAN_DENY_TIMER
#define CONFIG_NEW_SIGNAL_STAT_PROCESS
//#define CONFIG_SIGNAL_DISPLAY_DBM //display RX signal with dbm

#define CONFIG_BR_EXT	1	// Enable NAT2.5 support for STA mode interface with a L2 Bridge
#ifdef CONFIG_BR_EXT
#define CONFIG_BR_EXT_BRNAME	"br0"
#endif	// CONFIG_BR_EXT

#define CONFIG_TX_MCAST2UNI	1	// Support IP multicast->unicast

//#define CONFIG_CHECK_AC_LIFETIME	1	// Check packet lifetime of 4 ACs.
//#define CONFIG_DISABLE_MCS13TO15	1	// Disable MSC13-15 rates for more stable throughput with some 5G APs

/*
 * Interface  Related Config
 */
//#define CONFIG_USB_INTERRUPT_IN_PIPE	1

#ifndef CONFIG_MINIMAL_MEMORY_USAGE
	#define CONFIG_USB_TX_AGGREGATION	1
	#define CONFIG_USB_RX_AGGREGATION	1
#endif

#define CONFIG_PREALLOC_RECV_SKB	1
//#define CONFIG_REDUCE_USB_TX_INT	1	// Trade-off: Improve performance, but may cause TX URBs blocked by USB Host/Bus driver on few platforms.
//#define CONFIG_EASY_REPLACEMENT	1



/* 
 * CONFIG_USE_USB_BUFFER_ALLOC_XX uses Linux USB Buffer alloc API and is for Linux platform only now!
 */
#define CONFIG_USE_USB_BUFFER_ALLOC_TX 1	// Trade-off: For TX path, improve stability on some platforms, but may cause performance degrade on other platforms.
//#define CONFIG_USE_USB_BUFFER_ALLOC_RX 1	// For RX path

/* 
 * USB VENDOR REQ BUFFER ALLOCATION METHOD
 * if not set we'll use function local variable (stack memory)
 */
//#define CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE
#define CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC

#define CONFIG_USB_VENDOR_REQ_MUTEX


//#define CONFIG_USB_SUPPORT_ASYNC_VDN_REQ 1


/*
 * HAL  Related Config
 */

#define RTL8192C_RX_PACKET_NO_INCLUDE_CRC	1

#define CONFIG_ONLY_ONE_OUT_EP_TO_LOW	0

#define CONFIG_OUT_EP_WIFI_MODE	0

#define ENABLE_USB_DROP_INCORRECT_OUT	0

#define RTL8192CU_ASIC_VERIFICATION	0	// For ASIC verification.

#define RTL8192CU_ADHOC_WORKAROUND_SETTING 1

#define DISABLE_BB_RF	0	

#define RTL8191C_FPGA_NETWORKTYPE_ADHOC 0

#define ANTENNA_SELECTION_STATIC_SETTING 0

#define TX_POWER_FOR_5G_BAND				1	//For 5G band TX Power

#define RTL8192D_EASY_SMART_CONCURRENT	0

#define RTL8192D_DUAL_MAC_MODE_SWITCH	0

#define SWLCK   1

#define FW_PROCESS_VENDOR_CMD 1

#ifdef CONFIG_MP_INCLUDED
	#define MP_DRIVER 1
	#undef CONFIG_USB_TX_AGGREGATION
	#undef CONFIG_USB_RX_AGGREGATION
#else
	#define MP_DRIVER 0
#endif

/*
 * Platform  Related Config
 */
#ifndef CONFIG_WISTRON_PLATFORM
//#define CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE	1
#endif

/*
 * Debug  Related Config
 */
//#define CONFIG_DEBUG_RTL871X

#define DBG 0

#define CONFIG_DEBUG_RTL819X

#define CONFIG_PROC_DEBUG 1

#define DBG_MEMORY_LEAK	1
