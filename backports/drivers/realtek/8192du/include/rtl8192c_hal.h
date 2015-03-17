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
#ifndef __RTL8192C_HAL_H__
#define __RTL8192C_HAL_H__

#include "hal_com.h"
#include "rtl8192c_spec.h"
#include "Hal8192CPhyReg.h"
#include "Hal8192CPhyCfg.h"
#include "rtl8192c_rf.h"
#include "rtl8192c_dm.h"
#include "rtl8192c_recv.h"
#include "rtl8192c_xmit.h"
#include "rtl8192c_cmd.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8192c_sreset.h"
#endif
#include "rtw_efuse.h"

#ifdef CONFIG_PCI_HCI

	#include "Hal8192CEHWImg.h"

	#define RTL819X_DEFAULT_RF_TYPE			RF_2T2R
	//#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
	#define RTL819X_TOTAL_RF_PATH				2

	//2TODO:  The following need to check!!
	#define RTL8192C_FW_TSMC_IMG				"rtl8192CE\\rtl8192cfwT.bin"
	#define RTL8192C_FW_UMC_IMG				"rtl8192CE\\rtl8192cfwU.bin"
	#define RTL8192C_FW_UMC_B_IMG				"rtl8192CE\\rtl8192cfwU_B.bin"

	#define RTL8188C_PHY_REG					"rtl8192CE\\PHY_REG_1T.txt"
	#define RTL8188C_PHY_RADIO_A				"rtl8192CE\\radio_a_1T.txt"
	#define RTL8188C_PHY_RADIO_B				"rtl8192CE\\radio_b_1T.txt"
	#define RTL8188C_AGC_TAB					"rtl8192CE\\AGC_TAB_1T.txt"
	#define RTL8188C_PHY_MACREG				"rtl8192CE\\MACREG_1T.txt"

	#define RTL8192C_PHY_REG					"rtl8192CE\\PHY_REG_2T.txt"
	#define RTL8192C_PHY_RADIO_A				"rtl8192CE\\radio_a_2T.txt"
	#define RTL8192C_PHY_RADIO_B				"rtl8192CE\\radio_b_2T.txt"
	#define RTL8192C_AGC_TAB					"rtl8192CE\\AGC_TAB_2T.txt"
	#define RTL8192C_PHY_MACREG				"rtl8192CE\\MACREG_2T.txt"

	#define RTL819X_PHY_MACPHY_REG			"rtl8192CE\\MACPHY_reg.txt"
	#define RTL819X_PHY_MACPHY_REG_PG		"rtl8192CE\\MACPHY_reg_PG.txt"
	#define RTL819X_PHY_MACREG				"rtl8192CE\\MAC_REG.txt"
	#define RTL819X_PHY_REG					"rtl8192CE\\PHY_REG.txt"
	#define RTL819X_PHY_REG_1T2R				"rtl8192CE\\PHY_REG_1T2R.txt"
	#define RTL819X_PHY_REG_to1T1R				"rtl8192CE\\phy_to1T1R_a.txt"
	#define RTL819X_PHY_REG_to1T2R				"rtl8192CE\\phy_to1T2R.txt"
	#define RTL819X_PHY_REG_to2T2R				"rtl8192CE\\phy_to2T2R.txt"
	#define RTL819X_PHY_REG_PG					"rtl8192CE\\PHY_REG_PG.txt"
	#define RTL819X_AGC_TAB					"rtl8192CE\\AGC_TAB.txt"
	#define RTL819X_PHY_RADIO_A				"rtl8192CE\\radio_a.txt"
	#define RTL819X_PHY_RADIO_A_1T			"rtl8192CE\\radio_a_1t.txt"
	#define RTL819X_PHY_RADIO_A_2T			"rtl8192CE\\radio_a_2t.txt"
	#define RTL819X_PHY_RADIO_B				"rtl8192CE\\radio_b.txt"
	#define RTL819X_PHY_RADIO_B_GM			"rtl8192CE\\radio_b_gm.txt"
	#define RTL819X_PHY_RADIO_C				"rtl8192CE\\radio_c.txt"
	#define RTL819X_PHY_RADIO_D				"rtl8192CE\\radio_d.txt"
	#define RTL819X_EEPROM_MAP				"rtl8192CE\\8192ce.map"
	#define RTL819X_EFUSE_MAP					"rtl8192CE\\8192ce.map"

//---------------------------------------------------------------------
//		RTL8723E From file
//---------------------------------------------------------------------
	#define RTL8723_FW_UMC_IMG				"rtl8723E\\rtl8723fw.bin"
	#define RTL8723_PHY_REG					"rtl8723E\\PHY_REG_1T.txt" 
	#define RTL8723_PHY_RADIO_A				"rtl8723E\\radio_a_1T.txt"
	#define RTL8723_PHY_RADIO_B				"rtl8723E\\radio_b_1T.txt" 
	#define RTL8723_AGC_TAB					"rtl8723E\\AGC_TAB_1T.txt"
	#define RTL8723_PHY_MACREG 				"rtl8723E\\MAC_REG.txt"
	#define RTL8723_PHY_MACREG 				"rtl8723E\\MAC_REG.txt"
	#define RTL8723_PHY_REG_PG					"rtl8723E\\PHY_REG_PG.txt"
	#define RTL8723_PHY_REG_MP 				"rtl8723E\\PHY_REG_MP.txt" 	

	// The file name "_2T" is for 92CE, "_1T"  is for 88CE. Modified by tynli. 2009.11.24.
	#define Rtl819XFwTSMCImageArray			Rtl8192CEFwTSMCImgArray
	#define Rtl819XFwUMCACutImageArray			Rtl8192CEFwUMCACutImgArray
	#define Rtl819XFwUMCBCutImageArray			Rtl8192CEFwUMCBCutImgArray
	
	#define Rtl8723FwUMCImageArray				Rtl8192CEFwUMC8723ImgArray
	#define Rtl819XMAC_Array					Rtl8192CEMAC_2T_Array
	#define Rtl819XAGCTAB_2TArray				Rtl8192CEAGCTAB_2TArray
	#define Rtl819XAGCTAB_1TArray				Rtl8192CEAGCTAB_1TArray
	#define Rtl819XPHY_REG_2TArray				Rtl8192CEPHY_REG_2TArray
	#define Rtl819XPHY_REG_1TArray				Rtl8192CEPHY_REG_1TArray
	#define Rtl819XRadioA_2TArray				Rtl8192CERadioA_2TArray
	#define Rtl819XRadioA_1TArray				Rtl8192CERadioA_1TArray
	#define Rtl819XRadioB_2TArray				Rtl8192CERadioB_2TArray
	#define Rtl819XRadioB_1TArray				Rtl8192CERadioB_1TArray
	#define Rtl819XPHY_REG_Array_PG 			Rtl8192CEPHY_REG_Array_PG
	#define Rtl819XPHY_REG_Array_MP 			Rtl8192CEPHY_REG_Array_MP

