/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __HAL_DATA_H__
#define __HAL_DATA_H__

#include "odm_precomp.h"
#include <hal_btcoex.h>

#include <hal_sdio.h>

/*  */
/*  <Roger_Notes> For RTL8723 WiFi/BT/GPS multi-function configuration. 2010.10.06. */
/*  */
enum rt_multi_func {
	RT_MULTI_FUNC_NONE	= 0x00,
	RT_MULTI_FUNC_WIFI	= 0x01,
	RT_MULTI_FUNC_BT		= 0x02,
	RT_MULTI_FUNC_GPS	= 0x04,
};
/*  */
/*  <Roger_Notes> For RTL8723 WiFi PDn/GPIO polarity control configuration. 2010.10.08. */
/*  */
enum rt_polarity_ctl {
	RT_POLARITY_LOW_ACT	= 0,
	RT_POLARITY_HIGH_ACT	= 1,
};

/*  For RTL8723 regulator mode. by tynli. 2011.01.14. */
enum rt_regulator_mode {
	RT_SWITCHING_REGULATOR	= 0,
	RT_LDO_REGULATOR	= 1,
};

enum rt_ampdu_burst {
	RT_AMPDU_BURST_NONE	= 0,
	RT_AMPDU_BURST_92D	= 1,
	RT_AMPDU_BURST_88E	= 2,
	RT_AMPDU_BURST_8812_4	= 3,
	RT_AMPDU_BURST_8812_8	= 4,
	RT_AMPDU_BURST_8812_12	= 5,
	RT_AMPDU_BURST_8812_15	= 6,
	RT_AMPDU_BURST_8723B	= 7,
};

#define CHANNEL_MAX_NUMBER		(14 + 24 + 21)	/*  14 is the max channel number */
#define CHANNEL_MAX_NUMBER_2G		14
#define CHANNEL_MAX_NUMBER_5G		54			/*  Please refer to "phy_GetChnlGroup8812A" and "Hal_ReadTxPowerInfo8812A" */
#define CHANNEL_MAX_NUMBER_5G_80M	7
#define MAX_PG_GROUP			13

/*  Tx Power Limit Table Size */
#define MAX_REGULATION_NUM			4
#define MAX_2_4G_BANDWIDTH_NUM			4
#define MAX_RATE_SECTION_NUM			10
#define MAX_5G_BANDWIDTH_NUM			4

#define MAX_BASE_NUM_IN_PHY_REG_PG_2_4G		10 /*   CCK:1, OFDM:1, HT:4, VHT:4 */
#define MAX_BASE_NUM_IN_PHY_REG_PG_5G		9 /*  OFDM:1, HT:4, VHT:4 */


/*  duplicate code, will move to ODM ######### */
/* define IQK_MAC_REG_NUM		4 */
/* define IQK_ADDA_REG_NUM		16 */

/* define IQK_BB_REG_NUM			10 */

/* define HP_THERMAL_NUM		8 */
/*  duplicate code, will move to ODM ######### */

enum {
	SINGLEMAC_SINGLEPHY,	/* SMSP */
	DUALMAC_DUALPHY,		/* DMDP */
	DUALMAC_SINGLEPHY,	/* DMSP */
};

#define PAGE_SIZE_128	128
#define PAGE_SIZE_256	256
#define PAGE_SIZE_512	512

struct dm_priv {
	u8 DM_Type;

#define DYNAMIC_FUNC_BT BIT0

	u8 DMFlag;
	u8 InitDMFlag;
	/* u8   RSVD_1; */

	u32 InitODMFlag;
	/*  Upper and Lower Signal threshold for Rate Adaptive */
	int	UndecoratedSmoothedPWDB;
	int	UndecoratedSmoothedCCK;
	int	EntryMinUndecoratedSmoothedPWDB;
	int	EntryMaxUndecoratedSmoothedPWDB;
	int	MinUndecoratedPWDBForDM;
	int	LastMinUndecoratedPWDBForDM;

	s32	UndecoratedSmoothedBeacon;

/*  duplicate code, will move to ODM ######### */
	/* for High Power */
	u8 bDynamicTxPowerEnable;
	u8 LastDTPLvl;
	u8 DynamicTxHighPowerLvl;/* Add by Jacken Tx Power Control for Near/Far Range 2008/03/06 */

	/* for tx power tracking */
	u8 bTXPowerTracking;
	u8 TXPowercount;
	u8 bTXPowerTrackingInit;
	u8 TxPowerTrackControl;	/* for mp mode, turn off txpwrtracking as default */
	u8 TM_Trigger;

