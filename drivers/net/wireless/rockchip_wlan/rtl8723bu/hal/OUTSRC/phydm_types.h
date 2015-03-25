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




#define ODM_RATEMCS15_SG		0x1c
#define ODM_RATEMCS32			0x20


// CCK Rates, TxHT = 0
#define ODM_RATE1M				0x00
#define ODM_RATE2M				0x01
#define ODM_RATE5_5M			0x02
#define ODM_RATE11M				0x03
// OFDM Rates, TxHT = 0
#define ODM_RATE6M				0x04
#define ODM_RATE9M				0x05
#define ODM_RATE12M				0x06
#define ODM_RATE18M				0x07
#define ODM_RATE24M				0x08
#define ODM_RATE36M				0x09
#define ODM_RATE48M				0x0A
#define ODM_RATE54M				0x0B
// MCS Rates, TxHT = 1
#define ODM_RATEMCS0			0x0C
#define ODM_RATEMCS1			0x0D
#define ODM_RATEMCS2			0x0E
#define ODM_RATEMCS3			0x0F
#define ODM_RATEMCS4			0x10
#define ODM_RATEMCS5			0x11
#define ODM_RATEMCS6			0x12
#define ODM_RATEMCS7			0x13
#define ODM_RATEMCS8			0x14
#define ODM_RATEMCS9			0x15
#define ODM_RATEMCS10			0x16
#define ODM_RATEMCS11			0x17
#define ODM_RATEMCS12			0x18
#define ODM_RATEMCS13			0x19
#define ODM_RATEMCS14			0x1A
#define ODM_RATEMCS15			0x1B
#define ODM_RATEMCS16			0x1C
#define ODM_RATEMCS17			0x1D
#define ODM_RATEMCS18			0x1E
#define ODM_RATEMCS19			0x1F
#define ODM_RATEMCS20			0x20
#define ODM_RATEMCS21			0x21
#define ODM_RATEMCS22			0x22
#define ODM_RATEMCS23			0x23
#define ODM_RATEMCS24			0x24
#define ODM_RATEMCS25			0x25
#define ODM_RATEMCS26			0x26
#define ODM_RATEMCS27			0x27
#define ODM_RATEMCS28			0x28
#define ODM_RATEMCS29			0x29
#define ODM_RATEMCS30			0x2A
#define ODM_RATEMCS31			0x2B
#define ODM_RATEVHTSS1MCS0		0x2C
#define ODM_RATEVHTSS1MCS1		0x2D
#define ODM_RATEVHTSS1MCS2		0x2E
#define ODM_RATEVHTSS1MCS3		0x2F
#define ODM_RATEVHTSS1MCS4		0x30
#define ODM_RATEVHTSS1MCS5		0x31
#define ODM_RATEVHTSS1MCS6		0x32
#define ODM_RATEVHTSS1MCS7		0x33
#define ODM_RATEVHTSS1MCS8		0x34
#define ODM_RATEVHTSS1MCS9		0x35
#define ODM_RATEVHTSS2MCS0		0x36
#define ODM_RATEVHTSS2MCS1		0x37
#define ODM_RATEVHTSS2MCS2		0x38
#define ODM_RATEVHTSS2MCS3		0x39
#define ODM_RATEVHTSS2MCS4		0x3A
#define ODM_RATEVHTSS2MCS5		0x3B
#define ODM_RATEVHTSS2MCS6		0x3C
#define ODM_RATEVHTSS2MCS7		0x3D
#define ODM_RATEVHTSS2MCS8		0x3E
#define ODM_RATEVHTSS2MCS9		0x3F
#define ODM_RATEVHTSS3MCS0		0x40
#define ODM_RATEVHTSS3MCS1		0x41
#define ODM_RATEVHTSS3MCS2		0x42
#define ODM_RATEVHTSS3MCS3		0x43
#define ODM_RATEVHTSS3MCS4		0x44
#define ODM_RATEVHTSS3MCS5		0x45
#define ODM_RATEVHTSS3MCS6		0x46
#define ODM_RATEVHTSS3MCS7		0x47
#define ODM_RATEVHTSS3MCS8		0x48
#define ODM_RATEVHTSS3MCS9		0x49
#define ODM_RATEVHTSS4MCS0		0x4A
#define ODM_RATEVHTSS4MCS1		0x4B
#define ODM_RATEVHTSS4MCS2		0x4C
#define ODM_RATEVHTSS4MCS3		0x4D
#define ODM_RATEVHTSS4MCS4		0x4E
#define ODM_RATEVHTSS4MCS5		0x4F
#define ODM_RATEVHTSS4MCS6		0x50
#define ODM_RATEVHTSS4MCS7		0x51
#define ODM_RATEVHTSS4MCS8		0x52
#define ODM_RATEVHTSS4MCS9		0x53

