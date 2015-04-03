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
 ******************************************************************************/
#ifndef __RTL8723A_HAL_H__
#define __RTL8723A_HAL_H__

#include "rtl8723a_spec.h"
#include "rtl8723a_pg.h"
#include "Hal8723APhyReg.h"
#include "Hal8723APhyCfg.h"
#include "rtl8723a_rf.h"
#include "rtl8723a_bt_intf.h"
#ifdef CONFIG_8723AU_BT_COEXIST
#include "rtl8723a_bt-coexist.h"
#endif
#include "rtl8723a_dm.h"
#include "rtl8723a_recv.h"
#include "rtl8723a_xmit.h"
#include "rtl8723a_cmd.h"
#include "rtl8723a_sreset.h"
#include "rtw_efuse.h"
#include "rtw_eeprom.h"

#include "odm_precomp.h"
#include "odm.h"


/* 2TODO: We should define 8192S firmware related macro settings here!! */
#define RTL819X_DEFAULT_RF_TYPE			RF_1T2R
#define RTL819X_TOTAL_RF_PATH				2

/*  */
/*		RTL8723S From header */
/*  */

/*  Fw Array */
#define Rtl8723_FwImageArray				Rtl8723UFwImgArray
#define Rtl8723_FwUMCBCutImageArrayWithBT		Rtl8723UFwUMCBCutImgArrayWithBT
#define Rtl8723_FwUMCBCutImageArrayWithoutBT	Rtl8723UFwUMCBCutImgArrayWithoutBT

#define Rtl8723_ImgArrayLength				Rtl8723UImgArrayLength
#define Rtl8723_UMCBCutImgArrayWithBTLength		Rtl8723UUMCBCutImgArrayWithBTLength
#define Rtl8723_UMCBCutImgArrayWithoutBTLength	Rtl8723UUMCBCutImgArrayWithoutBTLength

#define Rtl8723_PHY_REG_Array_PG			Rtl8723UPHY_REG_Array_PG
#define Rtl8723_PHY_REG_Array_PGLength		Rtl8723UPHY_REG_Array_PGLength

#define Rtl8723_FwUMCBCutMPImageArray		Rtl8723SFwUMCBCutMPImgAr
#define Rtl8723_UMCBCutMPImgArrayLength		Rtl8723SUMCBCutMPImgArrayLength

#define DRVINFO_SZ				4 /*  unit is 8bytes */
#define PageNum_128(_Len)		(u32)(((_Len)>>7) + ((_Len)&0x7F ? 1:0))

#define FW_8723A_SIZE			0x8000
#define FW_8723A_START_ADDRESS	0x1000
#define FW_8723A_END_ADDRESS		0x1FFF /* 0x5FFF */

#define MAX_PAGE_SIZE			4096	/*  @ page : 4k bytes */

#define IS_FW_HEADER_EXIST(_pFwHdr)	((le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x92C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x88C0 ||\
									(le16_to_cpu(_pFwHdr->Signature)&0xFFF0) == 0x2300)

/*  */
/*  This structure must be cared byte-ordering */
/*  */
/*  Added by tynli. 2009.12.04. */
struct rt_8723a_firmware_hdr {
	/*  8-byte alinment required */

	/*  LONG WORD 0 ---- */
	u16		Signature;	/*  92C0: test chip; 92C, 88C0: test chip; 88C1: MP A-cut; 92C1: MP A-cut */
	u8		Category;	/*  AP/NIC and USB/PCI */
	u8		Function;	/*  Reserved for different FW function indcation, for further use when driver needs to download different FW in different conditions */
	u16		Version;		/*  FW Version */
	u8		Subversion;	/*  FW Subversion, default 0x00 */
	u16		Rsvd1;


	/*  LONG WORD 1 ---- */
	u8		Month;	/*  Release time Month field */
	u8		Date;	/*  Release time Date field */
	u8		Hour;	/*  Release time Hour field */
	u8		Minute;	/*  Release time Minute field */
	u16		RamCodeSize;	/*  The size of RAM code */
	u16		Rsvd2;

