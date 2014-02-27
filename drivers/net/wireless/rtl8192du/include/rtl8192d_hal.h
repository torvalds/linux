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

#include "hal_com.h"
#include "rtl8192d_spec.h"
#include "Hal8192DPhyReg.h"
#include "Hal8192DPhyCfg.h"
#include "rtl8192d_rf.h"
#include "rtl8192d_dm.h"
#include "rtl8192d_recv.h"
#include "rtl8192d_xmit.h"
#include "rtl8192d_cmd.h"

/*---------------------------Define Local Constant---------------------------*/
/* Channel switch:The size of command tables for switch channel*/
#define MAX_PRECMD_CNT 16
#define MAX_RFDEPENDCMD_CNT 16
#define MAX_POSTCMD_CNT 16

#define MAX_DOZE_WAITING_TIMES_9x 64

#define MAX_RF_IMR_INDEX 12
#define MAX_RF_IMR_INDEX_NORMAL 13
#define RF_REG_NUM_for_C_CUT_5G 	6
#define RF_REG_NUM_for_C_CUT_5G_internalPA	7
#define RF_REG_NUM_for_C_CUT_2G 	5
#define RF_CHNL_NUM_5G			19	
#define RF_CHNL_NUM_5G_40M		17
#define TARGET_CHNL_NUM_5G	221
#define TARGET_CHNL_NUM_2G	14
#define TARGET_CHNL_NUM_2G_5G	59
#define CV_CURVE_CNT			64

//static u32	 RF_REG_FOR_5G_SWCHNL[MAX_RF_IMR_INDEX]={0,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x38,0x39,0x0};
static u32	   RF_REG_FOR_5G_SWCHNL_NORMAL[MAX_RF_IMR_INDEX_NORMAL]={0,0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x0};

static u8	RF_REG_for_C_CUT_5G[RF_REG_NUM_for_C_CUT_5G] = 
			{RF_SYN_G1, RF_SYN_G2,	RF_SYN_G3,	RF_SYN_G4,	RF_SYN_G5,	RF_SYN_G6};

static u8	RF_REG_for_C_CUT_5G_internalPA[RF_REG_NUM_for_C_CUT_5G_internalPA] = 
			{0x0B,	0x48,	0x49,	0x4B,	0x03,	0x04,	0x0E};
static u8	RF_REG_for_C_CUT_2G[RF_REG_NUM_for_C_CUT_2G] = 
			{RF_SYN_G1, RF_SYN_G2,	RF_SYN_G3,	RF_SYN_G7,	RF_SYN_G8};

#if DBG
static u32	RF_REG_MASK_for_C_CUT_2G[RF_REG_NUM_for_C_CUT_2G] = 
			{BIT19|BIT18|BIT17|BIT14|BIT1,	BIT10|BIT9, 
			BIT18|BIT17|BIT16|BIT1, 	BIT2|BIT1,	
			BIT15|BIT14|BIT13|BIT12|BIT11};
#endif	//amy, temp remove
static u8	RF_CHNL_5G[RF_CHNL_NUM_5G] = 
			{36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140};
static u8	RF_CHNL_5G_40M[RF_CHNL_NUM_5G_40M] = 
			{38,42,46,50,54,58,62,102,106,110,114,118,122,126,130,134,138};

static u32	RF_REG_Param_for_C_CUT_5G[5][RF_REG_NUM_for_C_CUT_5G] = {
			{0xE43BE,	0xFC638,	0x77C0A,	0xDE471,	0xd7110,	0x8EB04},
			{0xE43BE,	0xFC078,	0xF7C1A,	0xE0C71,	0xD7550,	0xAEB04},	
			{0xE43BF,	0xFF038,	0xF7C0A,	0xDE471,	0xE5550,	0xAEB04},
			{0xE43BF,	0xFF079,	0xF7C1A,	0xDE471,	0xE5550,	0xAEB04},
			{0xE43BF,	0xFF038,	0xF7C1A,	0xDE471,	0xd7550,	0xAEB04}};