//
// Define Different SW team support
//
#define	ODM_AP		 	0x01	//BIT0 
#define	ODM_ADSL	 	0x02	//BIT1
#define	ODM_CE		 	0x04	//BIT2
#define	ODM_WIN		 	0x08	//BIT3

#define	DM_ODM_SUPPORT_TYPE			ODM_CE

// Deifne HW endian support
#define	ODM_ENDIAN_BIG	0
#define	ODM_ENDIAN_LITTLE	1

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define GET_PDM_ODM(__pAdapter)	((PDM_ODM_T)(&((GET_HAL_DATA(__pAdapter))->DM_OutSrc)))
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
#define GET_PDM_ODM(__pAdapter)	((PDM_ODM_T)(&((GET_HAL_DATA(__pAdapter))->odmpriv)))
#endif

#if (DM_ODM_SUPPORT_TYPE != ODM_WIN)
#define 	RT_PCI_INTERFACE				1
#define 	RT_USB_INTERFACE				2
#define 	RT_SDIO_INTERFACE				3
#endif

typedef enum _HAL_STATUS{
	HAL_STATUS_SUCCESS,
	HAL_STATUS_FAILURE,
	/*RT_STATUS_PENDING,
	RT_STATUS_RESOURCE,
	RT_STATUS_INVALID_CONTEXT,
	RT_STATUS_INVALID_PARAMETER,
	RT_STATUS_NOT_SUPPORT,
	RT_STATUS_OS_API_FAILED,*/
}HAL_STATUS,*PHAL_STATUS;

#if( DM_ODM_SUPPORT_TYPE == ODM_AP)
#define		MP_DRIVER		0
#endif
#if(DM_ODM_SUPPORT_TYPE != ODM_WIN)

#define		VISTA_USB_RX_REVISE			0

//
// Declare for ODM spin lock defintion temporarily fro compile pass.
//
typedef enum _RT_SPINLOCK_TYPE{
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
	RT_RF_STATE_SPINLOCK = 12, // For RF state. Added by Bruce, 2007-10-30.
#if VISTA_USB_RX_REVISE
	RT_USBRX_CONTEXT_SPINLOCK = 13,
	RT_USBRX_POSTPROC_SPINLOCK = 14, // protect data of Adapter->IndicateW/ IndicateR
#endif
	//Shall we define Ndis 6.2 SpinLock Here ?
	RT_PORT_SPINLOCK=16,
	RT_VNIC_SPINLOCK=17,
	RT_HVL_SPINLOCK=18,	
	RT_H2C_SPINLOCK = 20, // For H2C cmd. Added by tynli. 2009.11.09.

	RT_BTData_SPINLOCK=25,

	RT_WAPI_OPTION_SPINLOCK=26,
	RT_WAPI_RX_SPINLOCK=27,

      // add for 92D CCK control issue  
	RT_CCK_PAGEA_SPINLOCK = 28,
	RT_BUFFER_SPINLOCK = 29,
	RT_CHANNEL_AND_BANDWIDTH_SPINLOCK = 30,
	RT_GEN_TEMP_BUF_SPINLOCK = 31,
	RT_AWB_SPINLOCK = 32,
	RT_FW_PS_SPINLOCK = 33,
	RT_HW_TIMER_SPIN_LOCK = 34,
	RT_MPT_WI_SPINLOCK = 35,
	RT_P2P_SPIN_LOCK = 36,	// Protect P2P context
	RT_DBG_SPIN_LOCK = 37,
	RT_IQK_SPINLOCK = 38,
	RT_PENDED_OID_SPINLOCK = 39,
	RT_CHNLLIST_SPINLOCK = 40,	
	RT_INDIC_SPINLOCK = 41,	//protect indication	
	RT_RFD_SPINLOCK = 42,
	RT_LAST_SPINLOCK,
}RT_SPINLOCK_TYPE;

#endif


#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#define	STA_INFO_T			RT_WLAN_STA
	#define	PSTA_INFO_T			PRT_WLAN_STA

//    typedef unsigned long		u4Byte,*pu4Byte;
#define CONFIG_HW_ANTENNA_DIVERSITY 
#define CONFIG_SW_ANTENNA_DIVERSITY 

