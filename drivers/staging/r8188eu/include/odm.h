/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef	__HALDMOUTSRC_H__
#define __HALDMOUTSRC_H__

struct rtw_dig {
	u8		PreIGValue;
	u8		CurIGValue;
	u8		BackupIGValue;

	u8		rx_gain_range_max;
	u8		rx_gain_range_min;

	u8		CurCCK_CCAThres;

	u8		LargeFAHit;
	u8		ForbiddenIGI;
	u32		Recover_cnt;

	u8		DIG_Dynamic_MIN_0;
	bool		bMediaConnect_0;

	u32		AntDiv_RSSI_max;
	u32		RSSI_max;
};

struct rtl_ps {
	u8		pre_rf_state;
	u8		cur_rf_state;
	u8		initialize;
	u32		reg_874;
	u32		reg_c70;
	u32		reg_85c;
	u32		reg_a74;

};

struct false_alarm_stats {
	u32	Cnt_Parity_Fail;
	u32	Cnt_Rate_Illegal;
	u32	Cnt_Crc8_fail;
	u32	Cnt_Mcs_fail;
	u32	Cnt_Ofdm_fail;
	u32	Cnt_Cck_fail;
	u32	Cnt_all;
	u32	Cnt_Fast_Fsync;
	u32	Cnt_SB_Search_fail;
	u32	Cnt_OFDM_CCA;
	u32	Cnt_CCK_CCA;
	u32	Cnt_CCA_all;
	u32	Cnt_BW_USC;	/* Gary */
	u32	Cnt_BW_LSC;	/* Gary */
};

#define ODM_ASSOCIATE_ENTRY_NUM	32 /*  Max size of AsocEntry[]. */

struct sw_ant_switch {
	u8	CurAntenna;
	u8	SWAS_NoLink_State; /* Before link Antenna Switch check */
	u8	RxIdleAnt;
};

struct edca_turbo {
	bool bCurrentTurboEDCA;
	bool bIsCurRDLState;
	u32	prv_traffic_idx; /*  edca turbo */
};

struct odm_rate_adapt {
	u8	HighRSSIThresh;	/*  if RSSI > HighRSSIThresh	=> RATRState is DM_RATR_STA_HIGH */
	u8	LowRSSIThresh;	/*  if RSSI <= LowRSSIThresh	=> RATRState is DM_RATR_STA_LOW */
	u8	RATRState;	/*  Current RSSI level, DM_RATR_STA_HIGH/DM_RATR_STA_MIDDLE/DM_RATR_STA_LOW */
	u32	LastRATR;	/*  RATR Register Content */
};

#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM	16
#define IQK_BB_REG_NUM		9
#define HP_THERMAL_NUM		8

#define AVG_THERMAL_NUM		8
#define IQK_Matrix_REG_NUM	8

struct odm_phy_dbg_info {
	/* ODM Write,debug info */
	s8	RxSNRdB[MAX_PATH_NUM_92CS];
	u64	NumQryPhyStatus;
	u64	NumQryPhyStatusCCK;
	u64	NumQryPhyStatusOFDM;
	/* Others */
	s32	RxEVM[MAX_PATH_NUM_92CS];
};

struct odm_per_pkt_info {
	s8	Rate;
	u8	StationID;
	bool	bPacketMatchBSSID;
	bool	bPacketToSelf;
	bool	bPacketBeacon;
};

enum odm_ability {
	/*  BB Team */
	ODM_DIG			= 0x00000001,
	ODM_HIGH_POWER		= 0x00000002,
	ODM_CCK_CCA_TH		= 0x00000004,
	ODM_FA_STATISTICS	= 0x00000008,
	ODM_RAMASK		= 0x00000010,
	ODM_RSSI_MONITOR	= 0x00000020,
	ODM_SW_ANTDIV		= 0x00000040,
	ODM_HW_ANTDIV		= 0x00000080,
	ODM_BB_PWRSV		= 0x00000100,
	ODM_2TPATHDIV		= 0x00000200,
	ODM_1TPATHDIV		= 0x00000400,
	ODM_PSD2AFH		= 0x00000800
};

/*  2011/10/20 MH Define Common info enum for all team. */

enum odm_common_info_def {
	/*  Fixed value: */

	/* HOOK BEFORE REG INIT----------- */
	ODM_CMNINFO_ABILITY,		/* ODM_ABILITY_E */
	ODM_CMNINFO_MP_TEST_CHIP,
	/* HOOK BEFORE REG INIT-----------  */