#elif defined(CONFIG_USB_HCI)

	#include "Hal8192CUHWImg.h"
#ifdef CONFIG_WOWLAN
	#include "Hal8192CUHWImg_wowlan.h"
#endif //CONFIG_WOWLAN
	//2TODO: We should define 8192S firmware related macro settings here!!
	#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
	#define RTL819X_TOTAL_RF_PATH				2

	//TODO:  The following need to check!!
	#define RTL8192C_FW_TSMC_IMG				"rtl8192CU\\rtl8192cfwT.bin"
	#define RTL8192C_FW_UMC_IMG				"rtl8192CU\\rtl8192cfwU.bin"
	#define RTL8192C_FW_UMC_B_IMG				"rtl8192CU\\rtl8192cfwU_B.bin"
#ifdef CONFIG_WOWLAN
	#define 	RTL8192C_FW_TSMC_WW_IMG			"rtl8192CU\\rtl8192cfwTww.bin"
	#define 	RTL8192C_FW_UMC_WW_IMG			"rtl8192CU\\rtl8192cfwUww.bin"
	#define 	RTL8192C_FW_UMC_B_WW_IMG		"rtl8192CU\\rtl8192cfwU_Bww.bin"
#endif // CONFIG_WOWLAN
	//#define RTL819X_FW_BOOT_IMG   				"rtl8192CU\\boot.img"
	//#define RTL819X_FW_MAIN_IMG				"rtl8192CU\\main.img"
	//#define RTL819X_FW_DATA_IMG				"rtl8192CU\\data.img"

	#define RTL8188C_PHY_REG					"rtl8188CU\\PHY_REG.txt"
	#define RTL8188C_PHY_RADIO_A				"rtl8188CU\\radio_a.txt"
	#define RTL8188C_PHY_RADIO_B				"rtl8188CU\\radio_b.txt"
	#define RTL8188C_PHY_RADIO_A_mCard		"rtl8192CU\\radio_a_1T_mCard.txt"
	#define RTL8188C_PHY_RADIO_B_mCard		"rtl8192CU\\radio_b_1T_mCard.txt" 
	#define RTL8188C_PHY_RADIO_A_HP			"rtl8192CU\\radio_a_1T_HP.txt"
	#define RTL8188C_AGC_TAB					"rtl8188CU\\AGC_TAB.txt"
	#define RTL8188C_PHY_MACREG				"rtl8188CU\\MACREG.txt"

	#define RTL8192C_PHY_REG					"rtl8192CU\\PHY_REG.txt"
	#define RTL8192C_PHY_RADIO_A				"rtl8192CU\\radio_a.txt"
	#define RTL8192C_PHY_RADIO_B				"rtl8192CU\\radio_b.txt"
	#define RTL8192C_AGC_TAB					"rtl8192CU\\AGC_TAB.txt"
	#define RTL8192C_PHY_MACREG				"rtl8192CU\\MACREG.txt"

	#define RTL819X_PHY_REG_PG					"rtl8192CU\\PHY_REG_PG.txt"

//---------------------------------------------------------------------
//		RTL8723U From file
//---------------------------------------------------------------------
	#define RTL8723_FW_UMC_IMG				"rtl8723U\\rtl8723fw.bin"
	#define RTL8723_PHY_REG					"rtl8723U\\PHY_REG_1T.txt" 
	#define RTL8723_PHY_RADIO_A				"rtl8723U\\radio_a_1T.txt"
	#define RTL8723_PHY_RADIO_B				"rtl8723U\\radio_b_1T.txt" 
	#define RTL8723_AGC_TAB					"rtl8723U\\AGC_TAB_1T.txt"
	#define RTL8723_PHY_MACREG 				"rtl8723U\\MAC_REG.txt"
	#define RTL8723_PHY_MACREG 				"rtl8723U\\MAC_REG.txt"
	#define RTL8723_PHY_REG_PG					"rtl8723U\\PHY_REG_PG.txt"
	#define RTL8723_PHY_REG_MP					"rtl8723U\\PHY_REG_MP.txt"	

	// The file name "_2T" is for 92CU, "_1T"  is for 88CU. Modified by tynli. 2009.11.24.
	#define Rtl819XFwImageArray					Rtl8192CUFwTSMCImgArray
	#define Rtl819XFwTSMCImageArray			Rtl8192CUFwTSMCImgArray
	#define Rtl819XFwUMCACutImageArray			Rtl8192CUFwUMCACutImgArray
	#define Rtl819XFwUMCBCutImageArray			Rtl8192CUFwUMCBCutImgArray