#elif (DM_ODM_SUPPORT_TYPE == ODM_AP)

	// To let ADSL/AP project compile ok; it should be removed after all conflict are solved. Added by Annie, 2011-10-07.
	#define ADSL_AP_BUILD_WORKAROUND
	#define AP_BUILD_WORKAROUND
	
	//2 [ Configure Antenna Diversity ]
#if defined(CONFIG_RTL_8881A_ANT_SWITCH) || defined(CONFIG_SLOT_0_ANT_SWITCH) || defined(CONFIG_SLOT_1_ANT_SWITCH)
	#define CONFIG_HW_ANTENNA_DIVERSITY 
	#define ODM_EVM_ENHANCE_ANTDIV

        //----------
	#if(!defined(CONFIG_NO_2G_DIVERSITY) && !defined(CONFIG_2G_CGCS_RX_DIVERSITY) && !defined(CONFIG_2G_CG_TRX_DIVERSITY) && !defined(CONFIG_2G_CG_SMART_ANT_DIVERSITY))
		#define CONFIG_NO_2G_DIVERSITY
	#endif

	#ifdef CONFIG_NO_5G_DIVERSITY_8881A
		#define CONFIG_NO_5G_DIVERSITY
	#elif  defined(CONFIG_5G_CGCS_RX_DIVERSITY_8881A)
		#define CONFIG_5G_CGCS_RX_DIVERSITY
	#elif  defined(CONFIG_5G_CG_TRX_DIVERSITY_8881A)
		#define CONFIG_5G_CG_TRX_DIVERSITY
	#endif

	#if(!defined(CONFIG_NO_5G_DIVERSITY) && !defined(CONFIG_5G_CGCS_RX_DIVERSITY) && !defined(CONFIG_5G_CG_TRX_DIVERSITY) && !defined(CONFIG_5G_CG_SMART_ANT_DIVERSITY))
		#define CONFIG_NO_5G_DIVERSITY
	#endif	
	//----------
	#if ( defined(CONFIG_NO_2G_DIVERSITY) && defined(CONFIG_NO_5G_DIVERSITY) )
		#define CONFIG_NOT_SUPPORT_ANTDIV 
	#elif( !defined(CONFIG_NO_2G_DIVERSITY) && defined(CONFIG_NO_5G_DIVERSITY) )
		#define CONFIG_2G_SUPPORT_ANTDIV
	#elif( defined(CONFIG_NO_2G_DIVERSITY) && !defined(CONFIG_NO_5G_DIVERSITY) )
		#define CONFIG_5G_SUPPORT_ANTDIV
	#elif( !defined(CONFIG_NO_2G_DIVERSITY) && !defined(CONFIG_NO_5G_DIVERSITY) )
		#define CONFIG_2G5G_SUPPORT_ANTDIV 
	#endif
	//----------
#endif
	#ifdef AP_BUILD_WORKAROUND
	#include "../typedef.h"
	#else
	typedef void					VOID,*PVOID;
	typedef unsigned char			BOOLEAN,*PBOOLEAN;
	typedef unsigned char			u1Byte,*pu1Byte;
	typedef unsigned short			u2Byte,*pu2Byte;
	typedef unsigned int			u4Byte,*pu4Byte;
	typedef unsigned long long		u8Byte,*pu8Byte;
#if 1
/* In ARM platform, system would use the type -- "char" as "unsigned char"
 * And we only use s1Byte/ps1Byte as INT8 now, so changes the type of s1Byte.*/
    typedef signed char				s1Byte,*ps1Byte;
#else
	typedef char					s1Byte,*ps1Byte;
#endif
	typedef short					s2Byte,*ps2Byte;
	typedef long					s4Byte,*ps4Byte;
	typedef long long				s8Byte,*ps8Byte;
	#endif

	typedef struct rtl8192cd_priv	*prtl8192cd_priv;
	typedef struct stat_info		STA_INFO_T,*PSTA_INFO_T;
	typedef struct timer_list		RT_TIMER, *PRT_TIMER;
	typedef  void *				RT_TIMER_CALL_BACK;

#ifdef CONFIG_PCI_HCI
	#define DEV_BUS_TYPE		RT_PCI_INTERFACE
#endif

	#define _TRUE				1
	#define _FALSE				0
	
