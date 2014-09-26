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
//***** temporarily flag *******
#define CONFIG_SINGLE_IMG

//#define CONFIG_DISABLE_ODM
//for FPGA VERIFICATION config
#define RTL8188E_FPGA_TRUE_PHY_VERIFICATION 0

//***** temporarily flag *******
/*
 * Public  General Config
 */
#define AUTOCONF_INCLUDED
#define RTL871X_MODULE_NAME "88EU"
#define DRV_NAME "rtl8188eu"

#define CONFIG_USB_HCI	

#define PLATFORM_LINUX	

#define CONFIG_IOCTL_CFG80211 
//#define CONFIG_IEEE80211W

#if defined(CONFIG_PLATFORM_ACTIONS_ATM702X)
	#ifndef CONFIG_IOCTL_CFG80211 
		#define CONFIG_IOCTL_CFG80211 
	#endif
#endif

#ifdef CONFIG_IOCTL_CFG80211
	#define RTW_USE_CFG80211_STA_EVENT /* Indecate new sta asoc through cfg80211_new_sta */
	#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
	//#define CONFIG_DEBUG_CFG80211 
	//#define CONFIG_DRV_ISSUE_PROV_REQ // IOT FOR S2
	#define CONFIG_SET_SCAN_DENY_TIMER

#endif

/*
 * Internal  General Config
 */

//#define CONFIG_H2CLBK

#define CONFIG_EMBEDDED_FWIMG	
//#define CONFIG_FILE_FWIMG

#define CONFIG_XMIT_ACK
#ifdef CONFIG_XMIT_ACK
	#define CONFIG_ACTIVE_KEEP_ALIVE_CHECK
#endif
#define CONFIG_80211N_HT	

#define CONFIG_RECV_REORDERING_CTRL	

//#define CONFIG_TCP_CSUM_OFFLOAD_RX	

//#define CONFIG_DRVEXT_MODULE	

 #define CONFIG_SUPPORT_USB_INT
 #ifdef	CONFIG_SUPPORT_USB_INT
//#define CONFIG_USB_INTERRUPT_IN_PIPE	
#endif

//#ifndef CONFIG_MP_INCLUDED
	#define CONFIG_IPS	
	#ifdef CONFIG_IPS
	//#define CONFIG_IPS_LEVEL_2	 //enable this to set default IPS mode to IPS_LEVEL_2
	#endif
	#define SUPPORT_HW_RFOFF_DETECTED	

	#define CONFIG_LPS	
	#if defined(CONFIG_LPS) && defined(CONFIG_SUPPORT_USB_INT)


	//#define CONFIG_LPS_LCLK	
	#endif

	#ifdef CONFIG_LPS_LCLK
	#define CONFIG_XMIT_THREAD_MODE
	#endif

	//befor link
	#define CONFIG_ANTENNA_DIVERSITY

	//after link
	#ifdef CONFIG_ANTENNA_DIVERSITY	 
	#define CONFIG_HW_ANTENNA_DIVERSITY		
	#endif


	#define CONFIG_CONCURRENT_MODE 
	#ifdef CONFIG_CONCURRENT_MODE
		//#define CONFIG_HWPORT_SWAP				//Port0->Sec , Port1 -> Pri
		#define CONFIG_RUNTIME_PORT_SWITCH
		//#define DBG_RUNTIME_PORT_SWITCH
		#define CONFIG_STA_MODE_SCAN_UNDER_AP_MODE
		#define CONFIG_TSF_RESET_OFFLOAD 			// For 2 PORT TSF SYNC.
	#endif

	#define CONFIG_IOL
//#else 	//#ifndef CONFIG_MP_INCLUDED
	
//#endif 	//#ifndef CONFIG_MP_INCLUDED

#define CONFIG_AP_MODE	
#ifdef CONFIG_AP_MODE
	//#define CONFIG_INTERRUPT_BASED_TXBCN // Tx Beacon when driver BCN_OK ,BCN_ERR interrupt occurs
	#if defined(CONFIG_CONCURRENT_MODE) && defined(CONFIG_INTERRUPT_BASED_TXBCN)
		#undef CONFIG_INTERRUPT_BASED_TXBCN
	#endif
	#ifdef CONFIG_INTERRUPT_BASED_TXBCN
		//#define CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
		#define CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR		
	#endif
	
	#define CONFIG_NATIVEAP_MLME
	#ifndef CONFIG_NATIVEAP_MLME
		#define CONFIG_HOSTAPD_MLME	
	#endif			
	#define CONFIG_FIND_BEST_CHANNEL	
	//#define CONFIG_NO_WIRELESS_HANDLERS	
#endif

