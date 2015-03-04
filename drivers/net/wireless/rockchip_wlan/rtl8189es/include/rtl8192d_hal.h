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
#ifndef __RTL8192D_HAL_H__
#define __RTL8192D_HAL_H__

//#include "hal_com.h"

#include "hal_data.h"

#include "rtl8192d_spec.h"
#include "Hal8192DPhyReg.h"
#include "Hal8192DPhyCfg.h"
#include "rtl8192d_rf.h"
#include "rtl8192d_dm.h"
#include "rtl8192d_recv.h"
#include "rtl8192d_xmit.h"
#include "rtl8192d_cmd.h"
#include "rtl8192d_led.h"


#ifdef CONFIG_PCI_HCI
	#define RTL819X_DEFAULT_RF_TYPE			RF_2T2R

//---------------------------------------------------------------------
//		RTL8192DE From file
//---------------------------------------------------------------------
	#define	RTL8192D_FW_IMG			  	"rtl8192DE\\rtl8192dfw.bin"

	#define RTL8192D_PHY_REG					"rtl8192DE\\PHY_REG.txt"
	#define RTL8192D_PHY_REG_PG				"rtl8192DE\\PHY_REG_PG.txt"
	#define RTL8192D_PHY_REG_MP				"rtl8192DE\\PHY_REG_MP.txt"

	#define RTL8192D_AGC_TAB					"rtl8192DE\\AGC_TAB.txt"
	#define RTL8192D_AGC_TAB_2G				"rtl8192DE\\AGC_TAB_2G.txt"
	#define RTL8192D_AGC_TAB_5G				"rtl8192DE\\AGC_TAB_5G.txt"
	#define RTL8192D_PHY_RADIO_A				"rtl8192DE\\radio_a.txt"
	#define RTL8192D_PHY_RADIO_B				"rtl8192DE\\radio_b.txt"	
	#define RTL8192D_PHY_RADIO_A_intPA			"rtl8192DE\\radio_a_intPA.txt"
	#define RTL8192D_PHY_RADIO_B_intPA			"rtl8192DE\\radio_b_intPA.txt"
	#define RTL8192D_PHY_MACREG				"rtl8192DE\\MAC_REG.txt"

