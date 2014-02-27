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
#ifndef __RTL8188E_HAL_H__
#define __RTL8188E_HAL_H__


//include HAL Related header after HAL Related compiling flags 
#include "rtl8188e_spec.h"
#include "Hal8188EPhyReg.h"
#include "Hal8188EPhyCfg.h"
#include "rtl8188e_rf.h"
#include "rtl8188e_dm.h"
#include "rtl8188e_recv.h"
#include "rtl8188e_xmit.h"
#include "rtl8188e_cmd.h"
#include "Hal8188EPwrSeq.h"
#ifdef DBG_CONFIG_ERROR_DETECT
#include "rtl8188e_sreset.h"
#endif
#include "rtw_efuse.h"

#include "../hal/OUTSRC/odm_precomp.h"

	// Fw Array
	#define Rtl8188E_FwImageArray				Rtl8188EFwImgArray
	#define Rtl8188E_FWImgArrayLength			Rtl8188EFWImgArrayLength
#ifdef CONFIG_WOWLAN
	#define Rtl8188E_FwWoWImageArray			Array_8188E_FW_WoWLAN
	#define Rtl8188E_FwWoWImgArrayLength		ArrayLength_8188E_FW_WoWLAN
#endif //CONFIG_WOWLAN

	
#ifdef CONFIG_SDIO_HCI
	
	//TODO: We should define 8188ES firmware related macro settings here!!
	//TODO:  The following need to check!!
	#define RTL8188E_FW_UMC_IMG				"rtl8188E\\rtl8188efw.bin"
	#define RTL8188E_PHY_REG					"rtl8188E\\PHY_REG_1T.txt"
	#define RTL8188E_PHY_RADIO_A				"rtl8188E\\radio_a_1T.txt"
	#define RTL8188E_PHY_RADIO_B				"rtl8188E\\radio_b_1T.txt"
	#define RTL8188E_AGC_TAB					"rtl8188E\\AGC_TAB_1T.txt"
	#define RTL8188E_PHY_MACREG 				"rtl8188E\\MAC_REG.txt"
	#define RTL8188E_PHY_REG_PG				"rtl8188E\\PHY_REG_PG.txt"
	#define RTL8188E_PHY_REG_MP				"rtl8188E\\PHY_REG_MP.txt"

//---------------------------------------------------------------------
//		RTL8188E From header
//---------------------------------------------------------------------
#if 0
	#define Rtl8188E_PHY_REG_Array_PG			Rtl8188ESPHY_REG_Array_PG
	#define Rtl8188E_PHY_REG_Array_PGLength	Rtl8188ESPHY_REG_Array_PGLength	

#endif
	
	//---------------------------------------------------------------------
	//		RTL8188E Power Configuration CMDs for USB/SDIO interfaces
	//---------------------------------------------------------------------
	#define Rtl8188E_NIC_PWR_ON_FLOW				rtl8188E_power_on_flow
	#define Rtl8188E_NIC_RF_OFF_FLOW				rtl8188E_radio_off_flow
	#define Rtl8188E_NIC_DISABLE_FLOW				rtl8188E_card_disable_flow
	#define Rtl8188E_NIC_ENABLE_FLOW				rtl8188E_card_enable_flow
	#define Rtl8188E_NIC_SUSPEND_FLOW				rtl8188E_suspend_flow
	#define Rtl8188E_NIC_RESUME_FLOW				rtl8188E_resume_flow
	#define Rtl8188E_NIC_PDN_FLOW					rtl8188E_hwpdn_flow
	#define Rtl8188E_NIC_LPS_ENTER_FLOW			rtl8188E_enter_lps_flow
	#define Rtl8188E_NIC_LPS_LEAVE_FLOW			rtl8188E_leave_lps_flow

#elif defined(CONFIG_USB_HCI)
	#define RTL8188E_FW_UMC_IMG				"rtl8188E\\rtl8188efw.bin"
	#define RTL8188E_PHY_REG					"rtl8188E\\PHY_REG_1T.txt"
	#define RTL8188E_PHY_RADIO_A				"rtl8188E\\radio_a_1T.txt"
	#define RTL8188E_PHY_RADIO_B				"rtl8188E\\radio_b_1T.txt"
	#define RTL8188E_AGC_TAB					"rtl8188E\\AGC_TAB_1T.txt"
	#define RTL8188E_PHY_MACREG 				"rtl8188E\\MAC_REG.txt"
	#define RTL8188E_PHY_REG_PG				"rtl8188E\\PHY_REG_PG.txt"
	#define RTL8188E_PHY_REG_MP				"rtl8188E\\PHY_REG_MP.txt"