static u32	RF_REG_Param_for_C_CUT_2G[3][RF_REG_NUM_for_C_CUT_2G] = {
			{0x643BC,	0xFC038,	0x77C1A,	0x41289,	0x01840},
			{0x643BC,	0xFC038,	0x07C1A,	0x41289,	0x01840},
			{0x243BC,	0xFC438,	0x07C1A,	0x4128B,	0x0FC41}};

#if SWLCK == 1
static u32 RF_REG_SYN_G4_for_C_CUT_2G = 0xD1C31&0x7FF;
#endif

static u32	RF_REG_Param_for_C_CUT_5G_internalPA[3][RF_REG_NUM_for_C_CUT_5G_internalPA] = {
			{0x01a00,	0x40443,	0x00eb5,	0x89bec,	0x94a12,	0x94a12,	0x94a12},
			{0x01800,	0xc0443,	0x00730,	0x896ee,	0x94a52,	0x94a52,	0x94a52},	
			{0x01800,	0xc0443,	0x00730,	0x896ee,	0x94a12,	0x94a12,	0x94a12}};



//[mode][patha+b][reg]
static u32 RF_IMR_Param_Normal[1][3][MAX_RF_IMR_INDEX_NORMAL]={{
	{0x70000,0x00ff0,0x4400f,0x00ff0,0x0,0x0,0x0,0x0,0x0,0x64888,0xe266c,0x00090,0x22fff},// channel 1-14.
	{0x70000,0x22880,0x4470f,0x55880,0x00070, 0x88000, 0x0,0x88080,0x70000,0x64a82,0xe466c,0x00090,0x32c9a}, //path 36-64
	{0x70000,0x44880,0x4477f,0x77880,0x00070, 0x88000, 0x0,0x880b0,0x0,0x64b82,0xe466c,0x00090,0x32c9a} //100 -165
}
};

//static u32 CurveIndex_5G[TARGET_CHNL_NUM_5G]={0};
//static u32 CurveIndex_2G[TARGET_CHNL_NUM_2G]={0};
static u32 CurveIndex[TARGET_CHNL_NUM_2G_5G]={0};

static u32 TargetChnl_5G[TARGET_CHNL_NUM_5G] = {
25141,	25116,	25091,	25066,	25041,
25016,	24991,	24966,	24941,	24917,
24892,	24867,	24843,	24818,	24794,
24770,	24765,	24721,	24697,	24672,
24648,	24624,	24600,	24576,	24552,
24528,	24504,	24480,	24457,	24433,
24409,	24385,	24362,	24338,	24315,
24291,	24268,	24245,	24221,	24198,
24175,	24151,	24128,	24105,	24082,
24059,	24036,	24013,	23990,	23967,
23945,	23922,	23899,	23876,	23854,
23831,	23809,	23786,	23764,	23741,
23719,	23697,	23674,	23652,	23630,
23608,	23586,	23564,	23541,	23519,
23498,	23476,	23454,	23432,	23410,
23388,	23367,	23345,	23323,	23302,
23280,	23259,	23237,	23216,	23194,
23173,	23152,	23130,	23109,	23088,
23067,	23046,	23025,	23003,	22982,
22962,	22941,	22920,	22899,	22878,
22857,	22837,	22816,	22795,	22775,
22754,	22733,	22713,	22692,	22672,
22652,	22631,	22611,	22591,	22570,
22550,	22530,	22510,	22490,	22469,
22449,	22429,	22409,	22390,	22370,
22350,	22336,	22310,	22290,	22271,
22251,	22231,	22212,	22192,	22173,
22153,	22134,	22114,	22095,	22075,
22056,	22037,	22017,	21998,	21979,
21960,	21941,	21921,	21902,	21883,
21864,	21845,	21826,	21807,	21789,
21770,	21751,	21732,	21713,	21695,
21676,	21657,	21639,	21620,	21602,
21583,	21565,	21546,	21528,	21509,
21491,	21473,	21454,	21436,	21418,
21400,	21381,	21363,	21345,	21327,
21309,	21291,	21273,	21255,	21237,
21219,	21201,	21183,	21166,	21148,
21130,	21112,	21095,	21077,	21059,
21042,	21024,	21007,	20989,	20972,
25679,	25653,	25627,	25601,	25575,
25549,	25523,	25497,	25471,	25446,
25420,	25394,	25369,	25343,	25318,
25292,	25267,	25242,	25216,	25191,
25166	};

