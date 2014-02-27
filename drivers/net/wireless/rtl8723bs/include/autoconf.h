/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
 * Automatically generated C config: don't edit
 */

#define AUTOCONF_INCLUDED

#define RTL871X_MODULE_NAME "8723BS"
#define DRV_NAME "rtl8723bs"

#ifndef CONFIG_RTL8723B
#define CONFIG_RTL8723B
#endif
#define CONFIG_SDIO_HCI

//#define CONFIG_GSPI_HCI config from Makefile
//#define CONFIG_SDIO_HCI config from Makefile
#define PLATFORM_LINUX

#define CONFIG_EMBEDDED_FWIMG
//#define CONFIG_FILE_FWIMG

#define CONFIG_C2H_PACKET_EN


/*
 * Functions Config
 */
#define CONFIG_XMIT_ACK
#ifdef CONFIG_XMIT_ACK
	#define CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif
#define CONFIG_80211N_HT
#define CONFIG_RECV_REORDERING_CTRL

#define CONFIG_IOCTL_CFG80211

#if defined(CONFIG_PLATFORM_SPRD) && !defined(ANDROID_2X)
	#ifndef CONFIG_IOCTL_CFG80211
		#define CONFIG_IOCTL_CFG80211 1
	#endif
#endif

#ifdef CONFIG_IOCTL_CFG80211
	/*
	 * Indecate new sta asoc through cfg80211_new_sta
	 * If kernel version >= 3.2 or
	 * version < 3.2 but already apply cfg80211 patch,
	 * RTW_USE_CFG80211_STA_EVENT must be defiend!
	 */
	//#define RTW_USE_CFG80211_STA_EVENT
	#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
	//#define CONFIG_DEBUG_CFG80211
	#define CONFIG_SET_SCAN_DENY_TIMER
#endif

#define CONFIG_AP_MODE
#ifdef CONFIG_AP_MODE
	#define CONFIG_NATIVEAP_MLME
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME
	#endif
	//#define CONFIG_FIND_BEST_CHANNEL
	//#define CONFIG_NO_WIRELESS_HANDLERS
#endif

#define CONFIG_P2P
#ifdef CONFIG_P2P
	//Added by Albert 20110812
	//The CONFIG_WFD is for supporting the Wi-Fi display
	#define CONFIG_WFD

	#ifndef CONFIG_WIFI_TEST
		#define CONFIG_P2P_REMOVE_GROUP_INFO
	#endif
	//#define CONFIG_DBG_P2P
	#define CONFIG_P2P_PS
	#define CONFIG_P2P_IPS
	#define CONFIG_P2P_OP_CHK_SOCIAL_CH
	#define CONFIG_P2P_CHK_INVITE_CH_LIST
#endif

//	Added by Kurt 20110511
//#define CONFIG_TDLS
#ifdef CONFIG_TDLS
//	#ifndef CONFIG_WFD
//		#define CONFIG_WFD	
//	#endif
//	#define CONFIG_TDLS_AUTOSETUP			
//	#define CONFIG_TDLS_AUTOCHECKALIVE		
#endif

#define CONFIG_LAYER2_ROAMING
#define CONFIG_LAYER2_ROAMING_RESUME

#define CONFIG_SCAN_SPARSE 	//partial scan, ASUS RK3188 use the feature
#ifdef CONFIG_SCAN_SPARSE 
	#define ALLOW_SCAN_INTERVAL	12000 // unit is ms
	#define SCAN_DIVISION_NUM 4
#endif 	

//#define CONFIG_80211D


/*
 * Hardware Related Config
 */
//#define CONFIG_BT_COEXIST	// set from Makefile
//#define CONFIG_ANTENNA_DIVERSITY
//#define SUPPORT_HW_RFOFF_DETECTED

//#define CONFIG_SW_LED


/*
 * Interface Related Config
 */
#define CONFIG_SDIO_RX_COPY
#define CONFIG_XMIT_THREAD_MODE

/*
 * Others
 */
