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
#ifndef __RTL8723B_HAL_H__
#define __RTL8723B_HAL_H__

#include "hal_data.h"

#include "rtl8723b_spec.h"
#include "rtl8723b_rf.h"
#include "rtl8723b_dm.h"
#include "rtl8723b_recv.h"
#include "rtl8723b_xmit.h"
#include "rtl8723b_cmd.h"
#include "rtl8723b_led.h"
#include "Hal8723BPwrSeq.h"
#include "Hal8723BPhyReg.h"
#include "Hal8723BPhyCfg.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8723b_sreset.h"
#endif
#ifdef CONFIG_BT_COEXIST
#include "rtl8723b_bt-coexist.h"
#endif

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)

	//2TODO: We should define 8192S firmware related macro settings here!!
	#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
	#define RTL819X_TOTAL_RF_PATH				2

//---------------------------------------------------------------------
//		RTL8723BS From file
//---------------------------------------------------------------------
	#define RTL8723B_FW_IMG					"rtl8723B\\rtl8723bfw.bin"
	#define RTL8723B_PHY_REG					"rtl8723B\\PHY_REG_1T.txt"
	#define RTL8723B_PHY_RADIO_A				"rtl8723B\\radio_a_1T.txt"
	#define RTL8723B_PHY_RADIO_B				"rtl8723B\\radio_b_1T.txt"
	#define RTL8723B_TXPWR_TRACK				"rtl8723B\\TxPowerTrack.txt" 
	#define RTL8723B_AGC_TAB					"rtl8723B\\AGC_TAB_1T.txt"
	#define RTL8723B_PHY_MACREG 				"rtl8723B\\MAC_REG.txt"
	#define RTL8723B_PHY_REG_PG				"rtl8723B\\PHY_REG_PG.txt"
	#define RTL8723B_PHY_REG_MP				"rtl8723B\\PHY_REG_MP.txt"

//---------------------------------------------------------------------
//		RTL8723BS From header
//---------------------------------------------------------------------

	//#define Rtl8723B_FwImageArray				Array_MP_8723B_FW_NIC
	//#define Rtl8723B_FwImgArrayLength			ArrayLength_MP_8723B_FW_NIC
	//#define Rtl8723B_FwWoWImageArray			Array_MP_8723B_FW_WoWLAN
	//#define Rtl8723B_FwWoWImgArrayLength		ArrayLength_MP_8723B_FW_WoWLAN

	#define Rtl8723B_PHY_REG_Array_PG 			Rtl8723SPHY_REG_Array_PG
	#define Rtl8723B_PHY_REG_Array_PGLength	Rtl8723SPHY_REG_Array_PGLength

#if MP_DRIVER == 1
	#define Rtl8723B_FwBTImgArray				Rtl8723BFwBTImgArray
	#define Rtl8723B_FwBTImgArrayLength		Rtl8723BFwBTImgArrayLength

	#define Rtl8723B_FwMPImageArray			Rtl8723BFwMPImgArray
	#define Rtl8723B_FwMPImgArrayLength		Rtl8723BMPImgArrayLength

	#define Rtl8723B_PHY_REG_Array_MP			Rtl8723B_PHYREG_Array_MP
	#define Rtl8723B_PHY_REG_Array_MPLength	Rtl8723B_PHYREG_Array_MPLength
#endif

#endif // CONFIG_SDIO_HCI

#ifdef CONFIG_USB_HCI

	//2TODO: We should define 8192S firmware related macro settings here!!
	#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
	#define RTL819X_TOTAL_RF_PATH				2

	//TODO:  The following need to check!!
	#define RTL8723B_FW_IMG					"rtl8192CU\\rtl8723bfw.bin"
	#define RTL8723B_PHY_REG					"rtl8723B\\PHY_REG_1T.txt"
	#define RTL8723B_PHY_RADIO_A				"rtl8723B\\radio_a_1T.txt"
	#define RTL8723B_PHY_RADIO_B				"rtl8723B\\radio_b_1T.txt"
	#define RTL8723B_TXPWR_TRACK				"rtl8723B\\TxPowerTrack.txt" 
	#define RTL8723B_AGC_TAB					"rtl8723B\\AGC_TAB_1T.txt"
	#define RTL8723B_PHY_MACREG 				"rtl8723B\\MAC_REG.txt"
	#define RTL8723B_PHY_REG_PG				"rtl8723B\\PHY_REG_PG.txt"
	#define RTL8723B_PHY_REG_MP				"rtl8723B\\PHY_REG_MP.txt"

