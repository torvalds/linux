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
#ifndef __RTL8723A_HAL_H__
#define __RTL8723A_HAL_H__


//#include "hal_com.h"
#include "hal_data.h"

#include "rtl8723a_spec.h"
#include "rtl8723a_pg.h"
#include "Hal8723APhyReg.h"
#include "Hal8723APhyCfg.h"
#include "rtl8723a_rf.h"
#include "rtl8723a_dm.h"
#include "rtl8723a_recv.h"
#include "rtl8723a_xmit.h"
#include "rtl8723a_cmd.h"
#include "rtl8723a_led.h"
#include "Hal8723PwrSeq.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8723a_sreset.h"
#endif


#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)

	//2TODO: We should define 8192S firmware related macro settings here!!
	#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
	#define RTL819X_TOTAL_RF_PATH				2

//---------------------------------------------------------------------
//		RTL8723S From file
//---------------------------------------------------------------------
	#define RTL8723_FW_UMC_IMG				"rtl8723S\\rtl8723fw.bin"
	#define RTL8723_FW_UMC_B_IMG			"rtl8723S\\rtl8723fw_B.bin"
	#define RTL8723_PHY_REG					"rtl8723S\\PHY_REG_1T.txt"
	#define RTL8723_PHY_RADIO_A				"rtl8723S\\radio_a_1T.txt"
	#define RTL8723_PHY_RADIO_B				"rtl8723S\\radio_b_1T.txt"
	#define RTL8723_AGC_TAB					"rtl8723S\\AGC_TAB_1T.txt"
	#define RTL8723_PHY_MACREG 				"rtl8723S\\MAC_REG.txt"
	#define RTL8723_PHY_REG_PG				"rtl8723S\\PHY_REG_PG.txt"
	#define RTL8723_PHY_REG_MP				"rtl8723S\\PHY_REG_MP.txt"

//---------------------------------------------------------------------
//		RTL8723S From header
//---------------------------------------------------------------------

	// Fw Array
	#define Rtl8723_FwImageArray				Rtl8723SFwImgArray
	#define Rtl8723_FwUMCBCutImageArrayWithBT		Rtl8723SFwUMCBCutImgArrayWithBT
	#define Rtl8723_FwUMCBCutImageArrayWithoutBT	Rtl8723SFwUMCBCutImgArrayWithoutBT

	#define Rtl8723_ImgArrayLength				Rtl8723SImgArrayLength
	#define Rtl8723_UMCBCutImgArrayWithBTLength		Rtl8723SUMCBCutImgArrayWithBTLength
	#define Rtl8723_UMCBCutImgArrayWithoutBTLength	Rtl8723SUMCBCutImgArrayWithoutBTLength

	#define Rtl8723_PHY_REG_Array_PG 			Rtl8723SPHY_REG_Array_PG
	#define Rtl8723_PHY_REG_Array_PGLength		Rtl8723SPHY_REG_Array_PGLength
#if MP_DRIVER == 1
	#define Rtl8723E_FwBTImgArray				Rtl8723EFwBTImgArray
	#define Rtl8723E_FwBTImgArrayLength			Rtl8723EBTImgArrayLength

	#define Rtl8723_FwUMCBCutMPImageArray		Rtl8723SFwUMCBCutMPImgArray
	#define Rtl8723_UMCBCutMPImgArrayLength 	Rtl8723SUMCBCutMPImgArrayLength

	#define Rtl8723_PHY_REG_Array_MP			Rtl8723SPHY_REG_Array_MP
	#define Rtl8723_PHY_REG_Array_MPLength		Rtl8723SPHY_REG_Array_MPLength
#endif

#endif // CONFIG_SDIO_HCI

#ifdef CONFIG_USB_HCI

	//2TODO: We should define 8192S firmware related macro settings here!!
	#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
	#define RTL819X_TOTAL_RF_PATH				2

	//TODO:  The following need to check!!
	#define RTL8723_FW_UMC_IMG				"rtl8192CU\\rtl8723fw.bin"
	#define RTL8723_FW_UMC_B_IMG			"rtl8192CU\\rtl8723fw_B.bin"
	#define RTL8723_PHY_REG					"rtl8723S\\PHY_REG_1T.txt"
	#define RTL8723_PHY_RADIO_A				"rtl8723S\\radio_a_1T.txt"
	#define RTL8723_PHY_RADIO_B				"rtl8723S\\radio_b_1T.txt"
	#define RTL8723_AGC_TAB					"rtl8723S\\AGC_TAB_1T.txt"
	#define RTL8723_PHY_MACREG 				"rtl8723S\\MAC_REG.txt"
	#define RTL8723_PHY_REG_PG				"rtl8723S\\PHY_REG_PG.txt"
	#define RTL8723_PHY_REG_MP				"rtl8723S\\PHY_REG_MP.txt"