#if 0
	#define Rtl8188E_PHY_REG_Array_PG			Rtl8188EUPHY_REG_Array_PG
	#define Rtl8188E_PHY_REG_Array_PGLength	Rtl8188EUPHY_REG_Array_PGLength	
	
#endif	
	
	//---------------------------------------------------------------------
	//		RTL8188E Power Configuration CMDs for USB/SDIO interfaces
	//---------------------------------------------------------------------
	#define Rtl8188E_NIC_PWR_ON_FLOW				rtl8188E_power_on_flow
	#define Rtl8188E_NIC_RF_OFF_FLOW				rtl8188E_radio_off_flow
	#define Rtl8188E_NIC_DISABLE_FLOW				rtl8188E_card_disable_flow
	#define Rtl8188E_NIC_ENABLE_FLOW				rtl8188E_card_enable_flow
	#define Rtl8188E_NIC_SUSPEND_FLOW				rtl8188E_suspend_flow
	#define Rtl8188E_NIC_RESUME_FLOW				rtl8188E_resume_flow
	#define Rtl8188E_NIC_PDN_FLOW					rtl8188E_hwpdn_flow
	#define Rtl8188E_NIC_LPS_ENTER_FLOW			rtl8188E_enter_lps_flow
	#define Rtl8188E_NIC_LPS_LEAVE_FLOW			rtl8188E_leave_lps_flow

#elif defined(CONFIG_PCI_HCI)
	#define RTL8188E_FW_UMC_IMG				"rtl8188E\\rtl8188efw.bin"
	#define RTL8188E_PHY_REG					"rtl8188E\\PHY_REG_1T.txt"
	#define RTL8188E_PHY_RADIO_A				"rtl8188E\\radio_a_1T.txt"
	#define RTL8188E_PHY_RADIO_B				"rtl8188E\\radio_b_1T.txt"
	#define RTL8188E_AGC_TAB					"rtl8188E\\AGC_TAB_1T.txt"
	#define RTL8188E_PHY_MACREG 				"rtl8188E\\MAC_REG.txt"
	#define RTL8188E_PHY_REG_PG				"rtl8188E\\PHY_REG_PG.txt"
	#define RTL8188E_PHY_REG_MP				"rtl8188E\\PHY_REG_MP.txt"

	#define Rtl8188E_PHY_REG_Array_PG			Rtl8188EEPHY_REG_Array_PG
	#define Rtl8188E_PHY_REG_Array_PGLength	Rtl8188EEPHY_REG_Array_PGLength	

		
	#ifndef CONFIG_PHY_SETTING_WITH_ODM		
	#if MP_DRIVER == 1
	#define Rtl8188ES_PHY_REG_Array_MP 			Rtl8188ESPHY_REG_Array_MP
	#endif
	#endif
	
	//---------------------------------------------------------------------
	//		RTL8188E Power Configuration CMDs for USB/SDIO/PCIE interfaces
	//---------------------------------------------------------------------
	#define Rtl8188E_NIC_PWR_ON_FLOW				rtl8188E_power_on_flow
	#define Rtl8188E_NIC_RF_OFF_FLOW				rtl8188E_radio_off_flow
	#define Rtl8188E_NIC_DISABLE_FLOW				rtl8188E_card_disable_flow
	#define Rtl8188E_NIC_ENABLE_FLOW				rtl8188E_card_enable_flow
	#define Rtl8188E_NIC_SUSPEND_FLOW				rtl8188E_suspend_flow
	#define Rtl8188E_NIC_RESUME_FLOW				rtl8188E_resume_flow
	#define Rtl8188E_NIC_PDN_FLOW					rtl8188E_hwpdn_flow
	#define Rtl8188E_NIC_LPS_ENTER_FLOW			rtl8188E_enter_lps_flow
	#define Rtl8188E_NIC_LPS_LEAVE_FLOW			rtl8188E_leave_lps_flow
#endif //CONFIG_***_HCI


#define DRVINFO_SZ	4 // unit is 8bytes
#define PageNum_128(_Len)		(u32)(((_Len)>>7) + ((_Len)&0x7F ? 1:0))