#ifdef CONFIG_WOWLAN
	#define Rtl8192C_FwTSMCWWImageArray		Rtl8192CUFwTSMCWWImgArray
	#define Rtl8192C_FwUMCWWImageArray		Rtl8192CUFwUMCACutWWImgArray
	#define Rtl8192C_FwUMCBCutWWImageArray	Rtl8192CUFwUMCBCutWWImgArray		
#endif //CONFIG_WOWLAN
	#define Rtl819XMAC_Array					Rtl8192CUMAC_2T_Array
	#define Rtl819XAGCTAB_2TArray				Rtl8192CUAGCTAB_2TArray
	#define Rtl819XAGCTAB_1TArray				Rtl8192CUAGCTAB_1TArray
	#define Rtl819XAGCTAB_1T_HPArray			Rtl8192CUAGCTAB_1T_HPArray
	#define Rtl819XPHY_REG_2TArray				Rtl8192CUPHY_REG_2TArray
	#define Rtl819XPHY_REG_1TArray				Rtl8192CUPHY_REG_1TArray
	#define Rtl819XPHY_REG_1T_mCardArray		Rtl8192CUPHY_REG_1T_mCardArray 					
	#define Rtl819XPHY_REG_2T_mCardArray		Rtl8192CUPHY_REG_2T_mCardArray	
	#define Rtl819XPHY_REG_1T_HPArray			Rtl8192CUPHY_REG_1T_HPArray
	#define Rtl819XRadioA_2TArray				Rtl8192CURadioA_2TArray
	#define Rtl819XRadioA_1TArray				Rtl8192CURadioA_1TArray
	#define Rtl819XRadioA_1T_mCardArray		Rtl8192CURadioA_1T_mCardArray			
	#define Rtl819XRadioB_2TArray				Rtl8192CURadioB_2TArray
	#define Rtl819XRadioB_1TArray				Rtl8192CURadioB_1TArray	
	#define Rtl819XRadioB_1T_mCardArray		Rtl8192CURadioB_1T_mCardArray
	#define Rtl819XRadioA_1T_HPArray			Rtl8192CURadioA_1T_HPArray	
	#define Rtl819XPHY_REG_Array_PG 			Rtl8192CUPHY_REG_Array_PG
	#define Rtl819XPHY_REG_Array_PG_mCard 		Rtl8192CUPHY_REG_Array_PG_mCard			
	#define Rtl819XPHY_REG_Array_PG_HP			Rtl8192CUPHY_REG_Array_PG_HP
	#define Rtl819XPHY_REG_Array_MP 			Rtl8192CUPHY_REG_Array_MP
#endif

#define DRVINFO_SZ	4 // unit is 8bytes
#define PageNum_128(_Len)		(u32)(((_Len)>>7) + ((_Len)&0x7F ? 1:0))

#define FW_8192C_SIZE					16384+32//16k
#define FW_8192C_START_ADDRESS		0x1000
//#define FW_8192C_END_ADDRESS		0x3FFF //Filen said this is for test chip
#define FW_8192C_END_ADDRESS		0x1FFF

#define MAX_PAGE_SIZE			4096	// @ page : 4k bytes

#define IS_FW_HEADER_EXIST(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300)

typedef enum _FIRMWARE_SOURCE{
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,		//from header file
}FIRMWARE_SOURCE, *PFIRMWARE_SOURCE;

typedef struct _RT_FIRMWARE{
	FIRMWARE_SOURCE	eFWSource;
	u8*			szFwBuffer;
	u32			ulFwLength;
#ifdef CONFIG_WOWLAN
	u8*			szWoWLANFwBuffer;
	u32			ulWoWLANFwLength;
#endif //CONFIG_WOWLAN
}RT_FIRMWARE, *PRT_FIRMWARE, RT_FIRMWARE_92C, *PRT_FIRMWARE_92C;

//
// This structure must be cared byte-ordering
//
// Added by tynli. 2009.12.04.
typedef struct _RT_8192C_FIRMWARE_HDR {//8-byte alinment required

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

}RT_8192C_FIRMWARE_HDR, *PRT_8192C_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME		0x05
#define BCN_DMA_ATIME_INT_TIME		0x02

#ifdef CONFIG_USB_RX_AGGREGATION

typedef enum _USB_RX_AGG_MODE{
	USB_RX_AGG_DISABLE,
	USB_RX_AGG_DMA,
	USB_RX_AGG_USB,
	USB_RX_AGG_MIX
}USB_RX_AGG_MODE;

#define MAX_RX_DMA_BUFFER_SIZE	10240		// 10K for 8192C RX DMA buffer

#endif


#define TX_SELE_HQ			BIT(0)		// High Queue
#define TX_SELE_LQ			BIT(1)		// Low Queue
#define TX_SELE_NQ			BIT(2)		// Normal Queue


// Note: We will divide number of page equally for each queue other than public queue!

#define TX_TOTAL_PAGE_NUMBER		0xF8
#define TX_PAGE_BOUNDARY		(TX_TOTAL_PAGE_NUMBER + 1)

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER
#define NORMAL_PAGE_NUM_PUBQ			0xE7
#define NORMAL_PAGE_NUM_HPQ			0x0C
#define NORMAL_PAGE_NUM_LPQ			0x02
#define NORMAL_PAGE_NUM_NPQ			0x02


// For Test Chip Setting
// (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER
#define TEST_PAGE_NUM_PUBQ		0x7E


// For Test Chip Setting
#define WMM_TEST_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_TEST_TX_PAGE_BOUNDARY	(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) //F6

#define WMM_TEST_PAGE_NUM_PUBQ		0xA3
#define WMM_TEST_PAGE_NUM_HPQ		0x29
#define WMM_TEST_PAGE_NUM_LPQ		0x29


//Note: For Normal Chip Setting ,modify later
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_NORMAL_TX_PAGE_BOUNDARY	(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) //F6