//---------------------------------------------------------------------
//		RTL8723S From header
//---------------------------------------------------------------------

	// Fw Array
	#define Rtl8723_FwImageArray				Rtl8723UFwImgArray
	#define Rtl8723_FwUMCBCutImageArrayWithBT		Rtl8723UFwUMCBCutImgArrayWithBT
	#define Rtl8723_FwUMCBCutImageArrayWithoutBT	Rtl8723UFwUMCBCutImgArrayWithoutBT

	#define Rtl8723_ImgArrayLength				Rtl8723UImgArrayLength
	#define Rtl8723_UMCBCutImgArrayWithBTLength		Rtl8723UUMCBCutImgArrayWithBTLength
	#define Rtl8723_UMCBCutImgArrayWithoutBTLength	Rtl8723UUMCBCutImgArrayWithoutBTLength

	#define Rtl8723_PHY_REG_Array_PG 			Rtl8723UPHY_REG_Array_PG
	#define Rtl8723_PHY_REG_Array_PGLength		Rtl8723UPHY_REG_Array_PGLength

#if MP_DRIVER == 1
	#define Rtl8723E_FwBTImgArray				Rtl8723EFwBTImgArray
	#define Rtl8723E_FwBTImgArrayLength			Rtl8723EBTImgArrayLength

	#define Rtl8723_FwUMCBCutMPImageArray		Rtl8723SFwUMCBCutMPImgArray
	#define Rtl8723_UMCBCutMPImgArrayLength	    Rtl8723SUMCBCutMPImgArrayLength

	#define Rtl8723_PHY_REG_Array_MP			Rtl8723UPHY_REG_Array_MP
	#define Rtl8723_PHY_REG_Array_MPLength		Rtl8723UPHY_REG_Array_MPLength
#endif
#endif


#define FW_8723A_SIZE			0x8000
#define FW_8723A_START_ADDRESS	0x1000
#define FW_8723A_END_ADDRESS		0x1FFF //0x5FFF


#define IS_FW_HEADER_EXIST_8723A(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300)


typedef struct _RT_FIRMWARE_8723A {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szFwBuffer;
#else
	u8			szFwBuffer[FW_8723A_SIZE];
#endif
	u32			ulFwLength;
} RT_FIRMWARE_8723A, *PRT_FIRMWARE_8723A;

//
// This structure must be cared byte-ordering
//
// Added by tynli. 2009.12.04.
typedef struct _RT_8723A_FIRMWARE_HDR
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
}RT_8723A_FIRMWARE_HDR, *PRT_8723A_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME_8723A		0x05
#define BCN_DMA_ATIME_INT_TIME_8723A		0x02

//For General Reserved Page Number(Beacon Queue is reserved page)
//Beacon:2, PS-Poll:1, Null Data:1,Qos Null Data:1,BT Qos Null Data:1
#define BCNQ_PAGE_NUM_8723A		0x08

#define TX_TOTAL_PAGE_NUMBER_8723A	(0xFF - BCNQ_PAGE_NUM_8723A)
#define TX_PAGE_BOUNDARY_8723A		(TX_TOTAL_PAGE_NUMBER_8723A + 1)

#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8723A	TX_TOTAL_PAGE_NUMBER_8723A
#define WMM_NORMAL_TX_PAGE_BOUNDARY_8723A		(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER_8723A + 1)

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8723A
#define NORMAL_PAGE_NUM_HPQ_8723A		0x0C
#define NORMAL_PAGE_NUM_LPQ_8723A		0x02
#define NORMAL_PAGE_NUM_NPQ_8723A		0x02