static u32 TargetChnl_2G[TARGET_CHNL_NUM_2G] = {	// channel 1~14
26084, 26030, 25976, 25923, 25869, 25816, 25764,
25711, 25658, 25606, 25554, 25502, 25451, 25328
};


#ifdef CONFIG_PCI_HCI
	#include <pci_ops.h>
	#include "Hal8192DEHWImg.h"

	#define RTL819X_DEFAULT_RF_TYPE			RF_2T2R

//---------------------------------------------------------------------
//		RTL8192DE From file
//---------------------------------------------------------------------
	#define RTL8192D_FW_IMG 					"rtl8192DE\\rtl8192dfw.bin"

	#define RTL8192D_PHY_REG					"rtl8192DE\\PHY_REG.txt"
	#define RTL8192D_PHY_REG_PG				"rtl8192DE\\PHY_REG_PG.txt"
	#define RTL8192D_PHY_REG_MP				"rtl8192DE\\PHY_REG_MP.txt"
	
	#define RTL8192D_AGC_TAB					"rtl8192DE\\AGC_TAB.txt"
	#define RTL8192D_AGC_TAB_2G				"rtl8192DE\\AGC_TAB_2G.txt"
	#define RTL8192D_AGC_TAB_5G				"rtl8192DE\\AGC_TAB_5G.txt"
	#define RTL8192D_PHY_RADIO_A				"rtl8192DE\\radio_a.txt"
	#define RTL8192D_PHY_RADIO_B				"rtl8192DE\\radio_b.txt"
	#define RTL8192D_PHY_RADIO_A_intPA		"rtl8192DE\\radio_a_intPA.txt"
	#define RTL8192D_PHY_RADIO_B_intPA		"rtl8192DE\\radio_b_intPA.txt"			
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
	#define Rtl8192D_FwImageArrayLength				Rtl8192DEImgArrayLength
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

	#include "Hal8192DUHWImg.h"
#ifdef CONFIG_WOWLAN
	#include "Hal8192DUHWImg_wowlan.h"
#endif //CONFIG_WOWLAN
	#define RTL819X_DEFAULT_RF_TYPE		RF_1T2R

//---------------------------------------------------------------------
//		RTL8192DU From file
//---------------------------------------------------------------------
	#define RTL8192D_FW_IMG					"rtl8192DU\\rtl8192dfw.bin"

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
#ifdef CONFIG_WOWLAN
	#define Rtl8192D_FwWWImageArray				Rtl8192DUFwWWImgArray
#endif //CONFIG_WOWLAN
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

#define DRVINFO_SZ	4 // unit is 8bytes
#define PageNum_128(_Len)		(u32)(((_Len)>>7) + ((_Len)&0x7F ? 1:0))

//
// Check if FW header exists. We do not consider the lower 4 bits in this case. 
// By tynli. 2009.12.04.
//
#define IS_FW_HEADER_EXIST(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D1 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D2 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFFF) == 0x92D3 )

#define FW_8192D_SIZE				0x8020 // Max FW len = 32k + 32(FW header length).
#define FW_8192D_START_ADDRESS	0x1000
#define FW_8192D_END_ADDRESS		0x1FFF

#define MAX_PAGE_SIZE				4096	// @ page : 4k bytes

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
}RT_FIRMWARE, *PRT_FIRMWARE, RT_FIRMWARE_92D, *PRT_FIRMWARE_92D;

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

#define DRIVER_EARLY_INT_TIME		0x05
#define BCN_DMA_ATIME_INT_TIME		0x02

typedef	enum _BT_CoType{
	BT_2Wire			= 0,		
	BT_ISSC_3Wire	= 1,
	BT_Accel			= 2,
	BT_CSR			= 3,
	BT_CSR_ENHAN	= 4,
	BT_RTL8756		= 5,
} BT_CoType, *PBT_CoType;

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

typedef	enum _BT_RadioShared{
	BT_Radio_Shared 	= 0,	
	BT_Radio_Individual	= 1,
} BT_RadioShared, *PBT_RadioShared;

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

