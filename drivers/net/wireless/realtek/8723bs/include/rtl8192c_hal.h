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

//#include "hal_com.h"

#if 1
#include "hal_data.h"
#else
#include "../hal/OUTSRC/odm_precomp.h"
#endif


#include "drv_types.h"
#include "rtl8192c_spec.h"
#include "Hal8192CPhyReg.h"
#include "Hal8192CPhyCfg.h"
#include "rtl8192c_rf.h"
#include "rtl8192c_dm.h"
#include "rtl8192c_recv.h"
#include "rtl8192c_xmit.h"
#include "rtl8192c_cmd.h"
#include "rtl8192c_led.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8192c_sreset.h"
#endif


#ifdef CONFIG_PCI_HCI
	
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

	// The file name "_2T" is for 92CE, "_1T"  is for 88CE. Modified by tynli. 2009.11.24.
	#define Rtl819XFwTSMCImageArray			Rtl8192CEFwTSMCImgArray
	#define Rtl819XFwUMCACutImageArray			Rtl8192CEFwUMCACutImgArray
	#define Rtl819XFwUMCBCutImageArray			Rtl8192CEFwUMCBCutImgArray
	
//	#define Rtl8723FwUMCImageArray				Rtl8192CEFwUMC8723ImgArray
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

	#define PHY_REG_2TArrayLength 				Rtl8192CEPHY_REG_2TArrayLength 
	#define PHY_REG_1TArrayLength 				Rtl8192CEPHY_REG_1TArrayLength 
	#define PHY_ChangeTo_1T1RArrayLength 		Rtl8192CEPHY_ChangeTo_1T1RArrayLength 
	#define PHY_ChangeTo_1T2RArrayLength  		Rtl8192CEPHY_ChangeTo_1T2RArrayLength 
	#define PHY_ChangeTo_2T2RArrayLength  		Rtl8192CEPHY_ChangeTo_2T2RArrayLength 
	#define PHY_REG_Array_PGLength  			Rtl8192CEPHY_REG_Array_PGLength 
	//#define PHY_REG_Array_PG_mCardLength 		Rtl8192CEPHY_REG_Array_PG_mCardLength 
	#define PHY_REG_Array_MPLength 			Rtl8192CEPHY_REG_Array_MPLength 
	#define PHY_REG_Array_MPLength 			Rtl8192CEPHY_REG_Array_MPLength 
	//#define PHY_REG_1T_mCardArrayLength 		Rtl8192CEPHY_REG_1T_mCardArrayLength 
	//#define PHY_REG_2T_mCardArrayLength  		Rtl8192CEPHY_REG_2T_mCardArrayLength 
	//#define PHY_REG_Array_PG_HPLength 			Rtl8192CEPHY_REG_Array_PG_HPLength 
	#define RadioA_2TArrayLength  				Rtl8192CERadioA_2TArrayLength 
	#define RadioB_2TArrayLength 				Rtl8192CERadioB_2TArrayLength 
	#define RadioA_1TArrayLength  				Rtl8192CERadioA_1TArrayLength 
	#define RadioB_1TArrayLength 				Rtl8192CERadioB_1TArrayLength 
	//#define RadioA_1T_mCardArrayLength 			Rtl8192CERadioA_1T_mCardArrayLength 
	//#define RadioB_1T_mCardArrayLength 			Rtl8192CERadioB_1T_mCardArrayLength 
	//#define RadioA_1T_HPArrayLength 				Rtl8192CERadioA_1T_HPArrayLength 
	#define RadioB_GM_ArrayLength 				Rtl8192CERadioB_GM_ArrayLength 
	#define MAC_2T_ArrayLength					Rtl8192CEMAC_2T_ArrayLength 
	#define MACPHY_Array_PGLength 				Rtl8192CEMACPHY_Array_PGLength 
	#define AGCTAB_2TArrayLength 				Rtl8192CEAGCTAB_2TArrayLength 
	#define AGCTAB_1TArrayLength 				Rtl8192CEAGCTAB_1TArrayLength 
	//#define AGCTAB_1T_HPArrayLength 			Rtl8192CEAGCTAB_1T_HPArrayLength 	