	u8 ThermalMeter[2];				/*  ThermalMeter, index 0 for RFIC0, and 1 for RFIC1 */
	u8 ThermalValue;
	u8 ThermalValue_LCK;
	u8 ThermalValue_IQK;
	u8 ThermalValue_DPK;
	u8 bRfPiEnable;
	/* u8   RSVD_2; */

	/* for APK */
	u32 APKoutput[2][2];	/* path A/B; output1_1a/output1_2a */
	u8 bAPKdone;
	u8 bAPKThermalMeterIgnore;
	u8 bDPdone;
	u8 bDPPathAOK;
	u8 bDPPathBOK;
	/* u8   RSVD_3; */
	/* u8   RSVD_4; */
	/* u8   RSVD_5; */

	/* for IQK */
	u32 ADDA_backup[IQK_ADDA_REG_NUM];
	u32 IQK_MAC_backup[IQK_MAC_REG_NUM];
	u32 IQK_BB_backup_recover[9];
	u32 IQK_BB_backup[IQK_BB_REG_NUM];

	u8 PowerIndex_backup[6];
	u8 OFDM_index[2];

	u8 bCCKinCH14;
	u8 CCK_index;
	u8 bDoneTxpower;
	u8 CCK_index_HP;

	u8 OFDM_index_HP[2];
	u8 ThermalValue_HP[HP_THERMAL_NUM];
	u8 ThermalValue_HP_index;
	/* u8   RSVD_6; */

	/* for TxPwrTracking2 */
	s32	RegE94;
	s32  RegE9C;
	s32	RegEB4;
	s32	RegEBC;

	u32 TXPowerTrackingCallbackCnt;	/* cosa add for debug */

	u32 prv_traffic_idx; /*  edca turbo */
/*  duplicate code, will move to ODM ######### */

	/*  Add for Reading Initial Data Rate SEL Register 0x484 during watchdog. Using for fill tx desc. 2011.3.21 by Thomas */
	u8 INIDATA_RATE[32];
};


struct hal_com_data {
	struct hal_version VersionID;
	enum rt_multi_func MultiFunc; /*  For multi-function consideration. */
	enum rt_polarity_ctl PolarityCtl; /*  For Wifi PDn Polarity control. */
	enum rt_regulator_mode	RegulatorMode; /*  switching regulator or LDO */

	u16 FirmwareVersion;
	u16 FirmwareVersionRev;
	u16 FirmwareSubVersion;
	u16 FirmwareSignature;

	/* current WIFI_PHY values */
	enum wireless_mode CurrentWirelessMode;
	enum channel_width CurrentChannelBW;
	enum band_type CurrentBandType;	/* 0:2.4G, 1:5G */
	enum band_type BandSet;
	u8 CurrentChannel;
	u8 CurrentCenterFrequencyIndex1;
	u8 nCur40MhzPrimeSC;/*  Control channel sub-carrier */
	u8 nCur80MhzPrimeSC;   /* used for primary 40MHz of 80MHz mode */

	u16 CustomerID;
	u16 BasicRateSet;
	u16 ForcedDataRate;/*  Force Data Rate. 0: Auto, 0x02: 1M ~ 0x6C: 54M. */
	u32 ReceiveConfig;

	/* rf_ctrl */
	u8 rf_chip;
	u8 rf_type;
	u8 PackageType;
	u8 NumTotalRFPath;

	u8 InterfaceSel;
	u8 framesync;
	u32 framesyncC34;
	u8 framesyncMonitor;
	u8 DefaultInitialGain[4];
	/*  EEPROM setting. */
	u16 EEPROMVID;
	u16 EEPROMSVID;

	u8 EEPROMCustomerID;
	u8 EEPROMSubCustomerID;
	u8 EEPROMVersion;
	u8 EEPROMRegulatory;
	u8 EEPROMThermalMeter;
	u8 EEPROMBluetoothCoexist;
	u8 EEPROMBluetoothType;
	u8 EEPROMBluetoothAntNum;
	u8 EEPROMBluetoothAntIsolation;
	u8 EEPROMBluetoothRadioShared;
	u8 bTXPowerDataReadFromEEPORM;
	u8 bAPKThermalMeterIgnore;
	u8 bDisableSWChannelPlan; /*  flag of disable software change channel plan */

	bool		EepromOrEfuse;
	u8 		EfuseUsedPercentage;
	u16 			EfuseUsedBytes;
	struct efuse_hal		EfuseHal;