	/*  LONG WORD 2 ---- */
	u32		SvnIdx;	/*  The SVN entry index */
	u32		Rsvd3;

	/*  LONG WORD 3 ---- */
	u32		Rsvd4;
	u32		Rsvd5;
};

#define DRIVER_EARLY_INT_TIME		0x05
#define BCN_DMA_ATIME_INT_TIME		0x02


/*  BK, BE, VI, VO, HCCA, MANAGEMENT, COMMAND, HIGH, BEACON. */
#define MAX_TX_QUEUE		9

#define TX_SELE_HQ			BIT(0)		/*  High Queue */
#define TX_SELE_LQ			BIT(1)		/*  Low Queue */
#define TX_SELE_NQ			BIT(2)		/*  Normal Queue */

/*  Note: We will divide number of page equally for each queue other than public queue! */
#define TX_TOTAL_PAGE_NUMBER	0xF8
#define TX_PAGE_BOUNDARY		(TX_TOTAL_PAGE_NUMBER + 1)

/*  For Normal Chip Setting */
/*  (HPQ + LPQ + NPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER */
#define NORMAL_PAGE_NUM_PUBQ	0xE7
#define NORMAL_PAGE_NUM_HPQ		0x0C
#define NORMAL_PAGE_NUM_LPQ		0x02
#define NORMAL_PAGE_NUM_NPQ		0x02

/*  For Test Chip Setting */
/*  (HPQ + LPQ + PUBQ) shall be TX_TOTAL_PAGE_NUMBER */
#define TEST_PAGE_NUM_PUBQ		0x7E

/*  For Test Chip Setting */
#define WMM_TEST_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_TEST_TX_PAGE_BOUNDARY		(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) /* F6 */

#define WMM_TEST_PAGE_NUM_PUBQ		0xA3
#define WMM_TEST_PAGE_NUM_HPQ		0x29
#define WMM_TEST_PAGE_NUM_LPQ		0x29

/*  Note: For Normal Chip Setting, modify later */
#define WMM_NORMAL_TX_TOTAL_PAGE_NUMBER	0xF5
#define WMM_NORMAL_TX_PAGE_BOUNDARY		(WMM_TEST_TX_TOTAL_PAGE_NUMBER + 1) /* F6 */

#define WMM_NORMAL_PAGE_NUM_PUBQ	0xB0
#define WMM_NORMAL_PAGE_NUM_HPQ		0x29
#define WMM_NORMAL_PAGE_NUM_LPQ		0x1C
#define WMM_NORMAL_PAGE_NUM_NPQ		0x1C


/*  */
/*	Chip specific */
/*  */
#define CHIP_BONDING_IDENTIFIER(_value)	(((_value)>>22)&0x3)
#define CHIP_BONDING_92C_1T2R			0x1
#define CHIP_BONDING_88C_USB_MCARD		0x2
#define CHIP_BONDING_88C_USB_HP			0x1

#include "HalVerDef.h"
#include "hal_com.h"

/*  */
/*	Channel Plan */
/*  */
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

#define EFUSE_REAL_CONTENT_LEN		512
#define EFUSE_MAP_LEN				128
#define EFUSE_MAX_SECTION			16
#define EFUSE_IC_ID_OFFSET			506	/* For some inferiority IC purpose. added by Roger, 2009.09.02. */
#define AVAILABLE_EFUSE_ADDR(addr)	(addr < EFUSE_REAL_CONTENT_LEN)
/*  */
/*  <Roger_Notes> */
/*  To prevent out of boundary programming case, */
/*  leave 1byte and program full section */
/*  9bytes + 1byt + 5bytes and pre 1byte. */
/*  For worst case: */
/*  | 1byte|----8bytes----|1byte|--5bytes--| */
/*  |         |            Reserved(14bytes)	      | */
/*  */

/*  PG data exclude header, dummy 6 bytes frome CP test and reserved 1byte. */
#define EFUSE_OOB_PROTECT_BYTES			15

