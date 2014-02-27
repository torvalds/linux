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
#ifndef __RTL8812A_HAL_H__
#define __RTL8812A_HAL_H__

//#include "hal_com.h"
#if 1
#include "hal_data.h"
#else
#include "../hal/OUTSRC/odm_precomp.h"
#endif

//include HAL Related header after HAL Related compiling flags 
#include "rtl8812a_spec.h"
#include "rtl8812a_rf.h"
#include "rtl8812a_dm.h"
#include "rtl8812a_recv.h"
#include "rtl8812a_xmit.h"
#include "rtl8812a_cmd.h"
#include "rtl8812a_led.h"
#include "Hal8812PwrSeq.h"
#include "Hal8821APwrSeq.h" //for 8821A/8811A
#include "Hal8812PhyReg.h"
#include "Hal8812PhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8812a_sreset.h"
#endif


//---------------------------------------------------------------------
//		RTL8812AU From header
//---------------------------------------------------------------------
		#define RTL8812_FW_IMG						"rtl8812a/FW_NIC.bin"
		#define RTL8812_FW_WW_IMG				"rtl8812a/FW_WoWLAN.bin"
		#define RTL8812_PHY_REG					"rtl8812a/PHY_REG.txt" 
		#define RTL8812_PHY_RADIO_A				"rtl8812a/RadioA.txt"
		#define RTL8812_PHY_RADIO_B				"rtl8812a/RadioB.txt"
		#define RTL8812_TXPWR_TRACK				"rtl8812a/TxPowerTrack.txt"			
		#define RTL8812_AGC_TAB					"rtl8812a/AGC_TAB.txt"
		#define RTL8812_PHY_MACREG 				"rtl8812a/MAC_REG.txt"
		#define RTL8812_PHY_REG_PG					"rtl8812a/PHY_REG_PG.txt"
		#define RTL8812_PHY_REG_MP 				"rtl8812a/PHY_REG_MP.txt" 
		#define RTL8812_TXPWR_LMT					"rtl8812a/TXPWR_LMT.txt" 

//---------------------------------------------------------------------
//		RTL8821U From file
//---------------------------------------------------------------------
		#define RTL8821_FW_IMG						"rtl8821a/FW_NIC.bin"
		#define RTL8821_FW_WW_IMG				"rtl8821a/FW_WoWLAN.bin"
		#define RTL8821_PHY_REG					"rtl8821a/PHY_REG.txt" 
		#define RTL8821_PHY_RADIO_A				"rtl8821a/RadioA.txt"
		#define RTL8821_PHY_RADIO_B				"rtl8821a/RadioB.txt" 
		#define RTL8821_TXPWR_TRACK				"rtl8821a/TxPowerTrack.txt"		
		#define RTL8821_AGC_TAB					"rtl8821a/AGC_TAB.txt"
		#define RTL8821_PHY_MACREG 				"rtl8821a/MAC_REG.txt"
		#define RTL8821_PHY_REG_PG					"rtl8821a/PHY_REG_PG.txt"
		#define RTL8821_PHY_REG_MP 				"rtl8821a/PHY_REG_MP.txt"
		#define RTL8821_TXPWR_LMT					"rtl8821a/TXPWR_LMT.txt" 

//---------------------------------------------------------------------
//		RTL8812 Power Configuration CMDs for PCIe interface
//---------------------------------------------------------------------
#define Rtl8812_NIC_PWR_ON_FLOW				rtl8812_power_on_flow
#define Rtl8812_NIC_RF_OFF_FLOW				rtl8812_radio_off_flow
#define Rtl8812_NIC_DISABLE_FLOW				rtl8812_card_disable_flow
#define Rtl8812_NIC_ENABLE_FLOW				rtl8812_card_enable_flow
#define Rtl8812_NIC_SUSPEND_FLOW				rtl8812_suspend_flow
#define Rtl8812_NIC_RESUME_FLOW				rtl8812_resume_flow
#define Rtl8812_NIC_PDN_FLOW					rtl8812_hwpdn_flow
#define Rtl8812_NIC_LPS_ENTER_FLOW			rtl8812_enter_lps_flow
#define Rtl8812_NIC_LPS_LEAVE_FLOW				rtl8812_leave_lps_flow		