#if 1 // download firmware related data structure
#define FW_8188E_SIZE				0x4000 //16384,16k
#define FW_8188E_START_ADDRESS	0x1000
#define FW_8188E_END_ADDRESS		0x1FFF //0x5FFF

#define MAX_PAGE_SIZE			4096	// @ page : 4k bytes

#define IS_FW_HEADER_EXIST(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88E0)

typedef enum _FIRMWARE_SOURCE {
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,		//from header file
} FIRMWARE_SOURCE, *PFIRMWARE_SOURCE;

typedef struct _RT_FIRMWARE {
	FIRMWARE_SOURCE	eFWSource;
#ifdef CONFIG_EMBEDDED_FWIMG
	u8*			szFwBuffer;
#else
	u8			szFwBuffer[FW_8188E_SIZE];
#endif
	u32			ulFwLength;

#ifdef CONFIG_WOWLAN
	u8*			szWoWLANFwBuffer;
	u32			ulWoWLANFwLength;
#endif //CONFIG_WOWLAN
} RT_FIRMWARE, *PRT_FIRMWARE, RT_FIRMWARE_8188E, *PRT_FIRMWARE_8188E;

//
// This structure must be cared byte-ordering
//

typedef struct _RT_8188E_FIRMWARE_HDR
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
	u8		Foundry;
	u8		Rsvd2;

	//--- LONG WORD 2 ----
	u32		SvnIdx;	// The SVN entry index
	u32		Rsvd3;

	//--- LONG WORD 3 ----
	u32		Rsvd4;
	u32		Rsvd5;
}RT_8188E_FIRMWARE_HDR, *PRT_8188E_FIRMWARE_HDR;
#endif // download firmware related data structure


#define DRIVER_EARLY_INT_TIME		0x05
#define BCN_DMA_ATIME_INT_TIME		0x02

#ifdef CONFIG_USB_RX_AGGREGATION

typedef enum _USB_RX_AGG_MODE{
	USB_RX_AGG_DISABLE,
	USB_RX_AGG_DMA,
	USB_RX_AGG_USB,
	USB_RX_AGG_MIX
}USB_RX_AGG_MODE;

//#define MAX_RX_DMA_BUFFER_SIZE	10240		// 10K for 8192C RX DMA buffer

#endif


#define MAX_RX_DMA_BUFFER_SIZE_88E	      0x2400 //9k for 88E nornal chip , //MaxRxBuff=10k-max(TxReportSize(64*8), WOLPattern(16*24))

#define MAX_TX_REPORT_BUFFER_SIZE			0x0400 // 1k 


// BK, BE, VI, VO, HCCA, MANAGEMENT, COMMAND, HIGH, BEACON.
#define MAX_TX_QUEUE		9

#define TX_SELE_HQ			BIT(0)		// High Queue
#define TX_SELE_LQ			BIT(1)		// Low Queue
#define TX_SELE_NQ			BIT(2)		// Normal Queue

// Note: We will divide number of page equally for each queue other than public queue!
// 22k = 22528 bytes = 176 pages (@page =  128 bytes)
// must reserved about 7 pages for LPS =>  176-7 = 169 (0xA9)
// 2*BCN / 1*ps-poll / 1*null-data /1*prob_rsp /1*QOS null-data /1*BT QOS null-data 

#define TX_TOTAL_PAGE_NUMBER_88E		0xA9//  169 (21632=> 21k)

#ifdef RTL8188ES_MAC_LOOPBACK
#define TX_PAGE_BOUNDARY_88E 0x48 //72
#else //TX_PAGE_BOUNDARY_LOOPBACK_MODE
#define TX_PAGE_BOUNDARY_88E (TX_TOTAL_PAGE_NUMBER_88E + 1)
#endif


//Note: For Normal Chip Setting ,modify later
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER	TX_TOTAL_PAGE_NUMBER_88E  //0xA9 , 0xb0=>176=>22k
#define WMM_NORMAL_TX_PAGE_BOUNDARY_88E	(WMM_NORMAL_TX_TOTAL_PAGE_NUMBER + 1) //0xA9



//-------------------------------------------------------------------------
//	Chip specific
//-------------------------------------------------------------------------
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R	0x1
#define CHIP_BONDING_88C_USB_MCARD	0x2
#define CHIP_BONDING_88C_USB_HP	0x1
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