#define EFUSE_REAL_CONTENT_LEN_8723A	512
#define EFUSE_MAP_LEN_8723A				256
#define EFUSE_MAX_SECTION_8723A			32

/*  */
/*			EFUSE for BT definition */
/*  */
#define EFUSE_BT_REAL_BANK_CONTENT_LEN	512
#define EFUSE_BT_REAL_CONTENT_LEN		1536	/*  512*3 */
#define EFUSE_BT_MAP_LEN				1024	/*  1k bytes */
#define EFUSE_BT_MAX_SECTION			128		/*  1024/8 */

#define EFUSE_PROTECT_BYTES_BANK		16

/*  */
/*  <Roger_Notes> For RTL8723 WiFi/BT/GPS multi-function configuration. 2010.10.06. */
/*  */
enum RT_MULTI_FUNC {
	RT_MULTI_FUNC_NONE = 0x00,
	RT_MULTI_FUNC_WIFI = 0x01,
	RT_MULTI_FUNC_BT = 0x02,
	RT_MULTI_FUNC_GPS = 0x04,
};

/*  */
/*  <Roger_Notes> For RTL8723 WiFi PDn/GPIO polarity control configuration. 2010.10.08. */
/*  */
enum RT_POLARITY_CTL {
	RT_POLARITY_LOW_ACT = 0,
	RT_POLARITY_HIGH_ACT = 1,
};

/*  For RTL8723 regulator mode. by tynli. 2011.01.14. */
enum RT_REGULATOR_MODE {
	RT_SWITCHING_REGULATOR = 0,
	RT_LDO_REGULATOR = 1,
};

/*  Description: Determine the types of C2H events that are the same in driver and Fw. */
/*  Fisrt constructed by tynli. 2009.10.09. */
enum {
	C2H_DBG = 0,
	C2H_TSF = 1,
	C2H_AP_RPT_RSP = 2,
	C2H_CCX_TX_RPT = 3,	/*  The FW notify the report of the specific tx packet. */
	C2H_BT_RSSI = 4,
	C2H_BT_OP_MODE = 5,
	C2H_EXT_RA_RPT = 6,
	C2H_HW_INFO_EXCH = 10,
	C2H_C2H_H2C_TEST = 11,
	C2H_BT_INFO = 12,
	C2H_BT_MP_INFO = 15,
	MAX_C2HEVENT
};

struct hal_data_8723a {
	struct hal_version		VersionID;
	enum rt_customer_id CustomerID;

	u16	FirmwareVersion;
	u16	FirmwareVersionRev;
	u16	FirmwareSubVersion;
	u16	FirmwareSignature;

	/* current WIFI_PHY values */
	u32	ReceiveConfig;
	enum WIRELESS_MODE		CurrentWirelessMode;
	enum ht_channel_width	CurrentChannelBW;
	u8	CurrentChannel;
	u8	nCur40MhzPrimeSC;/*  Control channel sub-carrier */

	u16	BasicRateSet;

	/* rf_ctrl */
	u8	rf_type;
	u8	NumTotalRFPath;