#define CONFIG_P2P	
#ifdef CONFIG_P2P
	//The CONFIG_WFD is for supporting the Wi-Fi display
	#define CONFIG_WFD
	
	#ifndef CONFIG_WIFI_TEST
		#define CONFIG_P2P_REMOVE_GROUP_INFO
	#endif
	//#define CONFIG_DBG_P2P

	#define CONFIG_P2P_PS
	#define CONFIG_P2P_IPS
	#define CONFIG_P2P_OP_CHK_SOCIAL_CH
	#define CONFIG_CFG80211_ONECHANNEL_UNDER_CONCURRENT  //replace CONFIG_P2P_CHK_INVITE_CH_LIST flag
	#define CONFIG_P2P_INVITE_IOT
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


#define CONFIG_SKB_COPY	//for amsdu

//#define CONFIG_LED
#ifdef CONFIG_LED
	#define CONFIG_SW_LED
	#ifdef CONFIG_SW_LED
		//#define CONFIG_LED_HANDLED_BY_CMD_THREAD
	#endif
#endif // CONFIG_LED

#ifdef CONFIG_IOL
	#define CONFIG_IOL_NEW_GENERATION
	#define CONFIG_IOL_READ_EFUSE_MAP
	//#define DBG_IOL_READ_EFUSE_MAP
	//#define CONFIG_IOL_LLT
	#define CONFIG_IOL_EFUSE_PATCH		
	//#define CONFIG_IOL_IOREG_CFG
	//#define CONFIG_IOL_IOREG_CFG_DBG	
#endif


#define USB_INTERFERENCE_ISSUE // this should be checked in all usb interface
#define CONFIG_GLOBAL_UI_PID

#define CONFIG_LAYER2_ROAMING
#define CONFIG_LAYER2_ROAMING_RESUME
//#define CONFIG_ADAPTOR_INFO_CACHING_FILE // now just applied on 8192cu only, should make it general...
#define CONFIG_RESUME_IN_WORKQUEUE
//#define CONFIG_SET_SCAN_DENY_TIMER
#define CONFIG_LONG_DELAY_ISSUE
#define CONFIG_NEW_SIGNAL_STAT_PROCESS
//#define CONFIG_SIGNAL_DISPLAY_DBM //display RX signal with dbm
#define RTW_NOTCH_FILTER 0 /* 0:Disable, 1:Enable, */
#define CONFIG_DEAUTH_BEFORE_CONNECT

#define CONFIG_BR_EXT		// Enable NAT2.5 support for STA mode interface with a L2 Bridge
#ifdef CONFIG_BR_EXT
#define CONFIG_BR_EXT_BRNAME	"br0"
#endif	// CONFIG_BR_EXT

#define CONFIG_TX_MCAST2UNI		// Support IP multicast->unicast
//#define CONFIG_CHECK_AC_LIFETIME 	// Check packet lifetime of 4 ACs.

/* 
 * Interface  Related Config 
 */

#ifndef CONFIG_MINIMAL_MEMORY_USAGE
	#define CONFIG_USB_TX_AGGREGATION	
	#define CONFIG_USB_RX_AGGREGATION	
#endif

#define CONFIG_PREALLOC_RECV_SKB	
//#define CONFIG_REDUCE_USB_TX_INT		// Trade-off: Improve performance, but may cause TX URBs blocked by USB Host/Bus driver on few platforms.
//#define CONFIG_EASY_REPLACEMENT	

/* 
 * CONFIG_USE_USB_BUFFER_ALLOC_XX uses Linux USB Buffer alloc API and is for Linux platform only now!
 */
//#define CONFIG_USE_USB_BUFFER_ALLOC_TX 	// Trade-off: For TX path, improve stability on some platforms, but may cause performance degrade on other platforms.
//#define CONFIG_USE_USB_BUFFER_ALLOC_RX 	// For RX path
#ifdef CONFIG_USE_USB_BUFFER_ALLOC_RX
#undef CONFIG_PREALLOC_RECV_SKB
#endif

/* 
 * USB VENDOR REQ BUFFER ALLOCATION METHOD
 * if not set we'll use function local variable (stack memory)
 */
//#define CONFIG_USB_VENDOR_REQ_BUFFER_DYNAMIC_ALLOCATE
#define CONFIG_USB_VENDOR_REQ_BUFFER_PREALLOC

#define CONFIG_USB_VENDOR_REQ_MUTEX
#define CONFIG_VENDOR_REQ_RETRY

//#define CONFIG_USB_SUPPORT_ASYNC_VDN_REQ 


/*
 * HAL  Related Config
 */

#define RTL8188E_RX_PACKET_INCLUDE_CRC	0

#define SUPPORTED_BLOCK_IO


//#define CONFIG_ONLY_ONE_OUT_EP_TO_LOW	0

#define CONFIG_OUT_EP_WIFI_MODE	0

#define ENABLE_USB_DROP_INCORRECT_OUT


//#define RTL8192CU_ADHOC_WORKAROUND_SETTING	

#define DISABLE_BB_RF	0

