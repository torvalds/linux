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

#include "rtl8723a_spec.h"
#include "rtl8723a_pg.h"
#include "Hal8723APhyReg.h"
#include "Hal8723APhyCfg.h"
#include "rtl8723a_rf.h"
#ifdef CONFIG_BT_COEXIST
#include "rtl8723a_bt-coexist.h"
#endif
#include "rtl8723a_dm.h"
#include "rtl8723a_recv.h"
#include "rtl8723a_xmit.h"
#include "rtl8723a_cmd.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8723a_sreset.h"
#endif
#include "rtw_efuse.h"

#include "../hal/OUTSRC/odm_precomp.h"

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

#ifndef CONFIG_PHY_SETTING_WITH_ODM
	// MAC/BB/PHY Array
	#define Rtl8723_MAC_Array					Rtl8723SMAC_2T_Array
	//#define Rtl8723_AGCTAB_2TArray				Rtl8723SAGCTAB_2TArray
	#define Rtl8723_AGCTAB_1TArray				Rtl8723SAGCTAB_1TArray
	//#define Rtl8723_PHY_REG_2TArray				Rtl8723SPHY_REG_2TArray
	#define Rtl8723_PHY_REG_1TArray				Rtl8723SPHY_REG_1TArray
	//#define Rtl8723_RadioA_2TArray				Rtl8723SRadioA_2TArray
	#define Rtl8723_RadioA_1TArray				Rtl8723SRadioA_1TArray
	//#define Rtl8723_RadioB_2TArray				Rtl8723SRadioB_2TArray
	#define Rtl8723_RadioB_1TArray				Rtl8723SRadioB_1TArray

	// Array length
	#define Rtl8723_MAC_ArrayLength				Rtl8723SMAC_2T_ArrayLength
	#define Rtl8723_AGCTAB_1TArrayLength		Rtl8723SAGCTAB_1TArrayLength
	#define Rtl8723_PHY_REG_1TArrayLength 		Rtl8723SPHY_REG_1TArrayLength

	#define Rtl8723_RadioA_1TArrayLength			Rtl8723SRadioA_1TArrayLength
	#define Rtl8723_RadioB_1TArrayLength			Rtl8723SRadioB_1TArrayLength
#endif // CONFIG_PHY_SETTING_WITH_ODM
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
#ifndef CONFIG_PHY_SETTING_WITH_ODM
	// MAC/BB/PHY Array
	#define Rtl8723_MAC_Array					Rtl8723UMAC_2T_Array
	//#define Rtl8723_AGCTAB_2TArray				Rtl8723UAGCTAB_2TArray
	#define Rtl8723_AGCTAB_1TArray				Rtl8723UAGCTAB_1TArray
	//#define Rtl8723_PHY_REG_2TArray				Rtl8723UPHY_REG_2TArray
	#define Rtl8723_PHY_REG_1TArray				Rtl8723UPHY_REG_1TArray
	//#define Rtl8723_RadioA_2TArray				Rtl8723URadioA_2TArray
	#define Rtl8723_RadioA_1TArray				Rtl8723URadioA_1TArray
	//#define Rtl8723_RadioB_2TArray				Rtl8723URadioB_2TArray
	#define Rtl8723_RadioB_1TArray				Rtl8723URadioB_1TArray



	// Array length

	#define Rtl8723_MAC_ArrayLength				Rtl8723UMAC_2T_ArrayLength
	#define Rtl8723_AGCTAB_1TArrayLength			Rtl8723UAGCTAB_1TArrayLength
	#define Rtl8723_PHY_REG_1TArrayLength 			Rtl8723UPHY_REG_1TArrayLength


	#define Rtl8723_RadioA_1TArrayLength			Rtl8723URadioA_1TArrayLength
	#define Rtl8723_RadioB_1TArrayLength			Rtl8723URadioB_1TArrayLength
#endif
#endif

#define DRVINFO_SZ				4 // unit is 8bytes
#define PageNum_128(_Len)		(u32)(((_Len)>>7) + ((_Len)&0x7F ? 1:0))