	u8	BoardType;
	u8	CrystalCap;
	/*  */
	/*  EEPROM setting. */
	/*  */
	u8	EEPROMVersion;
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
	u8	TxPwrLevelHT40_1S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	/*  For HT 40MHZ pwr */
	u8	TxPwrLevelHT40_2S[RF_PATH_MAX][CHANNEL_MAX_NUMBER];	/*  For HT 40MHZ pwr */
	u8	TxPwrHt20Diff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];/*  HT 20<->40 Pwr diff */
	u8	TxPwrLegacyHtDiff[RF_PATH_MAX][CHANNEL_MAX_NUMBER];/*  For HT<->legacy pwr diff */
	/*  For power group */
	u8	PwrGroupHT20[RF_PATH_MAX][CHANNEL_MAX_NUMBER];
	u8	PwrGroupHT40[RF_PATH_MAX][CHANNEL_MAX_NUMBER];

	u8	LegacyHTTxPowerDiff;/*  Legacy to HT rate power diff */

	/*  Read/write are allow for following hardware information variables */
	u8	framesync;
	u32	framesyncC34;
	u8	framesyncMonitor;
	u8	pwrGroupCnt;
	u32	MCSTxPowerLevelOriginalOffset[7][16];
	u32	CCKTxPowerLevelOriginalOffset;

	u32	AntennaTxPath;					/*  Antenna path Tx */
	u32	AntennaRxPath;					/*  Antenna path Rx */
	u8	ExternalPA;

	u8	bLedOpenDrain; /*  Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16. */

	u8	b1x1RecvCombine;	/*  for 1T1R receive combining */

	/*  For EDCA Turbo mode */

	u32	AcParam_BE; /* Original parameter for BE, use for EDCA turbo. */

	/* vivi, for tx power tracking, 20080407 */
	/* u16	TSSI_13dBm; */
	/* u32	Pwr_Track; */
	/*  The current Tx Power Level */
	u8	CurrentCckTxPwrIdx;
	u8	CurrentOfdm24GTxPwrIdx;

	struct bb_reg_define	PHYRegDef[4];	/* Radio A/B/C/D */

	bool		bRFPathRxEnable[4];	/*  We support 4 RF path now. */

	u32	RfRegChnlVal[2];

	u8	bCckHighPower;

	/* RDG enable */
	bool	 bRDGEnable;

	/* for host message to fw */
	u8	LastHMEBoxNum;

	u8	RegTxPause;
	/*  Beacon function related global variable. */
	u8	RegFwHwTxQCtrl;
	u8	RegReg542;

	struct dm_priv	dmpriv;
	struct dm_odm_t		odmpriv;
	struct sreset_priv srestpriv;

#ifdef CONFIG_8723AU_BT_COEXIST
	u8				bBTMode;
	/*  BT only. */
	struct bt_30info		BtInfo;
	/*  For bluetooth co-existance */
	struct bt_coexist_str	bt_coexist;
#endif

	u8	bDumpRxPkt;/* for debug */
	u8	FwRsvdPageStartOffset; /* 2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ. */

	/*  2010/08/09 MH Add CU power down mode. */
	u8	pwrdown;

	u8	OutEpQueueSel;
	u8	OutEpNumber;

	/*  */
	/*  Add For EEPROM Efuse switch and  Efuse Shadow map Setting */
	/*  */
	u8			EepromOrEfuse;
	u16			EfuseUsedBytes;
	u16			BTEfuseUsedBytes;

	/*  Interrupt relatd register information. */
	u32			SysIntrStatus;
	u32			SysIntrMask;

	/*  */
	/*  2011/02/23 MH Add for 8723 mylti function definition. The define should be moved to an */
	/*  independent file in the future. */
	/*  */
	/* 8723-----------------------------------------*/
	enum RT_MULTI_FUNC	MultiFunc; /*  For multi-function consideration. */
	enum RT_POLARITY_CTL	PolarityCtl; /*  For Wifi PDn Polarity control. */
	enum RT_REGULATOR_MODE	RegulatorMode; /*  switching regulator or LDO */
	/* 8723-----------------------------------------
	 *  2011/02/23 MH Add for 8723 mylti function definition. The define should be moved to an */
	/*  independent file in the future. */

	/*  Interrupt related register information. */
	u32	IntArray[2];
	u32	IntrMask[2];
};

#define GET_HAL_DATA(__pAdapter)	((struct hal_data_8723a *)((__pAdapter)->HalData))
#define GET_RF_TYPE(priv)			(GET_HAL_DATA(priv)->rf_type)

#define INCLUDE_MULTI_FUNC_BT(_Adapter)		(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_BT)
#define INCLUDE_MULTI_FUNC_GPS(_Adapter)	(GET_HAL_DATA(_Adapter)->MultiFunc & RT_MULTI_FUNC_GPS)

struct rxreport_8723a {
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
};