#elif (DM_ODM_SUPPORT_TYPE == ODM_ADSL)

	// To let ADSL/AP project compile ok; it should be removed after all conflict are solved. Added by Annie, 2011-10-07.
	#define ADSL_AP_BUILD_WORKAROUND
	#define ADSL_BUILD_WORKAROUND
	//

	typedef unsigned char			BOOLEAN,*PBOOLEAN;
	typedef unsigned char			u1Byte,*pu1Byte;
	typedef unsigned short			u2Byte,*pu2Byte;
	typedef unsigned int			u4Byte,*pu4Byte;
	typedef unsigned long long		u8Byte,*pu8Byte;
#if 1
/* In ARM platform, system would use the type -- "char" as "unsigned char"
 * And we only use s1Byte/ps1Byte as INT8 now, so changes the type of s1Byte.*/
    typedef signed char				s1Byte,*ps1Byte;
#else
	typedef char					s1Byte,*ps1Byte;
#endif
	typedef short					s2Byte,*ps2Byte;
	typedef long					s4Byte,*ps4Byte;
	typedef long long				s8Byte,*ps8Byte;

	typedef struct rtl8192cd_priv	*prtl8192cd_priv;
	typedef struct stat_info		STA_INFO_T,*PSTA_INFO_T;
	typedef struct timer_list		RT_TIMER, *PRT_TIMER;
	typedef  void *				RT_TIMER_CALL_BACK;
	
	#define DEV_BUS_TYPE		RT_PCI_INTERFACE

	#define _TRUE				1
	#define _FALSE				0

#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
	#include <drv_types.h>

#if 0
	typedef u8					u1Byte, *pu1Byte;
	typedef u16					u2Byte,*pu2Byte;
	typedef u32					u4Byte,*pu4Byte;
	typedef u64					u8Byte,*pu8Byte;
	typedef s8					s1Byte,*ps1Byte;
	typedef s16					s2Byte,*ps2Byte;
	typedef s32					s4Byte,*ps4Byte;
	typedef s64					s8Byte,*ps8Byte;
#else
	#define u1Byte 		u8
	#define	pu1Byte 	u8*	

	#define u2Byte 		u16
	#define	pu2Byte 	u16*		

	#define u4Byte 		u32
	#define	pu4Byte 	u32*	

	#define u8Byte 		u64
	#define	pu8Byte 	u64*

	#define s1Byte 		s8
	#define	ps1Byte 	s8*	

	#define s2Byte 		s16
	#define	ps2Byte 	s16*	

	#define s4Byte 		s32
	#define	ps4Byte 	s32*	

	#define s8Byte 		s64
	#define	ps8Byte 	s64*	
	
#endif
	#ifdef CONFIG_USB_HCI
		#define DEV_BUS_TYPE  	RT_USB_INTERFACE
	#elif defined(CONFIG_PCI_HCI)
		#define DEV_BUS_TYPE  	RT_PCI_INTERFACE
	#elif defined(CONFIG_SDIO_HCI)
		#define DEV_BUS_TYPE  	RT_SDIO_INTERFACE
	#elif defined(CONFIG_GSPI_HCI)
		#define DEV_BUS_TYPE  	RT_SDIO_INTERFACE
	#endif
	

	#if defined(CONFIG_LITTLE_ENDIAN)	
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_LITTLE
	#elif defined (CONFIG_BIG_ENDIAN)
		#define	ODM_ENDIAN_TYPE			ODM_ENDIAN_BIG
	#endif
	
	typedef struct timer_list		RT_TIMER, *PRT_TIMER;
	typedef  void *				RT_TIMER_CALL_BACK;
	#define	STA_INFO_T			struct sta_info
	#define	PSTA_INFO_T		struct sta_info *
		


	#define TRUE 	_TRUE	
	#define FALSE	_FALSE


	#define SET_TX_DESC_ANTSEL_A_88E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 24, 1, __Value)
	#define SET_TX_DESC_ANTSEL_B_88E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+8, 25, 1, __Value)
	#define SET_TX_DESC_ANTSEL_C_88E(__pTxDesc, __Value) SET_BITS_TO_LE_4BYTE(__pTxDesc+28, 29, 1, __Value)

	//define useless flag to avoid compile warning
	#define	USE_WORKITEM 0
	#define 	FOR_BRAZIL_PRETEST 0
	#define   FPGA_TWO_MAC_VERIFICATION	0
	#define	RTL8881A_SUPPORT	0
#endif

#define READ_NEXT_PAIR(v1, v2, i) do { if (i+2 >= ArrayLen) break; i += 2; v1 = Array[i]; v2 = Array[i+1]; } while(0)
#define COND_ELSE  2
#define COND_ENDIF 3

#endif // __ODM_TYPES_H__

