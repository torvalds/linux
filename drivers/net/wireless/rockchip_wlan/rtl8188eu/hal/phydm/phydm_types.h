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
#ifndef __ODM_TYPES_H__
#define __ODM_TYPES_H__


/*Define Different SW team support*/
#define	ODM_AP			0x01	/*BIT0*/
#define	ODM_CE			0x04	/*BIT2*/
#define	ODM_WIN		0x08	/*BIT3*/
#define	ODM_ADSL		0x10	/*BIT4*/
#define	ODM_IOT		0x20	/*BIT5*/

/*Deifne HW endian support*/
#define	ODM_ENDIAN_BIG	0
#define	ODM_ENDIAN_LITTLE	1

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define GET_PDM_ODM(__padapter)	((struct PHY_DM_STRUCT*)(&((GET_HAL_DATA(__padapter))->DM_OutSrc)))
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#define GET_PDM_ODM(__padapter)	((struct PHY_DM_STRUCT*)(&((GET_HAL_DATA(__padapter))->odmpriv)))
#endif

#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)
	#define	RT_PCI_INTERFACE				1
	#define	RT_USB_INTERFACE				2
	#define	RT_SDIO_INTERFACE				3
#endif

enum hal_status {
	HAL_STATUS_SUCCESS,
	HAL_STATUS_FAILURE,
	/*RT_STATUS_PENDING,
	RT_STATUS_RESOURCE,
	RT_STATUS_INVALID_CONTEXT,
	RT_STATUS_INVALID_PARAMETER,
	RT_STATUS_NOT_SUPPORT,
	RT_STATUS_OS_API_FAILED,*/
};

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
	#define		MP_DRIVER		0
#endif
#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)

#define		VISTA_USB_RX_REVISE			0

/*
 * Declare for ODM spin lock defintion temporarily fro compile pass.
 *   */
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
	RT_RF_STATE_SPINLOCK = 12, /* For RF state. Added by Bruce, 2007-10-30. */
#if VISTA_USB_RX_REVISE
	RT_USBRX_CONTEXT_SPINLOCK = 13,
	RT_USBRX_POSTPROC_SPINLOCK = 14, /* protect data of adapter->IndicateW/ IndicateR */
#endif
	/* Shall we define Ndis 6.2 SpinLock Here ? */
	RT_PORT_SPINLOCK = 16,
	RT_VNIC_SPINLOCK = 17,
	RT_HVL_SPINLOCK = 18,
	RT_H2C_SPINLOCK = 20, /* For H2C cmd. Added by tynli. 2009.11.09. */

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

	#define	bool	BOOLEAN

	#define	timer_list	_RT_TIMER
	

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	/* To let ADSL/AP project compile ok; it should be removed after all conflict are solved. Added by Annie, 2011-10-07. */
	#define ADSL_AP_BUILD_WORKAROUND
	#define AP_BUILD_WORKAROUND

	#ifdef AP_BUILD_WORKAROUND
		#include "../typedef.h"
	#else
		typedef void					void, *void *;
		typedef unsigned char			bool, *bool *;
		typedef unsigned char			u8, *u8 *;
		typedef unsigned short			u16, *u16 *;
		typedef unsigned int			u32, *u32 *;
		typedef unsigned long long		u64, *u64 *;
		#if 1
			/* In ARM platform, system would use the type -- "char" as "unsigned char"
			* And we only use s8/s8* as INT8 now, so changes the type of s8.*/
			typedef signed char				s8, *s8 *;
		#else
			typedef char					s8, *s8 *;
		#endif
		typedef short					s16, *s16 *;
		typedef long					s32, *s32 *;
		typedef long long				s64, *s64 *;
	#endif


	#ifdef CONFIG_PCI_HCI
		#define DEV_BUS_TYPE		RT_PCI_INTERFACE
	#endif

	#define _TRUE				1
	#define _FALSE				0

	#if (defined(TESTCHIP_SUPPORT))
		#define	PHYDM_TESTCHIP_SUPPORT 1
	#else
		#define	PHYDM_TESTCHIP_SUPPORT 0
	#endif

	#define	sta_info stat_info

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include <drv_types.h>
	#if 0
		typedef u8					u8, *u8 *;
		typedef u16					u16, *u16 *;
		typedef u32					u32, *u32 *;
		typedef u64					u64, *u64 *;
		typedef s8					s8, *s8 *;
		typedef s16					s16, *s16 *;
		typedef s32					s32, *s32 *;
		typedef s64					s64, *s64 *;
	#elif 0
		#define u8		u8
		#define	u8 *u8*

		#define u16		u16
		#define	u16 *u16*

		#define u32		u32
		#define	u32 *u32*

		#define u64		u64
		#define	u64*	u64*

		#define s8		s8
		#define	s8*	s8*

		#define s16		s16
		#define	s16*	s16*

		#define s32		s32
		#define	s32*	s32*

		#define s64		s64
		#define	s64*	s64*

	#endif
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
	#elif defined (CONFIG_BIG_ENDIAN)
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_BIG
	#endif

	//#define	struct sta_info			struct sta_info
	//#define	struct sta_info*		struct sta_info *



	#define true	_TRUE
	#define false	_FALSE


	#define SET_TX_DESC_ANTSEL_A_88E(__ptx_desc, __value) SET_BITS_TO_LE_4BYTE(__ptx_desc+8, 24, 1, __value)
	#define SET_TX_DESC_ANTSEL_B_88E(__ptx_desc, __value) SET_BITS_TO_LE_4BYTE(__ptx_desc+8, 25, 1, __value)
	#define SET_TX_DESC_ANTSEL_C_88E(__ptx_desc, __value) SET_BITS_TO_LE_4BYTE(__ptx_desc+28, 29, 1, __value)

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
#endif

#define READ_NEXT_PAIR(v1, v2, i) do { if (i+2 >= array_len) break; i += 2; v1 = array[i]; v2 = array[i+1]; } while (0)
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
#define	MASKOFDM_D		0xffc00000
#define	MASKCCK			0x3f3f3f3f
#define RFREGOFFSETMASK	0xfffff
#define MASKH3BYTES		0xffffff00
#define MASKL3BYTES		0x00ffffff
#define MASKBYTE2HIGHNIBBLE		0x00f00000
#define MASKBYTE3LOWNIBBLE		0x0f000000
#define	MASKL3BYTES			0x00ffffff
#define RFREGOFFSETMASK	0xfffff


#include "phydm_features.h"

#endif /* __ODM_TYPES_H__ */
