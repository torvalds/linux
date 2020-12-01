/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/
#ifndef __ODM_TYPES_H__
#define __ODM_TYPES_H__

/*Define Different SW team support*/
#define	ODM_AP			0x01	/*BIT(0)*/
#define	ODM_CE			0x04	/*BIT(2)*/
#define	ODM_WIN		0x08	/*BIT(3)*/
#define	ODM_ADSL		0x10
/*BIT(4)*/		/*already combine with ODM_AP, and is nouse now*/
#define	ODM_IOT		0x20	/*BIT(5)*/

/*For FW API*/
#define	__iram_odm_func__
#define	__odm_func__
#define	__odm_func_aon__

/*Deifne HW endian support*/
#define	ODM_ENDIAN_BIG	0
#define	ODM_ENDIAN_LITTLE	1

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define GET_PDM_ODM(__padapter)	((struct dm_struct*)(&(GET_HAL_DATA(__padapter))->DM_OutSrc))
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#define GET_PDM_ODM(__padapter)	((struct dm_struct *)(&(GET_HAL_DATA(__padapter))->odmpriv))
#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#define GET_PDM_ODM(__padapter)	((struct dm_struct*)(&__padapter->pshare->_dmODM))
#endif

#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)
	#if defined(CONFIG_RTL_TRIBAND_SUPPORT) && defined(CONFIG_USB_HCI)
	/* enable PCI & USB HCI at the same time */
  	#define RT_PCI_USB_INTERFACE			1
  	#define	RT_PCI_INTERFACE			RT_PCI_USB_INTERFACE
	#define RT_USB_INTERFACE			RT_PCI_USB_INTERFACE
	#define	RT_SDIO_INTERFACE			3
  	#else
	#define	RT_PCI_INTERFACE			1
	#define	RT_USB_INTERFACE			2
	#define	RT_SDIO_INTERFACE			3
	#endif
#endif

enum hal_status {
	HAL_STATUS_SUCCESS,
	HAL_STATUS_FAILURE,
#if 0
	RT_STATUS_PENDING,
	RT_STATUS_RESOURCE,
	RT_STATUS_INVALID_CONTEXT,
	RT_STATUS_INVALID_PARAMETER,
	RT_STATUS_NOT_SUPPORT,
	RT_STATUS_OS_API_FAILED,
#endif
};

#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)

#define		VISTA_USB_RX_REVISE			0

/*
 * Declare for ODM spin lock definition temporarily fro compile pass.
 */
enum rt_spinlock_type {
	RT_TX_SPINLOCK = 1,
	RT_RX_SPINLOCK = 2,
	RT_RM_SPINLOCK = 3,
	RT_CAM_SPINLOCK = 4,
	RT_SCAN_SPINLOCK = 5,
	RT_LOG_SPINLOCK = 7,
	RT_BW_SPINLOCK = 8,
	RT_CHNLOP_SPINLOCK = 9,
	RT_RF_OPERATE_SPINLOCK = 10,
	RT_INITIAL_SPINLOCK = 11,
	RT_RF_STATE_SPINLOCK = 12,
	/* For RF state. Added by Bruce, 2007-10-30. */
#if VISTA_USB_RX_REVISE
	RT_USBRX_CONTEXT_SPINLOCK = 13,
	RT_USBRX_POSTPROC_SPINLOCK = 14,
	/* protect data of adapter->IndicateW/ IndicateR */
#endif
	/* Shall we define Ndis 6.2 SpinLock Here ? */
	RT_PORT_SPINLOCK = 16,
	RT_VNIC_SPINLOCK = 17,
	RT_HVL_SPINLOCK = 18,
	RT_H2C_SPINLOCK = 20,
	/* For H2C cmd. Added by tynli. 2009.11.09. */

	rt_bt_data_spinlock = 25,

	RT_WAPI_OPTION_SPINLOCK = 26,
	RT_WAPI_RX_SPINLOCK = 27,

	/* add for 92D CCK control issue */
	RT_CCK_PAGEA_SPINLOCK = 28,
	RT_BUFFER_SPINLOCK = 29,
	RT_CHANNEL_AND_BANDWIDTH_SPINLOCK = 30,
	RT_GEN_TEMP_BUF_SPINLOCK = 31,
	RT_AWB_SPINLOCK = 32,
	RT_FW_PS_SPINLOCK = 33,
	RT_HW_TIMER_SPIN_LOCK = 34,
	RT_MPT_WI_SPINLOCK = 35,
	RT_P2P_SPIN_LOCK = 36,	/* Protect P2P context */
	RT_DBG_SPIN_LOCK = 37,
	RT_IQK_SPINLOCK = 38,
	RT_PENDED_OID_SPINLOCK = 39,
	RT_CHNLLIST_SPINLOCK = 40,
	RT_INDIC_SPINLOCK = 41,	/* protect indication */
	RT_RFD_SPINLOCK = 42,
	RT_SYNC_IO_CNT_SPINLOCK = 43,
	RT_LAST_SPINLOCK,
};