//---------------------------------------------------------------------
//		RTL8192DE From header
//---------------------------------------------------------------------
	// Fw Array
	#define Rtl8192D_FwImageArray 				Rtl8192DEFwImgArray

	// MAC/BB/PHY Array
	#define Rtl8192D_MAC_Array					Rtl8192DEMAC_2T_Array
	#define Rtl8192D_AGCTAB_Array				Rtl8192DEAGCTAB_Array
	#define Rtl8192D_AGCTAB_5GArray			Rtl8192DEAGCTAB_5GArray
	#define Rtl8192D_AGCTAB_2GArray			Rtl8192DEAGCTAB_2GArray
	#define Rtl8192D_AGCTAB_2TArray 			Rtl8192DEAGCTAB_2TArray
	#define Rtl8192D_AGCTAB_1TArray 			Rtl8192DEAGCTAB_1TArray
	#define Rtl8192D_PHY_REG_2TArray			Rtl8192DEPHY_REG_2TArray		
	#define Rtl8192D_PHY_REG_1TArray			Rtl8192DEPHY_REG_1TArray
	#define Rtl8192D_PHY_REG_Array_PG			Rtl8192DEPHY_REG_Array_PG
	#define Rtl8192D_PHY_REG_Array_MP			Rtl8192DEPHY_REG_Array_MP
	#define Rtl8192D_RadioA_2TArray				Rtl8192DERadioA_2TArray
	#define Rtl8192D_RadioA_1TArray				Rtl8192DERadioA_1TArray
	#define Rtl8192D_RadioB_2TArray				Rtl8192DERadioB_2TArray
	#define Rtl8192D_RadioB_1TArray				Rtl8192DERadioB_1TArray
	#define Rtl8192D_RadioA_2T_intPAArray		Rtl8192DERadioA_2T_intPAArray
	#define Rtl8192D_RadioB_2T_intPAArray 		Rtl8192DERadioB_2T_intPAArray

	// Array length
	#define Rtl8192D_FwImageArrayLength			Rtl8192DEImgArrayLength
	#define Rtl8192D_MAC_ArrayLength				Rtl8192DEMAC_2T_ArrayLength
	#define Rtl8192D_AGCTAB_5GArrayLength			Rtl8192DEAGCTAB_5GArrayLength
	#define Rtl8192D_AGCTAB_2GArrayLength			Rtl8192DEAGCTAB_2GArrayLength
	#define Rtl8192D_AGCTAB_2TArrayLength			Rtl8192DEAGCTAB_2TArrayLength
	#define Rtl8192D_AGCTAB_1TArrayLength			Rtl8192DEAGCTAB_1TArrayLength
	#define Rtl8192D_AGCTAB_ArrayLength 			Rtl8192DEAGCTAB_ArrayLength
	#define Rtl8192D_PHY_REG_2TArrayLength			Rtl8192DEPHY_REG_2TArrayLength
	#define Rtl8192D_PHY_REG_1TArrayLength			Rtl8192DEPHY_REG_1TArrayLength
	#define Rtl8192D_PHY_REG_Array_PGLength		Rtl8192DEPHY_REG_Array_PGLength
	#define Rtl8192D_PHY_REG_Array_MPLength		Rtl8192DEPHY_REG_Array_MPLength
	#define Rtl8192D_RadioA_2TArrayLength			Rtl8192DERadioA_2TArrayLength
	#define Rtl8192D_RadioB_2TArrayLength			Rtl8192DERadioB_2TArrayLength
	#define Rtl8192D_RadioA_2T_intPAArrayLength		Rtl8192DERadioA_2T_intPAArrayLength
	#define Rtl8192D_RadioB_2T_intPAArrayLength		Rtl8192DERadioB_2T_intPAArrayLength

#elif defined(CONFIG_USB_HCI)

	
	#define RTL819X_DEFAULT_RF_TYPE		RF_1T2R

//---------------------------------------------------------------------
//		RTL8192DU From file
//---------------------------------------------------------------------
	#define	RTL8192D_FW_IMG					"rtl8192DU\\rtl8192dfw.bin"

	#define RTL8192D_PHY_REG					"rtl8192DU\\PHY_REG.txt"
	#define RTL8192D_PHY_REG_PG				"rtl8192DU\\PHY_REG_PG.txt"
	#define RTL8192D_PHY_REG_MP				"rtl8192DU\\PHY_REG_MP.txt"			
	
	#define RTL8192D_AGC_TAB					"rtl8192DU\\AGC_TAB.txt"
	#define RTL8192D_AGC_TAB_2G				"rtl8192DU\\AGC_TAB_2G.txt"
	#define RTL8192D_AGC_TAB_5G				"rtl8192DU\\AGC_TAB_5G.txt"
	#define RTL8192D_PHY_RADIO_A				"rtl8192DU\\radio_a.txt"
	#define RTL8192D_PHY_RADIO_B				"rtl8192DU\\radio_b.txt"
	#define RTL8192D_PHY_RADIO_A_intPA		"rtl8192DU\\radio_a_intPA.txt"
	#define RTL8192D_PHY_RADIO_B_intPA		"rtl8192DU\\radio_b_intPA.txt"
	#define RTL8192D_PHY_MACREG				"rtl8192DU\\MAC_REG.txt"