//#define RTL8191C_FPGA_NETWORKTYPE_ADHOC 0

#ifdef CONFIG_MP_INCLUDED
	#define MP_DRIVER 1
	#define CONFIG_MP_IWPRIV_SUPPORT	
	//#undef CONFIG_USB_TX_AGGREGATION
	//#undef CONFIG_USB_RX_AGGREGATION
#else
	#define MP_DRIVER 0
#endif


/*
 * Platform  Related Config
 */
#ifdef CONFIG_PLATFORM_MN10300
	#define CONFIG_SPECIAL_SETTING_FOR_FUNAI_TV
	#define CONFIG_USE_USB_BUFFER_ALLOC_RX 
	
	#if	defined (CONFIG_SW_ANTENNA_DIVERSITY)
		#undef CONFIG_SW_ANTENNA_DIVERSITY
		#define CONFIG_HW_ANTENNA_DIVERSITY
	#endif

	#if	defined (CONFIG_POWER_SAVING)
		#undef CONFIG_POWER_SAVING
	#endif
	
#endif//CONFIG_PLATFORM_MN10300



#ifdef CONFIG_PLATFORM_TI_DM365
#define CONFIG_USE_USB_BUFFER_ALLOC_RX 
#endif


#if defined(CONFIG_PLATFORM_ACTIONS_ATM702X)
	#ifdef CONFIG_USB_TX_AGGREGATION	
		#undef CONFIG_USB_TX_AGGREGATION
	#endif
	#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
		#define CONFIG_USE_USB_BUFFER_ALLOC_TX
	#endif
	#ifndef CONFIG_USE_USB_BUFFER_ALLOC_RX
		#define CONFIG_USE_USB_BUFFER_ALLOC_RX
	#endif
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
#define 	RTL8723AS_SUPPORT				0
#define 	RTL8723AE_SUPPORT				0
#define 	RTL8723A_SUPPORT				(RTL8723AU_SUPPORT|RTL8723AS_SUPPORT|RTL8723AE_SUPPORT)

#define 	RTL8723_FPGA_VERIFICATION		0

#define RTL8188EE_SUPPORT				0
#define RTL8188EU_SUPPORT				1
#define RTL8188ES_SUPPORT				0
#define RTL8188E_SUPPORT				(RTL8188EE_SUPPORT|RTL8188EU_SUPPORT|RTL8188ES_SUPPORT)
#define TESTCHIP_SUPPORT				0

#define RTL8812E_SUPPORT				0
#define RTL8812AU_SUPPORT				0
#define RTL8812A_SUPPORT				(RTL8812E_SUPPORT|RTL8812AU_SUPPORT)

#define RTL8821A_SUPPORT				0

#define RTL8723B_SUPPORT				0

#define RTL8192E_SUPPORT				0

#define RTL8813A_SUPPORT				0

//#if (RTL8188E_SUPPORT==1)
#define RATE_ADAPTIVE_SUPPORT 			1
#define POWER_TRAINING_ACTIVE			1

//#endif

#ifdef CONFIG_USB_TX_AGGREGATION
//#define 	CONFIG_TX_EARLY_MODE
#endif

#ifdef CONFIG_TX_EARLY_MODE
#define	RTL8188E_EARLY_MODE_PKT_NUM_10	0
#endif

#define CONFIG_80211D

#define CONFIG_ATTEMPT_TO_FIX_AP_BEACON_ERROR

/*
 * Debug Related Config
 */
#define DBG	1

//#define CONFIG_DEBUG /* DBG_871X, etc... */
//#define CONFIG_DEBUG_RTL871X /* RT_TRACE, RT_PRINT_DATA, _func_enter_, _func_exit_ */

#define CONFIG_PROC_DEBUG

#define DBG_CONFIG_ERROR_DETECT
//#define DBG_CONFIG_ERROR_DETECT_INT
#define DBG_CONFIG_ERROR_RESET

//#define DBG_IO
//#define DBG_DELAY_OS
//#define DBG_MEM_ALLOC
//#define DBG_IOCTL

//#define DBG_TX
//#define DBG_XMIT_BUF
//#define DBG_XMIT_BUF_EXT
//#define DBG_TX_DROP_FRAME

//#define DBG_RX_DROP_FRAME
//#define DBG_RX_SEQ
//#define DBG_RX_SIGNAL_DISPLAY_PROCESSING
//#define DBG_RX_SIGNAL_DISPLAY_SSID_MONITORED "jeff-ap"



//#define DBG_SHOW_MCUFWDL_BEFORE_51_ENABLE
//#define DBG_ROAMING_TEST

//#define DBG_HAL_INIT_PROFILING

//#define DBG_MEMORY_LEAK	

//TX use 1 urb
//#define CONFIG_SINGLE_XMIT_BUF
//RX use 1 urb
//#define CONFIG_SINGLE_RECV_BUF

