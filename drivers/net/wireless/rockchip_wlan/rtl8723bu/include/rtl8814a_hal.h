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
#ifndef __RTL8814A_HAL_H__
#define __RTL8814A_HAL_H__

//#include "hal_com.h"
#include "hal_data.h"

//include HAL Related header after HAL Related compiling flags 
#include "rtl8814a_spec.h"
#include "rtl8814a_rf.h"
#include "rtl8814a_dm.h"
#include "rtl8814a_recv.h"
#include "rtl8814a_xmit.h"
#include "rtl8814a_cmd.h"
#include "rtl8814a_led.h"
#include "Hal8814PwrSeq.h"
#include "Hal8814PhyReg.h"
#include "Hal8814PhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8814a_sreset.h"
#endif //DBG_CONFIG_ERROR_DETECT


typedef enum _TX_PWR_PERCENTAGE{
	TX_PWR_PERCENTAGE_0 = 0x01, // 12.5%
	TX_PWR_PERCENTAGE_1 = 0x02, // 25%
	TX_PWR_PERCENTAGE_2 = 0x04, // 50%
	TX_PWR_PERCENTAGE_3 = 0x08, //100%, default target output power.	
} TX_PWR_PERCENTAGE;


enum{
		VOLTAGE_V25						= 0x03,
		LDOE25_SHIFT					= 28 ,
	};