//---------------------------------------------------------------------
//		RTL8192DU From header
//---------------------------------------------------------------------

	// Fw Array
	#define Rtl8192D_FwImageArray 					Rtl8192DUFwImgArray
	
	// MAC/BB/PHY Array
	#define Rtl8192D_MAC_Array						Rtl8192DUMAC_2T_Array
	#define Rtl8192D_AGCTAB_Array					Rtl8192DUAGCTAB_Array
	#define Rtl8192D_AGCTAB_5GArray				Rtl8192DUAGCTAB_5GArray
	#define Rtl8192D_AGCTAB_2GArray				Rtl8192DUAGCTAB_2GArray
	#define Rtl8192D_AGCTAB_2TArray 				Rtl8192DUAGCTAB_2TArray
	#define Rtl8192D_AGCTAB_1TArray 				Rtl8192DUAGCTAB_1TArray
	#define Rtl8192D_PHY_REG_2TArray				Rtl8192DUPHY_REG_2TArray			
	#define Rtl8192D_PHY_REG_1TArray				Rtl8192DUPHY_REG_1TArray
	#define Rtl8192D_PHY_REG_Array_PG				Rtl8192DUPHY_REG_Array_PG
	#define Rtl8192D_PHY_REG_Array_MP				Rtl8192DUPHY_REG_Array_MP
	#define Rtl8192D_RadioA_2TArray					Rtl8192DURadioA_2TArray
	#define Rtl8192D_RadioA_1TArray					Rtl8192DURadioA_1TArray
	#define Rtl8192D_RadioB_2TArray					Rtl8192DURadioB_2TArray
	#define Rtl8192D_RadioB_1TArray					Rtl8192DURadioB_1TArray
	#define Rtl8192D_RadioA_2T_intPAArray			Rtl8192DURadioA_2T_intPAArray
	#define Rtl8192D_RadioB_2T_intPAArray 			Rtl8192DURadioB_2T_intPAArray
	
	// Array length
	#define Rtl8192D_FwImageArrayLength			Rtl8192DUImgArrayLength
	#define Rtl8192D_MAC_ArrayLength				Rtl8192DUMAC_2T_ArrayLength
	#define Rtl8192D_AGCTAB_5GArrayLength			Rtl8192DUAGCTAB_5GArrayLength
	#define Rtl8192D_AGCTAB_2GArrayLength			Rtl8192DUAGCTAB_2GArrayLength
	#define Rtl8192D_AGCTAB_2TArrayLength			Rtl8192DUAGCTAB_2TArrayLength
	#define Rtl8192D_AGCTAB_1TArrayLength			Rtl8192DUAGCTAB_1TArrayLength
	#define Rtl8192D_AGCTAB_ArrayLength 			Rtl8192DUAGCTAB_ArrayLength
	#define Rtl8192D_PHY_REG_2TArrayLength			Rtl8192DUPHY_REG_2TArrayLength
	#define Rtl8192D_PHY_REG_1TArrayLength			Rtl8192DUPHY_REG_1TArrayLength
	#define Rtl8192D_PHY_REG_Array_PGLength		Rtl8192DUPHY_REG_Array_PGLength
	#define Rtl8192D_PHY_REG_Array_MPLength		Rtl8192DUPHY_REG_Array_MPLength
	#define Rtl8192D_RadioA_2TArrayLength			Rtl8192DURadioA_2TArrayLength
	#define Rtl8192D_RadioB_2TArrayLength			Rtl8192DURadioB_2TArrayLength
	#define Rtl8192D_RadioA_2T_intPAArrayLength		Rtl8192DURadioA_2T_intPAArrayLength			
	#define Rtl8192D_RadioB_2T_intPAArrayLength		Rtl8192DURadioB_2T_intPAArrayLength

	// The file name "_2T" is for 92CU, "_1T"  is for 88CU. Modified by tynli. 2009.11.24.