// Note: For Normal Chip Setting, modify later
#define WMM_NORMAL_PAGE_NUM_HPQ_8723A		0x29
#define WMM_NORMAL_PAGE_NUM_LPQ_8723A		0x1C
#define WMM_NORMAL_PAGE_NUM_NPQ_8723A		0x1C


//-------------------------------------------------------------------------
//	Chip specific
//-------------------------------------------------------------------------
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R			0x1
#define CHIP_BONDING_88C_USB_MCARD		0x2
#define CHIP_BONDING_88C_USB_HP			0x1

//-------------------------------------------------------------------------
//	Channel Plan
//-------------------------------------------------------------------------


#define HAL_EFUSE_MEMORY

#define EFUSE_REAL_CONTENT_LEN		512
#define EFUSE_MAP_LEN				128
#define EFUSE_MAX_SECTION			16
#define EFUSE_IC_ID_OFFSET			506	//For some inferiority IC purpose. added by Roger, 2009.09.02.
#define AVAILABLE_EFUSE_ADDR(addr) 	(addr < EFUSE_REAL_CONTENT_LEN)
//
// <Roger_Notes>
// To prevent out of boundary programming case,
// leave 1byte and program full section
// 9bytes + 1byt + 5bytes and pre 1byte.
// For worst case:
// | 1byte|----8bytes----|1byte|--5bytes--|
// |         |            Reserved(14bytes)	      |
//

// PG data exclude header, dummy 6 bytes frome CP test and reserved 1byte.
#define EFUSE_OOB_PROTECT_BYTES 		15

#define EFUSE_REAL_CONTENT_LEN_8723A	512
#define EFUSE_MAP_LEN_8723A				256
#define EFUSE_MAX_SECTION_8723A			32

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
typedef enum _RTL8192C_C2H_EVT
{
	C2H_DBG = 0,
	C2H_TSF = 1,
	C2H_AP_RPT_RSP = 2,
	C2H_CCX_TX_RPT = 3,	// The FW notify the report of the specific tx packet.
	C2H_BT_RSSI = 4,
	C2H_BT_OP_MODE = 5,
	C2H_EXT_RA_RPT = 6,
	C2H_HW_INFO_EXCH = 10,
	C2H_C2H_H2C_TEST = 11,
	C2H_BT_INFO = 12,
	C2H_BT_MP_INFO = 15,
	MAX_C2HEVENT
} RTL8192C_C2H_EVT;


#define INCLUDE_MULTI_FUNC_BT(_Adapter)		(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

typedef struct rxreport_8723a
{
	u32 pktlen:14;
	u32 crc32:1;
	u32 icverr:1;
	u32 drvinfosize:4;
	u32 security:3;
	u32 qos:1;
	u32 shift:2;
	u32 physt:1;
	u32 swdec:1;
	u32 ls:1;
	u32 fs:1;
	u32 eor:1;
	u32 own:1;

	u32 macid:5;
	u32 tid:4;
	u32 hwrsvd:4;
	u32 amsdu:1;
	u32 paggr:1;
	u32 faggr:1;
	u32 a1fit:4;
	u32 a2fit:4;
	u32 pam:1;
	u32 pwr:1;
	u32 md:1;
	u32 mf:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;

	u32 seq:12;
	u32 frag:4;
	u32 nextpktlen:14;
	u32 nextind:1;
	u32 rsvd0831:1;

	u32 rxmcs:6;
	u32 rxht:1;
	u32 gf:1;
	u32 splcp:1;
	u32 bw:1;
	u32 htc:1;
	u32 eosp:1;
	u32 bssidfit:2;
	u32 rsvd1214:16;
	u32 unicastwake:1;
	u32 magicwake:1;

	u32 pattern0match:1;
	u32 pattern1match:1;
	u32 pattern2match:1;
	u32 pattern3match:1;
	u32 pattern4match:1;
	u32 pattern5match:1;
	u32 pattern6match:1;
	u32 pattern7match:1;
	u32 pattern8match:1;
	u32 pattern9match:1;
	u32 patternamatch:1;
	u32 patternbmatch:1;
	u32 patterncmatch:1;
	u32 rsvd1613:19;

	u32 tsfl;

	u32 bassn:12;
	u32 bavld:1;
	u32 rsvd2413:19;
} RXREPORT, *PRXREPORT;