//---------------------------------------------------------------------
//		RTL8723S From header
//---------------------------------------------------------------------

	// Fw Array
	//#define Rtl8723B_FwImageArray				Rtl8723UFwImgArray
	//#define Rtl8723_FwUMCBCutImageArray		Rtl8723UFwUMCBCutImgArray

	//#define Rtl8723B_ImgArrayLength				Rtl8723UImgArrayLength
	//#define Rtl8723B_UMCBCutImgArrayLength		Rtl8723UUMCBCutImgArrayLength

	//#define Rtl8723B_PHY_REG_Array_PG 			Rtl8723UPHY_REG_Array_PG
	//#define Rtl8723B_PHY_REG_Array_PGLength	Rtl8723UPHY_REG_Array_PGLength

#if MP_DRIVER == 1
	#define Rtl8723B_FwBTImgArray				Rtl8723BFwBTImgArray
	#define Rtl8723B_FwBTImgArrayLength		Rtl8723BFwBTImgArrayLength

	#define Rtl8723B_FwMPImageArray			Rtl8723BFwMPImgArray
	#define Rtl8723B_FwMPImgArrayLength		Rtl8723BMPImgArrayLength

	#define Rtl8723B_PHY_REG_Array_MP			Rtl8723B_PHY_REG_Array_MP
	#define Rtl8723B_PHY_REG_Array_MPLength	Rtl8723B_PHY_REG_Array_MPLength
#endif
#endif

#define FW_8723B_SIZE			0x8000
#define FW_8723B_START_ADDRESS	0x1000
#define FW_8723B_END_ADDRESS		0x1FFF //0x5FFF

#define IS_FW_HEADER_EXIST_8723B(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x5300)

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szFwBuffer;
#else
	u8			szFwBuffer[FW_8723B_SIZE];
#endif
	u32			ulFwLength;

#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szBTFwBuffer;
#else
	u8			szBTFwBuffer[FW_8723B_SIZE];
#endif
	u32			ulBTFwLength;

#ifdef CONFIG_WOWLAN_8723
	u8*			szWoWLANFwBuffer;
	u32			ulWoWLANFwLength;
#endif //CONFIG_WOWLAN_8723
} RT_FIRMWARE_8723B, *PRT_FIRMWARE_8723B;

//
// This structure must be cared byte-ordering
//
// Added by tynli. 2009.12.04.
typedef struct _RT_8723B_FIRMWARE_HDR
{
	// 8-byte alinment required

	//--- LONG WORD 0 ----
	u16		Signature;	// 92C0: test chip; 92C, 88C0: test chip; 88C1: MP A-cut; 92C1: MP A-cut
	u8		Category;	// AP/NIC and USB/PCI
	u8		Function;	// Reserved for different FW function indcation, for further use when driver needs to download different FW in different conditions
	u16		Version;		// FW Version
	u8		Subversion;	// FW Subversion, default 0x00
	u16		Rsvd1;


	//--- LONG WORD 1 ----
	u8		Month;	// Release time Month field
	u8		Date;	// Release time Date field
	u8		Hour;	// Release time Hour field
	u8		Minute;	// Release time Minute field
	u16		RamCodeSize;	// The size of RAM code
	u16		Rsvd2;

	//--- LONG WORD 2 ----
	u32		SvnIdx;	// The SVN entry index
	u32		Rsvd3;

	//--- LONG WORD 3 ----
	u32		Rsvd4;
	u32		Rsvd5;
}RT_8723B_FIRMWARE_HDR, *PRT_8723B_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME_8723B		0x05
#define BCN_DMA_ATIME_INT_TIME_8723B		0x02