/* max. iram is 64k , max dmen is 32k. Total = 96k = 0x18000*/
#define FW_SIZE							0x18000
#define FW_START_ADDRESS   0x1000
typedef struct _RT_FIRMWARE_8814 {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szFwBuffer;
#else
	u8			szFwBuffer[FW_SIZE];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8814, *PRT_FIRMWARE_8814;

#define PAGE_SIZE_TX_8814	PAGE_SIZE_128
#define BCNQ_PAGE_NUM_8814		0x08

//---------------------------------------------------------------------
//		RTL8814AU From header
//---------------------------------------------------------------------
		#define RTL8814A_FW_IMG					"rtl8814a/FW_NIC.bin"
		#define RTL8814A_FW_WW_IMG				"rtl8814a/FW_WoWLAN.bin"
		#define RTL8814A_PHY_REG					"rtl8814a/PHY_REG.txt" 
		#define RTL8814A_PHY_RADIO_A				"rtl8814a/RadioA.txt"
		#define RTL8814A_PHY_RADIO_B				"rtl8814a/RadioB.txt"
		#define RTL8814A_PHY_RADIO_C				"rtl8814a/RadioC.txt"
		#define RTL8814A_PHY_RADIO_D				"rtl8814a/RadioD.txt"
		#define RTL8814A_TXPWR_TRACK				"rtl8814a/TxPowerTrack.txt"			
		#define RTL8814A_AGC_TAB					"rtl8814a/AGC_TAB.txt"
		#define RTL8814A_PHY_MACREG 				"rtl8814a/MAC_REG.txt"
		#define RTL8814A_PHY_REG_PG				"rtl8814a/PHY_REG_PG.txt"
		#define RTL8814A_PHY_REG_MP 				"rtl8814a/PHY_REG_MP.txt" 
		#define RTL8814A_TXPWR_LMT				"rtl8814a/TXPWR_LMT.txt" 
		#define RTL8814A_WIFI_ANT_ISOLATION		"rtl8814a/wifi_ant_isolation.txt"

#define Rtl8814A_NIC_PWR_ON_FLOW				rtl8814A_power_on_flow
#define Rtl8814A_NIC_RF_OFF_FLOW				rtl8814A_radio_off_flow
#define Rtl8814A_NIC_DISABLE_FLOW				rtl8814A_card_disable_flow
#define Rtl8814A_NIC_ENABLE_FLOW				rtl8814A_card_enable_flow
#define Rtl8814A_NIC_SUSPEND_FLOW				rtl8814A_suspend_flow
#define Rtl8814A_NIC_RESUME_FLOW				rtl8814A_resume_flow
#define Rtl8814A_NIC_PDN_FLOW					rtl8814A_hwpdn_flow
#define Rtl8814A_NIC_LPS_ENTER_FLOW			rtl8814A_enter_lps_flow
#define Rtl8814A_NIC_LPS_LEAVE_FLOW			rtl8814A_leave_lps_flow	

//=====================================================
//				New	Firmware Header(8-byte alinment required)
//=====================================================
//--- LONG WORD 0 ----
#define GET_FIRMWARE_HDR_SIGNATURE_3081(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 0, 16) 
#define GET_FIRMWARE_HDR_CATEGORY_3081(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr, 16, 8) // AP/NIC and USB/PCI
#define GET_FIRMWARE_HDR_FUNCTION_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr, 24, 8) // Reserved for different FW function indcation, for further use when driver needs to download different FW in different conditions
#define GET_FIRMWARE_HDR_VERSION_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+4, 0, 16)// FW Version
#define GET_FIRMWARE_HDR_SUB_VER_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+4, 16, 8) // FW Subversion, default 0x00
#define GET_FIRMWARE_HDR_SUB_IDX_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+4, 24, 8) // FW Subversion Index

//--- LONG WORD 1 ----
#define GET_FIRMWARE_HDR_SVN_IDX_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+8, 0, 32)// The SVN entry index
#define GET_FIRMWARE_HDR_RSVD1_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+12, 0, 32)

//--- LONG WORD 2 ----
#define GET_FIRMWARE_HDR_MONTH_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+16, 0, 8) // Release time Month field
#define GET_FIRMWARE_HDR_DATE_3081(__FwHdr)				LE_BITS_TO_4BYTE(__FwHdr+16, 8, 8) // Release time Date field
#define GET_FIRMWARE_HDR_HOUR_3081(__FwHdr)				LE_BITS_TO_4BYTE(__FwHdr+16, 16, 8)// Release time Hour field
#define GET_FIRMWARE_HDR_MINUTE_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+16, 24, 8)// Release time Minute field
#define GET_FIRMWARE_HDR_YEAR_3081(__FwHdr)				LE_BITS_TO_4BYTE(__FwHdr+20, 0, 16)// Release time Year field
#define GET_FIRMWARE_HDR_FOUNDRY_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+20, 16, 8)// Release time Foundry field
#define GET_FIRMWARE_HDR_RSVD2_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+20, 24, 8)

//--- LONG WORD 3 ----
#define GET_FIRMWARE_HDR_MEM_UASGE_DL_FROM_3081(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+24, 0, 1)
#define GET_FIRMWARE_HDR_MEM_UASGE_BOOT_FROM_3081(__FwHdr)	LE_BITS_TO_4BYTE(__FwHdr+24, 1, 1)
#define GET_FIRMWARE_HDR_MEM_UASGE_BOOT_LOADER_3081(__FwHdr)LE_BITS_TO_4BYTE(__FwHdr+24, 2, 1)
#define GET_FIRMWARE_HDR_MEM_UASGE_IRAM_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+24, 3, 1)
#define GET_FIRMWARE_HDR_MEM_UASGE_ERAM_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+24, 4, 1)
#define GET_FIRMWARE_HDR_MEM_UASGE_RSVD4_3081(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+24, 5, 3)
#define GET_FIRMWARE_HDR_RSVD3_3081(__FwHdr)					LE_BITS_TO_4BYTE(__FwHdr+24, 8, 8)
#define GET_FIRMWARE_HDR_BOOT_LOADER_SZ_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+24, 16, 16)
#define GET_FIRMWARE_HDR_RSVD5_3081(__FwHdr)					LE_BITS_TO_4BYTE(__FwHdr+28, 0, 32)

//--- LONG WORD 4 ----
#define GET_FIRMWARE_HDR_TOTAL_DMEM_SZ_3081(__FwHdr)	LE_BITS_TO_4BYTE(__FwHdr+36, 0, 32)
#define GET_FIRMWARE_HDR_FW_CFG_SZ_3081(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+36, 0, 16)
#define GET_FIRMWARE_HDR_FW_ATTR_SZ_3081(__FwHdr)		LE_BITS_TO_4BYTE(__FwHdr+36, 16, 16)

//--- LONG WORD 5 ----
#define GET_FIRMWARE_HDR_IROM_3081(__FwHdr)				LE_BITS_TO_4BYTE(__FwHdr+40, 0, 32)
#define GET_FIRMWARE_HDR_EROM_3081(__FwHdr)				LE_BITS_TO_4BYTE(__FwHdr+44, 0, 32)

//--- LONG WORD 6 ----
#define GET_FIRMWARE_HDR_IRAM_SZ_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+48, 0, 32)
#define GET_FIRMWARE_HDR_ERAM_SZ_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+52, 0, 32)

//--- LONG WORD 7 ----
#define GET_FIRMWARE_HDR_RSVD6_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+56, 0, 32)
#define GET_FIRMWARE_HDR_RSVD7_3081(__FwHdr)			LE_BITS_TO_4BYTE(__FwHdr+60, 0, 32)



//
// 2013/08/16 MH MOve from SDIO.h for common use.
//
#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_USB_HCI)
#define TRX_SHARE_MODE_8814A				0	//TRX Buffer Share Index
#define BASIC_RXFF_SIZE_8814A				24576//Basic RXFF Size is 24K = 24*1024 Unit: Byte
#define TRX_SHARE_BUFF_UNIT_8814A			65536//TRX Share Buffer unit Size 64K = 64*1024 Unit: Byte
#define TRX_SHARE_BUFF_UNIT_PAGE_8814A	TRX_SHARE_BUFF_UNIT_8814A/PAGE_SIZE_8814A//512 Pages

//Origin: 
#define  HPQ_PGNUM_8814A	 				0x20	//High Queue
#define  LPQ_PGNUM_8814A	 				0x20	//Low Queue
#define  NPQ_PGNUM_8814A	 				0x20	//Normal Queue
#define  EPQ_PGNUM_8814A	 				0x20	//Extra Queue

#else	// #if defined(CONFIG_SDIO_HCI) || defined(CONFIG_USB_HCI)

#define  HPQ_PGNUM_8814A		20
#define  NPQ_PGNUM_8814A		20
#define  LPQ_PGNUM_8814A		20 //1972
#define  EPQ_PGNUM_8814A		20
#define  BCQ_PGNUM_8814A		32

#endif //#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_USB_HCI)

#ifdef CONFIG_WOWLAN
#define WOWLAN_PAGE_NUM_8814	0x00
#else
#define WOWLAN_PAGE_NUM_8814	0x00
#endif

#define PAGE_SIZE_8814A						128//TXFF Page Size, Unit: Byte
#define MAX_RX_DMA_BUFFER_SIZE_8814A		0x5C00	//BASIC_RXFF_SIZE_8814A+TRX_SHARE_MODE_8814A*TRX_SHARE_BUFF_UNIT_8814A //Basic RXFF Size + ShareBuffer Size
#define TX_PAGE_BOUNDARY_8814A			TXPKT_PGNUM_8814A	// Need to enlarge boundary, by KaiYuan
#define TX_PAGE_BOUNDARY_WOWLAN_8814A	TXPKT_PGNUM_8814A	//TODO: 20130415 KaiYuan Check this value later


#define  TOTAL_PGNUM_8814A		2048
#define  TXPKT_PGNUM_8814A		(2048 - BCNQ_PAGE_NUM_8814-WOWLAN_PAGE_NUM_8814)
#define  PUB_PGNUM_8814A		(TXPKT_PGNUM_8814A-HPQ_PGNUM_8814A-NPQ_PGNUM_8814A-LPQ_PGNUM_8814A-EPQ_PGNUM_8814A)

//Note: For WMM Normal Chip Setting ,modify later
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8814A	TX_PAGE_BOUNDARY_8814A
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8814A		(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8814A + 1)

#define DRIVER_EARLY_INT_TIME_8814		0x05
#define BCN_DMA_ATIME_INT_TIME_8814		0x02


#define MAX_PAGE_SIZE			4096	// @ page : 4k bytes

#define EFUSE_MAX_SECTION_JAGUAR				64

#define	HWSET_MAX_SIZE_8814A			512

#define	EFUSE_REAL_CONTENT_LEN_8814A	1024
#define	EFUSE_MAX_BANK_8814A		2

#define	EFUSE_MAP_LEN_8814A			512
#define	EFUSE_MAX_SECTION_8814A		64
#define	EFUSE_MAX_WORD_UNIT_8814A		4
#define	EFUSE_PROTECT_BYTES_BANK_8814A		16

#define	EFUSE_IC_ID_OFFSET_8814A		506	//For some inferiority IC purpose. added by Roger, 2009.09.02.
#define AVAILABLE_EFUSE_ADDR_8814A(addr) 	(addr < EFUSE_REAL_CONTENT_LEN_8814A)

/*-------------------------------------------------------------------------
Chip specific
-------------------------------------------------------------------------*/

/* pic buffer descriptor */
#if 1 /* according to the define in the rtw_xmit.h, rtw_recv.h */
#define RTL8814AE_SEG_NUM  TX_BUFFER_SEG_NUM /* 0:2 seg, 1: 4 seg, 2: 8 seg */
#define TX_DESC_NUM_8814A  TXDESC_NUM   /* 128 */
#define RX_DESC_NUM_8814A  PCI_MAX_RX_COUNT /* 128 */
#ifdef CONFIG_CONCURRENT_MODE
#define BE_QUEUE_TX_DESC_NUM_8814A  (TXDESC_NUM<<1)    /* 256 */
#else
#define BE_QUEUE_TX_DESC_NUM_8814A  (TXDESC_NUM+(TXDESC_NUM>>1)) /* 192 */
#endif
#else
#define RTL8814AE_SEG_NUM  TX_BUFFER_SEG_NUM /* 0:2 seg, 1: 4 seg, 2: 8 seg */
#define TX_DESC_NUM_8814A  128 /* 1024//2048 change by ylb 20130624 */
#define RX_DESC_NUM_8814A  128 /* 1024 //512 change by ylb 20130624 */
#endif

// <Roger_Notes> To prevent out of boundary programming case, leave 1byte and program full section
// 9bytes + 1byt + 5bytes and pre 1byte.
// For worst case:
// | 1byte|----8bytes----|1byte|--5bytes--| 
// |         |            Reserved(14bytes)	      |
//
#define	EFUSE_OOB_PROTECT_BYTES 		15	// PG data exclude header, dummy 6 bytes frome CP test and reserved 1byte.

/* rtl8814_hal_init.c */
s32 FirmwareDownload8814A( PADAPTER	Adapter, BOOLEAN bUsedWoWLANFw);
void	InitializeFirmwareVars8814(PADAPTER padapter);

VOID
Hal_InitEfuseVars_8814A(
	IN	PADAPTER	Adapter
	);

s32 InitLLTTable8814A(
	IN	PADAPTER	Adapter
	);


void InitRDGSetting8814A(PADAPTER padapter);

//void CheckAutoloadState8812A(PADAPTER padapter);

// EFuse
u8	GetEEPROMSize8814A(PADAPTER padapter);
void InitPGData8814A(PADAPTER padapter);

void	hal_ReadPROMVersion8814A(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void	hal_ReadTxPowerInfo8814A(PADAPTER padapter, u8* hwinfo,BOOLEAN	AutoLoadFail);
void	hal_ReadBoardType8814A(PADAPTER pAdapter, u8* hwinfo,BOOLEAN AutoLoadFail);
void	hal_ReadThermalMeter_8814A(PADAPTER	Adapter, u8* PROMContent,BOOLEAN 	AutoloadFail);
void	hal_ReadChannelPlan8814A(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void	hal_EfuseParseXtal_8814A(PADAPTER pAdapter, u8* hwinfo,BOOLEAN AutoLoadFail);
void	hal_ReadAntennaDiversity8814A(PADAPTER pAdapter,u8* PROMContent,BOOLEAN AutoLoadFail);
void	hal_Read_TRX_antenna_8814A(PADAPTER	Adapter, u8 *PROMContent, BOOLEAN AutoloadFail);
VOID hal_ReadAmplifierType_8814A(
	IN	PADAPTER		Adapter	
	);
VOID hal_ReadPAType_8814A(
	IN	PADAPTER	Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail,
	OUT u8*		pPAType, 
	OUT u8*		pLNAType
	);
void hal_GetRxGainOffset_8814A(
	PADAPTER	Adapter,
	pu1Byte		PROMContent,
	BOOLEAN		AutoloadFail
	);
void Hal_EfuseParseKFreeData_8814A(
	IN		PADAPTER		Adapter,
	IN		u8				*PROMContent,
	IN		BOOLEAN			AutoloadFail);
void	hal_ReadRFEType_8814A(PADAPTER Adapter,u8* PROMContent, BOOLEAN AutoloadFail);
void	hal_EfuseParseBTCoexistInfo8814A(PADAPTER Adapter, u8* hwinfo, BOOLEAN AutoLoadFail);

//void	hal_ReadUsbType_8812AU(PADAPTER Adapter, u8 *PROMContent, BOOLEAN AutoloadFail);
//int 	FirmwareDownloadBT(PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware);
void	hal_ReadRemoteWakeup_8814A(PADAPTER padapter, u8* hwinfo, BOOLEAN AutoLoadFail);
u8	MgntQuery_NssTxRate(u16 Rate);

//BOOLEAN HalDetectPwrDownMode8812(PADAPTER Adapter);
	
#ifdef CONFIG_WOWLAN
void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif //CONFIG_WOWLAN

void _InitBeaconParameters_8814A(PADAPTER padapter);
void SetBeaconRelatedRegisters8814A(PADAPTER padapter);

void ReadRFType8814A(PADAPTER padapter);
void InitDefaultValue8814A(PADAPTER padapter);

void SetHwReg8814A(PADAPTER padapter, u8 variable, u8 *pval);
void GetHwReg8814A(PADAPTER padapter, u8 variable, u8 *pval);
u8 SetHalDefVar8814A(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
u8 GetHalDefVar8814A(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval);
s32 c2h_id_filter_ccx_8814a(u8 *buf);
void rtl8814_set_hal_ops(struct hal_ops *pHalFunc);
void init_hal_spec_8814a(_adapter *adapter);

// register
void SetBcnCtrlReg(PADAPTER padapter, u8 SetBits, u8 ClearBits);
void SetBcnCtrlReg(PADAPTER	Adapter, u8	SetBits, u8	ClearBits);
void rtl8814_start_thread(PADAPTER padapter);
void rtl8814_stop_thread(PADAPTER padapter);


#ifdef CONFIG_PCI_HCI
BOOLEAN	InterruptRecognized8814AE(PADAPTER Adapter);
VOID	UpdateInterruptMask8814AE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
u16	get_txbd_idx_addr(u16 ff_hwaddr);
#endif

#ifdef CONFIG_BT_COEXIST
void rtl8812a_combo_card_WifiOnlyHwInit(PADAPTER Adapter);
#endif

#endif //__RTL8188E_HAL_H__