	/*  Dynamic value: */
/*  POINTER REFERENCE-----------  */
	ODM_CMNINFO_WM_MODE,		/*  ODM_WIRELESS_MODE_E */
	ODM_CMNINFO_SEC_CHNL_OFFSET,	/*  ODM_SEC_CHNL_OFFSET_E */
	ODM_CMNINFO_BW,			/*  ODM_BW_E */
	ODM_CMNINFO_CHNL,

	ODM_CMNINFO_SCAN,
	ODM_CMNINFO_POWER_SAVING,
/*  POINTER REFERENCE----------- */

/* CALL BY VALUE------------- */
	ODM_CMNINFO_LINK,
	ODM_CMNINFO_RSSI_MIN,
	ODM_CMNINFO_RF_ANTENNA_TYPE,		/*  u8 */
/* CALL BY VALUE-------------*/
};

/*  2011/10/20 MH Define ODM support ability.  ODM_CMNINFO_ABILITY */

enum odm_ability_def {
	/*  BB ODM section BIT 0-15 */
	ODM_BB_RSSI_MONITOR		= BIT(4),
	ODM_BB_ANT_DIV			= BIT(6),
	ODM_BB_PWR_TRA			= BIT(8),
};

# define ODM_ITRF_USB 0x2

/*  ODM_CMNINFO_OP_MODE */
enum odm_operation_mode {
	ODM_NO_LINK		= BIT(0),
	ODM_LINK		= BIT(1),
	ODM_SCAN		= BIT(2),
	ODM_POWERSAVE		= BIT(3),
	ODM_AP_MODE		= BIT(4),
	ODM_CLIENT_MODE		= BIT(5),
	ODM_AD_HOC		= BIT(6),
	ODM_WIFI_DIRECT		= BIT(7),
	ODM_WIFI_DISPLAY	= BIT(8),
};

/*  ODM_CMNINFO_WM_MODE */
enum odm_wireless_mode {
	ODM_WM_UNKNOW	= 0x0,
	ODM_WM_B	= BIT(0),
	ODM_WM_G	= BIT(1),
	ODM_WM_N24G	= BIT(3),
	ODM_WM_AUTO	= BIT(5),
};

/*  ODM_CMNINFO_BW */
enum odm_bw {
	ODM_BW20M		= 0,
	ODM_BW40M		= 1,
};

struct odm_ra_info {
	u8 RateID;
	u32 RateMask;
	u32 RAUseRate;
	u8 RateSGI;
	u8 RssiStaRA;
	u8 PreRssiStaRA;
	u8 SGIEnable;
	u8 DecisionRate;
	u8 PreRate;
	u8 HighestRate;
	u8 LowestRate;
	u32 NscUp;
	u32 NscDown;
	u16 RTY[5];
	u32 TOTAL;
	u16 DROP;
	u8 Active;
	u16 RptTime;
	u8 RAWaitingCounter;
	u8 RAPendingCounter;
	u8 PTActive;	/*  on or off */
	u8 PTTryState;	/*  0 trying state, 1 for decision state */
	u8 PTStage;	/*  0~6 */
	u8 PTStopCount;	/* Stop PT counter */
	u8 PTPreRate;	/*  if rate change do PT */
	u8 PTPreRssi;	/*  if RSSI change 5% do PT */
	u8 PTModeSS;	/*  decide whitch rate should do PT */
	u8 RAstage;	/*  StageRA, decide how many times RA will be done
			 * between PT */
	u8 PTSmoothFactor;
};

struct ijk_matrix_regs_set {
	bool	bIQKDone;
	s32	Value[1][IQK_Matrix_REG_NUM];
};

struct odm_rf_cal {
	/* for tx power tracking */
	u32	RegA24; /*  for TempCCK */
	s32	RegE94;
	s32	RegE9C;
	s32	RegEB4;
	s32	RegEBC;

	bool	bTXPowerTrackingInit;
	bool	bTXPowerTracking;
	u8	TxPowerTrackControl; /* for mp mode, turn off txpwrtracking
				      * as default */
	u8	TM_Trigger;
	u8	InternalPA5G[2];	/* pathA / pathB */

	u8	ThermalMeter[2];    /* ThermalMeter, index 0 for RFIC0,
				     * and 1 for RFIC1 */
	u8	ThermalValue;
	u8	ThermalValue_LCK;
	u8	ThermalValue_IQK;
	u8	ThermalValue_DPK;
	u8	ThermalValue_AVG[AVG_THERMAL_NUM];
	u8	ThermalValue_AVG_index;
	u8	ThermalValue_RxGain;
	u8	ThermalValue_Crystal;
	u8	ThermalValue_DPKstore;
	u8	ThermalValue_DPKtrack;
	bool	TxPowerTrackingInProgress;
	bool	bDPKenable;