//#define CONFIG_MAC_LOOPBACK_DRIVER
#define CONFIG_SKB_COPY	//for amsdu
#define CONFIG_LONG_DELAY_ISSUE
#define CONFIG_NEW_SIGNAL_STAT_PROCESS
#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable, */
#define CONFIG_DEAUTH_BEFORE_CONNECT
//#define CONFIG_PATCH_JOIN_WRONG_CHANNEL


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


#define CONFIG_CONCURRENT_MODE
#ifdef CONFIG_CONCURRENT_MODE
	#define CONFIG_TSF_RESET_OFFLOAD 1			// For 2 PORT TSF SYNC.
	//#define CONFIG_HWPORT_SWAP				//Port0->Sec , Port1 -> Pri
	#define CONFIG_RUNTIME_PORT_SWITCH
	//#define DBG_RUNTIME_PORT_SWITCH
	#define CONFIG_STA_MODE_SCAN_UNDER_AP_MODE
#endif	// CONFIG_CONCURRENT_MODE


#ifdef CONFIG_MP_INCLUDED
	#define MP_DRIVER		1
	#define CONFIG_MP_IWPRIV_SUPPORT
	// disable unnecessary functions for MP
	//#undef CONFIG_POWER_SAVING
	//#undef CONFIG_BT_COEXIST
	//#undef CONFIG_ANTENNA_DIVERSITY
	//#undef SUPPORT_HW_RFOFF_DETECTED
#else // !CONFIG_MP_INCLUDED
	#define MP_DRIVER		0
	#undef CONFIG_MP_IWPRIV_SUPPORT
#endif // !CONFIG_MP_INCLUDED


#ifdef CONFIG_POWER_SAVING
	#define CONFIG_IPS
	#define CONFIG_LPS

	#if defined(CONFIG_LPS) && (defined(CONFIG_GSPI_HCI) || defined(CONFIG_SDIO_HCI))
	#define CONFIG_LPS_LCLK
	#endif

	#ifdef CONFIG_LPS
		#define CONFIG_CHECK_LEAVE_LPS
	#endif

	#ifdef CONFIG_LPS_LCLK
	#define CONFIG_DETECT_CPWM_BY_POLLING
	#define CONFIG_LPS_RPWM_TIMER
	#if defined(CONFIG_LPS_RPWM_TIMER) || defined(CONFIG_DETECT_CPWM_BY_POLLING)
	#define LPS_RPWM_WAIT_MS 300
	#endif
	#define CONFIG_LPS_LCLK_WD_TIMER // Watch Dog timer in LPS LCLK
	#endif

	#ifdef CONFIG_IPS
	#define CONFIG_IPS_CHECK_IN_WD // Do IPS Check in WatchDog.
	//#define CONFIG_SWLPS_IN_IPS // Do SW LPS flow when entering and leaving IPS
	#define CONFIG_FWLPS_IN_IPS // issue H2C command to let FW do LPS when entering IPS
	#endif
	
#endif // #ifdef CONFIG_POWER_SAVING

#ifdef CONFIG_BT_COEXIST
	// for ODM and outsrc BT-Coex
	#define BT_30_SUPPORT 1

	#ifndef CONFIG_LPS
		#define CONFIG_LPS	// download reserved page to FW
	#endif

	#ifndef CONFIG_C2H_PACKET_EN
		#define CONFIG_C2H_PACKET_EN
	#endif
#else // !CONFIG_BT_COEXIST
	#define BT_30_SUPPORT 0
#endif // !CONFIG_BT_COEXIST


#ifdef CONFIG_ANTENNA_DIVERSITY
#define CONFIG_SW_ANTENNA_DIVERSITY
//#define CONFIG_HW_ANTENNA_DIVERSITY
#endif


#define CONFIG_RF_GAIN_OFFSET


#ifndef DISABLE_BB_RF
#define DISABLE_BB_RF	0
#endif

#if DISABLE_BB_RF
	#define HAL_MAC_ENABLE	0
	#define HAL_BB_ENABLE		0
	#define HAL_RF_ENABLE		0
#else
	#define HAL_MAC_ENABLE	1
	#define HAL_BB_ENABLE		1
	#define HAL_RF_ENABLE		1
#endif

/*
 * Outsource  Related Config
 */