typedef struct phystatus_8723a
{
	u32 rxgain_a:7;
	u32 trsw_a:1;
	u32 rxgain_b:7;
	u32 trsw_b:1;
	u32 chcorr_l:16;

	u32 sigqualcck:8;
	u32 cfo_a:8;
	u32 cfo_b:8;
	u32 chcorr_h:8;

	u32 noisepwrdb_h:8;
	u32 cfo_tail_a:8;
	u32 cfo_tail_b:8;
	u32 rsvd0824:8;

	u32 rsvd1200:8;
	u32 rxevm_a:8;
	u32 rxevm_b:8;
	u32 rxsnr_a:8;

	u32 rxsnr_b:8;
	u32 noisepwrdb_l:8;
	u32 rsvd1616:8;
	u32 postsnr_a:8;

	u32 postsnr_b:8;
	u32 csi_a:8;
	u32 csi_b:8;
	u32 targetcsi_a:8;

	u32 targetcsi_b:8;
	u32 sigevm:8;
	u32 maxexpwr:8;
	u32 exintflag:1;
	u32 sgien:1;
	u32 rxsc:2;
	u32 idlelong:1;
	u32 anttrainen:1;
	u32 antselb:1;
	u32 antsel:1;
} PHYSTATUS, *PPHYSTATUS;


// rtl8723a_hal_init.c
s32 rtl8723a_FirmwareDownload(PADAPTER padapter);
void rtl8723a_FirmwareSelfReset(PADAPTER padapter);
void rtl8723a_InitializeFirmwareVars(PADAPTER padapter);

void rtl8723a_InitAntenna_Selection(PADAPTER padapter);
void rtl8723a_DeinitAntenna_Selection(PADAPTER padapter);
void rtl8723a_CheckAntenna_Selection(PADAPTER padapter);
void rtl8723a_init_default_value(PADAPTER padapter);

s32 InitLLTTable(PADAPTER padapter, u32 boundary);

s32 CardDisableHWSM(PADAPTER padapter, u8 resetMCU);
s32 CardDisableWithoutHWSM(PADAPTER padapter);

// EFuse
u8 GetEEPROMSize8723A(PADAPTER padapter);
void Hal_InitPGData(PADAPTER padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(PADAPTER padapter, u8 *hwinfo);
void Hal_EfuseParseTxPowerInfo_8723A(PADAPTER padapter, u8 *PROMContent, BOOLEAN AutoLoadFail);
void Hal_EfuseParseBTCoexistInfo_8723A(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseEEPROMVer(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void rtl8723a_EfuseParseChnlPlan(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseAntennaDiversity(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseRateIndicationOption(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseXtal_8723A(PADAPTER pAdapter, u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8723A(PADAPTER padapter, u8 *hwinfo, u8 AutoLoadFail);

//RT_CHANNEL_DOMAIN rtl8723a_HalMapChannelPlan(PADAPTER padapter, u8 HalChannelPlan);
//VERSION_8192C rtl8723a_ReadChipVersion(PADAPTER padapter);
//void rtl8723a_ReadBluetoothCoexistInfo(PADAPTER padapter, u8 *PROMContent, BOOLEAN AutoloadFail);
void Hal_InitChannelPlan(PADAPTER padapter);

void rtl8723a_set_hal_ops(struct hal_ops *pHalFunc);
void SetHwReg8723A(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8723A(PADAPTER padapter, u8 variable, u8 *val);
u8 GetHalDefVar8723A(PADAPTER Adapter, HAL_DEF_VARIABLE eVariable, PVOID pValue);
#ifdef CONFIG_BT_COEXIST
void rtl8723a_SingleDualAntennaDetection(PADAPTER padapter);
#endif
int 	FirmwareDownloadBT(PADAPTER Adapter, PRT_MP_FIRMWARE pFirmware);

// register
void SetBcnCtrlReg(PADAPTER padapter, u8 SetBits, u8 ClearBits);
void rtl8723a_InitBeaconParameters(PADAPTER padapter);
void rtl8723a_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);

void rtl8723a_start_thread(_adapter *padapter);
void rtl8723a_stop_thread(_adapter *padapter);

s32 c2h_id_filter_ccx_8723a(u8 *buf);
void _InitTransferPageSize(PADAPTER padapter);
#endif// __RTL8723A_HAL_H__

