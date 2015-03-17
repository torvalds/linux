/******************************************************************************
 *
 * Copyright(c) 2007 - 2013 Realtek Corporation. All rights reserved.
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
#ifndef __RTL8192E_HAL_H__
#define __RTL8192E_HAL_H__

//#include "hal_com.h"

#if 1
#include "hal_data.h"
#else
#include "../hal/OUTSRC/odm_precomp.h"
#endif

//include HAL Related header after HAL Related compiling flags 
#include "rtl8192e_spec.h"
#include "rtl8192e_rf.h"
#include "rtl8192e_dm.h"
#include "rtl8192e_recv.h"
#include "rtl8192e_xmit.h"
#include "rtl8192e_cmd.h"
#include "rtl8192e_led.h"
#include "Hal8192EPwrSeq.h"
#include "Hal8192EPhyReg.h"
#include "Hal8192EPhyCfg.h"


#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8192e_sreset.h"
#endif


//---------------------------------------------------------------------
//		RTL8192E From header
//---------------------------------------------------------------------
	#define RTL8192E_FW_IMG					"rtl8192e/FW_NIC.bin"
	#define RTL8192E_FW_WW_IMG				"rtl8192e/FW_WoWLAN.bin"
	#define RTL8192E_PHY_REG					"rtl8192e/PHY_REG.txt" 
	#define RTL8192E_PHY_RADIO_A				"rtl8192e/RadioA.txt"
	#define RTL8192E_PHY_RADIO_B				"rtl8192e/RadioB.txt"
	#define RTL8192E_TXPWR_TRACK				"rtl8192e/TxPowerTrack.txt"
	#define RTL8192E_AGC_TAB					"rtl8192e/AGC_TAB.txt"
	#define RTL8192E_PHY_MACREG 				"rtl8192e/MAC_REG.txt"
	#define RTL8192E_PHY_REG_PG				"rtl8192e/PHY_REG_PG.txt"
	#define RTL8192E_PHY_REG_MP 				"rtl8192e/PHY_REG_MP.txt" 
	#define RTL8192E_TXPWR_LMT				"rtl8192e/TXPWR_LMT.txt" 

//---------------------------------------------------------------------
//		RTL8192E Power Configuration CMDs for PCIe interface
//---------------------------------------------------------------------
#define Rtl8192E_NIC_PWR_ON_FLOW				rtl8192E_power_on_flow
#define Rtl8192E_NIC_RF_OFF_FLOW				rtl8192E_radio_off_flow
#define Rtl8192E_NIC_DISABLE_FLOW				rtl8192E_card_disable_flow
#define Rtl8192E_NIC_ENABLE_FLOW				rtl8192E_card_enable_flow
#define Rtl8192E_NIC_SUSPEND_FLOW				rtl8192E_suspend_flow
#define Rtl8192E_NIC_RESUME_FLOW				rtl8192E_resume_flow
#define Rtl8192E_NIC_PDN_FLOW					rtl8192E_hwpdn_flow
#define Rtl8192E_NIC_LPS_ENTER_FLOW			rtl8192E_enter_lps_flow
#define Rtl8192E_NIC_LPS_LEAVE_FLOW			rtl8192E_leave_lps_flow	


#if 1 // download firmware related data structure
#define FW_SIZE_8192E			0x8000 // Compatible with RTL8192e Maximal RAM code size 32k
#define FW_START_ADDRESS		0x1000
#define FW_END_ADDRESS			0x5FFF


#define IS_FW_HEADER_EXIST_8192E(_pFwHdr)	((GET_FIRMWARE_HDR_SIGNATURE_8192E(_pFwHdr) &0xFFF0) ==  0x92E0)



typedef struct _RT_FIRMWARE_8192E {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szFwBuffer;
#else
	u8			szFwBuffer[FW_SIZE_8192E];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8192E, *PRT_FIRMWARE_8192E;

//
// This structure must be cared byte-ordering
//
// Added by tynli. 2009.12.04.

//=====================================================
//					Firmware Header(8-byte alinment required)
//=====================================================
//--- LONG WORD 0 ----
#define GET_FIRMWARE_HDR_SIGNATURE_8192E(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 0, 16) // 92C0: test chip; 92C, 88C0: test chip; 88C1: MP A-cut; 92C1: MP A-cut
#define GET_FIRMWARE_HDR_CATEGORY_8192E(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 16, 8) // AP/NIC and USB/PCI
#define GET_FIRMWARE_HDR_FUNCTION_8192E(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 24, 8) // Reserved for different FW function indcation, for further use when driver needs to download different FW in different conditions
#define GET_FIRMWARE_HDR_VERSION_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+4, 0, 16)// FW Version
#define GET_FIRMWARE_HDR_SUB_VER_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+4, 16, 8) // FW Subversion, default 0x00
#define GET_FIRMWARE_HDR_RSVD1_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+4, 24, 8) 		

//--- LONG WORD 1 ----
#define GET_FIRMWARE_HDR_MONTH_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 0, 8) // Release time Month field
#define GET_FIRMWARE_HDR_DATE_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 8, 8) // Release time Date field
#define GET_FIRMWARE_HDR_HOUR_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 16, 8)// Release time Hour field
#define GET_FIRMWARE_HDR_MINUTE_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 24, 8)// Release time Minute field
#define GET_FIRMWARE_HDR_ROMCODE_SIZE_8192E(__FwHdr)	LE_BITS_TO_4BYTE(__FwHdr+12, 0, 16)// The size of RAM code
#define GET_FIRMWARE_HDR_RSVD2_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+12, 16, 16)

//--- LONG WORD 2 ----
#define GET_FIRMWARE_HDR_SVN_IDX_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+16, 0, 32)// The SVN entry index
#define GET_FIRMWARE_HDR_RSVD3_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+20, 0, 32)

//--- LONG WORD 3 ----
#define GET_FIRMWARE_HDR_RSVD4_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+24, 0, 32)
#define GET_FIRMWARE_HDR_RSVD5_8192E(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+28, 0, 32)

#endif // download firmware related data structure

#define DRIVER_EARLY_INT_TIME_8192E		0x05
#define BCN_DMA_ATIME_INT_TIME_8192E		0x02

#define MAX_RX_DMA_BUFFER_SIZE_8192E		0x3d00 //0x3E80   //0x3FFF	// RX 16K reserved for WOW ?


#define TX_TOTAL_PAGE_NUMBER_8192E		243 //0x00~0xF3 totoal pages: F4

#define TX_PAGE_BOUNDARY_8192E			(TX_TOTAL_PAGE_NUMBER_8192E + 1)//0xF4,Rserved 12 pages for BCN/PS-POLL..
#define TX_PAGE_LOAD_FW_BOUNDARY_8192E		0x47 //0xA5
#define TX_PAGE_BOUNDARY_WOWLAN_8192E		0xE0

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_92C
#define NORMAL_PAGE_NUM_PUBQ_8192E			0xE0
#define NORMAL_PAGE_NUM_LPQ_8192E			0x0C
#define NORMAL_PAGE_NUM_HPQ_8192E			0x08
#define NORMAL_PAGE_NUM_NPQ_8192E			0x00
#define NORMAL_PAGE_NUM_EPQ_8192E			0x00



//Note: For WMM Normal Chip Setting ,modify later
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8192E	TX_PAGE_BOUNDARY_8192E
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8192E		(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8192E + 1)

#define WMM_NORMAL_PAGE_NUM_PUBQ_8192E	NORMAL_PAGE_NUM_PUBQ_8192E
#define WMM_NORMAL_PAGE_NUM_HPQ_8192E		NORMAL_PAGE_NUM_HPQ_8192E
#define WMM_NORMAL_PAGE_NUM_LPQ_8192E		NORMAL_PAGE_NUM_LPQ_8192E
#define WMM_NORMAL_PAGE_NUM_NPQ_8192E		NORMAL_PAGE_NUM_NPQ_8192E

#define USB_JAGUAR_DUMMY_OFFSET_8192EU		2
#define USB_JAGUAR_DUMMY_UNIT_8192EU			8
#define USB_JAGUAR_ALL_DUMMY_LENGTH_8192EU			(USB_JAGUAR_DUMMY_OFFSET_8192EU * USB_JAGUAR_DUMMY_UNIT_8192EU)
#define USB_HWDESC_HEADER_LEN_8192EU		(TX_DESC_SIZE_8192E + USB_JAGUAR_ALL_DUMMY_LENGTH_8192EU)


//-------------------------------------------------------------------------
//	Chip specific
//-------------------------------------------------------------------------

//-------------------------------------------------------------------------
//	Channel Plan
//-------------------------------------------------------------------------

#define		HWSET_MAX_SIZE_8192E			512

#define		EFUSE_REAL_CONTENT_LEN_8192E	512

#define		EFUSE_MAP_LEN_8192E			512
#define		EFUSE_MAX_SECTION_8192E		64
#define		EFUSE_MAX_WORD_UNIT_8192E		4
#define		EFUSE_IC_ID_OFFSET_8192E		506	//For some inferiority IC purpose. added by Roger, 2009.09.02.
#define 		AVAILABLE_EFUSE_ADDR_8192E(addr) 	(addr < EFUSE_REAL_CONTENT_LEN_8192E)
//
// <Roger_Notes> To prevent out of boundary programming case, leave 1byte and program full section
// 9bytes + 1byt + 5bytes and pre 1byte.
// For worst case:
// | 1byte|----8bytes----|1byte|--5bytes--| 
// |         |            Reserved(14bytes)	      |
//
#define		EFUSE_OOB_PROTECT_BYTES_8192E 		15	// PG data exclude header, dummy 6 bytes frome CP test and reserved 1byte.



//========================================================
//			EFUSE for BT definition
//========================================================
#define		EFUSE_BT_REAL_BANK_CONTENT_LEN_8192E	512
#define		EFUSE_BT_REAL_CONTENT_LEN_8192E			1024	// 512*2
#define		EFUSE_BT_MAP_LEN_8192E					1024	// 1k bytes
#define		EFUSE_BT_MAX_SECTION_8192E				128		// 1024/8

#define		EFUSE_PROTECT_BYTES_BANK_8192E			16
#define 	EFUSE_MAX_BANK_8192E					3
//===========================================================

#define INCLUDE_MULTI_FUNC_BT(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

//#define IS_MULTI_FUNC_CHIP(_Adapter)	(((((PHAL_DATA_TYPE)(_Adapter->HalData))->MultiFunc) & (RT_MULTI_FUNC_BT|RT_MULTI_FUNC_GPS)) ? _TRUE : _FALSE)

//#define RT_IS_FUNC_DISABLED(__pAdapter, __FuncBits) ( (__pAdapter)->DisabledFunctions & (__FuncBits) )

#define GetDefaultAdapter(padapter)	padapter

// rtl8812_hal_init.c
void	_8051Reset8192E(PADAPTER padapter);
s32	FirmwareDownload8192E(PADAPTER Adapter, BOOLEAN bUsedWoWLANFw);
void	InitializeFirmwareVars8192E(PADAPTER padapter);

s32	InitLLTTable8192E(PADAPTER padapter, u8 txpktbuf_bndy);

// EFuse
u8	GetEEPROMSize8192E(PADAPTER padapter);
void	hal_InitPGData_8192E(PADAPTER padapter, u8* PROMContent);
void	Hal_EfuseParseIDCode8192E(PADAPTER padapter, u8 *hwinfo);
void	Hal_ReadPROMVersion8192E(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void	Hal_ReadPowerSavingMode8192E(PADAPTER padapter, u8*	hwinfo, BOOLEAN	AutoLoadFail);
void	Hal_ReadTxPowerInfo8192E(PADAPTER padapter,u8* hwinfo,BOOLEAN	AutoLoadFail);
void	Hal_ReadBoardType8192E(PADAPTER pAdapter,u8* hwinfo,BOOLEAN AutoLoadFail);
void	Hal_ReadThermalMeter_8192E(PADAPTER	Adapter,u8* PROMContent,BOOLEAN 	AutoloadFail);
void	Hal_ReadChannelPlan8192E(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void	Hal_EfuseParseXtal_8192E(PADAPTER pAdapter,u8* hwinfo,BOOLEAN AutoLoadFail);
void	Hal_ReadAntennaDiversity8192E(PADAPTER pAdapter,u8* PROMContent,BOOLEAN AutoLoadFail);
void	Hal_ReadPAType_8192E(PADAPTER Adapter,u8* PROMContent, BOOLEAN AutoloadFail);
void	Hal_EfuseParseBTCoexistInfo8192E(PADAPTER Adapter, u8* hwinfo, BOOLEAN AutoLoadFail);
u8 Hal_CrystalAFEAdjust(_adapter * Adapter);

BOOLEAN HalDetectPwrDownMode8192E(PADAPTER Adapter);
	
#ifdef CONFIG_WOWLAN
void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif //CONFIG_WOWLAN

/***********************************************************/
// RTL8192E-MAC Setting
VOID _InitTxBufferBoundary_8192E(IN PADAPTER Adapter,IN u8 txpktbuf_bndy);
VOID _InitPageBoundary_8192E(IN PADAPTER Adapter);
//VOID _InitTransferPageSize_8192E(IN PADAPTER Adapter);
VOID _InitDriverInfoSize_8192E(IN PADAPTER Adapter,IN u8 drvInfoSize);
VOID _InitRxSetting_8192E(PADAPTER Adapter);
VOID _InitRDGSetting_8192E(PADAPTER Adapter);
void _InitID_8192E(IN  PADAPTER Adapter);
VOID _InitNetworkType_8192E(IN  PADAPTER Adapter);
VOID _InitWMACSetting_8192E(IN PADAPTER Adapter);
VOID _InitAdaptiveCtrl_8192E(IN  PADAPTER Adapter);
VOID _InitEDCA_8192E( IN  PADAPTER Adapter);
VOID _InitRetryFunction_8192E(	IN  PADAPTER Adapter);
VOID _InitBeaconParameters_8192E(IN  PADAPTER Adapter);
VOID _InitBeaconMaxError_8192E(
	IN  PADAPTER	Adapter,
	IN	BOOLEAN		InfraMode
	);
void _BBTurnOnBlock_8192E(PADAPTER padapter);
void SetBeaconRelatedRegisters8192E(PADAPTER padapter);
VOID hal_ReadRFType_8192E(PADAPTER	Adapter);
// RTL8192E-MAC Setting
/***********************************************************/

void SetHwReg8192E(PADAPTER Adapter, u8 variable, u8* val);
void GetHwReg8192E(PADAPTER Adapter, u8 variable, u8* val);
u8
SetHalDefVar8192E(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	);
u8
GetHalDefVar8192E(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	);

void rtl8192e_set_hal_ops(struct hal_ops *pHalFunc);
void rtl8192e_init_default_value(_adapter * padapter);
// register
void SetBcnCtrlReg(PADAPTER padapter, u8 SetBits, u8 ClearBits);

void rtl8192e_start_thread(_adapter *padapter);
void rtl8192e_stop_thread(_adapter *padapter);
#endif //__RTL8192E_HAL_H__