/*	#define Rtl819XFwImageArray					Rtl8192DUFwImgArray
	#define Rtl819XMAC_Array					Rtl8192DUMAC_2TArray
	#define Rtl819XAGCTAB_Array					Rtl8192DUAGCTAB_Array
	#define Rtl819XAGCTAB_5GArray				Rtl8192DUAGCTAB_5GArray
	#define Rtl819XAGCTAB_2GArray				Rtl8192DUAGCTAB_2GArray
	#define Rtl819XPHY_REG_2TArray				Rtl8192DUPHY_REG_2TArray
	#define Rtl819XPHY_REG_1TArray				Rtl8192DUPHY_REG_1TArray
	#define Rtl819XRadioA_2TArray				Rtl8192DURadioA_2TArray
	#define Rtl819XRadioA_1TArray				Rtl8192DURadioA_1TArray
	#define Rtl819XRadioA_2T_intPAArray 			Rtl8192DURadioA_2T_intPAArray
	#define Rtl819XRadioB_2TArray				Rtl8192DURadioB_2TArray
	#define Rtl819XRadioB_1TArray				Rtl8192DURadioB_1TArray
	#define Rtl819XRadioB_2T_intPAArray 			Rtl8192DURadioB_2T_intPAArray
	#define Rtl819XPHY_REG_Array_PG 			Rtl8192DUPHY_REG_Array_PG
	#define Rtl819XPHY_REG_Array_MP 			Rtl8192DUPHY_REG_Array_MP

	#define Rtl819XAGCTAB_2TArray				Rtl8192DUAGCTAB_2TArray
	#define Rtl819XAGCTAB_1TArray				Rtl8192DUAGCTAB_1TArray*/

#endif

//
// Check if FW header exists. We do not consider the lower 4 bits in this case. 
// By tynli. 2009.12.04.
//
#define IS_FW_HEADER_EXIST_92D(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D1 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D2 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D3 )

#define FW_8192D_SIZE				0x8020 // Max FW len = 32k + 32(FW header length).
#define FW_8192D_START_ADDRESS	0x1000
#define FW_8192D_END_ADDRESS		0x1FFF



typedef struct _RT_FIRMWARE_8192D{
	FIRMWARE_SOURCE	eFWSource;
	u8*			szFwBuffer;
	u32			ulFwLength;
} RT_FIRMWARE_8192D, *PRT_FIRMWARE_8192D;

//
// This structure must be cared byte-ordering
//
// Added by tynli. 2009.12.04.
typedef struct _RT_8192D_FIRMWARE_HDR {//8-byte alinment required

	//--- LONG WORD 0 ----
	u16		Signature;	// 92C0: test chip; 92C, 88C0: test chip; 88C1: MP A-cut; 92C1: MP A-cut
	u8		Category;	// AP/NIC and USB/PCI
	u8		Function;	// Reserved for different FW function indcation, for further use when driver needs to download different FW in different conditions
	u16		Version;		// FW Version
	u8		Subversion;	// FW Subversion, default 0x00
	u8		Rsvd1;


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

}RT_8192D_FIRMWARE_HDR, *PRT_8192D_FIRMWARE_HDR;

#define DRIVER_EARLY_INT_TIME_8192D		0x05
#define BCN_DMA_ATIME_INT_TIME_8192D		0x02

typedef	enum _BT_CurState{
	BT_OFF		= 0,	
	BT_ON		= 1,
} BT_CurState, *PBT_CurState;

typedef	enum _BT_ServiceType{
	BT_SCO			= 0,	
	BT_A2DP			= 1,
	BT_HID			= 2,
	BT_HID_Idle		= 3,
	BT_Scan			= 4,
	BT_Idle			= 5,
	BT_OtherAction	= 6,
	BT_Busy			= 7,
	BT_OtherBusy		= 8,
} BT_ServiceType, *PBT_ServiceType;

typedef struct _BT_COEXIST_STR{
	u8					BluetoothCoexist;
	u8					BT_Ant_Num;
	u8					BT_CoexistType;
	u8					BT_State;
	u8					BT_CUR_State;		//0:on, 1:off
	u8					BT_Ant_isolation;	//0:good, 1:bad
	u8					BT_PapeCtrl;		//0:SW, 1:SW/HW dynamic
	u8					BT_Service;			
	u8					BT_RadioSharedType;
	u8					Ratio_Tx;
	u8					Ratio_PRI;
}BT_COEXIST_STR, *PBT_COEXIST_STR;