#define 	RTL8192CE_SUPPORT 				0
#define 	RTL8192CU_SUPPORT 			0
#define 	RTL8192C_SUPPORT 				(RTL8192CE_SUPPORT|RTL8192CU_SUPPORT)

#define 	RTL8192DE_SUPPORT 				0
#define 	RTL8192DU_SUPPORT 			0
#define 	RTL8192D_SUPPORT 				(RTL8192DE_SUPPORT|RTL8192DU_SUPPORT)


#define 	RTL8723AS_SUPPORT				0
#define 	RTL8723AU_SUPPORT				0
#define 	RTL8723AE_SUPPORT				0
#define 	RTL8723A_SUPPORT				(RTL8723AU_SUPPORT|RTL8723AS_SUPPORT|RTL8723AE_SUPPORT)


#define 	RTL8723_FPGA_VERIFICATION		0

#define RTL8188EE_SUPPORT				0
#define RTL8188EU_SUPPORT				0
#define RTL8188ES_SUPPORT				0
#define RTL8188E_SUPPORT				(RTL8188EE_SUPPORT|RTL8188EU_SUPPORT|RTL8188ES_SUPPORT)

#define RTL8812E_SUPPORT				0
#define RTL8812AU_SUPPORT				0
#define RTL8812A_SUPPORT				(RTL8812E_SUPPORT|RTL8812AU_SUPPORT)

#define RTL8821A_SUPPORT				0

#define RTL8723B_SUPPORT				1

#define RTL8192E_SUPPORT				0

#define RTL8813A_SUPPORT				0

#define TESTCHIP_SUPPORT			0

//#if (RTL8188E_SUPPORT==1)
#define RATE_ADAPTIVE_SUPPORT 			0
#define POWER_TRAINING_ACTIVE			0
//#endif

//#define CONFIG_HW_ANTENNA_DIVERSITY

/*
 * Platform dependent
 */
#ifdef CONFIG_PLATFORM_SPRD
 
#undef CONFIG_SDIO_RX_COPY

#ifdef ANDROID_2X

#define CONFIG_SDIO_RX_COPY

#else // !ANDROID_2X
	#undef CONFIG_WOWLAN
	#undef CONFIG_WOWLAN_8723
//	#define CONFIG_SDIO_RX_COPY
//	#define CONFIG_LINKED_LCOK
	#define CONFIG_AUTH_DIRECT_WITHOUT_BCN
	//#define CONFIG_DISCONNECT_H2CWAY
//	#define CONFIG_DONT_CARE_TP
	#define CONFIG_LOW_PWR_LPS
	//#define CONFIG_CMCC_TEST

	//1) LPS unit is only 102 ms, it's not
	//a good idear to retry it use timer,
	//2) we must wait ACK, or lots of IO
	//is not allowed under 32k, because
	//this will cause hw hang
	#undef CONFIG_LPS_RPWM_TIMER
	#define CONFIG_WAIT_PS_ACK
	#define CONFIG_SOFTAP_11N
	#define CONFIG_CHECK_BT_HANG

//	#define CONFIG_8723BS_TEST
#endif // !ANDROID_2X

#endif // CONFIG_PLATFORM_SPRD

#define CONFIG_ATTEMPT_TO_FIX_AP_BEACON_ERROR

#define WAKEUP_GPIO_IDX	12	//WIFI Chip Side
#ifdef CONFIG_WOWLAN
#define CONFIG_GTK_OL
#endif //CONFIG_WOWLAN

/*
 * Debug Related Config
 */
//#define CONFIG_DEBUG

#ifdef CONFIG_DEBUG
#define DBG	1	// for ODM & BTCOEX debug
//#define CONFIG_DEBUG_RTL871X /* RT_TRACE, RT_PRINT_DATA, _func_enter_, _func_exit_ */
#else // !CONFIG_DEBUG
#define DBG	0	// for ODM & BTCOEX debug
#endif // !CONFIG_DEBUG
//
#define CONFIG_PROC_DEBUG
//
#define DBG_CONFIG_ERROR_DETECT
//#define DBG_XMIT_BUF
//#define DBG_XMIT_BUF_EXT
//#define DBG_CHECK_FW_PS_STATE
//#define DBG_CHECK_FW_PS_STATE_H2C
//
