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

#define CONFIG_PHY_SETTING_WITH_ODM
#define CONFIG_CHIP_VER_INTEGRATION

/*
 * Automatically generated C config: don't edit
 */

#define AUTOCONF_INCLUDED

#define RTL871X_MODULE_NAME "8723AS"
#define DRV_NAME "rtl8723as"

#define CONFIG_RTL8723A 1
#define CONFIG_SDIO_HCI 1
#define PLATFORM_LINUX 1

#define CONFIG_EMBEDDED_FWIMG 1

#define CONFIG_DEBUG 1


/*
 * Functions Config
 */
#define CONFIG_80211N_HT 1
#define CONFIG_RECV_REORDERING_CTRL 1

#define CONFIG_IOCTL_CFG80211 1
#ifdef CONFIG_IOCTL_CFG80211
	#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
	//#define CONFIG_DEBUG_CFG80211 1
#endif

#define CONFIG_AP_MODE	1
#ifdef CONFIG_AP_MODE
	#define CONFIG_NATIVEAP_MLME 1
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME	1
	#endif			
	//#define CONFIG_FIND_BEST_CHANNEL	1
	//#define CONFIG_NO_WIRELESS_HANDLERS	1
#endif

#define CONFIG_P2P	1
#ifdef CONFIG_P2P
	//Added by Albert 20110812
	//The CONFIG_WFD is for supporting the Wi-Fi display
	//#define CONFIG_WFD	1

	//Unmarked if there is low p2p scanned ratio; Kurt
	//#define CONFIG_P2P_AGAINST_NOISE	1
	
	#define CONFIG_P2P_REMOVE_GROUP_INFO
	//#define CONFIG_DBG_P2P
#endif

// Added by Kurt 20110511
//#define CONFIG_TDLS	1

#define CONFIG_LAYER2_ROAMING
#define CONFIG_LAYER2_ROAMING_RESUME

//#define CONFIG_80211D 1
#define CONFIG_LPS_RPWM_TIMER

/*
 * Hardware Related Config
 */
//#define CONFIG_BT_COEXIST	1 // set from Makefile
//#define CONFIG_ANTENNA_DIVERSITY	1
//#define SUPPORT_HW_RFOFF_DETECTED	1

//#define CONFIG_SW_LED 1


/*
 * Interface Related Config
 */


/*
 * Others
 */
//#define CONFIG_MAC_LOOPBACK_DRIVER 1
#define CONFIG_SKB_COPY	1//for amsdu
#define CONFIG_LONG_DELAY_ISSUE 1
//#define CONFIG_PATCH_JOIN_WRONG_CHANNEL 1


/*
 * Auto Config Section
 */
#if defined(CONFIG_RTL8723A) && defined(CONFIG_SDIO_HCI)
#define CONFIG_RTL8723A_SDIO 1
#define CONFIG_XMIT_THREAD_MODE 1
#endif


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
	#define MP_DRIVER		1
	#define CONFIG_MP_IWPRIV_SUPPORT 1
	// disable unnecessary functions for MP
	#undef CONFIG_POWER_SAVING
	#undef CONFIG_BT_COEXIST
	#undef CONFIG_ANTENNA_DIVERSITY
	#undef SUPPORT_HW_RFOFF_DETECTED
#else // !CONFIG_MP_INCLUDED
	#define MP_DRIVER		0
	#undef CONFIG_MP_IWPRIV_SUPPORT
#endif // !CONFIG_MP_INCLUDED


#ifdef CONFIG_POWER_SAVING
	#define CONFIG_IPS		1
	#define CONFIG_LPS		1

	#if defined(CONFIG_LPS) && defined(CONFIG_SDIO_HCI)
	#define CONFIG_LPS_LCLK	1
	#endif
#endif // #ifdef CONFIG_POWER_SAVING


#ifdef CONFIG_BT_COEXIST
	#ifndef CONFIG_LPS
		#define CONFIG_LPS 1	// download reserved page to FW
	#endif
#endif


#ifdef CONFIG_ANTENNA_DIVERSITY
#define CONFIG_SW_ANTENNA_DIVERSITY
//#define CONFIG_HW_ANTENNA_DIVERSITY
#endif

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


#define DBG	0
#ifdef CONFIG_DEBUG
//#define CONFIG_DEBUG_RTL871X
#define CONFIG_DEBUG_RTL819X
//#define CONFIG_PROC_DEBUG
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

#define 	RTL8723AU_SUPPORT				0
#define 	RTL8723AS_SUPPORT				1
#define 	RTL8723AE_SUPPORT				0
#define 	RTL8723A_SUPPORT				(RTL8723AU_SUPPORT|RTL8723AS_SUPPORT|RTL8723AE_SUPPORT)

#define 	RTL8723_FPGA_VERIFICATION		0

#define RTL8188EE_SUPPORT				0
#define RTL8188EU_SUPPORT				0
#define RTL8188ES_SUPPORT				0
#define RTL8188E_SUPPORT				(RTL8188EE_SUPPORT|RTL8188EU_SUPPORT|RTL8188ES_SUPPORT)
#define RTL8188E_FOR_TEST_CHIP			0
//#if (RTL8188E_SUPPORT==1)
#define RATE_ADAPTIVE_SUPPORT 			0
#define POWER_TRAINING_ACTIVE			0
//#endif