#elif defined(CONFIG_USB_HCI)


	//2TODO: We should define 8192S firmware related macro settings here!!
	#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
	#define RTL819X_TOTAL_RF_PATH				2

	//TODO:  The following need to check!!
	#define RTL8192C_FW_TSMC_IMG				"rtl8192CU\\rtl8192cfwT.bin"
	#define RTL8192C_FW_UMC_IMG				"rtl8192CU\\rtl8192cfwU.bin"
	#define RTL8192C_FW_UMC_B_IMG				"rtl8192CU\\rtl8192cfwU_B.bin"

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

	// The file name "_2T" is for 92CU, "_1T"  is for 88CU. Modified by tynli. 2009.11.24.
	#define Rtl819XFwImageArray					Rtl8192CUFwTSMCImgArray
	#define Rtl819XFwTSMCImageArray			Rtl8192CUFwTSMCImgArray
	#define Rtl819XFwUMCACutImageArray			Rtl8192CUFwUMCACutImgArray
	#define Rtl819XFwUMCBCutImageArray			Rtl8192CUFwUMCBCutImgArray

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
	#define Rtl819XRadioA_1T_mCardArray			Rtl8192CURadioA_1T_mCardArray			
	#define Rtl819XRadioB_2TArray				Rtl8192CURadioB_2TArray
	#define Rtl819XRadioB_1TArray				Rtl8192CURadioB_1TArray	
	#define Rtl819XRadioB_1T_mCardArray			Rtl8192CURadioB_1T_mCardArray
	#define Rtl819XRadioA_1T_HPArray			Rtl8192CURadioA_1T_HPArray	
	#define Rtl819XPHY_REG_Array_PG 			Rtl8192CUPHY_REG_Array_PG
	#define Rtl819XPHY_REG_Array_PG_mCard 		Rtl8192CUPHY_REG_Array_PG_mCard			
	#define Rtl819XPHY_REG_Array_PG_HP			Rtl8192CUPHY_REG_Array_PG_HP
	#define Rtl819XPHY_REG_Array_MP 			Rtl8192CUPHY_REG_Array_MP

	#define PHY_REG_2TArrayLength 				Rtl8192CUPHY_REG_2TArrayLength 
	#define PHY_REG_1TArrayLength 				Rtl8192CUPHY_REG_1TArrayLength 
	#define PHY_ChangeTo_1T1RArrayLength 		Rtl8192CUPHY_ChangeTo_1T1RArrayLength 
	#define PHY_ChangeTo_1T2RArrayLength  		Rtl8192CUPHY_ChangeTo_1T2RArrayLength 
	#define PHY_ChangeTo_2T2RArrayLength  		Rtl8192CUPHY_ChangeTo_2T2RArrayLength 
	#define PHY_REG_Array_PGLength  			Rtl8192CUPHY_REG_Array_PGLength 
	#define PHY_REG_Array_PG_mCardLength 		Rtl8192CUPHY_REG_Array_PG_mCardLength 
	#define PHY_REG_Array_MPLength 			Rtl8192CUPHY_REG_Array_MPLength 
	#define PHY_REG_Array_MPLength 			Rtl8192CUPHY_REG_Array_MPLength 
	#define PHY_REG_1T_mCardArrayLength 		Rtl8192CUPHY_REG_1T_mCardArrayLength 
	#define PHY_REG_2T_mCardArrayLength  		Rtl8192CUPHY_REG_2T_mCardArrayLength 
	#define PHY_REG_Array_PG_HPLength 			Rtl8192CUPHY_REG_Array_PG_HPLength 
	#define RadioA_2TArrayLength  				Rtl8192CURadioA_2TArrayLength 
	#define RadioB_2TArrayLength 				Rtl8192CURadioB_2TArrayLength 
	#define RadioA_1TArrayLength  				Rtl8192CURadioA_1TArrayLength 
	#define RadioB_1TArrayLength 				Rtl8192CURadioB_1TArrayLength 
	#define RadioA_1T_mCardArrayLength 			Rtl8192CURadioA_1T_mCardArrayLength 
	#define RadioB_1T_mCardArrayLength 			Rtl8192CURadioB_1T_mCardArrayLength 
	#define RadioA_1T_HPArrayLength 				Rtl8192CURadioA_1T_HPArrayLength 
	#define RadioB_GM_ArrayLength 				Rtl8192CURadioB_GM_ArrayLength 
	#define MAC_2T_ArrayLength					Rtl8192CUMAC_2T_ArrayLength 
	#define MACPHY_Array_PGLength 				Rtl8192CUMACPHY_Array_PGLength 
	#define AGCTAB_2TArrayLength 				Rtl8192CUAGCTAB_2TArrayLength 
	#define AGCTAB_1TArrayLength 				Rtl8192CUAGCTAB_1TArrayLength 
	#define AGCTAB_1T_HPArrayLength 			Rtl8192CUAGCTAB_1T_HPArrayLength 
	#define PHY_REG_1T_HPArrayLength			Rtl8192CUPHY_REG_1T_HPArrayLength