//Added for 92D IQK setting.
typedef struct _IQK_MATRIX_REGS_SETTING{
	BOOLEAN 	bIQKDone;
#if 1
	int		Value[1][IQK_Matrix_REG_NUM];
#else	
	u32		Mark[IQK_Matrix_REG_NUM];
	u32		Value[IQK_Matrix_REG_NUM];
#endif	
}IQK_MATRIX_REGS_SETTING,*PIQK_MATRIX_REGS_SETTING;

#ifdef CONFIG_USB_RX_AGGREGATION

typedef enum _USB_RX_AGG_MODE{
	USB_RX_AGG_DISABLE,
	USB_RX_AGG_DMA,
	USB_RX_AGG_USB,
	USB_RX_AGG_DMA_USB
}USB_RX_AGG_MODE;

#define MAX_RX_DMA_BUFFER_SIZE	10240		// 10K for 8192C RX DMA buffer

#endif


#define TX_SELE_HQ			BIT(0)		// High Queue
#define TX_SELE_LQ			BIT(1)		// Low Queue
#define TX_SELE_NQ			BIT(2)		// Normal Queue


// Note: We will divide number of page equally for each queue other than public queue!

#define TX_TOTAL_PAGE_NUMBER		0xF8
#define TX_PAGE_BOUNDARY			(TX_TOTAL_PAGE_NUMBER + 1)

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER
#define NORMAL_PAGE_NUM_PUBQ		0x56


// For Test Chip Setting
// (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER
#define TEST_PAGE_NUM_PUBQ_92DU			0x89
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

#define WMM_NORMAL_PAGE_NUM_PUBQ_92D		0X65//0x82
#define WMM_NORMAL_PAGE_NUM_HPQ_92D		0X30//0x29
#define WMM_NORMAL_PAGE_NUM_LPQ_92D		0X30
#define WMM_NORMAL_PAGE_NUM_NPQ_92D		0X30

#define WMM_NORMAL_PAGE_NUM_PUBQ_92D_DUAL_MAC		0X32
#define WMM_NORMAL_PAGE_NUM_HPQ_92D_DUAL_MAC		0X18
#define WMM_NORMAL_PAGE_NUM_LPQ_92D_DUAL_MAC		0X18
#define WMM_NORMAL_PAGE_NUM_NPQ_92D_DUAL_MAC		0X18

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
#define D_CUT_VERSION					((BIT(12)|BIT(13)))
#define E_CUT_VERSION					BIT(14)


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
#define IS_CHIP_VENDOR_UMC(version)			((GET_CVID_MANUFACTUER(version))? _TRUE: _FALSE)

#define IS_92C_SERIAL(version)   				((IS_81XXC(version) && IS_2T2R(version)) ? _TRUE : _FALSE)
#define IS_VENDOR_UMC_A_CUT(version)		((IS_CHIP_VENDOR_UMC(version)) ? ((GET_CVID_CUT_VERSION(version)) ? _FALSE : _TRUE) : _FALSE)
#define IS_VENDOR_8723_A_CUT(version)		((IS_8723_SERIES(version)) ? ((GET_CVID_CUT_VERSION(version)) ? _FALSE : _TRUE) : _FALSE)
// <tynli_Note> 88/92C UMC B-cut vendor is set to TSMC so we need to check CHIP_VENDOR_UMC bit is not 1. 
#define IS_81xxC_VENDOR_UMC_B_CUT(version)	((IS_CHIP_VENDOR_UMC(version)) ? ((GET_CVID_CUT_VERSION(version) == B_CUT_VERSION) ? _TRUE : _FALSE):_FALSE)
#define IS_92D_SINGLEPHY(version)     			((IS_92D(version)) ? (IS_2T2R(version) ? _TRUE: _FALSE) : _FALSE)