	/* 3 [2.4G] */
	u8 Index24G_CCK_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	u8 Index24G_BW40_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	/* If only one tx, only BW20 and OFDM are used. */
	s8	CCK_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	OFDM_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW20_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW40_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	/* 3 [5G] */
	u8 Index5G_BW40_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	u8 Index5G_BW80_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER_5G_80M];
	s8	OFDM_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW20_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW40_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW80_5G_Diff[MAX_RF_PATH][MAX_TX_COUNT];

	u8 Regulation2_4G;
	u8 Regulation5G;

	u8 TxPwrInPercentage;

	u8 TxPwrCalibrateRate;
	/*  TX power by rate table at most 4RF path. */
	/*  The register is */
	/*  VHT TX power by rate off setArray = */
	/*  Band:-2G&5G = 0 / 1 */
	/*  RF: at most 4*4 = ABCD = 0/1/2/3 */
	/*  CCK = 0 OFDM = 1/2 HT-MCS 0-15 =3/4/56 VHT =7/8/9/10/11 */
	u8 TxPwrByRateTable;
	u8 TxPwrByRateBand;
	s8	TxPwrByRateOffset[TX_PWR_BY_RATE_NUM_BAND]
						 [TX_PWR_BY_RATE_NUM_RF]
						 [TX_PWR_BY_RATE_NUM_RF]
						 [TX_PWR_BY_RATE_NUM_RATE];
	/*  */

	/* 2 Power Limit Table */
	u8 TxPwrLevelCck[RF_PATH_MAX_92C_88E][CHANNEL_MAX_NUMBER];
	u8 TxPwrLevelHT40_1S[RF_PATH_MAX_92C_88E][CHANNEL_MAX_NUMBER];	/*  For HT 40MHZ pwr */
	u8 TxPwrLevelHT40_2S[RF_PATH_MAX_92C_88E][CHANNEL_MAX_NUMBER];	/*  For HT 40MHZ pwr */
	s8	TxPwrHt20Diff[RF_PATH_MAX_92C_88E][CHANNEL_MAX_NUMBER];/*  HT 20<->40 Pwr diff */
	u8 TxPwrLegacyHtDiff[RF_PATH_MAX_92C_88E][CHANNEL_MAX_NUMBER];/*  For HT<->legacy pwr diff */

	/*  Power Limit Table for 2.4G */
	s8	TxPwrLimit_2_4G[MAX_REGULATION_NUM]
						[MAX_2_4G_BANDWIDTH_NUM]
	                                [MAX_RATE_SECTION_NUM]
	                                [CHANNEL_MAX_NUMBER_2G]
						[MAX_RF_PATH_NUM];

	/*  Power Limit Table for 5G */
	s8	TxPwrLimit_5G[MAX_REGULATION_NUM]
						[MAX_5G_BANDWIDTH_NUM]
						[MAX_RATE_SECTION_NUM]
						[CHANNEL_MAX_NUMBER_5G]
						[MAX_RF_PATH_NUM];


	/*  Store the original power by rate value of the base of each rate section of rf path A & B */
	u8 TxPwrByRateBase2_4G[TX_PWR_BY_RATE_NUM_RF]
						[TX_PWR_BY_RATE_NUM_RF]
						[MAX_BASE_NUM_IN_PHY_REG_PG_2_4G];
	u8 TxPwrByRateBase5G[TX_PWR_BY_RATE_NUM_RF]
						[TX_PWR_BY_RATE_NUM_RF]
						[MAX_BASE_NUM_IN_PHY_REG_PG_5G];

	/*  For power group */
	u8 PwrGroupHT20[RF_PATH_MAX_92C_88E][CHANNEL_MAX_NUMBER];
	u8 PwrGroupHT40[RF_PATH_MAX_92C_88E][CHANNEL_MAX_NUMBER];




	u8 PGMaxGroup;
	u8 LegacyHTTxPowerDiff;/*  Legacy to HT rate power diff */
	/*  The current Tx Power Level */
	u8 CurrentCckTxPwrIdx;
	u8 CurrentOfdm24GTxPwrIdx;
	u8 CurrentBW2024GTxPwrIdx;
	u8 CurrentBW4024GTxPwrIdx;

	/*  Read/write are allow for following hardware information variables */
	u8 pwrGroupCnt;
	u32 MCSTxPowerLevelOriginalOffset[MAX_PG_GROUP][16];
	u32 CCKTxPowerLevelOriginalOffset;

	u8 CrystalCap;
	u32 AntennaTxPath;					/*  Antenna path Tx */
	u32 AntennaRxPath;					/*  Antenna path Rx */

	u8 PAType_2G;
	u8 PAType_5G;
	u8 LNAType_2G;
	u8 LNAType_5G;
	u8 ExternalPA_2G;
	u8 ExternalLNA_2G;
	u8 ExternalPA_5G;
	u8 ExternalLNA_5G;
	u8 TypeGLNA;
	u8 TypeGPA;
	u8 TypeALNA;
	u8 TypeAPA;
	u8 RFEType;
	u8 BoardType;
	u8 ExternalPA;
	u8 bIQKInitialized;
	bool		bLCKInProgress;

	bool		bSwChnl;
	bool		bSetChnlBW;
	bool		bChnlBWInitialized;
	bool		bNeedIQK;

	u8 bLedOpenDrain; /*  Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16. */
	u8 TxPowerTrackControl; /* for mp mode, turn off txpwrtracking as default */
	u8 b1x1RecvCombine;	/*  for 1T1R receive combining */

	u32 AcParam_BE; /* Original parameter for BE, use for EDCA turbo. */

	struct bb_register_def PHYRegDef[4];	/* Radio A/B/C/D */

	u32 RfRegChnlVal[2];

	/* RDG enable */
	bool	 bRDGEnable;

	/* for host message to fw */
	u8 LastHMEBoxNum;

	u8 fw_ractrl;
	u8 RegTxPause;
	/*  Beacon function related global variable. */
	u8 RegBcnCtrlVal;
	u8 RegFwHwTxQCtrl;
	u8 RegReg542;
	u8 RegCR_1;
	u8 Reg837;
	u8 RegRFPathS1;
	u16 RegRRSR;

	u8 CurAntenna;
	u8 AntDivCfg;
	u8 AntDetection;
	u8 TRxAntDivType;
	u8 ant_path; /* for 8723B s0/s1 selection */

	u8 u1ForcedIgiLb;			/*  forced IGI lower bound */

	u8 bDumpRxPkt;/* for debug */
	u8 bDumpTxPkt;/* for debug */
	u8 FwRsvdPageStartOffset; /* 2010.06.23. Added by tynli. Reserve page start offset except beacon in TxQ. */

	/*  2010/08/09 MH Add CU power down mode. */
	bool		pwrdown;

	/*  Add for dual MAC  0--Mac0 1--Mac1 */
	u32 interfaceIndex;

	u8 OutEpQueueSel;
	u8 OutEpNumber;

	/*  2010/12/10 MH Add for USB aggregation mode dynamic scheme. */
	bool		UsbRxHighSpeedMode;

	/*  2010/11/22 MH Add for slim combo debug mode selective. */
	/*  This is used for fix the drawback of CU TSMC-A/UMC-A cut. HW auto suspend ability. Close BT clock. */
	bool		SlimComboDbg;

	/* u8 AMPDUDensity; */

	/*  Auto FSM to Turn On, include clock, isolation, power control for MAC only */
	u8 bMacPwrCtrlOn;

	u8 RegIQKFWOffload;
	struct submit_ctx	iqk_sctx;

	enum rt_ampdu_burst	AMPDUBurstMode; /* 92C maybe not use, but for compile successfully */

	u32 		sdio_himr;
	u32 		sdio_hisr;

	/*  SDIO Tx FIFO related. */
	/*  HIQ, MID, LOW, PUB free pages; padapter->xmitpriv.free_txpg */
	u8 	SdioTxFIFOFreePage[SDIO_TX_FREE_PG_QUEUE];
	spinlock_t		SdioTxFIFOFreePageLock;
	u8 	SdioTxOQTMaxFreeSpace;
	u8 	SdioTxOQTFreeSpace;


	/*  SDIO Rx FIFO related. */
	u8 	SdioRxFIFOCnt;
	u16 		SdioRxFIFOSize;

	u32 		sdio_tx_max_len[SDIO_MAX_TX_QUEUE];/*  H, N, L, used for sdio tx aggregation max length per queue */

	struct dm_priv dmpriv;
	struct dm_odm_t		odmpriv;

	/*  For bluetooth co-existance */
	struct bt_coexist		bt_coexist;

	/*  Interrupt related register information. */
	u32 		SysIntrStatus;
	u32 		SysIntrMask;
};

#define GET_HAL_DATA(__padapter)	((struct hal_com_data *)((__padapter)->HalData))
#define GET_HAL_RFPATH_NUM(__padapter) (((struct hal_com_data *)((__padapter)->HalData))->NumTotalRFPath)
#define RT_GetInterfaceSelection(_Adapter)	(GET_HAL_DATA(_Adapter)->InterfaceSel)
#define GET_RF_TYPE(__padapter)		(GET_HAL_DATA(__padapter)->rf_type)

#endif /* __HAL_DATA_H__ */