#endif

#define FW_8192C_SIZE					16384+32//16k
#define FW_8192C_START_ADDRESS		0x1000
//#define FW_8192C_END_ADDRESS		0x3FFF //Filen said this is for test chip
#define FW_8192C_END_ADDRESS		0x1FFF

#define IS_FW_HEADER_EXIST_92C(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300)


typedef struct _RT_FIRMWARE_8192C{
	FIRMWARE_SOURCE	eFWSource;
	u8*			szFwBuffer;
	u32			ulFwLength;
} RT_FIRMWARE_8192C, *PRT_FIRMWARE_8192C;

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

#define DRIVER_EARLY_INT_TIME_8192C		0x05
#define BCN_DMA_ATIME_INT_TIME_8192C		0x02



// Note: We will divide number of page equally for each queue other than public queue!

#define TX_TOTAL_PAGE_NUMBER_8192C		0xF8
#define TX_PAGE_BOUNDARY		(TX_TOTAL_PAGE_NUMBER_8192C + 1)

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8192C
#define NORMAL_PAGE_NUM_PUBQ			0xE7
#define NORMAL_PAGE_NUM_HPQ			0x0C
#define NORMAL_PAGE_NUM_LPQ			0x02
#define NORMAL_PAGE_NUM_NPQ			0x02


// For Test Chip Setting
// (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8192C
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

#define WMM_NORMAL_PAGE_NUM_PUBQ		0xB0
#define WMM_NORMAL_PAGE_NUM_HPQ		0x29
#define WMM_NORMAL_PAGE_NUM_LPQ			0x1C
#define WMM_NORMAL_PAGE_NUM_NPQ		0x1C

//-------------------------------------------------------------------------
//	Chip specific
//-------------------------------------------------------------------------
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R	0x1
#define CHIP_BONDING_88C_USB_MCARD	0x2
#define CHIP_BONDING_88C_USB_HP	0x1

//-------------------------------------------------------------------------
//	Channel Plan
//-------------------------------------------------------------------------

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

//
// Function disabled.
//
#define DF_TX_BIT		BIT0
#define DF_RX_BIT		BIT1
#define DF_IO_BIT		BIT2
#define DF_IO_D3_BIT			BIT3

#define RT_DF_TYPE		u32
//#define RT_DISABLE_FUNC(__pAdapter, __FuncBits) ((__pAdapter)->DisabledFunctions |= ((RT_DF_TYPE)(__FuncBits)))
//#define RT_ENABLE_FUNC(__pAdapter, __FuncBits) ((__pAdapter)->DisabledFunctions &= (~((RT_DF_TYPE)(__FuncBits))))
//#define RT_IS_FUNC_DISABLED(__pAdapter, __FuncBits) ( (__pAdapter)->DisabledFunctions & (__FuncBits) )
#define IS_MULTI_FUNC_CHIP(_Adapter)	(((((PHAL_DATA_TYPE)(_Adapter->HalData))->MultiFunc) & (RT_MULTI_FUNC_BT|RT_MULTI_FUNC_GPS)) ? _TRUE : _FALSE)

void InterruptRecognized8192CE(PADAPTER Adapter, PRT_ISR_CONTENT pIsrContent);
VOID UpdateInterruptMask8192CE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
#endif

#define INCLUDE_MULTI_FUNC_BT(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

VOID rtl8192c_FirmwareSelfReset(IN PADAPTER Adapter);
int FirmwareDownload92C(IN PADAPTER Adapter);
VOID InitializeFirmwareVars92C(PADAPTER Adapter);
u8 GetEEPROMSize8192C(PADAPTER Adapter);
void rtl8192c_EfuseParseChnlPlan(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);

HAL_VERSION rtl8192c_ReadChipVersion(IN PADAPTER Adapter);
void rtl8192c_ReadBluetoothCoexistInfo(PADAPTER Adapter, u8 *PROMContent, BOOLEAN AutoloadFail);

VOID rtl8192c_EfuseParseIDCode(PADAPTER pAdapter, u8 *hwinfo);
void rtl8192c_set_hal_ops(struct hal_ops *pHalFunc);

s32 c2h_id_filter_ccx_8192c(u8 *buf);

void SetHwReg8192C(PADAPTER padapter, u8 variable, u8 *val);
void GetHwReg8192C(PADAPTER padapter, u8 variable, u8 *val);

#endif