	bool	bReloadtxpowerindex;
	u8	bRfPiEnable;

	u8	bCCKinCH14;
	u8	CCK_index;
	u8	OFDM_index[2];
	bool bDoneTxpower;

	u8	ThermalValue_HP[HP_THERMAL_NUM];
	u8	ThermalValue_HP_index;
	struct ijk_matrix_regs_set IQKMatrixRegSetting;

	u8	Delta_IQK;
	u8	Delta_LCK;

	/* for IQK */
	u32	RegC04;
	u32	Reg874;
	u32	RegC08;
	u32	RegB68;
	u32	RegB6C;
	u32	Reg870;
	u32	Reg860;
	u32	Reg864;

	bool	bIQKInitialized;
	bool	bAntennaDetected;
	u32	ADDA_backup[IQK_ADDA_REG_NUM];
	u32	IQK_MAC_backup[IQK_MAC_REG_NUM];
	u32	IQK_BB_backup_recover[9];
	u32	IQK_BB_backup[IQK_BB_REG_NUM];

	/* for APK */
	u32	APKoutput[2][2]; /* path A/B; output1_1a/output1_2a */
	u8	bAPKdone;
	u8	bAPKThermalMeterIgnore;
	u8	bDPdone;
	u8	bDPPathAOK;
	u8	bDPPathBOK;
};

/*  ODM Dynamic common info value definition */

struct fast_ant_train {
	u8	antsel_rx_keep_0;
	u8	antsel_rx_keep_1;
	u8	antsel_rx_keep_2;
	u8	antsel_a[ODM_ASSOCIATE_ENTRY_NUM];
	u8	antsel_b[ODM_ASSOCIATE_ENTRY_NUM];
	u8	antsel_c[ODM_ASSOCIATE_ENTRY_NUM];
	u32	MainAnt_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32	AuxAnt_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32	MainAnt_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u32	AuxAnt_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u8	RxIdleAnt;
	bool	bBecomeLinked;
};

enum ant_div_type {
	NO_ANTDIV			= 0xFF,
	CG_TRX_HW_ANTDIV		= 0x01,
	CGCS_RX_HW_ANTDIV		= 0x02,
	FIXED_HW_ANTDIV			= 0x03,
	CG_TRX_SMART_ANTDIV		= 0x04,
};

/* Copy from SD4 defined structure. We use to support PHY DM integration. */
struct odm_dm_struct {
	struct adapter *Adapter;	/*  For CE/NIC team */

/*  ODM HANDLE, DRIVER NEEDS NOT TO HOOK------ */
	bool	bCckHighPower;
	u8	RFPathRxEnable;		/*  ODM_CMNINFO_RFPATH_ENABLE */
	u8	ControlChannel;
/*  ODM HANDLE, DRIVER NEEDS NOT TO HOOK------ */

/* 1  COMMON INFORMATION */
	/*  Init Value */
/* HOOK BEFORE REG INIT----------- */
	/*  ODM Support Ability DIG/RATR/TX_PWR_TRACK/ �K�K = 1/2/3/�K */
	u32	SupportAbility;

	u32	BK_SupportAbility;
	u8	AntDivType;
/* HOOK BEFORE REG INIT----------- */

	/*  Dynamic Value */
/*  POINTER REFERENCE----------- */
	/*  Wireless mode B/G/A/N = BIT(0)/BIT(1)/BIT(2)/BIT(3) */
	u8	*pWirelessMode; /* ODM_WIRELESS_MODE_E */
	/*  Secondary channel offset don't_care/below/above = 0/1/2 */
	u8	*pSecChOffset;
	/*  BW info 20M/40M/80M = 0/1/2 */
	u8	*pBandWidth;
	/*  Central channel location Ch1/Ch2/.... */
	u8	*pChannel;	/* central channel number */

	/*  Common info for Status */
	bool	*pbScanInProcess;
	bool	*pbPowerSaving;
/*  POINTER REFERENCE----------- */
	/*  */
/* CALL BY VALUE------------- */
	bool	bLinked;
	u8	RSSI_Min;
	bool	bIsMPChip;
	bool	bOneEntryOnly;
/* CALL BY VALUE------------- */

	/* 2 Define STA info. */
	/*  _ODM_STA_INFO */
	/*  For MP, we need to reduce one array pointer for default port.?? */
	struct sta_info *pODM_StaInfo[ODM_ASSOCIATE_ENTRY_NUM];