#define FW_8723A_SIZE			0x8000
#define FW_8723A_START_ADDRESS	0x1000
#define FW_8723A_END_ADDRESS		0x1FFF //0x5FFF

#define MAX_PAGE_SIZE			4096	// @ page : 4k bytes

#define IS_FW_HEADER_EXIST(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300)

typedef enum _FIRMWARE_SOURCE {
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,		//from header file
} FIRMWARE_SOURCE, *PFIRMWARE_SOURCE;

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szFwBuffer;
#else
	u8			szFwBuffer[FW_8723A_SIZE];
#endif
	u32			ulFwLength;

#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szBTFwBuffer;
#else
	u8			szBTFwBuffer[FW_8723A_SIZE];
#endif
	u32			ulBTFwLength;
} RT_FIRMWARE, *PRT_FIRMWARE, RT_FIRMWARE_8723A, *PRT_FIRMWARE_8723A;

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


// BK, BE, VI, VO, HCCA, MANAGEMENT, COMMAND, HIGH, BEACON.
#define MAX_TX_QUEUE		9

#define TX_SELE_HQ			BIT(0)		// High Queue
#define TX_SELE_LQ			BIT(1)		// Low Queue
#define TX_SELE_NQ			BIT(2)		// Normal Queue

// Note: We will divide number of page equally for each queue other than public queue!
#define TX_TOTAL_PAGE_NUMBER	0xF8
#define TX_PAGE_BOUNDARY		(TX_TOTAL_PAGE_NUMBER + 1)

// For Normal Chip Setting
// (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER
#define NORMAL_PAGE_NUM_PUBQ	0xE7
#define NORMAL_PAGE_NUM_HPQ		0x0C
#define NORMAL_PAGE_NUM_LPQ		0x02
#define NORMAL_PAGE_NUM_NPQ		0x02

// For Test Chip Setting
// (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER
#define TEST_PAGE_NUM_PUBQ		0x7E

// For Test Chip Setting
#define WMM_TEST_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_TEST_TX_PAGE_BOUNDARY		(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) //F6

#define WMM_TEST_PAGE_NUM_PUBQ		0xA3
#define WMM_TEST_PAGE_NUM_HPQ		0x29
#define WMM_TEST_PAGE_NUM_LPQ		0x29

// Note: For Normal Chip Setting, modify later
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_NORMAL_TX_PAGE_BOUNDARY		(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) //F6

#define WMM_NORMAL_PAGE_NUM_PUBQ	0xB0
#define WMM_NORMAL_PAGE_NUM_HPQ		0x29
#define WMM_NORMAL_PAGE_NUM_LPQ		0x1C
#define WMM_NORMAL_PAGE_NUM_NPQ		0x1C


//-------------------------------------------------------------------------
//	Chip specific
//-------------------------------------------------------------------------
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R			0x1
#define CHIP_BONDING_88C_USB_MCARD		0x2
#define CHIP_BONDING_88C_USB_HP			0x1

#include "HalVerDef.h"
#include "hal_com.h"