//---------------------------------------------------------------------
//		RTL8821 Power Configuration CMDs for PCIe interface
//---------------------------------------------------------------------
#define Rtl8821A_NIC_PWR_ON_FLOW				rtl8821A_power_on_flow
#define Rtl8821A_NIC_RF_OFF_FLOW				rtl8821A_radio_off_flow
#define Rtl8821A_NIC_DISABLE_FLOW				rtl8821A_card_disable_flow
#define Rtl8821A_NIC_ENABLE_FLOW				rtl8821A_card_enable_flow
#define Rtl8821A_NIC_SUSPEND_FLOW				rtl8821A_suspend_flow
#define Rtl8821A_NIC_RESUME_FLOW				rtl8821A_resume_flow
#define Rtl8821A_NIC_PDN_FLOW					rtl8821A_hwpdn_flow
#define Rtl8821A_NIC_LPS_ENTER_FLOW			rtl8821A_enter_lps_flow
#define Rtl8821A_NIC_LPS_LEAVE_FLOW			rtl8821A_leave_lps_flow	


#if 1 // download firmware related data structure
#define FW_SIZE_8812			0x8000 // Compatible with RTL8723 Maximal RAM code size 24K.   modified to 32k, TO compatible with 92d maximal fw size 32k
#define FW_START_ADDRESS		0x1000
#define FW_END_ADDRESS		0x5FFF