typedef struct _TxPowerInfo
{
	u8 CCKIndex[RF_PATH_MAX][CHANNEL_GROUP_MAX_88E];
	u8 HT40_1SIndex[RF_PATH_MAX][CHANNEL_GROUP_MAX_88E];
	u8 HT40_2SIndexDiff[RF_PATH_MAX][CHANNEL_GROUP_MAX_88E];
	u8 HT20IndexDiff[RF_PATH_MAX][CHANNEL_GROUP_MAX_88E];
	u8 OFDMIndexDiff[RF_PATH_MAX][CHANNEL_GROUP_MAX_88E];
	u8 HT40MaxOffset[RF_PATH_MAX][CHANNEL_GROUP_MAX_88E];
	u8 HT20MaxOffset[RF_PATH_MAX][CHANNEL_GROUP_MAX_88E];
	u8 TSSI_A[3];
	u8 TSSI_B[3];
	u8 TSSI_A_5G[3];		//5GL/5GM/5GH
	u8 TSSI_B_5G[3];
} TxPowerInfo, *PTxPowerInfo;

typedef struct _TxPowerInfo24G{
	u1Byte IndexCCK_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G];
	u1Byte IndexBW40_Base[MAX_RF_PATH][MAX_CHNL_GROUP_24G-1];
	//If only one tx, only BW20 and OFDM are used.
	s1Byte CCK_Diff[MAX_RF_PATH][MAX_TX_COUNT];	
	s1Byte OFDM_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s1Byte BW20_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s1Byte BW40_Diff[MAX_RF_PATH][MAX_TX_COUNT];
}TxPowerInfo24G, *PTxPowerInfo24G;

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
#define EFUSE_OOB_PROTECT_BYTES 		15	// PG data exclude header, dummy 6 bytes frome CP test and reserved 1byte.

#define		HWSET_MAX_SIZE_88E		512

#define		EFUSE_REAL_CONTENT_LEN_88E	256
#define		EFUSE_MAP_LEN_88E		512
#define		EFUSE_MAX_SECTION_88E		64
#define		EFUSE_MAX_WORD_UNIT_88E		4
#define		EFUSE_IC_ID_OFFSET_88E			506	//For some inferiority IC purpose. added by Roger, 2009.09.02.
#define 		AVAILABLE_EFUSE_ADDR_88E(addr) 	(addr < EFUSE_REAL_CONTENT_LEN_88E)
// <Roger_Notes> To prevent out of boundary programming case, leave 1byte and program full section
// 9bytes + 1byt + 5bytes and pre 1byte.
// For worst case:
// | 2byte|----8bytes----|1byte|--7bytes--| //92D
#define 		EFUSE_OOB_PROTECT_BYTES_88E	18 	// PG data exclude header, dummy 7 bytes frome CP test and reserved 1byte.
#define		EFUSE_PROTECT_BYTES_BANK_88E	16


//========================================================
//			EFUSE for BT definition
//========================================================
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