// Note: We will divide number of page equally for each queue other than public queue!
#ifdef CONFIG_WOWLAN_8723
#define TX_TOTAL_PAGE_NUMBER_8723B	0xF3
#else
#define TX_TOTAL_PAGE_NUMBER_8723B	0xF8
#endif //CONFIG_WOWLAN_8723
#define TX_PAGE_BOUNDARY_8723B		(TX_TOTAL_PAGE_NUMBER_8723B + 1)

#ifdef CONFIG_WOWLAN_8723
// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8723B
#define NORMAL_PAGE_NUM_PUBQ_8723B	0xE2
#define NORMAL_PAGE_NUM_HPQ_8723B		0x0C
#define NORMAL_PAGE_NUM_LPQ_8723B		0x02
#define NORMAL_PAGE_NUM_NPQ_8723B		0x02
#else
// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8723B
#define NORMAL_PAGE_NUM_PUBQ_8723B	0xE7
#define NORMAL_PAGE_NUM_HPQ_8723B		0x0C
#define NORMAL_PAGE_NUM_LPQ_8723B		0x02
#define NORMAL_PAGE_NUM_NPQ_8723B		0x02
#endif //CONFIG_WOWLAN_8723
// For Test Chip Setting
// (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8723B
#define TEST_PAGE_NUM_PUBQ		0x7E

// For Test Chip Setting
#define WMM_TEST_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_TEST_TX_PAGE_BOUNDARY		(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) //F6

#define WMM_TEST_PAGE_NUM_PUBQ		0xA3
#define WMM_TEST_PAGE_NUM_HPQ		0x29
#define WMM_TEST_PAGE_NUM_LPQ		0x29

#ifdef CONFIG_WOWLAN_8723
// Note: For Normal Chip Setting, modify later
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER	0xF3
#define WMM_NORMAL_TX_PAGE_BOUNDARY		(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) //F6

#define WMM_NORMAL_PAGE_NUM_PUBQ_8723B	0xAE
#define WMM_NORMAL_PAGE_NUM_HPQ_8723B		0x29
#define WMM_NORMAL_PAGE_NUM_LPQ_8723B		0x1C
#define WMM_NORMAL_PAGE_NUM_NPQ_8723B		0x1C
#else
// Note: For Normal Chip Setting, modify later
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_NORMAL_TX_PAGE_BOUNDARY		(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) //F6

#define WMM_NORMAL_PAGE_NUM_PUBQ_8723B	0xB0
#define WMM_NORMAL_PAGE_NUM_HPQ_8723B		0x29
#define WMM_NORMAL_PAGE_NUM_LPQ_8723B		0x1C
#define WMM_NORMAL_PAGE_NUM_NPQ_8723B		0x1C
#endif //CONFIG_WOWLAN_8723


#include "HalVerDef.h"
#include "hal_com.h"

#define EFUSE_OOB_PROTECT_BYTES 		15

#define HAL_EFUSE_MEMORY

#define HWSET_MAX_SIZE_8723B			512
#define EFUSE_REAL_CONTENT_LEN_8723B		512
#define EFUSE_MAP_LEN_8723B				512
#define EFUSE_MAX_SECTION_8723B			64

#define EFUSE_IC_ID_OFFSET			506	//For some inferiority IC purpose. added by Roger, 2009.09.02.
#define AVAILABLE_EFUSE_ADDR(addr) 	(addr < EFUSE_REAL_CONTENT_LEN_8723B)

#define EFUSE_ACCESS_ON			0x69	// For RTL8723 only.
#define EFUSE_ACCESS_OFF			0x00	// For RTL8723 only.

//========================================================
//			EFUSE for BT definition
//========================================================
#define EFUSE_BT_REAL_BANK_CONTENT_LEN	512
#define EFUSE_BT_REAL_CONTENT_LEN		1536	// 512*3
#define EFUSE_BT_MAP_LEN				1024	// 1k bytes
#define EFUSE_BT_MAX_SECTION			128		// 1024/8