#define WMM_NORMAL_PAGE_NUM_PUBQ		0x65
#define WMM_NORMAL_PAGE_NUM_HPQ		0x30
#define WMM_NORMAL_PAGE_NUM_LPQ			0x30
#define WMM_NORMAL_PAGE_NUM_NPQ		0x30

//-------------------------------------------------------------------------
//	Chip specific
//-------------------------------------------------------------------------
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R	0x1
#define CHIP_BONDING_88C_USB_MCARD	0x2
#define CHIP_BONDING_88C_USB_HP	0x1

//
// 2011.01.06. Define new structure of chip version for RTL8723 and so on. Added by tynli.
//
/*
     | BIT15:12           |  BIT11:8        | BIT 7              |  BIT6:4  |      BIT3          | BIT2:0  |
     |-------------+-----------+-----------+-------+-----------+-------|
     | IC version(CUT)  | ROM version  | Manufacturer  | RF type  |  Chip type       | IC Type |
     |                           |                      | TSMC/UMC    |              | TEST/NORMAL|             |
*/
// [15:12] IC version(CUT): A-cut=0, B-cut=1, C-cut=2, D-cut=3
// [7] Manufacturer: TSMC=0, UMC=1
// [6:4] RF type: 1T1R=0, 1T2R=1, 2T2R=2
// [3] Chip type: TEST=0, NORMAL=1
// [2:0] IC type: 81xxC=0, 8723=1, 92D=2

#define CHIP_8723						BIT(0)
#define CHIP_92D						BIT(1)
#define NORMAL_CHIP  					BIT(3)
#define RF_TYPE_1T1R					(~(BIT(4)|BIT(5)|BIT(6)))
#define RF_TYPE_1T2R					BIT(4)
#define RF_TYPE_2T2R					BIT(5)
#define CHIP_VENDOR_UMC				BIT(7)
#define B_CUT_VERSION					BIT(12)
#define C_CUT_VERSION					BIT(13)
#define D_CUT_VERSION					((BIT(13)|BIT(14)))


// MASK
#define IC_TYPE_MASK					(BIT(0)|BIT(1)|BIT(2))
#define CHIP_TYPE_MASK 				BIT(3)
#define RF_TYPE_MASK					(BIT(4)|BIT(5)|BIT(6))
#define MANUFACTUER_MASK			BIT(7)	
#define ROM_VERSION_MASK				(BIT(11)|BIT(10)|BIT(9)|BIT(8))
#define CUT_VERSION_MASK				(BIT(15)|BIT(14)|BIT(13)|BIT(12))

// Get element
#define GET_CVID_IC_TYPE(version)			((version) & IC_TYPE_MASK)
#define GET_CVID_CHIP_TYPE(version)			((version) & CHIP_TYPE_MASK)
#define GET_CVID_RF_TYPE(version)			((version) & RF_TYPE_MASK)
#define GET_CVID_MANUFACTUER(version)		((version) & MANUFACTUER_MASK)
#define GET_CVID_ROM_VERSION(version)		((version) & ROM_VERSION_MASK)
#define GET_CVID_CUT_VERSION(version)		((version) & CUT_VERSION_MASK)

#define IS_81XXC(version)					((GET_CVID_IC_TYPE(version) == 0)? _TRUE : _FALSE)
#define IS_8723_SERIES(version)				((GET_CVID_IC_TYPE(version) == CHIP_8723)? _TRUE : _FALSE)
#define IS_92D(version)						((GET_CVID_IC_TYPE(version) == CHIP_92D)? _TRUE : _FALSE)
#define IS_1T1R(version)						((GET_CVID_RF_TYPE(version))? _FALSE : _TRUE)
#define IS_1T2R(version)						((GET_CVID_RF_TYPE(version) == RF_TYPE_1T2R)? _TRUE : _FALSE)
#define IS_2T2R(version)						((GET_CVID_RF_TYPE(version) == RF_TYPE_2T2R)? _TRUE : _FALSE)
#define IS_NORMAL_CHIP(version)				((GET_CVID_CHIP_TYPE(version))? _TRUE: _FALSE)
#define IS_CHIP_VENDOR_UMC(version)			((GET_CVID_MANUFACTUER(version))? _TRUE: _FALSE)

#define IS_81XXC_TEST_CHIP(version)			((IS_81XXC(version) && (!IS_NORMAL_CHIP(version)))? _TRUE: _FALSE)
#define IS_92D_TEST_CHIP(version)				((IS_92D(version) && (!IS_NORMAL_CHIP(version)))? _TRUE: _FALSE)
#define IS_92C_SERIAL(version)   				((IS_81XXC(version) && IS_2T2R(version)) ? _TRUE : _FALSE)
#define IS_VENDOR_UMC_A_CUT(version)		((IS_CHIP_VENDOR_UMC(version)) ? ((GET_CVID_CUT_VERSION(version)) ? _FALSE : _TRUE) : _FALSE)
#define IS_VENDOR_8723_A_CUT(version)		((IS_8723_SERIES(version)) ? ((GET_CVID_CUT_VERSION(version)) ? _FALSE : _TRUE) : _FALSE)
// <tynli_Note> 88/92C UMC B-cut vendor is set to TSMC so we need to check CHIP_VENDOR_UMC bit is not 1. 
#define IS_81xxC_VENDOR_UMC_B_CUT(version)	((IS_CHIP_VENDOR_UMC(version)) ? ((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? _TRUE : _FALSE):_FALSE)
#define IS_92D_SINGLEPHY(version)     			((IS_92D(version)) ? (IS_2T2R(version) ? _TRUE: _FALSE) : _FALSE)
#define IS_92D_C_CUT(version)    				((IS_92D(version)) ? ((GET_CVID_CUT_VERSION(version) == 0x2) ? _TRUE : _FALSE) : _FALSE)
#define IS_92D_D_CUT(version)    				((IS_92D(version)) ? ((GET_CVID_CUT_VERSION(version) == 0x3) ? _TRUE : _FALSE) : _FALSE)