typedef struct hal_data_8188e
{
	HAL_VERSION			VersionID;
	RT_MULTI_FUNC		MultiFunc; // For multi-function consideration.
	RT_POLARITY_CTL		PolarityCtl; // For Wifi PDn Polarity control.
	RT_REGULATOR_MODE	RegulatorMode; // switching regulator or LDO
	u16	CustomerID;

	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;
	u16	FirmwareSignature;
	u8	PGMaxGroup;
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
	u8	bAPKThermalMeterIgnore;

	BOOLEAN 			EepromOrEfuse;
	u8				EfuseMap[2][HWSET_MAX_SIZE_512]; //92C:256bytes, 88E:512bytes, we use union set (512bytes)
	u8				EfuseUsedPercentage;
	EFUSE_HAL			EfuseHal;
	
	//u8	bIQKInitialized;

	
	u8	Index24G_CCK_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	u8	Index24G_BW40_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	//If only one tx, only BW20 and OFDM are used.
	s8	CCK_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];	
	s8	OFDM_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW20_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW40_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	
	u8	TxPwrLevelCck[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	// For HT 40MHZ pwr
	u8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// HT 20<->40 Pwr diff
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];// For HT<->legacy pwr diff
	// For power group
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	u8	LegacyHTTxPowerDiff;// Legacy to HT rate power diff
	// The current Tx Power Level
	u8	CurrentCckTxPwrIdx;
	u8	CurrentOfdm24GTxPwrIdx;
	u8	CurrentBW2024GTxPwrIdx;
	u8	CurrentBW4024GTxPwrIdx;
	

	// Read/write are allow for following hardware information variables
	u8	framesync;
	u32	framesyncC34;
	u8	framesyncMonitor;
	u8	DefaultInitialGain[4];
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[MAX_PG_GROUP][16];
	u32	CCKTxPowerLevelOriginalOffset;

	u8	CrystalCap;
	u32	AntennaTxPath;					// Antenna path Tx
	u32	AntennaRxPath;					// Antenna path Rx
	u8	BluetoothCoexist;
	u8	ExternalPA;

	u8	bLedOpenDrain; // Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16.

	//u32	LedControlNum;
	//u32	LedControlMode;
	//u32	TxPowerTrackControl;
	u8	b1x1RecvCombine;	// for 1T1R receive combining

	//u8	bCurrentTurboEDCA;
	u32	AcParam_BE; //Original parameter for BE, use for EDCA turbo.	

	BB_REGISTER_DEFINITION_T	PHYRegDef[4];	//Radio A/B/C/D

	u32	RfRegChnlVal[2];

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
	u8	RegCR_1;

	struct dm_priv	dmpriv;
	DM_ODM_T 		odmpriv;
	//_lock			odm_stainfo_lock;
#ifdef DBG_CONFIG_ERROR_DETECT
	struct sreset_priv srestpriv;
#endif

#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	bt_coexist;
#endif

	u8	CurAntenna;
	u8	AntDivCfg;
	u8	TRxAntDivType;


	u8	bDumpRxPkt;//for debug
	u8	bDumpTxPkt;//for debug
	u8	FwRsvdPageStartOffset; //2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ.

	// 2010/08/09 MH Add CU power down mode.
	BOOLEAN		pwrdown;

	// Add for dual MAC  0--Mac0 1--Mac1
	u32	interfaceIndex;

	u8	OutEpQueueSel;
	u8	OutEpNumber;

	// 2010/12/10 MH Add for USB aggreation mode dynamic shceme.
	BOOLEAN		UsbRxHighSpeedMode;

	// 2010/11/22 MH Add for slim combo debug mode selective.
	// This is used for fix the drawback of CU TSMC-A/UMC-A cut. HW auto suspend ability. Close BT clock.
	BOOLEAN		SlimComboDbg;

	u16	EfuseUsedBytes;

#ifdef CONFIG_P2P
	struct P2P_PS_Offload_t	p2p_ps_offload;
#endif

    // Auto FSM to Turn On, include clock, isolation, power control for MAC only
	u8			bMacPwrCtrlOn;

#ifdef CONFIG_SDIO_HCI
	//
	// For SDIO Interface HAL related
	//

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
#endif //CONFIG_SDIO_HCI

#ifdef CONFIG_USB_HCI
	u32	UsbBulkOutSize;

	// Interrupt relatd register information.
	u32	IntArray[3];//HISR0,HISR1,HSISR
	u32	IntrMask[3];
	u8	C2hArray[16];
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
#endif //CONFIG_USB_HCI


#ifdef CONFIG_PCI_HCI

	//
	// EEPROM setting.
	//

	u16	EEPROMDID;
	u16	EEPROMSMID;
	u16	EEPROMChannelPlan;
	
	u8	EEPROMTSSI[2];
	u8	EEPROMBoardType;
	u32	TransmitConfig;	

	u32	IntrMask[2];
	u32	IntrMaskToSet[2];
	
	u8	bDefaultAntenna;
	u8	bIQKInitialized;
	
	u8	bInterruptMigration;
	u8	bDisableTxInt;
	u8	bGpioHwWpsPbc;
#endif //CONFIG_PCI_HCI


#ifdef CONFIG_TX_EARLY_MODE
	u8 			bEarlyModeEnable;
#endif
} HAL_DATA_8188E, *PHAL_DATA_8188E;

typedef struct hal_data_8188e HAL_DATA_TYPE, *PHAL_DATA_TYPE;


#define GET_HAL_DATA(__pAdapter)	((HAL_DATA_TYPE *)((__pAdapter)->HalData))
#define GET_RF_TYPE(priv)			(GET_HAL_DATA(priv)->rf_type)