#endif

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define sta_info 	_RT_WLAN_STA
	#define	__func__		__FUNCTION__
	#define	PHYDM_TESTCHIP_SUPPORT	TESTCHIP_SUPPORT
	#define MASKH3BYTES			0xffffff00
	#define SUCCESS	0
	#define FAIL	(-1)

	#define	u8 		u1Byte
	#define	s8 		s1Byte

	#define	u16		u2Byte
	#define	s16		s2Byte

	#define	u32 	u4Byte
	#define	s32 		s4Byte

	#define	u64		u8Byte
	#define	s64		s8Byte

	#define	phydm_timer_list	_RT_TIMER

	// for power limit table
	enum odm_pw_lmt_regulation_type {
		PW_LMT_REGU_FCC = 0,
		PW_LMT_REGU_ETSI = 1,
		PW_LMT_REGU_MKK = 2,
		PW_LMT_REGU_WW13 = 3,
		PW_LMT_REGU_IC = 4,
		PW_LMT_REGU_KCC = 5,
		PW_LMT_REGU_ACMA = 6,
		PW_LMT_REGU_CHILE = 7,
		PW_LMT_REGU_UKRAINE = 8,
		PW_LMT_REGU_MEXICO = 9,
		PW_LMT_REGU_CN = 10
	};

	enum odm_pw_lmt_band_type {
		PW_LMT_BAND_2_4G = 0,
		PW_LMT_BAND_5G = 1
	};

	enum odm_pw_lmt_bandwidth_type {
		PW_LMT_BW_20M = 0,
		PW_LMT_BW_40M = 1,
		PW_LMT_BW_80M = 2,
		PW_LMT_BW_160M = 3
	};

	enum odm_pw_lmt_ratesection_type {
		PW_LMT_RS_CCK = 0,
		PW_LMT_RS_OFDM = 1,
		PW_LMT_RS_HT = 2,
		PW_LMT_RS_VHT = 3
	};

	enum odm_pw_lmt_rfpath_type {
		PW_LMT_PH_1T = 0,
		PW_LMT_PH_2T = 1,
		PW_LMT_PH_3T = 2,
		PW_LMT_PH_4T = 3
	};

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#include "../typedef.h"

	#ifdef CONFIG_PCI_HCI
	#if defined(CONFIG_RTL_TRIBAND_SUPPORT) && defined(CONFIG_USB_HCI)
		#define DEV_BUS_TYPE		RT_PCI_USB_INTERFACE
	#else
		#define DEV_BUS_TYPE		RT_PCI_INTERFACE
	#endif
	#endif

	#if (defined(TESTCHIP_SUPPORT))
		#define	PHYDM_TESTCHIP_SUPPORT 1
	#else
		#define	PHYDM_TESTCHIP_SUPPORT 0
	#endif

	#define	sta_info stat_info
	#define	boolean	bool

	#define	phydm_timer_list	timer_list
	#if defined(__ECOS)
	#define s64	s8Byte
	#endif 
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE) && defined(DM_ODM_CE_MAC80211)

	#include <asm/byteorder.h>

	#define DEV_BUS_TYPE	RT_PCI_INTERFACE

	#if defined(__LITTLE_ENDIAN)
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_LITTLE
	#elif defined(__BIG_ENDIAN)
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_BIG
	#else
		#error
	#endif

	/* define useless flag to avoid compile warning */
	#define	USE_WORKITEM 0
	#define	FOR_BRAZIL_PRETEST 0
	#define	FPGA_TWO_MAC_VERIFICATION	0
	#define	RTL8881A_SUPPORT	0
	#define	PHYDM_TESTCHIP_SUPPORT 0


	#define RATE_ADAPTIVE_SUPPORT			0
	#define POWER_TRAINING_ACTIVE			0

	#define sta_info	rtl_sta_info
	#define	boolean		bool

	#define	phydm_timer_list	timer_list

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include <drv_types.h>

	#ifdef CONFIG_USB_HCI
		#define DEV_BUS_TYPE	RT_USB_INTERFACE
	#elif defined(CONFIG_PCI_HCI)
		#define DEV_BUS_TYPE	RT_PCI_INTERFACE
	#elif defined(CONFIG_SDIO_HCI)
		#define DEV_BUS_TYPE	RT_SDIO_INTERFACE
	#elif defined(CONFIG_GSPI_HCI)
		#define DEV_BUS_TYPE	RT_SDIO_INTERFACE
	#endif


	#if defined(CONFIG_LITTLE_ENDIAN)
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_LITTLE
	#elif defined(CONFIG_BIG_ENDIAN)
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_BIG
	#endif

	#define	boolean	bool

	#define SET_TX_DESC_ANTSEL_A_88E(__ptx_desc, __value) SET_BITS_TO_LE_4BYTE(__ptx_desc + 8, 24, 1, __value)
	#define SET_TX_DESC_ANTSEL_B_88E(__ptx_desc, __value) SET_BITS_TO_LE_4BYTE(__ptx_desc + 8, 25, 1, __value)
	#define SET_TX_DESC_ANTSEL_C_88E(__ptx_desc, __value) SET_BITS_TO_LE_4BYTE(__ptx_desc + 28, 29, 1, __value)

	/* define useless flag to avoid compile warning */
	#define	USE_WORKITEM 0
	#define	FOR_BRAZIL_PRETEST 0
	#define	FPGA_TWO_MAC_VERIFICATION	0
	#define	RTL8881A_SUPPORT	0

	#if (defined(TESTCHIP_SUPPORT))
		#define	PHYDM_TESTCHIP_SUPPORT 1
	#else
		#define	PHYDM_TESTCHIP_SUPPORT 0
	#endif

	#define	phydm_timer_list	rtw_timer_list

	// for power limit table
	enum odm_pw_lmt_regulation_type {
		PW_LMT_REGU_FCC = 0,
		PW_LMT_REGU_ETSI = 1,
		PW_LMT_REGU_MKK = 2,
		PW_LMT_REGU_WW13 = 3,
		PW_LMT_REGU_IC = 4,
		PW_LMT_REGU_KCC = 5,
		PW_LMT_REGU_ACMA = 6,
		PW_LMT_REGU_CHILE = 7,
		PW_LMT_REGU_UKRAINE = 8,
		PW_LMT_REGU_MEXICO = 9,
		PW_LMT_REGU_CN = 10
	};

	enum odm_pw_lmt_band_type {
		PW_LMT_BAND_2_4G = 0,
		PW_LMT_BAND_5G = 1
	};

	enum odm_pw_lmt_bandwidth_type {
		PW_LMT_BW_20M = 0,
		PW_LMT_BW_40M = 1,
		PW_LMT_BW_80M = 2,
		PW_LMT_BW_160M = 3
	};

	enum odm_pw_lmt_ratesection_type {
		PW_LMT_RS_CCK = 0,
		PW_LMT_RS_OFDM = 1,
		PW_LMT_RS_HT = 2,
		PW_LMT_RS_VHT = 3
	};

	enum odm_pw_lmt_rfpath_type {
		PW_LMT_PH_1T = 0,
		PW_LMT_PH_2T = 1,
		PW_LMT_PH_3T = 2,
		PW_LMT_PH_4T = 3
	};