// Note: We will divide number of page equally for each queue other than public queue!

#define TX_TOTAL_PAGE_NUMBER_8192D		0xF8
#define TX_PAGE_BOUNDARY			(TX_TOTAL_PAGE_NUMBER_8192D + 1)

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8192D
#define NORMAL_PAGE_NUM_PUBQ		0x56


// For Test Chip Setting
// (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER_8192D
#define TEST_PAGE_NUM_PUBQ			0x89
#define TX_TOTAL_PAGE_NUMBER_92D_DUAL_MAC		0x7A
#define NORMAL_PAGE_NUM_PUBQ_92D_DUAL_MAC			0x5A
#define NORMAL_PAGE_NUM_HPQ_92D_DUAL_MAC			0x10
#define NORMAL_PAGE_NUM_LPQ_92D_DUAL_MAC			0x10
#define NORMAL_PAGE_NUM_NORMALQ_92D_DUAL_MAC		0

#define TX_PAGE_BOUNDARY_DUAL_MAC			(TX_TOTAL_PAGE_NUMBER_92D_DUAL_MAC + 1)

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

#define WMM_NORMAL_PAGE_NUM_PUBQ_92D		0X65//0x82
#define WMM_NORMAL_PAGE_NUM_HPQ_92D		0X30//0x29
#define WMM_NORMAL_PAGE_NUM_LPQ_92D		0X30
#define WMM_NORMAL_PAGE_NUM_NPQ_92D		0X30

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


#define EFUSE_REAL_CONTENT_LEN	1024
#define EFUSE_MAP_LEN				256
#define EFUSE_MAX_SECTION			32
#define EFUSE_MAX_SECTION_BASE	16
// <Roger_Notes> To prevent out of boundary programming case, leave 1byte and program full section
// 9bytes + 1byt + 5bytes and pre 1byte.
// For worst case:
// | 2byte|----8bytes----|1byte|--7bytes--| //92D
#define EFUSE_OOB_PROTECT_BYTES 	18 // PG data exclude header, dummy 7 bytes frome CP test and reserved 1byte.

typedef enum _PA_MODE {
	PA_MODE_EXTERNAL = 0x00,
	PA_MODE_INTERNAL_SP3T = 0x01,
	PA_MODE_INTERNAL_SPDT = 0x02	
} PA_MODE;

/* Copy from rtl8192c */
enum c2h_id_8192d {
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

void InterruptRecognized8192DE(PADAPTER Adapter, PRT_ISR_CONTENT pIsrContent);
VOID UpdateInterruptMask8192DE(PADAPTER Adapter, u32 AddMSR, u32 RemoveMSR);
#endif

int FirmwareDownload92D(IN PADAPTER Adapter);
VOID rtl8192d_FirmwareSelfReset(IN PADAPTER Adapter);
void rtl8192d_ReadChipVersion(IN PADAPTER Adapter);
VOID rtl8192d_EfuseParseChnlPlan(PADAPTER Adapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
VOID rtl8192d_ReadTxPowerInfo(PADAPTER Adapter, u8* PROMContent, BOOLEAN AutoLoadFail);
VOID rtl8192d_ResetDualMacSwitchVariables(IN PADAPTER Adapter);
u8 GetEEPROMSize8192D(PADAPTER Adapter);
BOOLEAN PHY_CheckPowerOffFor8192D(PADAPTER Adapter);
VOID PHY_SetPowerOnFor8192D(PADAPTER Adapter);
//void PHY_ConfigMacPhyMode92D(PADAPTER Adapter);
void rtl8192d_free_hal_data(_adapter * padapter);
void rtl8192d_init_default_value(_adapter *adapter);
void rtl8192d_set_hal_ops(struct hal_ops *pHalFunc);

void SetHwReg8192D(_adapter *adapter, u8 variable, u8 *val);
void GetHwReg8192D(_adapter *adapter, u8 variable, u8 *val);
#endif