#define INCLUDE_MULTI_FUNC_BT(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

//#define IS_MULTI_FUNC_CHIP(_Adapter)	(((((PHAL_DATA_TYPE)(_Adapter->HalData))->MultiFunc) & (RT_MULTI_FUNC_BT|RT_MULTI_FUNC_GPS)) ? _TRUE : _FALSE)

//#define RT_IS_FUNC_DISABLED(__pAdapter, __FuncBits) ( (__pAdapter)->DisabledFunctions & (__FuncBits) )

#ifdef CONFIG_PCI_HCI
void InterruptRecognized8188EE(PADAPTER Adapter, PRT_ISR_CONTENT pIsrContent);
void UpdateInterruptMask8188EE(PADAPTER Adapter, u32 AddMSR, u32 AddMSR1, u32 RemoveMSR, u32 RemoveMSR1);
#endif	//CONFIG_PCI_HCI

// rtl8188e_hal_init.c
#ifdef CONFIG_WOWLAN
s32 rtl8188e_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw);
#else
s32 rtl8188e_FirmwareDownload(PADAPTER padapter);
#endif
void _8051Reset88E(PADAPTER padapter);
void rtl8188e_InitializeFirmwareVars(PADAPTER padapter);


s32 InitLLTTable(PADAPTER padapter, u8 txpktbuf_bndy);
void Read_LLT_Tab(PADAPTER padapter);

// EFuse
u8 GetEEPROMSize8188E(PADAPTER padapter);
void Hal_InitPGData88E(PADAPTER padapter);
void Hal_EfuseParseIDCode88E(PADAPTER padapter, u8 *hwinfo);
void Hal_ReadTxPowerInfo88E(PADAPTER padapter,u8* hwinfo,BOOLEAN	AutoLoadFail);
	
void Hal_EfuseParseEEPROMVer88E(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void rtl8188e_EfuseParseChnlPlan(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_EfuseParseCustomerID88E(PADAPTER padapter, u8 *hwinfo, BOOLEAN AutoLoadFail);
void Hal_ReadAntennaDiversity88E	(PADAPTER pAdapter,u8*PROMContent,BOOLEAN AutoLoadFail);
void Hal_ReadThermalMeter_88E(PADAPTER	Adapter,u8* PROMContent,BOOLEAN 	AutoloadFail);
void Hal_EfuseParseXtal_8188E(PADAPTER pAdapter,u8* hwinfo,BOOLEAN AutoLoadFail);
void Hal_EfuseParseBoardType88E(PADAPTER pAdapter,u8* hwinfo,BOOLEAN AutoLoadFail);
void Hal_ReadPowerSavingMode88E(PADAPTER pAdapter,u8* hwinfo,BOOLEAN AutoLoadFail);

BOOLEAN HalDetectPwrDownMode88E(PADAPTER Adapter);
	
#ifdef CONFIG_WOWLAN
void Hal_DetectWoWMode(PADAPTER pAdapter);
#endif //CONFIG_WOWLAN

#ifdef CONFIG_RF_GAIN_OFFSET
void Hal_ReadRFGainOffset(PADAPTER pAdapter,u8* hwinfo,BOOLEAN AutoLoadFail);
#endif //CONFIG_RF_GAIN_OFFSET

//RT_CHANNEL_DOMAIN rtl8723a_HalMapChannelPlan(PADAPTER padapter, u8 HalChannelPlan);
//VERSION_8192C rtl8723a_ReadChipVersion(PADAPTER padapter);
//void rtl8723a_ReadBluetoothCoexistInfo(PADAPTER padapter, u8 *PROMContent, BOOLEAN AutoloadFail);
void Hal_InitChannelPlan(PADAPTER padapter);

void rtl8188e_set_hal_ops(struct hal_ops *pHalFunc);

// register
void SetBcnCtrlReg(PADAPTER padapter, u8 SetBits, u8 ClearBits);

void rtl8188e_start_thread(_adapter *padapter);
void rtl8188e_stop_thread(_adapter *padapter);

void rtw_IOL_cmd_tx_pkt_buf_dump(ADAPTER *Adapter,int data_len);
#ifdef CONFIG_IOL_EFUSE_PATCH
s32 rtl8188e_iol_efuse_patch(PADAPTER padapter);
#endif//CONFIG_IOL_EFUSE_PATCH

#endif //__RTL8188E_HAL_H__