#define IS_92D_C_CUT(version)    			((IS_92D(version)) ? ((GET_CVID_CUT_VERSION(version) == C_CUT_VERSION) ? _TRUE : _FALSE) : _FALSE)
#define IS_92D_D_CUT(version)    			((IS_92D(version)) ? ((GET_CVID_CUT_VERSION(version) == D_CUT_VERSION) ? _TRUE : _FALSE) : _FALSE)
#define IS_92D_E_CUT(version)    			((IS_92D(version)) ? ((GET_CVID_CUT_VERSION(version) == E_CUT_VERSION) ? _TRUE : _FALSE) : _FALSE)
#define IS_NORMAL_CHIP92D(version)		((GET_CVID_CHIP_TYPE(version))? _TRUE: _FALSE)

typedef enum _VERSION_8192D{
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
	VERSION_NORMAL_CHIP_92D_E_CUT_SINGLEPHY = 0x402a,
	VERSION_NORMAL_CHIP_92D_E_CUT_DUALPHY = 0x400a,
}VERSION_8192D,*PVERSION_8192D;


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
	u8 TSSI_A[3];
	u8 TSSI_B[3];
	u8 TSSI_A_5G[3];		//5GL/5GM/5GH
	u8 TSSI_B_5G[3];
}TxPowerInfo, *PTxPowerInfo;

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
struct hal_data_8192de
{
	VERSION_8192D	VersionID;

	// add for 92D Phy mode/mac/Band mode 
	MACPHY_MODE_8192D	MacPhyMode92D;
	BAND_TYPE	CurrentBandType92D;	//0:2.4G, 1:5G
	BAND_TYPE	BandSet92D;
	BOOLEAN		bIsVS;
	BOOLEAN		bSupportRemoteWakeUp;
	u8	AutoLoadStatusFor8192D;

	BOOLEAN		bNOPG;

	BOOLEAN       bMasterOfDMSP;
	BOOLEAN       bSlaveOfDMSP;

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
	WIRELESS_MODE	CurrentWirelessMode;
	HT_CHANNEL_WIDTH	CurrentChannelBW;
	u8	CurrentChannel;
	u8	nCur40MhzPrimeSC;// Control channel sub-carrier
	u16	BasicRateSet;

	//rf_ctrl
	u8	rf_chip;
	u8	rf_type;
	u8	NumTotalRFPath;

	//
	// EEPROM setting.
	//
	u16	EEPROMVID;
	u16	EEPROMDID;
	u16	EEPROMSVID;
	u16	EEPROMSMID;
	u16	EEPROMChannelPlan;
	u16	EEPROMVersion;

	u8	EEPROMCustomerID;
	u8	EEPROMBoardType;
	u8	EEPROMRegulatory;

	u8	EEPROMThermalMeter;

	u8	EEPROMC9;
	u8	EEPROMCC;
	u8	PAMode;

	u8	TxPwrLevelCck[RF_PATH_MAX][CHANNEL_MAX_NUMBER_2G];
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr	
	s8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// HT 20<->40 Pwr diff
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// For HT<->legacy pwr diff
	// For power group
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	u8	LegacyHTTxPowerDiff;// Legacy to HT rate power diff

	u8	CrystalCap;	// CrystalCap.

#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	bt_coexist;
#endif

	// Read/write are allow for following hardware information variables
	u8	framesync;
	u32	framesyncC34;
	u8	framesyncMonitor;
	u8	DefaultInitialGain[4];
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[MAX_PG_GROUP][16];
	u32	CCKTxPowerLevelOriginalOffset;

	u32	AntennaTxPath;					// Antenna path Tx
	u32	AntennaRxPath;					// Antenna path Rx
	u8	BluetoothCoexist;
	u8	ExternalPA;
	u8	InternalPA5G[2];	//pathA / pathB

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

	BOOLEAN		bPhyValueInitReady;

	BOOLEAN		bTXPowerDataReadFromEEPORM;

	BOOLEAN		bInSetPower;

	//RDG enable
	BOOLEAN		bRDGEnable;

	BOOLEAN		bLoadIMRandIQKSettingFor2G;// True if IMR or IQK  have done  for 2.4G in scan progress
	BOOLEAN		bNeedIQK;

	BOOLEAN		bLCKInProgress;