#elif (DM_ODM_SUPPORT_TYPE == ODM_IOT)
	#define	boolean	bool
	#define true	_TRUE
	#define false	_FALSE

	// for power limit table
	enum odm_pw_lmt_regulation_type {
		PW_LMT_REGU_NULL = 0,
		PW_LMT_REGU_FCC = 1,
		PW_LMT_REGU_ETSI = 2,
		PW_LMT_REGU_MKK = 3,
		PW_LMT_REGU_WW13 = 4
	};

	enum odm_pw_lmt_band_type {
		PW_LMT_BAND_NULL = 0,
		PW_LMT_BAND_2_4G = 1,
		PW_LMT_BAND_5G = 2
	};

	enum odm_pw_lmt_bandwidth_type {
		PW_LMT_BW_NULL = 0,
		PW_LMT_BW_20M = 1,
		PW_LMT_BW_40M = 2,
		PW_LMT_BW_80M = 3
	};

	enum odm_pw_lmt_ratesection_type {
		PW_LMT_RS_NULL = 0,
		PW_LMT_RS_CCK = 1,
		PW_LMT_RS_OFDM = 2,
		PW_LMT_RS_HT = 3,
		PW_LMT_RS_VHT = 4
	};

	enum odm_pw_lmt_rfpath_type {
		PW_LMT_PH_NULL = 0,
		PW_LMT_PH_1T = 1,
		PW_LMT_PH_2T = 2,
		PW_LMT_PH_3T = 3,
		PW_LMT_PH_4T = 4
	};

	#define	phydm_timer_list	timer_list

#endif

#define READ_NEXT_PAIR(v1, v2, i) do { if (i + 2 >= array_len) break; i += 2; v1 = array[i]; v2 = array[i + 1]; } while (0)
#define COND_ELSE  2
#define COND_ENDIF 3

#define	MASKBYTE0		0xff
#define	MASKBYTE1		0xff00
#define	MASKBYTE2		0xff0000
#define	MASKBYTE3		0xff000000
#define	MASKHWORD		0xffff0000
#define	MASKLWORD		0x0000ffff
#define	MASKDWORD		0xffffffff

#define	MASK7BITS		0x7f
#define	MASK12BITS		0xfff
#define	MASKH4BITS		0xf0000000
#define	MASK20BITS		0xfffff
#define	MASK24BITS		0xffffff
#define	MASKOFDM_D		0xffc00000
#define	MASKCCK			0x3f3f3f3f

#define RFREGOFFSETMASK		0xfffff
#define RFREG_MASK		0xfffff

#define MASKH3BYTES		0xffffff00
#define MASKL3BYTES		0x00ffffff
#define MASKBYTE2HIGHNIBBLE	0x00f00000
#define MASKBYTE3LOWNIBBLE	0x0f000000
#define	MASKL3BYTES		0x00ffffff

#endif /* __ODM_TYPES_H__ */