#define EFUSE_PROTECT_BYTES_BANK		16

// Description: Determine the types of C2H events that are the same in driver and Fw.
// Fisrt constructed by tynli. 2009.10.09.
typedef enum _C2H_EVT
{
	C2H_DBG = 0,
	C2H_TSF = 1,
	C2H_AP_RPT_RSP = 2,
	C2H_CCX_TX_RPT = 3,	// The FW notify the report of the specific tx packet.
	C2H_BT_RSSI = 4,
	C2H_BT_OP_MODE = 5,
	C2H_EXT_RA_RPT = 6,
	C2H_8723B_BT_INFO = 9,
	C2H_HW_INFO_EXCH = 10,
	C2H_8723B_BT_MP_INFO = 11,
	MAX_C2HEVENT
} C2H_EVT;


#define GET_RF_TYPE(priv)			(GET_HAL_DATA(priv)->rf_type)

#define INCLUDE_MULTI_FUNC_BT(_Adapter)		(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

// rtl8723a_hal_init.c
s32 rtl8723b_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw);
void rtl8723b_FirmwareSelfReset(PADAPTER padapter);
void rtl8723b_InitializeFirmwareVars(PADAPTER padapter);

void rtl8723b_InitAntenna_Selection(PADAPTER padapter);
void rtl8723b_DeinitAntenna_Selection(PADAPTER padapter);
void rtl8723b_CheckAntenna_Selection(PADAPTER padapter);
void rtl8723b_init_default_value(PADAPTER padapter);

s32 InitLLTTable(PADAPTER padapter, u32 boundary);

s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU);
s32 CardDisableWithoutHWSM(PADAPTER padapter);

// EFuse
u8 GetEEPROMSize8723B(PADAPTER padapter);
void Hal_InitPGData(PADAPTER padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(PADAPTER padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8723B(PADAPTER padapter, u8 *PROMContent, BOOLEAN AutoLoadFail);
void Hal_EfuseParseBTCoexistInfo_8723B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseEEPROMVer_8723B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseChnlPlan_8723B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID_8723B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseAntennaDiversity_8723B(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseXtal_8723B(PADAPTER pAdapter, u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8723B(PADAPTER padapter, u8 *hwinfo, u8 AutoLoadFail);

void rtl8723b_set_hal_ops(struct hal_ops *pHalFunc);
void SetHwReg8723B(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8723B(PADAPTER padapter, u8 variable, u8 *val);
#ifdef CONFIG_BT_COEXIST
void rtl8723b_SingleDualAntennaDetection(PADAPTER padapter);
#endif

// register
void SetBcnCtrlReg(PADAPTER padapter, u8 SetBits, u8 ClearBits);
void rtl8723b_InitBeaconParameters(PADAPTER padapter);
void rtl8723b_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);
#ifdef CONFIG_WOWLAN_8723
void _8051Reset8723(PADAPTER padapter);
void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif //CONFIG_WOWLAN_8723

void rtl8723b_clone_haldata(_adapter *dst_adapter, _adapter *src_adapter);
void rtl8723b_start_thread(_adapter *padapter);
void rtl8723b_stop_thread(_adapter *padapter);

#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
void rtl8723bs_init_checkbthang_workqueue(_adapter * adapter);
void rtl8723bs_free_checkbthang_workqueue(_adapter * adapter);
void rtl8723bs_cancle_checkbthang_workqueue(_adapter * adapter);
void rtl8723bs_hal_check_bt_hang(_adapter * adapter);
#endif

#ifdef CONFIG_WOWLAN_8723
void rtw_get_current_ip_address(PADAPTER padapter, u8 *pcurrentip);
void rtw_get_sec_iv(PADAPTER padapter, u8*pcur_dot11txpn, u8 *StaAddr);
#endif

s32 c2h_id_filter_ccx_8723b(u8 id);
s32 c2h_handler_8723b(PADAPTER padapter, struct c2h_evt_hdr *pC2hEvent);

#endif