	BOOLEAN		bEarlyModeEnable;

#if 1
	IQK_MATRIX_REGS_SETTING IQKMatrixRegSetting[IQK_Matrix_Settings_NUM];
#else
	//regc80、regc94、regc4c、regc88、regc9c、regc14、regca0、regc1c、regc78
	u4Byte				IQKMatrixReg[IQK_Matrix_REG_NUM];
	IQK_MATRIX_REGS_SETTING			   IQKMatrixRegSetting[IQK_Matrix_Settings_NUM];	// 1->2G,24->5G 20M channel,21->5G 40M channel.													
#endif

	//for host message to fw
	u8	LastHMEBoxNum;

	u8	fw_ractrl;
	// Beacon function related global variable.
	u32	RegBcnCtrlVal;
	u8	RegTxPause;
	u8	RegFwHwTxQCtrl;
	u8	RegReg542;
	u8	RegCR_1;

	struct dm_priv	dmpriv;

	u8	bDumpRxPkt;//for debug

	u8	bInterruptMigration;

	u8	FwRsvdPageStartOffset; //2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ.

	// Add for dual MAC  0--Mac0 1--Mac1
	u32	interfaceIndex;

	u16	RegRRSR;

	u16	EfuseUsedBytes;
	u8	RTSInitRate;	 // 2010.11.24.by tynli.
#ifdef CONFIG_P2P
	struct P2P_PS_Offload_t	p2p_ps_offload;
#endif //CONFIG_P2P
};

typedef struct hal_data_8192de HAL_DATA_TYPE, *PHAL_DATA_TYPE;

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

void InterruptRecognized8192DE(PADAPTER Adapter, PRT_ISR_CONTENT pIsrContent);
VOID UpdateInterruptMask8192DE(PADAPTER Adapter, u32 AddMSR, u32 RemoveMSR);
#endif

#ifdef CONFIG_USB_HCI

//should be renamed and moved to another file
typedef	enum _INTERFACE_SELECT_8192DUSB{
	INTF_SEL0_USB 			= 0,		// USB
	INTF_SEL1_MINICARD	= 1,		// Minicard
	INTF_SEL2_EKB_PRO		= 2,		// Eee keyboard proprietary
	INTF_SEL3_PRO			= 3,		// Customized proprietary
} INTERFACE_SELECT_8192DUSB, *PINTERFACE_SELECT_8192DUSB;

typedef INTERFACE_SELECT_8192DUSB INTERFACE_SELECT_USB;

struct hal_data_8192du
{
	VERSION_8192D	VersionID;

	// add for 92D Phy mode/mac/Band mode 
	MACPHY_MODE_8192D	MacPhyMode92D;
	BAND_TYPE	CurrentBandType92D;	//0:2.4G, 1:5G
	BAND_TYPE	BandSet92D;
	BOOLEAN		bIsVS;

	BOOLEAN		bNOPG;

	BOOLEAN		bSupportRemoteWakeUp;
	BOOLEAN		bMasterOfDMSP;
	BOOLEAN		bSlaveOfDMSP;
#ifdef CONFIG_DUALMAC_CONCURRENT
	BOOLEAN		bInModeSwitchProcess;
#endif

	u16	CustomerID;

	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;

	//current WIFI_PHY values
	u32	ReceiveConfig;
	WIRELESS_MODE	CurrentWirelessMode;
	HT_CHANNEL_WIDTH	CurrentChannelBW;
	u8	CurrentChannel;
	u8	nCur40MhzPrimeSC;// Control channel sub-carrier
	u16	BasicRateSet;

	INTERFACE_SELECT_8192DUSB	InterfaceSel;

	//rf_ctrl
	u8	rf_chip;
	u8	rf_type;
	u8	NumTotalRFPath;

	//
	// EEPROM setting.
	//
	u8	EEPROMVersion;
	u16	EEPROMVID;
	u16	EEPROMPID;
	u16	EEPROMSVID;
	u16	EEPROMSDID;
	u8	EEPROMCustomerID;
	u8	EEPROMSubCustomerID;	
	u8	EEPROMRegulatory;

	u8	EEPROMThermalMeter;

	u8	EEPROMC9;
	u8	EEPROMCC;
	u8	PAMode;