typedef enum _VERSION_8192C{
	VERSION_TEST_CHIP_88C = 0x0000,
	VERSION_TEST_CHIP_92C = 0x0020,
	VERSION_TEST_UMC_CHIP_8723 = 0x0081,
	VERSION_NORMAL_TSMC_CHIP_88C = 0x0008, 
	VERSION_NORMAL_TSMC_CHIP_92C = 0x0028,
	VERSION_NORMAL_TSMC_CHIP_92C_1T2R = 0x0018,
	VERSION_NORMAL_UMC_CHIP_88C_A_CUT = 0x0088,
	VERSION_NORMAL_UMC_CHIP_92C_A_CUT = 0x00a8,
	VERSION_NORMAL_UMC_CHIP_92C_1T2R_A_CUT = 0x0098,		
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_A_CUT = 0x0089,
	VERSION_NORMAL_UMC_CHIP_8723_1T1R_B_CUT = 0x1089,	
	VERSION_NORMAL_UMC_CHIP_88C_B_CUT = 0x1088, 
	VERSION_NORMAL_UMC_CHIP_92C_B_CUT = 0x10a8, 
	VERSION_NORMAL_UMC_CHIP_92C_1T2R_B_CUT = 0x1090, 
	VERSION_TEST_CHIP_92D_SINGLEPHY= 0x0022,
	VERSION_TEST_CHIP_92D_DUALPHY = 0x0002,
	VERSION_NORMAL_CHIP_92D_SINGLEPHY= 0x002a,
	VERSION_NORMAL_CHIP_92D_DUALPHY = 0x000a,
	VERSION_NORMAL_CHIP_92D_C_CUT_SINGLEPHY = 0x202a,
	VERSION_NORMAL_CHIP_92D_C_CUT_DUALPHY = 0x200a,
	VERSION_NORMAL_CHIP_92D_D_CUT_SINGLEPHY = 0x302a,
	VERSION_NORMAL_CHIP_92D_D_CUT_DUALPHY = 0x300a,
}VERSION_8192C,*PVERSION_8192C;



//-------------------------------------------------------------------------
//	Channel Plan
//-------------------------------------------------------------------------
enum ChannelPlan{
	CHPL_FCC	= 0,
	CHPL_IC		= 1,
	CHPL_ETSI	= 2,
	CHPL_SPAIN	= 3,
	CHPL_FRANCE	= 4,
	CHPL_MKK	= 5,
	CHPL_MKK1	= 6,
	CHPL_ISRAEL	= 7,
	CHPL_TELEC	= 8,
	CHPL_GLOBAL	= 9,
	CHPL_WORLD	= 10,
};

typedef struct _TxPowerInfo{
	u8 CCKIndex[RF_PATH_MAX][CHANNEL_GROUP_MAX];
	u8 HT40_1SIndex[RF_PATH_MAX][CHANNEL_GROUP_MAX];
	u8 HT40_2SIndexDiff[RF_PATH_MAX][CHANNEL_GROUP_MAX];
	s8 HT20IndexDiff[RF_PATH_MAX][CHANNEL_GROUP_MAX];
	u8 OFDMIndexDiff[RF_PATH_MAX][CHANNEL_GROUP_MAX];
	u8 HT40MaxOffset[RF_PATH_MAX][CHANNEL_GROUP_MAX];
	u8 HT20MaxOffset[RF_PATH_MAX][CHANNEL_GROUP_MAX];
	u8 TSSI_A;
	u8 TSSI_B;
}TxPowerInfo, *PTxPowerInfo;

#define		EFUSE_REAL_CONTENT_LEN		512
#define		EFUSE_MAP_LEN					128
#define		EFUSE_MAX_SECTION			16
#define		EFUSE_IC_ID_OFFSET			506	//For some inferiority IC purpose. added by Roger, 2009.09.02.
#define 		AVAILABLE_EFUSE_ADDR(addr) 	(addr < EFUSE_REAL_CONTENT_LEN)
//
// <Roger_Notes> To prevent out of boundary programming case, leave 1byte and program full section
// 9bytes + 1byt + 5bytes and pre 1byte.
// For worst case:
// | 1byte|----8bytes----|1byte|--5bytes--| 
// |         |            Reserved(14bytes)	      |
//
#define		EFUSE_OOB_PROTECT_BYTES 		15	// PG data exclude header, dummy 6 bytes frome CP test and reserved 1byte.


#define		EFUSE_MAP_LEN_8723			256
#define		EFUSE_MAX_SECTION_8723		32

//========================================================
//			EFUSE for BT definition
//========================================================
#define		EFUSE_BT_REAL_CONTENT_LEN		1536	// 512*3
#define		EFUSE_BT_MAP_LEN					1024	// 1k bytes
#define		EFUSE_BT_MAX_SECTION				128		// 1024/8

#define		EFUSE_PROTECT_BYTES_BANK			16

//
// <Roger_Notes> For RTL8723 WiFi/BT/GPS multi-function configuration. 2010.10.06.
//
typedef enum _RT_MULTI_FUNC{
	RT_MULTI_FUNC_NONE = 0x00,
	RT_MULTI_FUNC_WIFI = 0x01,
	RT_MULTI_FUNC_BT = 0x02,
	RT_MULTI_FUNC_GPS = 0x04,
}RT_MULTI_FUNC,*PRT_MULTI_FUNC;