	u16	CurrminRptTime;
	struct odm_ra_info RAInfo[ODM_ASSOCIATE_ENTRY_NUM]; /* Use MacID as
			* array index. STA MacID=0,
			* VWiFi Client MacID={1, ODM_ASSOCIATE_ENTRY_NUM-1} */

	/*  Latest packet phy info (ODM write) */
	struct odm_phy_dbg_info PhyDbgInfo;

	/* ODM Structure */
	struct fast_ant_train DM_FatTable;
	struct rtw_dig	DM_DigTable;
	struct rtl_ps	DM_PSTable;
	struct false_alarm_stats FalseAlmCnt;
	struct sw_ant_switch DM_SWAT_Table;

	struct edca_turbo DM_EDCA_Table;

	/* PSD */
	bool	bDMInitialGainEnable;

	struct odm_rate_adapt RateAdaptive;

	struct odm_rf_cal RFCalibrateInfo;

	/*  TX power tracking */
	u8	BbSwingIdxOfdm;
	u8	BbSwingIdxOfdmCurrent;
	u8	BbSwingIdxOfdmBase;
	bool	BbSwingFlagOfdm;
	u8	BbSwingIdxCck;
	u8	BbSwingIdxCckCurrent;
	u8	BbSwingIdxCckBase;
	bool	BbSwingFlagCck;
};

enum odm_bb_config_type {
    CONFIG_BB_PHY_REG,
    CONFIG_BB_AGC_TAB,
    CONFIG_BB_AGC_TAB_2G,
    CONFIG_BB_PHY_REG_PG,
};

#define		DM_DIG_MAX_NIC			0x4e
#define		DM_DIG_MIN_NIC			0x1e /* 0x22/0x1c */

#define		DM_DIG_MAX_AP			0x32

/* vivi 92c&92d has different definition, 20110504 */
/* this is for 92c */
#define		DM_DIG_FA_TH0			0x200/* 0x20 */
#define		DM_DIG_FA_TH1			0x300/* 0x100 */
#define		DM_DIG_FA_TH2			0x400/* 0x200 */

/* 3=========================================================== */
/* 3 Rate Adaptive */
/* 3=========================================================== */
#define		DM_RATR_STA_INIT		0
#define		DM_RATR_STA_HIGH		1
#define		DM_RATR_STA_MIDDLE		2
#define		DM_RATR_STA_LOW			3

/* 3=========================================================== */
/* 3 BB Power Save */
/* 3=========================================================== */

enum dm_rf {
	RF_Save = 0,
	RF_Normal = 1,
	RF_MAX = 2,
};

/* 3=========================================================== */
/* 3 Antenna Diversity */
/* 3=========================================================== */
enum dm_swas {
	Antenna_A = 1,
	Antenna_B = 2,
	Antenna_MAX = 3,
};

/*  Extern Global Variables. */
#define	OFDM_TABLE_SIZE_92D	43
#define	CCK_TABLE_SIZE		33

extern	u32 OFDMSwingTable[OFDM_TABLE_SIZE_92D];
extern	u8 CCKSwingTable_Ch1_Ch13[CCK_TABLE_SIZE][8];
extern	u8 CCKSwingTable_Ch14 [CCK_TABLE_SIZE][8];

/*  check Sta pointer valid or not */
#define IS_STA_VALID(pSta)		(pSta)

void ODM_Write_DIG(struct odm_dm_struct *pDM_Odm, u8 CurrentIGI);
void ODM_Write_CCK_CCA_Thres(struct odm_dm_struct *pDM_Odm, u8 CurCCK_CCAThres);

void ODM_RF_Saving(struct odm_dm_struct *pDM_Odm, u8 bForceInNormal);

void ODM_TXPowerTrackingCheck(struct odm_dm_struct *pDM_Odm);

bool ODM_RAStateCheck(struct odm_dm_struct *pDM_Odm, s32 RSSI,
		      bool bForceUpdate, u8 *pRATRState);

u32 ODM_Get_Rate_Bitmap(struct odm_dm_struct *pDM_Odm, u32 macid,
			u32 ra_mask, u8 rssi_level);

void ODM_DMInit(struct odm_dm_struct *pDM_Odm);

void ODM_DMWatchdog(struct odm_dm_struct *pDM_Odm);

void ODM_CmnInfoInit(struct odm_dm_struct *pDM_Odm,
		     enum odm_common_info_def CmnInfo, u32 Value);

void ODM_CmnInfoHook(struct odm_dm_struct *pDM_Odm,
		     enum odm_common_info_def CmnInfo, void *pValue);

void ODM_CmnInfoUpdate(struct odm_dm_struct *pDM_Odm, u32 CmnInfo, u64 Value);

#endif