	u8	TxPwrLevelCck[RF_PATH_MAX][CHANNEL_MAX_NUMBER_2G];
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr	
	s8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// HT 20<->40 Pwr diff
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// For HT<->legacy pwr diff
	// For power group
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	u8	LegacyHTTxPowerDiff;// Legacy to HT rate power diff

	u8	CrystalCap;	// CrystalCap.

#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	bt_coexist;
#endif

	// Read/write are allow for following hardware information variables
	u8	framesync;
	u32	framesyncC34;
	u8	framesyncMonitor;
	u8	DefaultInitialGain[4];
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[MAX_PG_GROUP][16];
	u32	CCKTxPowerLevelOriginalOffset;

	u32	AntennaTxPath;					// Antenna path Tx
	u32	AntennaRxPath;					// Antenna path Rx
	u8	BluetoothCoexist;
	u8	ExternalPA;
	u8	InternalPA5G[2];	//pathA / pathB

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

	BOOLEAN		bPhyValueInitReady;

	BOOLEAN		bTXPowerDataReadFromEEPORM;

	BOOLEAN		bInSetPower;

	//RDG enable
	BOOLEAN		bRDGEnable;

	BOOLEAN		bLoadIMRandIQKSettingFor2G;// True if IMR or IQK  have done  for 2.4G in scan progress
	BOOLEAN		bNeedIQK;

	BOOLEAN		bLCKInProgress;

	BOOLEAN		bEarlyModeEnable;

#if 1
	IQK_MATRIX_REGS_SETTING IQKMatrixRegSetting[IQK_Matrix_Settings_NUM];
#else
	//regc80、regc94、regc4c、regc88、regc9c、regc14、regca0、regc1c、regc78
	u4Byte				IQKMatrixReg[IQK_Matrix_REG_NUM];
	IQK_MATRIX_REGS_SETTING			   IQKMatrixRegSetting[IQK_Matrix_Settings_NUM];	// 1->2G,24->5G 20M channel,21->5G 40M channel.													
#endif

	//for host message to fw
	u8	LastHMEBoxNum;

	u8	fw_ractrl;
	// Beacon function related global variable.
	u32	RegBcnCtrlVal;
	u8	RegTxPause;
	u8	RegFwHwTxQCtrl;
	u8	RegReg542;
	u8	RegCR_1;

	struct dm_priv	dmpriv;
	u8	bDumpRxPkt;//for debug
	u8	FwRsvdPageStartOffset; //2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ.

	//Query RF by FW
	BOOLEAN		bReadRFbyFW;

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

	u16	RegRRSR;

	u16	EfuseUsedBytes;
	u8	RTSInitRate;	 // 2010.11.24.by tynli.
#ifdef CONFIG_P2P
	struct P2P_PS_Offload_t	p2p_ps_offload;
#endif //CONFIG_P2P
};

typedef struct hal_data_8192du HAL_DATA_TYPE, *PHAL_DATA_TYPE;
#endif

#define GET_HAL_DATA(__pAdapter)	((HAL_DATA_TYPE *)((__pAdapter)->HalData))
#define GET_RF_TYPE(priv)	(GET_HAL_DATA(priv)->rf_type)

int FirmwareDownload92D(IN	PADAPTER Adapter,IN	BOOLEAN  bUsedWoWLANFw);
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
void rtl8192d_set_hal_ops(struct hal_ops *pHalFunc);

#endif

#ifdef CONFIG_MP_INCLUDED


extern void Hal_SetAntenna(PADAPTER pAdapter);
extern void Hal_SetBandwidth(PADAPTER pAdapter);

extern void Hal_SetTxPower(PADAPTER pAdapter);
extern void Hal_SetCarrierSuppressionTx(PADAPTER pAdapter, u8 bStart);
extern void Hal_SetSingleToneTx ( PADAPTER pAdapter , u8 bStart );
extern void Hal_SetSingleCarrierTx (PADAPTER pAdapter, u8 bStart);
extern void Hal_SetContinuousTx (PADAPTER pAdapter, u8 bStart);
extern void Hal_SetBandwidth(PADAPTER pAdapter);

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


#endif //end CONFIG_MP_INCLUDED