//
// <Roger_Notes> For RTL8723 WiFi PDn/GPIO polarity control configuration. 2010.10.08.
//
typedef enum _RT_POLARITY_CTL{
	RT_POLARITY_LOW_ACT = 0,
	RT_POLARITY_HIGH_ACT = 1,	
}RT_POLARITY_CTL,*PRT_POLARITY_CTL;

// For RTL8723 regulator mode. by tynli. 2011.01.14.
typedef enum _RT_REGULATOR_MODE{
	RT_SWITCHING_REGULATOR = 0,
	RT_LDO_REGULATOR = 1,	
}RT_REGULATOR_MODE,*PRT_REGULATOR_MODE;

enum c2h_id_8192c {
	C2H_DBG = 0,
	C2H_TSF = 1,
	C2H_AP_RPT_RSP = 2,
	C2H_CCX_TX_RPT = 3,
	C2H_BT_RSSI = 4,
	C2H_BT_OP_MODE = 5,
	C2H_EXT_RA_RPT = 6,
	C2H_HW_INFO_EXCH = 10,
	C2H_C2H_H2C_TEST = 11,
	C2H_BT_INFO = 12,
	C2H_BT_MP_INFO = 15,
	MAX_C2HEVENT
};

#ifdef CONFIG_PCI_HCI
struct hal_data_8192ce
{
	VERSION_8192C		VersionID;
	RT_MULTI_FUNC		MultiFunc; // For multi-function consideration.
	RT_POLARITY_CTL		PolarityCtl; // For Wifi PDn Polarity control.
	RT_REGULATOR_MODE	RegulatorMode; // switching regulator or LDO
	u16	CustomerID;

	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;

	u32	IntrMask[2];
	u32	IntrMaskToSet[2];

	u32	DisabledFunctions;
	
	//current WIFI_PHY values
	u32	ReceiveConfig;
	u32	TransmitConfig;
	WIRELESS_MODE		CurrentWirelessMode;
	HT_CHANNEL_WIDTH	CurrentChannelBW;
	u8	CurrentChannel;
	u8	nCur40MhzPrimeSC;// Control channel sub-carrier

	u16	BasicRateSet;

	//rf_ctrl
	_lock	rf_lock;
	u8	rf_chip;
	u8	rf_type;
	u8	NumTotalRFPath;

	INTERFACE_SELECT_8192CPCIe	InterfaceSel;

	//
	// EEPROM setting.
	//
	u16	EEPROMVID;
	u16	EEPROMDID;
	u16	EEPROMSVID;
	u16	EEPROMSMID;
	u16	EEPROMChannelPlan;
	u16	EEPROMVersion;

	u8	EEPROMChnlAreaTxPwrCCK[2][3];	
	u8	EEPROMChnlAreaTxPwrHT40_1S[2][3];	
	u8	EEPROMChnlAreaTxPwrHT40_2SDiff[2][3];
	u8	EEPROMPwrLimitHT20[3];
	u8	EEPROMPwrLimitHT40[3];

	u8	bTXPowerDataReadFromEEPORM;
	u8	EEPROMThermalMeter;
	u8	EEPROMTSSI[2];

	u8	EEPROMCustomerID;
	u8	EEPROMBoardType;
	u8	EEPROMRegulatory;

	u8	bDefaultAntenna;
	u8	bIQKInitialized;

	u8	TxPwrLevelCck[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr	
	s8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// HT 20<->40 Pwr diff
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// For HT<->legacy pwr diff
	// For power group
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	u8	LegacyHTTxPowerDiff;// Legacy to HT rate power diff

#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	bt_coexist;
#endif

	// Read/write are allow for following hardware information variables
	u8	framesync;
	u32	framesyncC34;
	u8	framesyncMonitor;
	u8	DefaultInitialGain[4];
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[7][16];
	u32	CCKTxPowerLevelOriginalOffset;

	u32	AntennaTxPath;					// Antenna path Tx
	u32	AntennaRxPath;					// Antenna path Rx
	u8	BluetoothCoexist;
	u8	ExternalPA;

	//u32	LedControlNum;
	//u32	LedControlMode;
	u8	bLedOpenDrain; // Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16.
	//u32	TxPowerTrackControl;
	u8	b1x1RecvCombine;	// for 1T1R receive combining

	u8	bCurrentTurboEDCA;
	u32	AcParam_BE; //Original parameter for BE, use for EDCA turbo.

	//vivi, for tx power tracking, 20080407
	//u16	TSSI_13dBm;
	//u32	Pwr_Track;
	// The current Tx Power Level
	u8	CurrentCckTxPwrIdx;
	u8	CurrentOfdm24GTxPwrIdx;

	BB_REGISTER_DEFINITION_T	PHYRegDef[4];	//Radio A/B/C/D

	BOOLEAN		bRFPathRxEnable[4];	// We support 4 RF path now.

	u32	RfRegChnlVal[2];

	u8	bCckHighPower;

	//RDG enable
	BOOLEAN	 bRDGEnable;

	//for host message to fw
	u8	LastHMEBoxNum;

	u8	fw_ractrl;
	u8	RegTxPause;
	// Beacon function related global variable.
	u32	RegBcnCtrlVal;
	u8	RegFwHwTxQCtrl;
	u8	RegReg542;
	u8	CurAntenna;	
	u8	AntDivCfg;

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	//SW Antenna Switch
	s32				RSSI_sum_A;
	s32				RSSI_sum_B;
	s32				RSSI_cnt_A;
	s32				RSSI_cnt_B;
	BOOLEAN		RSSI_test;
#endif
#ifdef CONFIG_HW_ANTENNA_DIVERSITY
	//Hybrid Antenna Diversity
	u32				CCK_Ant1_Cnt;
	u32				CCK_Ant2_Cnt;
	u32				OFDM_Ant1_Cnt;
	u32				OFDM_Ant2_Cnt;
#endif

	struct dm_priv	dmpriv;
	u8	bDumpRxPkt;//for debug
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv srestpriv;
#endif
	u8	bInterruptMigration;
	u8	bDisableTxInt;
	u8	bGpioHwWpsPbc;

	u8	FwRsvdPageStartOffset; //2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ.

	u16	EfuseUsedBytes;

	EFUSE_HAL			EfuseHal;
	
#ifdef CONFIG_P2P
	struct P2P_PS_Offload_t	p2p_ps_offload;
#endif //CONFIG_P2P
};

typedef struct hal_data_8192ce HAL_DATA_TYPE, *PHAL_DATA_TYPE;

//
// Function disabled.
//
#define DF_TX_BIT		BIT0
#define DF_RX_BIT		BIT1
#define DF_IO_BIT		BIT2
#define DF_IO_D3_BIT			BIT3

#define RT_DF_TYPE		u32
#define RT_DISABLE_FUNC(__pAdapter, __FuncBits) ((__pAdapter)->DisabledFunctions |= ((RT_DF_TYPE)(__FuncBits)))
#define RT_ENABLE_FUNC(__pAdapter, __FuncBits) ((__pAdapter)->DisabledFunctions &= (~((RT_DF_TYPE)(__FuncBits))))
#define RT_IS_FUNC_DISABLED(__pAdapter, __FuncBits) ( (__pAdapter)->DisabledFunctions & (__FuncBits) )
#define IS_MULTI_FUNC_CHIP(_Adapter)	(((((PHAL_DATA_TYPE)(_Adapter->HalData))->MultiFunc) & (RT_MULTI_FUNC_BT|RT_MULTI_FUNC_GPS)) ? _TRUE : _FALSE)

void InterruptRecognized8192CE(PADAPTER Adapter, PRT_ISR_CONTENT pIsrContent);
VOID UpdateInterruptMask8192CE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
#endif

#ifdef CONFIG_USB_HCI
struct hal_data_8192cu
{
	VERSION_8192C		VersionID;
	RT_MULTI_FUNC		MultiFunc; // For multi-function consideration.
	RT_POLARITY_CTL		PolarityCtl; // For Wifi PDn Polarity control.
	RT_REGULATOR_MODE	RegulatorMode; // switching regulator or LDO
	u16	CustomerID;

	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;

