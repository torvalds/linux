/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define RTL871X_MODULE_NAME "RTL8712U-BGN"

//#define CONFIG_DEBUG_RTL871X 1

#define CONFIG_USB_HCI	1
#undef  CONFIG_SDIO_HCI

#undef CONFIG_RTL8711
#define  CONFIG_RTL8712 1
#undef  CONFIG_RTL8716


#define CONFIG_IOCTL_CFG80211	1

#ifdef CONFIG_IOCTL_CFG80211
#define CONFIG_CFG80211_FORCE_COMPATIBLE_2_6_37_UNDER
#endif //CONFIG_IOCTL_CFG80211

//#define CONFIG_LITTLE_ENDIAN 1
//#undef CONFIG_BIG_ENDIAN

#undef PLATFORM_WINDOWS
#undef PLATFORM_OS_XP 
#undef PLATFORM_OS_CE


#define PLATFORM_LINUX 1

#define CONFIG_PWRCTRL	1
//#define CONFIG_H2CLBK 1

#define CONFIG_MP_INCLUDED

//#undef CONFIG_EMBEDDED_FWIMG
#define CONFIG_EMBEDDED_FWIMG 1

#define CONFIG_R871X_TEST 1


//#define CONFIG_DRVEXT_MODULE 1


#ifdef CONFIG_RTL8712

	#define CONFIG_DEBUG_RTL8712 1	

	#define CONFIG_XMIT_ENQUEUE

	//#define CONFIG_XMIT_DIRECT

	#define CONFIG_80211N_HT 1

	#define CONFIG_RECV_REORDERING_CTRL 1

	#ifdef PLATFORM_LINUX

		#define CONFIG_XMIT_BH 1
		#define CONFIG_SKB_COPY 1

		#define CONFIG_RECV_TASKLET 1		

		#ifndef CONFIG_RECV_TASKLET
			#define CONFIG_RECV_BH 1
		#else			
			#define CONFIG_PREALLOC_RECV_SKB 1
		#endif
	
	#endif

	#ifdef CONFIG_R871X_TEST

		#define CONFIG_R8712_TEST 1

		//#define CONFIG_MLME_EXT 1
		//#define CONFIG_AP_MODE 1
		//#define CONFIG_HOSTAPD_MODE 1	

		#ifdef CONFIG_MLME_EXT
			#define CONFIG_EVENT_THREAD_MODE 1
		#endif  

		//#define CONFIG_R8712_TEST_ASTA 1

		#define CONFIG_R8712_TEST_BSTA 1			  

	#endif

	#define CONFIG_RTL8712_TCP_CSUM_OFFLOAD_RX
	#define CONFIG_REDUCE_USB_TX_INT

#ifndef CONFIG_MINIMAL_MEMORY_USAGE
	//#define CONFIG_USB_TX_AGGREGATION	1
	#define CONFIG_USB_RX_AGGREGATION	1
#endif

#endif

#ifdef CONFIG_DRVEXT_MODULE

	#define INTERNAL_MD5
	#define INTERNAL_MD4
	#define INTERNAL_DES
	#define INTERNAL_SHA1
	#define CONFIG_CRYPTO_INTERNAL
	

	#define CONFIG_INTERNAL_AES
	#define CONFIG_INTERNAL_SHA1
	#define CONFIG_INTERNAL_MD5
	#define CONFIG_INTERNAL_SHA256

	#define CONFIG_INTERNAL_LIBTOMMATH
		
	#define OPENSSL_NO_FP_API
	#define OPENSSL_NO_ENGINE



	#define CONFIG_NO_STDOUT_DEBUG 1

	#define CONFIG_ETH_INTF	1

	//#define CONFIG_WSC_CRYPTO_TEST	1
	#ifdef CONFIG_WSC_CRYPTO_TEST
	//#define CONFIG_WSC_CRYPTO_DH_TEST 1
	//#define CONFIG_WSC_CRYPTO_SHA256_TEST 1
	//#define CONFIG_WSC_CRYPTO_AES128_TEST 1
	#endif


	//#define CONFIG_DRVEXT_MODULE_WSC 1
	
	#ifdef CONFIG_DRVEXT_MODULE_WSC
		//#define CONFIG_WSCCRYPTO_PREALLOC  1
	#endif

	//#define CONFIG_WSC_DEBUG 1  //for temmprarily setting debug
	#ifdef CONFIG_WSC_DEBUG
		#define allowed_ssid "802.11g-SSID"
	#endif


	#ifdef CONFIG_DEBUG_RTL871X

		#define CONFIG_DEBUG_WSC 1
		//#define CONFIG_DUMP_WSC_KEY	1

		
		#define CONFIG_DEBUG_WPA 1
		//#define CONFIG_DUMP_WPA_KEY 1

	#endif	

#endif

