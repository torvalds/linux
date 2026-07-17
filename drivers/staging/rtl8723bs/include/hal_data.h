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

#define CHANNEL_MAX_NUMBER		(14)	/*  14 is the max channel number */
#define CHANNEL_MAX_NUMBER_2G		14
#define MAX_PG_GROUP			13

/*  Tx Power Limit Table Size */
#define MAX_REGULATION_NUM			4
#define MAX_2_4G_BANDWIDTH_NUM			2
#define MAX_RATE_SECTION_NUM			3 /* CCK:1, OFDM:1, HT:1 */

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

#define DYNAMIC_FUNC_BT BIT(0)

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
	u16 FirmwareVersion;
	u16 FirmwareVersionRev;
	u16 FirmwareSubVersion;
	u16 FirmwareSignature;

	/* current WIFI_PHY values */
	enum wireless_mode CurrentWirelessMode;
	enum channel_width CurrentChannelBW;
	u8 CurrentChannel;
	u8 CurrentCenterFrequencyIndex1;
	u8 nCur40MhzPrimeSC;/*  Control channel sub-carrier */
	u8 nCur80MhzPrimeSC;   /* used for primary 40MHz of 80MHz mode */

	u16 CustomerID;
	u16 BasicRateSet;
	u32 ReceiveConfig;

	/* rf_ctrl */
	u8 PackageType;

	/*  EEPROM setting. */
	u8 EEPROMRegulatory;
	u8 EEPROMThermalMeter;
	u8 EEPROMBluetoothCoexist;
	u8 EEPROMBluetoothAntNum;
	u8 bDisableSWChannelPlan; /*  flag of disable software change channel plan */

	bool		EepromOrEfuse;
	u8 		EfuseUsedPercentage;
	u16 			EfuseUsedBytes;
	struct efuse_hal		EfuseHal;

	/* 3 [2.4G] */
	u8 Index24G_CCK_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	u8 Index24G_BW40_Base[MAX_RF_PATH][CHANNEL_MAX_NUMBER];
	/* If only one tx, only BW20 and OFDM are used. */
	s8	OFDM_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW20_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];
	s8	BW40_24G_Diff[MAX_RF_PATH][MAX_TX_COUNT];

	u8 Regulation2_4G;

	/*  TX power by rate table */
	/*  RF: at most 2 = AB = 0/1 */
	/*  CCK = 0 OFDM = 1 HT-MCS 0-7 = 2 */
	s8 TxPwrByRateOffset[MAX_RF_PATH_NUM][TX_PWR_BY_RATE_NUM_RATE];
	/*  */

	/*  Power Limit Table for 2.4G */
	s8	TxPwrLimit_2_4G[MAX_REGULATION_NUM]
						[MAX_2_4G_BANDWIDTH_NUM]
	                                [MAX_RATE_SECTION_NUM]
	                                [CHANNEL_MAX_NUMBER_2G]
						[MAX_RF_PATH_NUM];

	/*  Store the original power by rate value of the base of each rate section of rf path A & B */
	u8 TxPwrByRateBase2_4G[MAX_RF_PATH_NUM][MAX_RATE_SECTION_NUM];

	u8 CrystalCap;

	u8 TypeGLNA;
	u8 TypeGPA;
	u8 TypeALNA;
	u8 TypeAPA;
	u8 RFEType;
	u8 BoardType;
	bool		bLCKInProgress;

	bool		bSwChnl;
	bool		bSetChnlBW;

	u8 TxPowerTrackControl; /* for mp mode, turn off txpwrtracking as default */

	u32 AcParam_BE; /* Original parameter for BE, use for EDCA turbo. */

	struct bb_register_def PHYRegDef[4];	/* Radio A/B/C/D */

	u32 RfRegChnlVal[2];

	/* for host message to fw */
	u8 LastHMEBoxNum;

	u8 fw_ractrl;
	/*  Beacon function related global variable. */
	u8 RegFwHwTxQCtrl;
	u8 RegReg542;
	u16 RegRRSR;

	u8 CurAntenna;
	u8 AntDivCfg;
	u8 AntDetection;
	u8 ant_path; /* for 8723B s0/s1 selection */

	u8 u1ForcedIgiLb;			/*  forced IGI lower bound */

	/*  2010/08/09 MH Add CU power down mode. */
	bool		pwrdown;

	u8 OutEpQueueSel;
	u8 OutEpNumber;

	/*  Auto FSM to Turn On, include clock, isolation, power control for MAC only */
	u8 bMacPwrCtrlOn;

	enum rt_ampdu_burst	AMPDUBurstMode; /* 92C maybe not use, but for compile successfully */

	u32 		sdio_himr;
	u32 		sdio_hisr;

	/*  SDIO Tx FIFO related. */
	/*  HIQ, MID, LOW, PUB free pages; padapter->xmitpriv.free_txpg */
	u8 	SdioTxFIFOFreePage[SDIO_TX_FREE_PG_QUEUE];
	u8 	SdioTxOQTMaxFreeSpace;
	u8 	SdioTxOQTFreeSpace;


	/*  SDIO Rx FIFO related. */
	u8 	SdioRxFIFOCnt;
	u16 		SdioRxFIFOSize;

	u32 		sdio_tx_max_len[SDIO_MAX_TX_QUEUE];/*  H, N, L, used for sdio tx aggregation max length per queue */

	struct dm_priv dmpriv;
	struct dm_odm_t		odmpriv;

	/*  For bluetooth co-existence */
	struct bt_coexist		bt_coexist;

	/* Chip version information */
	bool chip_normal;	/* true - normal chip, false - test chip */
};

#define GET_HAL_DATA(__padapter)	((struct hal_com_data *)((__padapter)->HalData))

#endif /* __HAL_DATA_H__ */