	//current WIFI_PHY values
	u32	ReceiveConfig;
	WIRELESS_MODE		CurrentWirelessMode;
	HT_CHANNEL_WIDTH	CurrentChannelBW;
	u8	CurrentChannel;
	u8	nCur40MhzPrimeSC;// Control channel sub-carrier

	u16	BasicRateSet;

	//rf_ctrl
	u8	rf_chip;
	u8	rf_type;
	u8	NumTotalRFPath;

	u8	BoardType;
	//INTERFACE_SELECT_8192CUSB	InterfaceSel;

	//
	// EEPROM setting.
	//
	u16	EEPROMVID;
	u16	EEPROMPID;
	u16	EEPROMSVID;
	u16	EEPROMSDID;
	u8	EEPROMCustomerID;
	u8	EEPROMSubCustomerID;
	u8	EEPROMVersion;
	u8	EEPROMRegulatory;

	u8	bTXPowerDataReadFromEEPORM;
	u8	EEPROMThermalMeter;

	u8	bIQKInitialized;

	u8	TxPwrLevelCck[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr	
	s8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// HT 20<->40 Pwr diff
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// For HT<->legacy pwr diff
	// For power group
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	u8	LegacyHTTxPowerDiff;// Legacy to HT rate power diff

	// Read/write are allow for following hardware information variables
	u8	framesync;
	u32	framesyncC34;
	u8	framesyncMonitor;
	u8	DefaultInitialGain[4];
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[7][16];
	u32	CCKTxPowerLevelOriginalOffset;

	u32	AntennaTxPath;					// Antenna path Tx
	u32	AntennaRxPath;					// Antenna path Rx
	u8	BluetoothCoexist;
	u8	ExternalPA;

	u8	bLedOpenDrain; // Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16.

	//u32	LedControlNum;
	//u32	LedControlMode;
	//u32	TxPowerTrackControl;
	u8	b1x1RecvCombine;	// for 1T1R receive combining

	u8	bCurrentTurboEDCA;
	u32	AcParam_BE; //Original parameter for BE, use for EDCA turbo.

	//vivi, for tx power tracking, 20080407
	//u16	TSSI_13dBm;
	//u32	Pwr_Track;
	// The current Tx Power Level
	u8	CurrentCckTxPwrIdx;
	u8	CurrentOfdm24GTxPwrIdx;

	BB_REGISTER_DEFINITION_T	PHYRegDef[4];	//Radio A/B/C/D

	BOOLEAN		bRFPathRxEnable[4];	// We support 4 RF path now.

	u32	RfRegChnlVal[2];

	u8	bCckHighPower;

	//RDG enable
	BOOLEAN	 bRDGEnable;

	//for host message to fw
	u8	LastHMEBoxNum;

	u8	fw_ractrl;
	u8	RegTxPause;
	// Beacon function related global variable.
	u32	RegBcnCtrlVal;
	u8	RegFwHwTxQCtrl;
	u8	RegReg542;

	struct dm_priv	dmpriv;
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv srestpriv;
#endif	

#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	bt_coexist;
#endif
	u8	CurAntenna;	
	u8	AntDivCfg;

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	//SW Antenna Switch
	s32				RSSI_sum_A;
	s32				RSSI_sum_B;
	s32				RSSI_cnt_A;
	s32				RSSI_cnt_B;
	BOOLEAN		RSSI_test;
#endif
#ifdef CONFIG_HW_ANTENNA_DIVERSITY
	//Hybrid Antenna Diversity
	u32				CCK_Ant1_Cnt;
	u32				CCK_Ant2_Cnt;
	u32				OFDM_Ant1_Cnt;
	u32				OFDM_Ant2_Cnt;
#endif

	u8	bDumpRxPkt;//for debug
	u8	FwRsvdPageStartOffset; //2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ.

	// 2010/08/09 MH Add CU power down mode.
	BOOLEAN		pwrdown;

	// For 92C USB endpoint setting
	//

	u32	UsbBulkOutSize;

	int	RtBulkOutPipe[3];
	int	RtBulkInPipe;
	int	RtIntInPipe;
	// Add for dual MAC  0--Mac0 1--Mac1
	u32	interfaceIndex;

	u8	OutEpQueueSel;
	u8	OutEpNumber;

	u8	Queue2EPNum[8];//for out endpoint number mapping

#ifdef CONFIG_USB_TX_AGGREGATION
	u8	UsbTxAggMode;
	u8	UsbTxAggDescNum;
#endif
#ifdef CONFIG_USB_RX_AGGREGATION
	u16	HwRxPageSize;				// Hardware setting
	u32	MaxUsbRxAggBlock;

	USB_RX_AGG_MODE	UsbRxAggMode;
	u8	UsbRxAggBlockCount;			// USB Block count. Block size is 512-byte in hight speed and 64-byte in full speed
	u8	UsbRxAggBlockTimeout;
	u8	UsbRxAggPageCount;			// 8192C DMA page count
	u8	UsbRxAggPageTimeout;
#endif

	// 2010/12/10 MH Add for USB aggreation mode dynamic shceme.
	BOOLEAN		UsbRxHighSpeedMode;

	// 2010/11/22 MH Add for slim combo debug mode selective.
	// This is used for fix the drawback of CU TSMC-A/UMC-A cut. HW auto suspend ability. Close BT clock.
	BOOLEAN		SlimComboDbg;

	u16	EfuseUsedBytes;
	
	EFUSE_HAL			EfuseHal;
	
#ifdef CONFIG_P2P
	struct P2P_PS_Offload_t	p2p_ps_offload;
#endif //CONFIG_P2P
};

typedef struct hal_data_8192cu HAL_DATA_TYPE, *PHAL_DATA_TYPE;
#endif

#define GET_HAL_DATA(__pAdapter)	((HAL_DATA_TYPE *)((__pAdapter)->HalData))
#define GET_RF_TYPE(priv)	(GET_HAL_DATA(priv)->rf_type)

#define INCLUDE_MULTI_FUNC_BT(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

VOID rtl8192c_FirmwareSelfReset(IN PADAPTER Adapter);
int FirmwareDownload92C(IN PADAPTER Adapter,IN	BOOLEAN			bUsedWoWLANFw);
VOID InitializeFirmwareVars92C(PADAPTER Adapter);
u8 GetEEPROMSize8192C(PADAPTER Adapter);
void rtl8192c_EfuseParseChnlPlan(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
VERSION_8192C rtl8192c_ReadChipVersion(IN PADAPTER Adapter);
void rtl8192c_ReadBluetoothCoexistInfo(PADAPTER Adapter, u8 *PROMContent, BOOLEAN AutoloadFail);
//void rtl8192c_free_hal_data(_adapter * padapter);
VOID rtl8192c_EfuseParseIDCode(PADAPTER pAdapter, u8 *hwinfo);
void rtl8192c_set_hal_ops(struct hal_ops *pHalFunc);

s32 c2h_id_filter_ccx_8192c(u8 id);

void SetHwReg8192C(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8192C(PADAPTER padapter, u8 variable, u8 *val);
u8 SetHalDefVar8192C(_adapter *adapter, HAL_DEF_VARIABLE variable, void *val);
u8 GetHalDefVar8192C(_adapter *adapter, HAL_DEF_VARIABLE variable, void *val);

#endif

#ifdef CONFIG_MP_INCLUDED

extern void Hal_SetAntenna(PADAPTER pAdapter);
extern void Hal_SetBandwidth(PADAPTER pAdapter);

extern void Hal_SetTxPower(PADAPTER pAdapter);
extern void Hal_SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart);
extern void Hal_SetSingleToneTx ( PADAPTER pAdapter , u8 bStart );
extern void Hal_SetSingleCarrierTx (PADAPTER pAdapter, u8 bStart);
extern void Hal_SetContinuousTx (PADAPTER pAdapter, u8 bStart);

extern void Hal_SetDataRate(PADAPTER pAdapter);
extern void Hal_SetChannel(PADAPTER pAdapter);
extern void Hal_SetAntennaPathPower(PADAPTER pAdapter);
extern s32 Hal_SetThermalMeter(PADAPTER pAdapter, u8 target_ther);
extern s32 Hal_SetPowerTracking(PADAPTER padapter, u8 enable);
extern void Hal_GetPowerTracking(PADAPTER padapter, u8 * enable);
extern void Hal_GetThermalMeter(PADAPTER pAdapter, u8 *value);
extern void Hal_mpt_SwitchRfSetting(PADAPTER pAdapter);
extern void Hal_MPT_CCKTxPowerAdjust(PADAPTER Adapter, BOOLEAN bInCH14);
extern void Hal_MPT_CCKTxPowerAdjustbyIndex(PADAPTER pAdapter, BOOLEAN beven);
extern void Hal_SetCCKTxPower(PADAPTER pAdapter, u8 * TxPower);
extern void Hal_SetOFDMTxPower(PADAPTER pAdapter, u8 * TxPower);
extern void Hal_TriggerRFThermalMeter(PADAPTER pAdapter);
extern u8 Hal_ReadRFThermalMeter(PADAPTER pAdapter);
extern void Hal_SetCCKContinuousTx(PADAPTER pAdapter, u8 bStart);
extern void Hal_SetOFDMContinuousTx(PADAPTER pAdapter, u8 bStart);

#endif



