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
 *
 ******************************************************************************/

/*
 * Public  General Config
 */
#define AUTOCONF_INCLUDED
#define DRV_NAME "r8192du"
#define DRIVERVERSION	"v4.2.1_7122.20130408"

#define CONFIG_SET_SCAN_DENY_TIMER

/*
 * Internal  General Config
 */
/* define CONFIG_PWRCTRL	1 */
/* define CONFIG_H2CLBK	1 */

#define CONFIG_EMBEDDED_FWIMG	1
/* define CONFIG_FILE_FWIMG */

#ifdef CONFIG_WAKE_ON_WLAN
#define CONFIG_WOWLAN 1
#endif /* CONFIG_WAKE_ON_WLAN */
#define CONFIG_R871X_TEST	1

#define CONFIG_ACTIVE_KEEP_ALIVE_CHECK

#define CONFIG_80211N_HT	1

#define CONFIG_RECV_REORDERING_CTRL	1

#define CONFIG_IPS	1
//#define CONFIG_LPS	1

#define CONFIG_92D_AP_MODE 1
#define CONFIG_NATIVEAP_MLME 1
#ifndef CONFIG_NATIVEAP_MLME
	#define CONFIG_HOSTAPD_MLME	1
#endif
#define CONFIG_FIND_BEST_CHANNEL	1

#define CONFIG_DFS	1

#define CONFIG_LAYER2_ROAMING
#define CONFIG_LAYER2_ROAMING_RESUME
/* define CONFIG_SET_SCAN_DENY_TIMER */
#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable,*/

//#define CONFIG_CONCURRENT_MODE 1
#ifdef CONFIG_CONCURRENT_MODE
	#define CONFIG_TSF_RESET_OFFLOAD 1			/*  For 2 PORT TSF SYNC. */
#endif	/*  CONFIG_CONCURRENT_MODE */

#define CONFIG_80211D

/* Interface  Related Config */

#define CONFIG_PREALLOC_RECV_SKB	1

/*
 * USB VENDOR REQ BUFFER ALLOCATION METHOD
 * if not set we'll use function local variable (stack memory)
 */

/* HAL  Related Config */

#define RTL8192C_RX_PACKET_NO_INCLUDE_CRC	1

#define CONFIG_ONLY_ONE_OUT_EP_TO_LOW	0

#define CONFIG_OUT_EP_WIFI_MODE	0

#define RTL8192CU_ASIC_VERIFICATION	0	/*  For ASIC verification. */

#define RTL8192CU_ADHOC_WORKAROUND_SETTING 1

#define DISABLE_BB_RF	0

#define RTL8191C_FPGA_NETWORKTYPE_ADHOC 0

#define ANTENNA_SELECTION_STATIC_SETTING 0

#define TX_POWER_FOR_5G_BAND				1	/* For 5G band TX Power */

#define RTL8192D_EASY_SMART_CONCURRENT	0

#define RTL8192D_DUAL_MAC_MODE_SWITCH	0

#define SWLCK   1

#define FW_PROCESS_VENDOR_CMD 1

#define MP_DRIVER 0

#define DBG 0

#define CONFIG_DEBUG_RTL819X

#define CONFIG_PROC_DEBUG 1