//-------------------------------------------------------------------------
//	Channel Plan
//-------------------------------------------------------------------------
enum ChannelPlan
{
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

//
// <Roger_Notes> For RTL8723 WiFi/BT/GPS multi-function configuration. 2010.10.06.
//
typedef enum _RT_MULTI_FUNC {
	RT_MULTI_FUNC_NONE = 0x00,
	RT_MULTI_FUNC_WIFI = 0x01,
	RT_MULTI_FUNC_BT = 0x02,
	RT_MULTI_FUNC_GPS = 0x04,
} RT_MULTI_FUNC, *PRT_MULTI_FUNC;

//
// <Roger_Notes> For RTL8723 WiFi PDn/GPIO polarity control configuration. 2010.10.08.
//
typedef enum _RT_POLARITY_CTL {
	RT_POLARITY_LOW_ACT = 0,
	RT_POLARITY_HIGH_ACT = 1,
} RT_POLARITY_CTL, *PRT_POLARITY_CTL;

// For RTL8723 regulator mode. by tynli. 2011.01.14.
typedef enum _RT_REGULATOR_MODE {
	RT_SWITCHING_REGULATOR = 0,
	RT_LDO_REGULATOR = 1,
} RT_REGULATOR_MODE, *PRT_REGULATOR_MODE;

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

typedef struct hal_data_8723a
{
	HAL_VERSION			VersionID;
	RT_CUSTOMER_ID	CustomerID;

	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;
	u16	FirmwareSignature;

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
	u8	CrystalCap;
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
	u8	EEPROMBluetoothCoexist;
	u8	EEPROMBluetoothType;
	u8	EEPROMBluetoothAntNum;
	u8	EEPROMBluetoothAntIsolation;
	u8	EEPROMBluetoothRadioShared;

	u8	bTXPowerDataReadFromEEPORM;
	u8	bAPKThermalMeterIgnore;

	u8	bIQKInitialized;
	u8	bAntennaDetected;

	u8	TxPwrLevelCck[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// HT 20<->40 Pwr diff
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
	u8	ExternalPA;

	u8	bLedOpenDrain; // Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16.

	//u32	LedControlNum;
	//u32	LedControlMode;
	//u32	TxPowerTrackControl;
	u8	b1x1RecvCombine;	// for 1T1R receive combining

	// For EDCA Turbo mode
//	u8	bIsAnyNonBEPkts; // Adapter->recvpriv.bIsAnyNonBEPkts
//	u8	bCurrentTurboEDCA;
//	u8	bForcedDisableTurboEDCA;
//	u8	bIsCurRDLState;	// pdmpriv->prv_traffic_idx

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
	DM_ODM_T 		odmpriv;
	//_lock			odm_stainfo_lock;
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv srestpriv;
#endif

#ifdef CONFIG_BT_COEXIST
	u8				bBTMode;
	// BT only.
	BT30Info		BtInfo;
	// For bluetooth co-existance
	BT_COEXIST_STR	bt_coexist;
#endif

#ifdef CONFIG_ANTENNA_DIVERSITY
	u8	CurAntenna;

	// SW Antenna Switch
	s32	RSSI_sum_A;
	s32	RSSI_sum_B;
	s32	RSSI_cnt_A;
	s32	RSSI_cnt_B;
	u8	RSSI_test;
	u8	AntDivCfg;
#endif

	u8	bDumpRxPkt;//for debug
	u8	FwRsvdPageStartOffset; //2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ.

	// 2010/08/09 MH Add CU power down mode.
	u8	pwrdown;

	// Add for dual MAC  0--Mac0 1--Mac1
	u32	interfaceIndex;

	u8	OutEpQueueSel;
	u8	OutEpNumber;

	// 2010/12/10 MH Add for USB aggreation mode dynamic shceme.
	BOOLEAN		UsbRxHighSpeedMode;

	// 2010/11/22 MH Add for slim combo debug mode selective.
	// This is used for fix the drawback of CU TSMC-A/UMC-A cut. HW auto suspend ability. Close BT clock.
	BOOLEAN		SlimComboDbg;

	//
	// Add For EEPROM Efuse switch and  Efuse Shadow map Setting
	//
	u8 			EepromOrEfuse;
//	u8			EfuseMap[2][HWSET_MAX_SIZE_512]; //92C:256bytes, 88E:512bytes, we use union set (512bytes)
	u16			EfuseUsedBytes;
	u8			EfuseUsedPercentage;
#ifdef HAL_EFUSE_MEMORY
	EFUSE_HAL	EfuseHal;
#endif

	// Interrupt relatd register information.
	u32			SysIntrStatus;
	u32			SysIntrMask;

	//
	// 2011/02/23 MH Add for 8723 mylti function definition. The define should be moved to an
	// independent file in the future.
	//
	//------------------------8723-----------------------------------------//
	RT_MULTI_FUNC			MultiFunc; // For multi-function consideration.
	RT_POLARITY_CTL 		PolarityCtl; // For Wifi PDn Polarity control.
	RT_REGULATOR_MODE		RegulatorMode; // switching regulator or LDO
	//------------------------8723-----------------------------------------//
	//
	// 2011/02/23 MH Add for 8723 mylti function definition. The define should be moved to an
	// independent file in the future.

	BOOLEAN 				bMACFuncEnable;

#ifdef CONFIG_P2P
	struct P2P_PS_Offload_t	p2p_ps_offload;
#endif


	//
	// For USB Interface HAL related
	//
#ifdef CONFIG_USB_HCI
	u32	UsbBulkOutSize;

	// Interrupt relatd register information.
	u32	IntArray[2];
	u32	IntrMask[2];
#endif


	//
	// For SDIO Interface HAL related
	//

	// Auto FSM to Turn On, include clock, isolation, power control for MAC only
	u8			bMacPwrCtrlOn;

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
	//
	// SDIO ISR Related
	//
//	u32			IntrMask[1];
//	u32			IntrMaskToSet[1];
//	LOG_INTERRUPT		InterruptLog;
	u32			sdio_himr;
	u32			sdio_hisr;

	//
	// SDIO Tx FIFO related.
	//
	// HIQ, MID, LOW, PUB free pages; padapter->xmitpriv.free_txpg
	u8			SdioTxFIFOFreePage[SDIO_TX_FREE_PG_QUEUE];
	_lock		SdioTxFIFOFreePageLock;

	//
	// SDIO Rx FIFO related.
	//
	u8			SdioRxFIFOCnt;
	u16			SdioRxFIFOSize;
#endif
} HAL_DATA_8723A, *PHAL_DATA_8723A;

#if 0
#define HAL_DATA_TYPE HAL_DATA_8723A
#define PHAL_DATA_TYPE PHAL_DATA_8723A
#else
typedef struct hal_data_8723a HAL_DATA_TYPE, *PHAL_DATA_TYPE;
#endif

#define GET_HAL_DATA(__pAdapter)	((HAL_DATA_TYPE *)((__pAdapter)->HalData))
#define GET_RF_TYPE(priv)			(GET_HAL_DATA(priv)->rf_type)

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
int FirmwareDownloadBT(IN PADAPTER Adapter, PRT_FIRMWARE_8723A pFirmware);
s32 rtl8723a_FirmwareDownload(PADAPTER padapter);
void rtl8723a_FirmwareSelfReset(PADAPTER padapter);
void rtl8723a_InitializeFirmwareVars(PADAPTER padapter);
void _8051Reset8723A(PADAPTER padapter);

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
#ifdef CONFIG_BT_COEXIST
void rtl8723a_SingleDualAntennaDetection(PADAPTER padapter);
#endif

// register
void SetBcnCtrlReg(PADAPTER padapter, u8 SetBits, u8 ClearBits);
void rtl8723a_InitBeaconParameters(PADAPTER padapter);
void rtl8723a_InitBeaconMaxError(PADAPTER padapter, u8 InfraMode);

void rtl8723a_start_thread(_adapter *padapter);
void rtl8723a_stop_thread(_adapter *padapter);

s32 c2h_id_filter_ccx_8723a(u8 id);


#if defined(CONFIG_CHECK_BT_HANG) && defined(CONFIG_BT_COEXIST)
void rtl8723a_init_checkbthang_workqueue(_adapter * padapter);
void rtl8723a_free_checkbthang_workqueue(_adapter * padapter);
void rtl8723a_cancel_checkbthang_workqueue(_adapter * padapter);
void rtl8723a_hal_check_bt_hang(_adapter * padapter);
#endif


#ifdef CONFIG_RF_GAIN_OFFSET
void Hal_ReadRFGainOffset(PADAPTER pAdapter,u8* hwinfo,BOOLEAN AutoLoadFail);
#endif //CONFIG_RF_GAIN_OFFSET

#endif