typedef struct _RT_FIRMWARE_8812 {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szFwBuffer;
#else
	u8			szFwBuffer[FW_SIZE_8812];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8812, *PRT_FIRMWARE_8812;

//
// This structure must be cared byte-ordering
//
// Added by tynli. 2009.12.04.
#define IS_FW_HEADER_EXIST_8812(_pFwHdr)	((GET_FIRMWARE_HDR_SIGNATURE_8812(_pFwHdr) &0xFFF0) ==  0x9500)

#define IS_FW_HEADER_EXIST_8821(_pFwHdr)	((GET_FIRMWARE_HDR_SIGNATURE_8812(_pFwHdr) &0xFFF0) ==  0x2100)
//=====================================================
//					Firmware Header(8-byte alinment required)
//=====================================================
//--- LONG WORD 0 ----
#define GET_FIRMWARE_HDR_SIGNATURE_8812(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 0, 16) // 92C0: test chip; 92C, 88C0: test chip; 88C1: MP A-cut; 92C1: MP A-cut
#define GET_FIRMWARE_HDR_CATEGORY_8812(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 16, 8) // AP/NIC and USB/PCI
#define GET_FIRMWARE_HDR_FUNCTION_8812(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 24, 8) // Reserved for different FW function indcation, for further use when driver needs to download different FW in different conditions
#define GET_FIRMWARE_HDR_VERSION_8812(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+4, 0, 16)// FW Version
#define GET_FIRMWARE_HDR_SUB_VER_8812(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+4, 16, 8) // FW Subversion, default 0x00
#define GET_FIRMWARE_HDR_RSVD1_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+4, 24, 8) 		

//--- LONG WORD 1 ----
#define GET_FIRMWARE_HDR_MONTH_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 0, 8) // Release time Month field
#define GET_FIRMWARE_HDR_DATE_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 8, 8) // Release time Date field
#define GET_FIRMWARE_HDR_HOUR_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 16, 8)// Release time Hour field
#define GET_FIRMWARE_HDR_MINUTE_8812(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+8, 24, 8)// Release time Minute field
#define GET_FIRMWARE_HDR_ROMCODE_SIZE_8812(__FwHdr)	LE_BITS_TO_4BYTE(__FwHdr+12, 0, 16)// The size of RAM code
#define GET_FIRMWARE_HDR_RSVD2_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+12, 16, 16)

//--- LONG WORD 2 ----
#define GET_FIRMWARE_HDR_SVN_IDX_8812(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+16, 0, 32)// The SVN entry index
#define GET_FIRMWARE_HDR_RSVD3_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+20, 0, 32)

//--- LONG WORD 3 ----
#define GET_FIRMWARE_HDR_RSVD4_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+24, 0, 32)
#define GET_FIRMWARE_HDR_RSVD5_8812(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+28, 0, 32)

#endif // download firmware related data structure


#define DRIVER_EARLY_INT_TIME_8812		0x05
#define BCN_DMA_ATIME_INT_TIME_8812		0x02

//for 8812
// TX 128K, RX 16K, Page size 512B for TX, 128B for RX
#define MAX_RX_DMA_BUFFER_SIZE_8812	0x3E80   //0x3FFF	// RX 16K

#define BCNQ_PAGE_NUM_8812		0x07

//For WoWLan , more reserved page
//ARP Rsp:1, RWC:1, GTK Info:1,GTK RSP:1,GTK EXT MEM:1, PNO: 6
#ifdef CONFIG_WOWLAN
#define WOWLAN_PAGE_NUM_8812	0x05
#else
#define WOWLAN_PAGE_NUM_8812	0x00
#endif

#define TX_TOTAL_PAGE_NUMBER_8812	(0xFF - BCNQ_PAGE_NUM_8812 - WOWLAN_PAGE_NUM_8812)
#define TX_PAGE_BOUNDARY_8812			(TX_TOTAL_PAGE_NUMBER_8812 + 1)

#define TX_PAGE_BOUNDARY_WOWLAN_8812		0xE0

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8812	TX_PAGE_BOUNDARY_8812
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8812		(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8812 + 1)

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8812
#define NORMAL_PAGE_NUM_LPQ_8812				0x10
#define NORMAL_PAGE_NUM_HPQ_8812			0x10
#define NORMAL_PAGE_NUM_NPQ_8812			0x00

#define WMM_NORMAL_PAGE_NUM_HPQ_8812		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8812		0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8812		0x20


// for 8821A
// TX 64K, RX 16K, Page size 256B for TX, 128B for RX
#define PAGE_SIZE_TX_8821A					256
#define PAGE_SIZE_RX_8821A					128

#define MAX_RX_DMA_BUFFER_SIZE_8821			0x3E80	// RX 16K

#define BCNQ_PAGE_NUM_8821		0x08
#ifdef CONFIG_CONCURRENT_MODE
#define BCNQ1_PAGE_NUM_8821		0x04
#else
#define BCNQ1_PAGE_NUM_8821		0x00
#endif

//For WoWLan , more reserved page
//ARP Rsp:1, RWC:1, GTK Info:1,GTK RSP:1,GTK EXT MEM:1, PNO: 6
#ifdef CONFIG_WOWLAN
#define WOWLAN_PAGE_NUM_8821	0x06
#else
#define WOWLAN_PAGE_NUM_8821	0x00
#endif

#define TX_TOTAL_PAGE_NUMBER_8821	(0xFF - BCNQ_PAGE_NUM_8821 - BCNQ1_PAGE_NUM_8821 - WOWLAN_PAGE_NUM_8821)
#define TX_PAGE_BOUNDARY_8821				(TX_TOTAL_PAGE_NUMBER_8821 + 1)
//#define TX_PAGE_BOUNDARY_WOWLAN_8821		0xE0

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8821	TX_TOTAL_PAGE_NUMBER_8821
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8821		(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8821 + 1)


// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER
#define NORMAL_PAGE_NUM_LPQ_8821			0x08//0x10
#define NORMAL_PAGE_NUM_HPQ_8821		0x08//0x10
#define NORMAL_PAGE_NUM_NPQ_8821		0x00

#define WMM_NORMAL_PAGE_NUM_HPQ_8821		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ_8821		0x20
#define WMM_NORMAL_PAGE_NUM_NPQ_8821		0x20


#define	EFUSE_HIDDEN_812AU					0
#define	EFUSE_HIDDEN_812AU_VS				1
#define	EFUSE_HIDDEN_812AU_VL				2
#define	EFUSE_HIDDEN_812AU_VN				3

#ifdef CONFIG_PCI_HCI
#define EFUSE_REAL_CONTENT_LEN_JAGUAR		1024
#define HWSET_MAX_SIZE_JAGUAR					1024
#else
#define EFUSE_REAL_CONTENT_LEN_JAGUAR		512
#define HWSET_MAX_SIZE_JAGUAR					512
#endif

#define EFUSE_MAX_BANK_8812A					2
#define EFUSE_MAP_LEN_JAGUAR					512
#define EFUSE_MAX_SECTION_JAGUAR				64
#define EFUSE_MAX_WORD_UNIT_JAGUAR			4
#define EFUSE_IC_ID_OFFSET_JAGUAR				506	//For some inferiority IC purpose. added by Roger, 2009.09.02.
#define AVAILABLE_EFUSE_ADDR_8812(addr) 	(addr < EFUSE_REAL_CONTENT_LEN_JAGUAR)
// <Roger_Notes> To prevent out of boundary programming case, leave 1byte and program full section
// 9bytes + 1byt + 5bytes and pre 1byte.
// For worst case:
// | 2byte|----8bytes----|1byte|--7bytes--| //92D
#define EFUSE_OOB_PROTECT_BYTES_JAGUAR		18 	// PG data exclude header, dummy 7 bytes frome CP test and reserved 1byte.
#define EFUSE_PROTECT_BYTES_BANK_JAGUAR		16
// Added for different registry settings to adjust TxPwr index. added by Roger, 2010.03.09.
typedef enum _TX_PWR_PERCENTAGE{
	TX_PWR_PERCENTAGE_0 = 0x01, // 12.5%
	TX_PWR_PERCENTAGE_1 = 0x02, // 25%
	TX_PWR_PERCENTAGE_2 = 0x04, // 50%
	TX_PWR_PERCENTAGE_3 = 0x08, //100%, default target output power.	
} TX_PWR_PERCENTAGE;

#define INCLUDE_MULTI_FUNC_BT(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

//#define IS_MULTI_FUNC_CHIP(_Adapter)	(((((PHAL_DATA_TYPE)(_Adapter->HalData))->MultiFunc) & (RT_MULTI_FUNC_BT|RT_MULTI_FUNC_GPS)) ? _TRUE : _FALSE)

//#define RT_IS_FUNC_DISABLED(__pAdapter, __FuncBits) ( (__pAdapter)->DisabledFunctions & (__FuncBits) )

#define GetRegTxBBSwing_2G(_Adapter)	(_Adapter->registrypriv.TxBBSwing_2G)
#define GetRegTxBBSwing_5G(_Adapter)	(_Adapter->registrypriv.TxBBSwing_5G)

#define GetRegAmplifierType2G(_Adapter)	(_Adapter->registrypriv.AmplifierType_2G)
#define GetRegAmplifierType5G(_Adapter)	(_Adapter->registrypriv.AmplifierType_5G)

#define GetRegbENRFEType(_Adapter)	(_Adapter->registrypriv.bEn_RFE)
#define GetRegRFEType(_Adapter)	(_Adapter->registrypriv.RFE_Type)

#define GetDefaultAdapter(padapter)	padapter

// rtl8812_hal_init.c
void	_8051Reset8812(PADAPTER padapter);
s32	FirmwareDownload8812(PADAPTER Adapter, BOOLEAN bUsedWoWLANFw);
void	InitializeFirmwareVars8812(PADAPTER padapter);

s32	_LLTWrite_8812A(PADAPTER Adapter, u32 address, u32 data);
s32	InitLLTTable8812A(PADAPTER padapter, u8 txpktbuf_bndy);
void InitRDGSetting8812A(PADAPTER padapter);

void CheckAutoloadState8812A(PADAPTER padapter);

// EFuse
u8	GetEEPROMSize8812A(PADAPTER padapter);
void InitPGData8812A(PADAPTER padapter);
void	Hal_EfuseParseIDCode8812A(PADAPTER padapter, u8 *hwinfo);
void	Hal_ReadPROMVersion8812A(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void	Hal_ReadTxPowerInfo8812A(PADAPTER padapter, u8* hwinfo,BOOLEAN	AutoLoadFail);
void	Hal_ReadBoardType8812A(PADAPTER pAdapter, u8* hwinfo,BOOLEAN AutoLoadFail);
void	Hal_ReadThermalMeter_8812A(PADAPTER	Adapter, u8* PROMContent,BOOLEAN 	AutoloadFail);
void	Hal_ReadChannelPlan8812A(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void	Hal_EfuseParseXtal_8812A(PADAPTER pAdapter, u8* hwinfo,BOOLEAN AutoLoadFail);
void	Hal_ReadAntennaDiversity8812A(PADAPTER pAdapter,u8* PROMContent,BOOLEAN AutoLoadFail);
void	Hal_ReadAntennaDiversity8821A(PADAPTER pAdapter, u8* PROMContent, BOOLEAN AutoLoadFail);
void	Hal_ReadAmplifierType_8812A(PADAPTER Adapter,u8* PROMContent, BOOLEAN AutoloadFail);
void	Hal_ReadPAType_8821A(PADAPTER Adapter,u8* PROMContent, BOOLEAN AutoloadFail);
void	Hal_ReadRFEType_8812A(PADAPTER Adapter,u8* PROMContent, BOOLEAN AutoloadFail);
void	Hal_EfuseParseBTCoexistInfo8812A(PADAPTER Adapter, u8* hwinfo, BOOLEAN AutoLoadFail);
void	hal_ReadUsbType_8812AU(PADAPTER Adapter, u8 *PROMContent, BOOLEAN AutoloadFail);
int 	FirmwareDownloadBT(PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware);
void	Hal_ReadRemoteWakeup_8812A(PADAPTER padapter, u8* hwinfo, BOOLEAN AutoLoadFail);

BOOLEAN HalDetectPwrDownMode8812(PADAPTER Adapter);
	
#ifdef CONFIG_WOWLAN
void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif //CONFIG_WOWLAN

void _InitBeaconParameters_8812A(PADAPTER padapter);
void SetBeaconRelatedRegisters8812A(PADAPTER padapter);

void ReadRFType8812A(PADAPTER padapter);
void InitDefaultValue8821A(PADAPTER padapter);

void SetHwReg8812A(PADAPTER padapter, u8 variable, u8 *pval);
void GetHwReg8812A(PADAPTER padapter, u8 variable, u8 *pval);
u8 SetHalDefVar8812A(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
u8 GetHalDefVar8812A(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
s32 c2h_id_filter_ccx_8812a(u8 *buf);
void rtl8812_set_hal_ops(struct hal_ops *pHalFunc);

// register
void SetBcnCtrlReg(PADAPTER padapter, u8 SetBits, u8 ClearBits);

void rtl8812_start_thread(PADAPTER padapter);
void rtl8812_stop_thread(PADAPTER padapter);

#ifdef CONFIG_PCI_HCI
BOOLEAN	InterruptRecognized8812AE(PADAPTER Adapter);
VOID	UpdateInterruptMask8812AE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
#endif

#endif //__RTL8188E_HAL_H__