/*  rtl8723a_hal_init.c */
s32 rtl8723a_FirmwareDownload(struct rtw_adapter *padapter);
void rtl8723a_FirmwareSelfReset(struct rtw_adapter *padapter);
void rtl8723a_InitializeFirmwareVars(struct rtw_adapter *padapter);

void rtl8723a_InitAntenna_Selection(struct rtw_adapter *padapter);
void rtl8723a_DeinitAntenna_Selection(struct rtw_adapter *padapter);
void rtl8723a_CheckAntenna_Selection(struct rtw_adapter *padapter);
void rtl8723a_init_default_value(struct rtw_adapter *padapter);

s32 InitLLTTable23a(struct rtw_adapter *padapter, u32 boundary);

s32 CardDisableHWSM(struct rtw_adapter *padapter, u8 resetMCU);
s32 CardDisableWithoutHWSM(struct rtw_adapter *padapter);

/*  EFuse */
u8 GetEEPROMSize8723A(struct rtw_adapter *padapter);
void Hal_InitPGData(struct rtw_adapter *padapter, u8 *PROMContent);
void Hal_EfuseParseIDCode(struct rtw_adapter *padapter, u8 *hwinfo);
void Hal_EfuseParsetxpowerinfo_8723A(struct rtw_adapter *padapter, u8 *PROMContent, bool AutoLoadFail);
void Hal_EfuseParseBTCoexistInfo_8723A(struct rtw_adapter *padapter, u8 *hwinfo, bool AutoLoadFail);
void Hal_EfuseParseEEPROMVer(struct rtw_adapter *padapter, u8 *hwinfo, bool AutoLoadFail);
void rtl8723a_EfuseParseChnlPlan(struct rtw_adapter *padapter, u8 *hwinfo, bool AutoLoadFail);
void Hal_EfuseParseCustomerID(struct rtw_adapter *padapter, u8 *hwinfo, bool AutoLoadFail);
void Hal_EfuseParseAntennaDiversity(struct rtw_adapter *padapter, u8 *hwinfo, bool AutoLoadFail);
void Hal_EfuseParseRateIndicationOption(struct rtw_adapter *padapter, u8 *hwinfo, bool AutoLoadFail);
void Hal_EfuseParseXtal_8723A(struct rtw_adapter *pAdapter, u8 *hwinfo, u8 AutoLoadFail);
void Hal_EfuseParseThermalMeter_8723A(struct rtw_adapter *padapter, u8 *hwinfo, bool AutoLoadFail);

/*  register */
void SetBcnCtrlReg23a(struct rtw_adapter *padapter, u8 SetBits, u8 ClearBits);
void rtl8723a_InitBeaconParameters(struct rtw_adapter *padapter);

void rtl8723a_start_thread(struct rtw_adapter *padapter);
void rtl8723a_stop_thread(struct rtw_adapter *padapter);

bool c2h_id_filter_ccx_8723a(u8 id);
int c2h_handler_8723a(struct rtw_adapter *padapter, struct c2h_evt_hdr *c2h_evt);

void rtl8723a_read_adapter_info(struct rtw_adapter *Adapter);
void rtl8723a_read_chip_version(struct rtw_adapter *padapter);
void rtl8723a_notch_filter(struct rtw_adapter *adapter, bool enable);
void rtl8723a_SetBeaconRelatedRegisters(struct rtw_adapter *padapter);
void rtl8723a_SetHalODMVar(struct rtw_adapter *Adapter,
			   enum hal_odm_variable eVariable,
			   void *pValue1, bool bSet);
void
rtl8723a_readefuse(struct rtw_adapter *padapter,
		   u8 efuseType, u16 _offset, u16 _size_byte, u8 *pbuf);
u16 rtl8723a_EfuseGetCurrentSize_WiFi(struct rtw_adapter *padapter);
u16 rtl8723a_EfuseGetCurrentSize_BT(struct rtw_adapter *padapter);
void rtl8723a_update_ramask(struct rtw_adapter *padapter,
			    u32 mac_id, u8 rssi_level);

#endif
