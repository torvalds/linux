/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/


#ifndef	__HALDMOUTSRC_H__
#define __HALDMOUTSRC_H__

#include "odm_EdcaTurboCheck.h"
#include "odm_DIG.h"
#include "odm_PathDiv.h"
#include "odm_DynamicBBPowerSaving.h"
#include "odm_DynamicTxPower.h"
#include "odm_CfoTracking.h"
#include "odm_NoiseMonitor.h"

#define	TP_MODE		0
#define	RSSI_MODE		1
#define	TRAFFIC_LOW	0
#define	TRAFFIC_HIGH	1
#define	NONE			0

/* 3 Tx Power Tracking */
/* 3 ============================================================ */
#define		DPK_DELTA_MAPPING_NUM	13
#define		index_mapping_HP_NUM	15
#define	OFDM_TABLE_SIZE		43
#define	CCK_TABLE_SIZE			33
#define TXSCALE_TABLE_SIZE		37
#define TXPWR_TRACK_TABLE_SIZE	30
#define DELTA_SWINGIDX_SIZE     30
#define BAND_NUM				4

/* 3 PSD Handler */
/* 3 ============================================================ */

#define	AFH_PSD		1	/* 0:normal PSD scan, 1: only do 20 pts PSD */
#define	MODE_40M		0	/* 0:20M, 1:40M */
#define	PSD_TH2		3
#define	PSD_CHMIN		20   /*  Minimum channel number for BT AFH */
#define	SIR_STEP_SIZE	3
#define   Smooth_Size_1		5
#define	Smooth_TH_1	3
#define   Smooth_Size_2		10
#define	Smooth_TH_2	4
#define   Smooth_Size_3		20
#define	Smooth_TH_3	4
#define   Smooth_Step_Size 5
#define	Adaptive_SIR	1
#define	PSD_RESCAN		4
#define	PSD_SCAN_INTERVAL	700 /* ms */

/* 8723A High Power IGI Setting */
#define		DM_DIG_HIGH_PWR_IGI_LOWER_BOUND	0x22
#define			DM_DIG_Gmode_HIGH_PWR_IGI_LOWER_BOUND 0x28
#define		DM_DIG_HIGH_PWR_THRESHOLD	0x3a
#define		DM_DIG_LOW_PWR_THRESHOLD	0x14

/* ANT Test */
#define			ANTTESTALL		0x00		/* Ant A or B will be Testing */
#define		ANTTESTA		0x01		/* Ant A will be Testing */
#define		ANTTESTB		0x02		/* Ant B will be testing */

#define	PS_MODE_ACTIVE 0x01

/* for 8723A Ant Definition--2012--06--07 due to different IC may be different ANT define */
#define		MAIN_ANT		1		/* Ant A or Ant Main */
#define		AUX_ANT		2		/* AntB or Ant Aux */
#define		MAX_ANT		3		/*  3 for AP using */

/* Antenna Diversity Type */
#define	SW_ANTDIV	0
#define	HW_ANTDIV	1
/*  structure and define */

/* Remove DIG by Yuchen */

/* Remoce BB power saving by Yuchn */

/* Remove DIG by yuchen */

struct dynamic_primary_CCA {
	u8 PriCCA_flag;
	u8 intf_flag;
	u8 intf_type;
	u8 DupRTS_flag;
	u8 Monitor_flag;
	u8 CH_offset;
	u8 MF_state;
};

typedef struct _Rate_Adaptive_Table_ {
	u8 firstconnect;
} RA_T, *pRA_T;

typedef struct _RX_High_Power_ {
	u8 RXHP_flag;
	u8 PSD_func_trigger;
	u8 PSD_bitmap_RXHP[80];
	u8 Pre_IGI;
	u8 Cur_IGI;
	u8 Pre_pw_th;
	u8 Cur_pw_th;
	bool First_time_enter;
	bool RXHP_enable;
	u8 TP_Mode;
	RT_TIMER PSDTimer;
} RXHP_T, *pRXHP_T;

#define ASSOCIATE_ENTRY_NUM					32 /*  Max size of AsocEntry[]. */
#define	ODM_ASSOCIATE_ENTRY_NUM				ASSOCIATE_ENTRY_NUM

/*  This indicates two different the steps. */
/*  In SWAW_STEP_PEAK, driver needs to switch antenna and listen to the signal on the air. */
/*  In SWAW_STEP_DETERMINE, driver just compares the signal captured in SWAW_STEP_PEAK */
/*  with original RSSI to determine if it is necessary to switch antenna. */
#define SWAW_STEP_PEAK		0
#define SWAW_STEP_DETERMINE	1

#define	TP_MODE		0
#define	RSSI_MODE		1
#define	TRAFFIC_LOW	0
#define	TRAFFIC_HIGH	1
#define	TRAFFIC_UltraLOW	2

typedef struct _SW_Antenna_Switch_ {
	u8 Double_chk_flag;
	u8 try_flag;
	s32 PreRSSI;
	u8 CurAntenna;
	u8 PreAntenna;
	u8 RSSI_Trying;
	u8 TestMode;
	u8 bTriggerAntennaSwitch;
	u8 SelectAntennaMap;
	u8 RSSI_target;
	u8 reset_idx;
	u16 Single_Ant_Counter;
	u16 Dual_Ant_Counter;
	u16 Aux_FailDetec_Counter;
	u16 Retry_Counter;

	/*  Before link Antenna Switch check */
	u8 SWAS_NoLink_State;
	u32 SWAS_NoLink_BK_Reg860;
	u32 SWAS_NoLink_BK_Reg92c;
	u32 SWAS_NoLink_BK_Reg948;
	bool ANTA_ON;	/* To indicate Ant A is or not */
	bool ANTB_ON;	/* To indicate Ant B is on or not */
	bool Pre_Aux_FailDetec;
	bool RSSI_AntDect_bResult;
	u8 Ant5G;
	u8 Ant2G;

	s32 RSSI_sum_A;
	s32 RSSI_sum_B;
	s32 RSSI_cnt_A;
	s32 RSSI_cnt_B;

	u64 lastTxOkCnt;
	u64 lastRxOkCnt;
	u64 TXByteCnt_A;
	u64 TXByteCnt_B;
	u64 RXByteCnt_A;
	u64 RXByteCnt_B;
	u8 TrafficLoad;
	u8 Train_time;
	u8 Train_time_flag;
	RT_TIMER SwAntennaSwitchTimer;
	RT_TIMER SwAntennaSwitchTimer_8723B;
	u32 PktCnt_SWAntDivByCtrlFrame;
	bool bSWAntDivByCtrlFrame;
} SWAT_T, *pSWAT_T;

/* Remove Edca by YuChen */


typedef struct _ODM_RATE_ADAPTIVE {
	u8 Type;				/*  DM_Type_ByFW/DM_Type_ByDriver */
	u8 LdpcThres;			/*  if RSSI > LdpcThres => switch from LPDC to BCC */
	bool bUseLdpc;
	bool bLowerRtsRate;
	u8 HighRSSIThresh;		/*  if RSSI > HighRSSIThresh	=> RATRState is DM_RATR_STA_HIGH */
	u8 LowRSSIThresh;		/*  if RSSI <= LowRSSIThresh	=> RATRState is DM_RATR_STA_LOW */
	u8 RATRState;			/*  Current RSSI level, DM_RATR_STA_HIGH/DM_RATR_STA_MIDDLE/DM_RATR_STA_LOW */

} ODM_RATE_ADAPTIVE, *PODM_RATE_ADAPTIVE;

#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM_MAX	10
#define IQK_BB_REG_NUM		9
#define HP_THERMAL_NUM		8

#define AVG_THERMAL_NUM		8
#define IQK_Matrix_REG_NUM	8
#define IQK_Matrix_Settings_NUM	(14 + 24 + 21) /*   Channels_2_4G_NUM
						* + Channels_5G_20M_NUM
						* + Channels_5G
						*/

#define		DM_Type_ByFW			0
#define		DM_Type_ByDriver		1

/*  */
/*  Declare for common info */
/*  */
#define MAX_PATH_NUM_92CS		2
#define MAX_PATH_NUM_8188E		1
#define MAX_PATH_NUM_8192E		2
#define MAX_PATH_NUM_8723B		1
#define MAX_PATH_NUM_8812A		2
#define MAX_PATH_NUM_8821A		1
#define MAX_PATH_NUM_8814A		4
#define MAX_PATH_NUM_8822B		2

#define IQK_THRESHOLD			8
#define DPK_THRESHOLD			4

struct odm_phy_info {
	/*
	 *  Be care, if you want to add any element, please insert it between
	 *  rx_pwd_ball and signal_strength.
	 */
	u8 rx_pwd_ba11;

	u8 signal_quality;             /* in 0-100 index. */
	s8 rx_mimo_signal_quality[4];  /* per-path's EVM */
	u8 rx_mimo_evm_dbm[4];         /* per-path's EVM dbm */

	u8 rx_mimo_signal_strength[4]; /* in 0~100 index */

	u16 cfo_short[4];              /* per-path's Cfo_short */
	u16 cfo_tail[4];               /* per-path's Cfo_tail */

	s8 rx_power;                   /* in dBm Translate from PWdB */

	/*
	 * Real power in dBm for this packet, no beautification and
	 * aggregation. Keep this raw info to be used for the other procedures.
	 */
	s8 recv_signal_power;
	u8 bt_rx_rssi_percentage;
	u8 signal_strength;	       /* in 0-100 index. */

	s8 rx_pwr[4];                  /* per-path's pwdb */

	u8 rx_snr[4];                  /* per-path's SNR */
	u8 band_width;
	u8 bt_coex_pwr_adjust;
};

struct odm_packet_info {
	u8 data_rate;
	u8 station_id;
	bool bssid_match;
	bool to_self;
	bool is_beacon;
};

typedef struct _ODM_Phy_Dbg_Info_ {
	/* ODM Write, debug info */
	s8 RxSNRdB[4];
	u32 NumQryPhyStatus;
	u32 NumQryPhyStatusCCK;
	u32 NumQryPhyStatusOFDM;
	u8 NumQryBeaconPkt;
	/* Others */
	s32 RxEVM[4];

} ODM_PHY_DBG_INFO_T;

typedef struct _ODM_Mac_Status_Info_ {
	u8 test;
} ODM_MAC_INFO;

typedef enum tag_Dynamic_ODM_Support_Ability_Type {
	/*  BB Team */
	ODM_DIG				= 0x00000001,
	ODM_HIGH_POWER		= 0x00000002,
	ODM_CCK_CCA_TH		= 0x00000004,
	ODM_FA_STATISTICS	= 0x00000008,
	ODM_RAMASK			= 0x00000010,
	ODM_RSSI_MONITOR	= 0x00000020,
	ODM_SW_ANTDIV		= 0x00000040,
	ODM_HW_ANTDIV		= 0x00000080,
	ODM_BB_PWRSV		= 0x00000100,
	ODM_2TPATHDIV		= 0x00000200,
	ODM_1TPATHDIV		= 0x00000400,
	ODM_PSD2AFH			= 0x00000800
} ODM_Ability_E;

/*  */
/*  2011/20/20 MH For MP driver RT_WLAN_STA =  STA_INFO_T */
/*  Please declare below ODM relative info in your STA info structure. */
/*  */
typedef struct _ODM_STA_INFO {
	/*  Driver Write */
	bool bUsed;				/*  record the sta status link or not? */
	/* u8 WirelessMode;		 */
	u8 IOTPeer;			/*  Enum value.	HT_IOT_PEER_E */

	/*  ODM Write */
	/* 1 PHY_STATUS_INFO */
	u8 RSSI_Path[4];		/*  */
	u8 RSSI_Ave;
	u8 RXEVM[4];
	u8 RXSNR[4];

	/*  ODM Write */
	/* 1 TX_INFO (may changed by IC) */
	/* TX_INFO_T		pTxInfo;		Define in IC folder. Move lower layer. */

	/*  */
	/* 	Please use compile flag to disabe the strcutrue for other IC except 88E. */
	/* 	Move To lower layer. */
	/*  */
	/*  ODM Write Wilson will handle this part(said by Luke.Lee) */
	/* TX_RPT_T		pTxRpt;			Define in IC folder. Move lower layer. */
} ODM_STA_INFO_T, *PODM_STA_INFO_T;

/*  */
/*  2011/10/20 MH Define Common info enum for all team. */
/*  */
typedef enum _ODM_Common_Info_Definition {
	/*  Fixed value: */

	/* HOOK BEFORE REG INIT----------- */
	ODM_CMNINFO_PLATFORM = 0,
	ODM_CMNINFO_ABILITY,					/*  ODM_ABILITY_E */
	ODM_CMNINFO_INTERFACE,				/*  ODM_INTERFACE_E */
	ODM_CMNINFO_MP_TEST_CHIP,
	ODM_CMNINFO_IC_TYPE,					/*  ODM_IC_TYPE_E */
	ODM_CMNINFO_CUT_VER,					/*  ODM_CUT_VERSION_E */
	ODM_CMNINFO_FAB_VER,					/*  ODM_FAB_E */
	ODM_CMNINFO_RF_TYPE,					/*  ODM_RF_PATH_E or ODM_RF_TYPE_E? */
	ODM_CMNINFO_RFE_TYPE,
	ODM_CMNINFO_BOARD_TYPE,				/*  ODM_BOARD_TYPE_E */
	ODM_CMNINFO_PACKAGE_TYPE,
	ODM_CMNINFO_EXT_LNA,					/*  true */
	ODM_CMNINFO_5G_EXT_LNA,
	ODM_CMNINFO_EXT_PA,
	ODM_CMNINFO_5G_EXT_PA,
	ODM_CMNINFO_GPA,
	ODM_CMNINFO_APA,
	ODM_CMNINFO_GLNA,
	ODM_CMNINFO_ALNA,
	ODM_CMNINFO_EXT_TRSW,
	ODM_CMNINFO_PATCH_ID,				/* CUSTOMER ID */
	ODM_CMNINFO_BINHCT_TEST,
	ODM_CMNINFO_BWIFI_TEST,
	ODM_CMNINFO_SMART_CONCURRENT,
	/* HOOK BEFORE REG INIT----------- */

	/*  Dynamic value: */
/*  POINTER REFERENCE----------- */
	ODM_CMNINFO_MAC_PHY_MODE,	/*  ODM_MAC_PHY_MODE_E */
	ODM_CMNINFO_TX_UNI,
	ODM_CMNINFO_RX_UNI,
	ODM_CMNINFO_WM_MODE,		/*  ODM_WIRELESS_MODE_E */
	ODM_CMNINFO_BAND,		/*  ODM_BAND_TYPE_E */
	ODM_CMNINFO_SEC_CHNL_OFFSET,	/*  ODM_SEC_CHNL_OFFSET_E */
	ODM_CMNINFO_SEC_MODE,		/*  ODM_SECURITY_E */
	ODM_CMNINFO_BW,			/*  ODM_BW_E */
	ODM_CMNINFO_CHNL,
	ODM_CMNINFO_FORCED_RATE,

	ODM_CMNINFO_DMSP_GET_VALUE,
	ODM_CMNINFO_BUDDY_ADAPTOR,
	ODM_CMNINFO_DMSP_IS_MASTER,
	ODM_CMNINFO_SCAN,
	ODM_CMNINFO_POWER_SAVING,
	ODM_CMNINFO_ONE_PATH_CCA,	/*  ODM_CCA_PATH_E */
	ODM_CMNINFO_DRV_STOP,
	ODM_CMNINFO_PNP_IN,
	ODM_CMNINFO_INIT_ON,
	ODM_CMNINFO_ANT_TEST,
	ODM_CMNINFO_NET_CLOSED,
	ODM_CMNINFO_MP_MODE,
	/* ODM_CMNINFO_RTSTA_AID,	 For win driver only? */
	ODM_CMNINFO_FORCED_IGI_LB,
	ODM_CMNINFO_IS1ANTENNA,
	ODM_CMNINFO_RFDEFAULTPATH,
/*  POINTER REFERENCE----------- */

/* CALL BY VALUE------------- */
	ODM_CMNINFO_WIFI_DIRECT,
	ODM_CMNINFO_WIFI_DISPLAY,
	ODM_CMNINFO_LINK_IN_PROGRESS,
	ODM_CMNINFO_LINK,
	ODM_CMNINFO_STATION_STATE,
	ODM_CMNINFO_RSSI_MIN,
	ODM_CMNINFO_DBG_COMP,			/*  u64 */
	ODM_CMNINFO_DBG_LEVEL,			/*  u32 */
	ODM_CMNINFO_RA_THRESHOLD_HIGH,		/*  u8 */
	ODM_CMNINFO_RA_THRESHOLD_LOW,		/*  u8 */
	ODM_CMNINFO_RF_ANTENNA_TYPE,		/*  u8 */
	ODM_CMNINFO_BT_ENABLED,
	ODM_CMNINFO_BT_HS_CONNECT_PROCESS,
	ODM_CMNINFO_BT_HS_RSSI,
	ODM_CMNINFO_BT_OPERATION,
	ODM_CMNINFO_BT_LIMITED_DIG,		/* Need to Limited Dig or not */
	ODM_CMNINFO_BT_DISABLE_EDCA,
/* CALL BY VALUE------------- */

	/*  Dynamic ptr array hook itms. */
	ODM_CMNINFO_STA_STATUS,
	ODM_CMNINFO_PHY_STATUS,
	ODM_CMNINFO_MAC_STATUS,

	ODM_CMNINFO_MAX,
} ODM_CMNINFO_E;

/*  2011/10/20 MH Define ODM support ability.  ODM_CMNINFO_ABILITY */
typedef enum _ODM_Support_Ability_Definition {
	/*  */
	/*  BB ODM section BIT 0-15 */
	/*  */
	ODM_BB_DIG			= BIT0,
	ODM_BB_RA_MASK			= BIT1,
	ODM_BB_DYNAMIC_TXPWR		= BIT2,
	ODM_BB_FA_CNT			= BIT3,
	ODM_BB_RSSI_MONITOR		= BIT4,
	ODM_BB_CCK_PD			= BIT5,
	ODM_BB_ANT_DIV			= BIT6,
	ODM_BB_PWR_SAVE			= BIT7,
	ODM_BB_PWR_TRAIN		= BIT8,
	ODM_BB_RATE_ADAPTIVE		= BIT9,
	ODM_BB_PATH_DIV			= BIT10,
	ODM_BB_PSD			= BIT11,
	ODM_BB_RXHP			= BIT12,
	ODM_BB_ADAPTIVITY		= BIT13,
	ODM_BB_CFO_TRACKING		= BIT14,

	/*  MAC DM section BIT 16-23 */
	ODM_MAC_EDCA_TURBO		= BIT16,
	ODM_MAC_EARLY_MODE		= BIT17,

	/*  RF ODM section BIT 24-31 */
	ODM_RF_TX_PWR_TRACK		= BIT24,
	ODM_RF_RX_GAIN_TRACK	= BIT25,
	ODM_RF_CALIBRATION		= BIT26,
} ODM_ABILITY_E;

/* 	ODM_CMNINFO_INTERFACE */
typedef enum tag_ODM_Support_Interface_Definition {
	ODM_ITRF_SDIO	=	0x4,
	ODM_ITRF_ALL	=	0x7,
} ODM_INTERFACE_E;

/*  ODM_CMNINFO_IC_TYPE */
typedef enum tag_ODM_Support_IC_Type_Definition {
	ODM_RTL8723B	=	BIT8,
} ODM_IC_TYPE_E;

/* ODM_CMNINFO_CUT_VER */
typedef enum tag_ODM_Cut_Version_Definition {
	ODM_CUT_A		=	0,
	ODM_CUT_B		=	1,
	ODM_CUT_C		=	2,
	ODM_CUT_D		=	3,
	ODM_CUT_E		=	4,
	ODM_CUT_F		=	5,

	ODM_CUT_I		=	8,
	ODM_CUT_J		=	9,
	ODM_CUT_K		=	10,
	ODM_CUT_TEST	=	15,
} ODM_CUT_VERSION_E;

/*  ODM_CMNINFO_FAB_VER */
typedef enum tag_ODM_Fab_Version_Definition {
	ODM_TSMC	=	0,
	ODM_UMC		=	1,
} ODM_FAB_E;

/*  ODM_CMNINFO_RF_TYPE */
/*  */
/*  For example 1T2R (A+AB = BIT0|BIT4|BIT5) */
/*  */
typedef enum tag_ODM_RF_Path_Bit_Definition {
	ODM_RF_TX_A	=	BIT0,
	ODM_RF_TX_B	=	BIT1,
	ODM_RF_TX_C	=	BIT2,
	ODM_RF_TX_D	=	BIT3,
	ODM_RF_RX_A	=	BIT4,
	ODM_RF_RX_B	=	BIT5,
	ODM_RF_RX_C	=	BIT6,
	ODM_RF_RX_D	=	BIT7,
} ODM_RF_PATH_E;

typedef enum tag_ODM_RF_Type_Definition {
	ODM_1T1R	=	0,
	ODM_1T2R	=	1,
	ODM_2T2R	=	2,
	ODM_2T3R	=	3,
	ODM_2T4R	=	4,
	ODM_3T3R	=	5,
	ODM_3T4R	=	6,
	ODM_4T4R	=	7,
} ODM_RF_TYPE_E;

/*  */
/*  ODM Dynamic common info value definition */
/*  */

/* typedef enum _MACPHY_MODE_8192D{ */
/* 	SINGLEMAC_SINGLEPHY, */
/* 	DUALMAC_DUALPHY, */
/* 	DUALMAC_SINGLEPHY, */
/* MACPHY_MODE_8192D,*PMACPHY_MODE_8192D; */
/*  Above is the original define in MP driver. Please use the same define. THX. */
typedef enum tag_ODM_MAC_PHY_Mode_Definition {
	ODM_SMSP	= 0,
	ODM_DMSP	= 1,
	ODM_DMDP	= 2,
} ODM_MAC_PHY_MODE_E;

typedef enum tag_BT_Coexist_Definition {
	ODM_BT_BUSY		= 1,
	ODM_BT_ON		= 2,
	ODM_BT_OFF		= 3,
	ODM_BT_NONE		= 4,
} ODM_BT_COEXIST_E;

/*  ODM_CMNINFO_OP_MODE */
typedef enum tag_Operation_Mode_Definition {
	ODM_NO_LINK      = BIT0,
	ODM_LINK         = BIT1,
	ODM_SCAN         = BIT2,
	ODM_POWERSAVE    = BIT3,
	ODM_AP_MODE      = BIT4,
	ODM_CLIENT_MODE  = BIT5,
	ODM_AD_HOC       = BIT6,
	ODM_WIFI_DIRECT  = BIT7,
	ODM_WIFI_DISPLAY = BIT8,
} ODM_OPERATION_MODE_E;

/*  ODM_CMNINFO_WM_MODE */
typedef enum tag_Wireless_Mode_Definition {
	ODM_WM_UNKNOW     = 0x0,
	ODM_WM_B          = BIT0,
	ODM_WM_G          = BIT1,
	ODM_WM_A          = BIT2,
	ODM_WM_N24G       = BIT3,
	ODM_WM_N5G        = BIT4,
	ODM_WM_AUTO       = BIT5,
	ODM_WM_AC         = BIT6,
} ODM_WIRELESS_MODE_E;

/*  ODM_CMNINFO_BAND */
typedef enum tag_Band_Type_Definition {
	ODM_BAND_2_4G = 0,
	ODM_BAND_5G,
	ODM_BAND_ON_BOTH,
	ODM_BANDMAX
} ODM_BAND_TYPE_E;

/*  ODM_CMNINFO_SEC_CHNL_OFFSET */
typedef enum tag_Secondary_Channel_Offset_Definition {
	ODM_DONT_CARE	= 0,
	ODM_BELOW		= 1,
	ODM_ABOVE		= 2
} ODM_SEC_CHNL_OFFSET_E;

/*  ODM_CMNINFO_SEC_MODE */
typedef enum tag_Security_Definition {
	ODM_SEC_OPEN		= 0,
	ODM_SEC_WEP40		= 1,
	ODM_SEC_TKIP		= 2,
	ODM_SEC_RESERVE		= 3,
	ODM_SEC_AESCCMP		= 4,
	ODM_SEC_WEP104		= 5,
	ODM_WEP_WPA_MIXED	= 6, /*  WEP + WPA */
	ODM_SEC_SMS4		= 7,
} ODM_SECURITY_E;

/*  ODM_CMNINFO_BW */
typedef enum tag_Bandwidth_Definition {
	ODM_BW20M		= 0,
	ODM_BW40M		= 1,
	ODM_BW80M		= 2,
	ODM_BW160M		= 3,
	ODM_BW10M		= 4,
} ODM_BW_E;

/*  ODM_CMNINFO_BOARD_TYPE */
/*  For non-AC-series IC , ODM_BOARD_5G_EXT_PA and ODM_BOARD_5G_EXT_LNA are ignored */
/*  For AC-series IC, external PA & LNA can be indivisuallly added on 2.4G and/or 5G */
typedef enum tag_Board_Definition {
	ODM_BOARD_DEFAULT    = 0,      /*  The DEFAULT case. */
	ODM_BOARD_MINICARD   = BIT(0), /*  0 = non-mini card, 1 = mini card. */
	ODM_BOARD_SLIM       = BIT(1), /*  0 = non-slim card, 1 = slim card */
	ODM_BOARD_BT         = BIT(2), /*  0 = without BT card, 1 = with BT */
	ODM_BOARD_EXT_PA     = BIT(3), /*  0 = no 2G ext-PA, 1 = existing 2G ext-PA */
	ODM_BOARD_EXT_LNA    = BIT(4), /*  0 = no 2G ext-LNA, 1 = existing 2G ext-LNA */
	ODM_BOARD_EXT_TRSW   = BIT(5), /*  0 = no ext-TRSW, 1 = existing ext-TRSW */
	ODM_BOARD_EXT_PA_5G  = BIT(6), /*  0 = no 5G ext-PA, 1 = existing 5G ext-PA */
	ODM_BOARD_EXT_LNA_5G = BIT(7), /*  0 = no 5G ext-LNA, 1 = existing 5G ext-LNA */
} ODM_BOARD_TYPE_E;

typedef enum tag_ODM_Package_Definition {
	ODM_PACKAGE_DEFAULT      = 0,
	ODM_PACKAGE_QFN68        = BIT(0),
	ODM_PACKAGE_TFBGA90      = BIT(1),
	ODM_PACKAGE_TFBGA79      = BIT(2),
} ODM_Package_TYPE_E;

typedef enum tag_ODM_TYPE_GPA_Definition {
	TYPE_GPA0 = 0,
	TYPE_GPA1 = BIT(1)|BIT(0)
} ODM_TYPE_GPA_E;

typedef enum tag_ODM_TYPE_APA_Definition {
	TYPE_APA0 = 0,
	TYPE_APA1 = BIT(1)|BIT(0)
} ODM_TYPE_APA_E;

typedef enum tag_ODM_TYPE_GLNA_Definition {
	TYPE_GLNA0 = 0,
	TYPE_GLNA1 = BIT(2)|BIT(0),
	TYPE_GLNA2 = BIT(3)|BIT(1),
	TYPE_GLNA3 = BIT(3)|BIT(2)|BIT(1)|BIT(0)
} ODM_TYPE_GLNA_E;

typedef enum tag_ODM_TYPE_ALNA_Definition {
	TYPE_ALNA0 = 0,
	TYPE_ALNA1 = BIT(2)|BIT(0),
	TYPE_ALNA2 = BIT(3)|BIT(1),
	TYPE_ALNA3 = BIT(3)|BIT(2)|BIT(1)|BIT(0)
} ODM_TYPE_ALNA_E;

/*  ODM_CMNINFO_ONE_PATH_CCA */
typedef enum tag_CCA_Path {
	ODM_CCA_2R			= 0,
	ODM_CCA_1R_A		= 1,
	ODM_CCA_1R_B		= 2,
} ODM_CCA_PATH_E;

typedef struct _ODM_RA_Info_ {
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
	u8 PTActive;  /*  on or off */
	u8 PTTryState;  /*  0 trying state, 1 for decision state */
	u8 PTStage;  /*  0~6 */
	u8 PTStopCount; /* Stop PT counter */
	u8 PTPreRate;  /*  if rate change do PT */
	u8 PTPreRssi; /*  if RSSI change 5% do PT */
	u8 PTModeSS;  /*  decide whitch rate should do PT */
	u8 RAstage;  /*  StageRA, decide how many times RA will be done between PT */
	u8 PTSmoothFactor;
} ODM_RA_INFO_T, *PODM_RA_INFO_T;

typedef struct _IQK_MATRIX_REGS_SETTING {
	bool bIQKDone;
	s32 Value[3][IQK_Matrix_REG_NUM];
	bool bBWIqkResultSaved[3];
} IQK_MATRIX_REGS_SETTING, *PIQK_MATRIX_REGS_SETTING;

/* Remove PATHDIV_PARA struct to odm_PathDiv.h */

typedef struct ODM_RF_Calibration_Structure {
	/* for tx power tracking */

	u32 RegA24; /*  for TempCCK */
	s32 RegE94;
	s32 RegE9C;
	s32 RegEB4;
	s32 RegEBC;

	u8 TXPowercount;
	bool bTXPowerTrackingInit;
	bool bTXPowerTracking;
	u8 TxPowerTrackControl; /* for mp mode, turn off txpwrtracking as default */
	u8 TM_Trigger;
	u8 InternalPA5G[2];	/* pathA / pathB */

	u8 ThermalMeter[2];    /*  ThermalMeter, index 0 for RFIC0, and 1 for RFIC1 */
	u8 ThermalValue;
	u8 ThermalValue_LCK;
	u8 ThermalValue_IQK;
	u8 ThermalValue_DPK;
	u8 ThermalValue_AVG[AVG_THERMAL_NUM];
	u8 ThermalValue_AVG_index;
	u8 ThermalValue_RxGain;
	u8 ThermalValue_Crystal;
	u8 ThermalValue_DPKstore;
	u8 ThermalValue_DPKtrack;
	bool TxPowerTrackingInProgress;

	bool bReloadtxpowerindex;
	u8 bRfPiEnable;
	u32 TXPowerTrackingCallbackCnt; /* cosa add for debug */

	/*  Tx power Tracking ------------------------- */
	u8 bCCKinCH14;
	u8 CCK_index;
	u8 OFDM_index[MAX_RF_PATH];
	s8 PowerIndexOffset[MAX_RF_PATH];
	s8 DeltaPowerIndex[MAX_RF_PATH];
	s8 DeltaPowerIndexLast[MAX_RF_PATH];
	bool bTxPowerChanged;

	u8 ThermalValue_HP[HP_THERMAL_NUM];
	u8 ThermalValue_HP_index;
	IQK_MATRIX_REGS_SETTING IQKMatrixRegSetting[IQK_Matrix_Settings_NUM];
	bool bNeedIQK;
	bool bIQKInProgress;
	u8 Delta_IQK;
	u8 Delta_LCK;
	s8 BBSwingDiff2G, BBSwingDiff5G; /*  Unit: dB */
	u8 DeltaSwingTableIdx_2GCCKA_P[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GCCKA_N[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GCCKB_P[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GCCKB_N[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GA_P[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GA_N[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GB_P[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GB_N[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_5GA_P[BAND_NUM][DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_5GA_N[BAND_NUM][DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_5GB_P[BAND_NUM][DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_5GB_N[BAND_NUM][DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GA_P_8188E[DELTA_SWINGIDX_SIZE];
	u8 DeltaSwingTableIdx_2GA_N_8188E[DELTA_SWINGIDX_SIZE];

	/*  */

	/* for IQK */
	u32 RegC04;
	u32 Reg874;
	u32 RegC08;
	u32 RegB68;
	u32 RegB6C;
	u32 Reg870;
	u32 Reg860;
	u32 Reg864;

	bool bIQKInitialized;
	bool bLCKInProgress;
	bool bAntennaDetected;
	u32 ADDA_backup[IQK_ADDA_REG_NUM];
	u32 IQK_MAC_backup[IQK_MAC_REG_NUM];
	u32 IQK_BB_backup_recover[9];
	u32 IQK_BB_backup[IQK_BB_REG_NUM];
	u32 TxIQC_8723B[2][3][2]; /*  { {S1: 0xc94, 0xc80, 0xc4c} , {S0: 0xc9c, 0xc88, 0xc4c}} */
	u32 RxIQC_8723B[2][2][2]; /*  { {S1: 0xc14, 0xca0} ,           {S0: 0xc14, 0xca0}} */

	/* for APK */
	u32 APKoutput[2][2]; /* path A/B; output1_1a/output1_2a */
	u8 bAPKdone;
	u8 bAPKThermalMeterIgnore;

	/*  DPK */
	bool bDPKFail;
	u8 bDPdone;
	u8 bDPPathAOK;
	u8 bDPPathBOK;

	u32 TxLOK[2];

} ODM_RF_CAL_T, *PODM_RF_CAL_T;
/*  */
/*  ODM Dynamic common info value definition */
/*  */

typedef struct _FAST_ANTENNA_TRAINNING_ {
	u8 Bssid[6];
	u8 antsel_rx_keep_0;
	u8 antsel_rx_keep_1;
	u8 antsel_rx_keep_2;
	u8 antsel_rx_keep_3;
	u32 antSumRSSI[7];
	u32 antRSSIcnt[7];
	u32 antAveRSSI[7];
	u8 FAT_State;
	u32 TrainIdx;
	u8 antsel_a[ODM_ASSOCIATE_ENTRY_NUM];
	u8 antsel_b[ODM_ASSOCIATE_ENTRY_NUM];
	u8 antsel_c[ODM_ASSOCIATE_ENTRY_NUM];
	u32 MainAnt_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32 AuxAnt_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32 MainAnt_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u32 AuxAnt_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u8 RxIdleAnt;
	bool	bBecomeLinked;
	u32 MinMaxRSSI;
	u8 idx_AntDiv_counter_2G;
	u8 idx_AntDiv_counter_5G;
	u32 AntDiv_2G_5G;
	u32 CCK_counter_main;
	u32 CCK_counter_aux;
	u32 OFDM_counter_main;
	u32 OFDM_counter_aux;

	u32 CCK_CtrlFrame_Cnt_main;
	u32 CCK_CtrlFrame_Cnt_aux;
	u32 OFDM_CtrlFrame_Cnt_main;
	u32 OFDM_CtrlFrame_Cnt_aux;
	u32 MainAnt_CtrlFrame_Sum;
	u32 AuxAnt_CtrlFrame_Sum;
	u32 MainAnt_CtrlFrame_Cnt;
	u32 AuxAnt_CtrlFrame_Cnt;

} FAT_T, *pFAT_T;

typedef enum _FAT_STATE {
	FAT_NORMAL_STATE			= 0,
	FAT_TRAINING_STATE		= 1,
} FAT_STATE_E, *PFAT_STATE_E;

typedef enum _ANT_DIV_TYPE {
	NO_ANTDIV			= 0xFF,
	CG_TRX_HW_ANTDIV		= 0x01,
	CGCS_RX_HW_ANTDIV	= 0x02,
	FIXED_HW_ANTDIV		= 0x03,
	CG_TRX_SMART_ANTDIV	= 0x04,
	CGCS_RX_SW_ANTDIV	= 0x05,
	S0S1_SW_ANTDIV          = 0x06 /* 8723B intrnal switch S0 S1 */
} ANT_DIV_TYPE_E, *PANT_DIV_TYPE_E;

typedef struct _ODM_PATH_DIVERSITY_ {
	u8 RespTxPath;
	u8 PathSel[ODM_ASSOCIATE_ENTRY_NUM];
	u32 PathA_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32 PathB_Sum[ODM_ASSOCIATE_ENTRY_NUM];
	u32 PathA_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
	u32 PathB_Cnt[ODM_ASSOCIATE_ENTRY_NUM];
} PATHDIV_T, *pPATHDIV_T;

typedef enum _BASEBAND_CONFIG_PHY_REG_PG_VALUE_TYPE{
	PHY_REG_PG_RELATIVE_VALUE = 0,
	PHY_REG_PG_EXACT_VALUE = 1
} PHY_REG_PG_TYPE;

/*  */
/*  Antenna detection information from single tone mechanism, added by Roger, 2012.11.27. */
/*  */
typedef struct _ANT_DETECTED_INFO {
	bool bAntDetected;
	u32 dBForAntA;
	u32 dBForAntB;
	u32 dBForAntO;
} ANT_DETECTED_INFO, *PANT_DETECTED_INFO;

/*  */
/*  2011/09/22 MH Copy from SD4 defined structure. We use to support PHY DM integration. */
/*  */
typedef  struct DM_Out_Source_Dynamic_Mechanism_Structure {
	/* RT_TIMER	FastAntTrainingTimer; */
	/*  */
	/* 	Add for different team use temporarily */
	/*  */
	struct adapter *Adapter;		/*  For CE/NIC team */
	/*  WHen you use Adapter or priv pointer, you must make sure the pointer is ready. */
	bool odm_ready;

	PHY_REG_PG_TYPE PhyRegPgValueType;
	u8 PhyRegPgVersion;

	u64	DebugComponents;
	u32 DebugLevel;

	u32 NumQryPhyStatusAll;	/* CCK + OFDM */
	u32 LastNumQryPhyStatusAll;
	u32 RxPWDBAve;
	bool MPDIG_2G;		/* off MPDIG */
	u8 Times_2G;

/*  ODM HANDLE, DRIVER NEEDS NOT TO HOOK------ */
	bool bCckHighPower;
	u8 RFPathRxEnable;		/*  ODM_CMNINFO_RFPATH_ENABLE */
	u8 ControlChannel;
/*  ODM HANDLE, DRIVER NEEDS NOT TO HOOK------ */

/* REMOVED COMMON INFO---------- */
	/* u8 		PseudoMacPhyMode; */
	/* bool			*BTCoexist; */
	/* bool			PseudoBtCoexist; */
	/* u8 		OPMode; */
	/* bool			bAPMode; */
	/* bool			bClientMode; */
	/* bool			bAdHocMode; */
	/* bool			bSlaveOfDMSP; */
/* REMOVED COMMON INFO---------- */

/* 1  COMMON INFORMATION */

	/*  */
	/*  Init Value */
	/*  */
/* HOOK BEFORE REG INIT----------- */
	/*  ODM Platform info AP/ADSL/CE/MP = 1/2/3/4 */
	u8 SupportPlatform;
	/*  ODM Support Ability DIG/RATR/TX_PWR_TRACK/... = 1/2/3/... */
	u32 SupportAbility;
	/*  ODM PCIE/USB/SDIO = 1/2/3 */
	u8 SupportInterface;
	/*  ODM composite or independent. Bit oriented/ 92C+92D+ .... or any other type = 1/2/3/... */
	u32 SupportICType;
	/*  Cut Version TestChip/A-cut/B-cut... = 0/1/2/3/... */
	u8 CutVersion;
	/*  Fab Version TSMC/UMC = 0/1 */
	u8 FabVersion;
	/*  RF Type 4T4R/3T3R/2T2R/1T2R/1T1R/... */
	u8 RFType;
	u8 RFEType;
	/*  Board Type Normal/HighPower/MiniCard/SLIM/Combo/... = 0/1/2/3/4/... */
	u8 BoardType;
	u8 PackageType;
	u8 TypeGLNA;
	u8 TypeGPA;
	u8 TypeALNA;
	u8 TypeAPA;
	/*  with external LNA  NO/Yes = 0/1 */
	u8 ExtLNA;
	u8 ExtLNA5G;
	/*  with external PA  NO/Yes = 0/1 */
	u8 ExtPA;
	u8 ExtPA5G;
	/*  with external TRSW  NO/Yes = 0/1 */
	u8 ExtTRSW;
	u8 PatchID; /* Customer ID */
	bool bInHctTest;
	bool bWIFITest;

	bool bDualMacSmartConcurrent;
	u32 BK_SupportAbility;
	u8 AntDivType;
/* HOOK BEFORE REG INIT----------- */

	/*  */
	/*  Dynamic Value */
	/*  */
/*  POINTER REFERENCE----------- */

	u8 u8_temp;
	bool bool_temp;
	struct adapter *adapter_temp;

	/*  MAC PHY Mode SMSP/DMSP/DMDP = 0/1/2 */
	u8 *pMacPhyMode;
	/* TX Unicast byte count */
	u64 *pNumTxBytesUnicast;
	/* RX Unicast byte count */
	u64 *pNumRxBytesUnicast;
	/*  Wireless mode B/G/A/N = BIT0/BIT1/BIT2/BIT3 */
	u8 *pwirelessmode; /* ODM_WIRELESS_MODE_E */
	/*  Frequence band 2.4G/5G = 0/1 */
	u8 *pBandType;
	/*  Secondary channel offset don't_care/below/above = 0/1/2 */
	u8 *pSecChOffset;
	/*  Security mode Open/WEP/AES/TKIP = 0/1/2/3 */
	u8 *pSecurity;
	/*  BW info 20M/40M/80M = 0/1/2 */
	u8 *pBandWidth;
	/*  Central channel location Ch1/Ch2/.... */
	u8 *pChannel; /* central channel number */
	bool DPK_Done;
	/*  Common info for 92D DMSP */

	bool *pbGetValueFromOtherMac;
	struct adapter **pBuddyAdapter;
	bool *pbMasterOfDMSP; /* MAC0: master, MAC1: slave */
	/*  Common info for Status */
	bool *pbScanInProcess;
	bool *pbPowerSaving;
	/*  CCA Path 2-path/path-A/path-B = 0/1/2; using ODM_CCA_PATH_E. */
	u8 *pOnePathCCA;
	/* pMgntInfo->AntennaTest */
	u8 *pAntennaTest;
	bool *pbNet_closed;
	u8 *mp_mode;
	/* u8 	*pAidMap; */
	u8 *pu1ForcedIgiLb;
/*  For 8723B IQK----------- */
	bool *pIs1Antenna;
	u8 *pRFDefaultPath;
	/*  0:S1, 1:S0 */

/*  POINTER REFERENCE----------- */
	u16 *pForcedDataRate;
/* CALL BY VALUE------------- */
	bool bLinkInProcess;
	bool bWIFI_Direct;
	bool bWIFI_Display;
	bool bLinked;

	bool bsta_state;
	u8 RSSI_Min;
	u8 InterfaceIndex; /*  Add for 92D  dual MAC: 0--Mac0 1--Mac1 */
	bool bIsMPChip;
	bool bOneEntryOnly;
	/*  Common info for BTDM */
	bool bBtEnabled;			/*  BT is disabled */
	bool bBtConnectProcess;	/*  BT HS is under connection progress. */
	u8 btHsRssi;				/*  BT HS mode wifi rssi value. */
	bool bBtHsOperation;		/*  BT HS mode is under progress */
	bool bBtDisableEdcaTurbo;	/*  Under some condition, don't enable the EDCA Turbo */
	bool bBtLimitedDig;			/*  BT is busy. */
/* CALL BY VALUE------------- */
	u8 RSSI_A;
	u8 RSSI_B;
	u64 RSSI_TRSW;
	u64 RSSI_TRSW_H;
	u64 RSSI_TRSW_L;
	u64 RSSI_TRSW_iso;

	u8 RxRate;
	bool bNoisyState;
	u8 TxRate;
	u8 LinkedInterval;
	u8 preChannel;
	u32 TxagcOffsetValueA;
	bool IsTxagcOffsetPositiveA;
	u32 TxagcOffsetValueB;
	bool IsTxagcOffsetPositiveB;
	u64	lastTxOkCnt;
	u64	lastRxOkCnt;
	u32 BbSwingOffsetA;
	bool IsBbSwingOffsetPositiveA;
	u32 BbSwingOffsetB;
	bool IsBbSwingOffsetPositiveB;
	s8 TH_L2H_ini;
	s8 TH_EDCCA_HL_diff;
	s8 IGI_Base;
	u8 IGI_target;
	bool ForceEDCCA;
	u8 AdapEn_RSSI;
	s8 Force_TH_H;
	s8 Force_TH_L;
	u8 IGI_LowerBound;
	u8 antdiv_rssi;
	u8 AntType;
	u8 pre_AntType;
	u8 antdiv_period;
	u8 antdiv_select;
	u8 NdpaPeriod;
	bool H2C_RARpt_connect;

	/*  add by Yu Cehn for adaptivtiy */
	bool adaptivity_flag;
	bool NHM_disable;
	bool TxHangFlg;
	bool Carrier_Sense_enable;
	u8 tolerance_cnt;
	u64 NHMCurTxOkcnt;
	u64 NHMCurRxOkcnt;
	u64 NHMLastTxOkcnt;
	u64 NHMLastRxOkcnt;
	u8 txEdcca1;
	u8 txEdcca0;
	s8 H2L_lb;
	s8 L2H_lb;
	u8 Adaptivity_IGI_upper;
	u8 NHM_cnt_0;

	ODM_NOISE_MONITOR noise_level;/* ODM_MAX_CHANNEL_NUM]; */
	/*  */
	/* 2 Define STA info. */
	/*  _ODM_STA_INFO */
	/*  2012/01/12 MH For MP, we need to reduce one array pointer for default port.?? */
	PSTA_INFO_T pODM_StaInfo[ODM_ASSOCIATE_ENTRY_NUM];

	/*  */
	/*  2012/02/14 MH Add to share 88E ra with other SW team. */
	/*  We need to colelct all support abilit to a proper area. */
	/*  */
	bool RaSupport88E;

	/*  Define ........... */

	/*  Latest packet phy info (ODM write) */
	ODM_PHY_DBG_INFO_T PhyDbgInfo;
	/* PHY_INFO_88E		PhyInfo; */

	/*  Latest packet phy info (ODM write) */
	ODM_MAC_INFO *pMacInfo;
	/* MAC_INFO_88E		MacInfo; */

	/*  Different Team independt structure?? */

	/*  */
	/* TX_RTP_CMN		TX_retrpo; */
	/* TX_RTP_88E		TX_retrpo; */
	/* TX_RTP_8195		TX_retrpo; */

	/*  */
	/* ODM Structure */
	/*  */
	FAT_T DM_FatTable;
	DIG_T DM_DigTable;
	PS_T DM_PSTable;
	struct dynamic_primary_CCA DM_PriCCA;
	RXHP_T DM_RXHP_Table;
	RA_T DM_RA_Table;
	false_ALARM_STATISTICS FalseAlmCnt;
	false_ALARM_STATISTICS FlaseAlmCntBuddyAdapter;
	SWAT_T DM_SWAT_Table;
	bool RSSI_test;
	CFO_TRACKING DM_CfoTrack;

	EDCA_T DM_EDCA_Table;
	u32 WMMEDCA_BE;
	PATHDIV_T DM_PathDiv;
	/*  Copy from SD4 structure */
	/*  */
	/*  ================================================== */
	/*  */

	/* common */
	/* u8 DM_Type; */
	/* u8    PSD_Report_RXHP[80];    Add By Gary */
	/* u8    PSD_func_flag;                Add By Gary */
	/* for DIG */
	/* u8 bDMInitialGainEnable; */
	/* u8 binitialized;  for dm_initial_gain_Multi_STA use. */
	/* for Antenna diversity */
	/* u8 AntDivCfg; 0:OFF , 1:ON, 2:by efuse */
	/* PSTA_INFO_T RSSI_target; */

	bool *pbDriverStopped;
	bool *pbDriverIsGoingToPnpSetPowerSleep;
	bool *pinit_adpt_in_progress;

	/* PSD */
	bool bUserAssignLevel;
	RT_TIMER PSDTimer;
	u8 RSSI_BT;			/* come from BT */
	bool bPSDinProcess;
	bool bPSDactive;
	bool bDMInitialGainEnable;

	/* MPT DIG */
	RT_TIMER MPT_DIGTimer;

	/* for rate adaptive, in fact,  88c/92c fw will handle this */
	u8 bUseRAMask;

	ODM_RATE_ADAPTIVE RateAdaptive;

	ANT_DETECTED_INFO AntDetectedInfo; /*  Antenna detected information for RSSI tool */

	ODM_RF_CAL_T RFCalibrateInfo;

	/*  */
	/*  TX power tracking */
	/*  */
	u8 BbSwingIdxOfdm[MAX_RF_PATH];
	u8 BbSwingIdxOfdmCurrent;
	u8 BbSwingIdxOfdmBase[MAX_RF_PATH];
	bool BbSwingFlagOfdm;
	u8 BbSwingIdxCck;
	u8 BbSwingIdxCckCurrent;
	u8 BbSwingIdxCckBase;
	u8 DefaultOfdmIndex;
	u8 DefaultCckIndex;
	bool BbSwingFlagCck;

	s8 Absolute_OFDMSwingIdx[MAX_RF_PATH];
	s8 Remnant_OFDMSwingIdx[MAX_RF_PATH];
	s8 Remnant_CCKSwingIdx;
	s8 Modify_TxAGC_Value;       /* Remnat compensate value at TxAGC */
	bool Modify_TxAGC_Flag_PathA;
	bool Modify_TxAGC_Flag_PathB;
	bool Modify_TxAGC_Flag_PathC;
	bool Modify_TxAGC_Flag_PathD;
	bool Modify_TxAGC_Flag_PathA_CCK;

	s8 KfreeOffset[MAX_RF_PATH];
	/*  */
	/*  ODM system resource. */
	/*  */

	/*  ODM relative time. */
	RT_TIMER PathDivSwitchTimer;
	/* 2011.09.27 add for Path Diversity */
	RT_TIMER CCKPathDiversityTimer;
	RT_TIMER FastAntTrainingTimer;

	/*  ODM relative workitem. */

	#if (BEAMFORMING_SUPPORT == 1)
	RT_BEAMFORMING_INFO BeamformingInfo;
	#endif
} DM_ODM_T, *PDM_ODM_T; /*  DM_Dynamic_Mechanism_Structure */

#define ODM_RF_PATH_MAX 2

typedef enum _ODM_RF_RADIO_PATH {
	ODM_RF_PATH_A = 0,   /* Radio Path A */
	ODM_RF_PATH_B = 1,   /* Radio Path B */
	ODM_RF_PATH_C = 2,   /* Radio Path C */
	ODM_RF_PATH_D = 3,   /* Radio Path D */
	ODM_RF_PATH_AB,
	ODM_RF_PATH_AC,
	ODM_RF_PATH_AD,
	ODM_RF_PATH_BC,
	ODM_RF_PATH_BD,
	ODM_RF_PATH_CD,
	ODM_RF_PATH_ABC,
	ODM_RF_PATH_ACD,
	ODM_RF_PATH_BCD,
	ODM_RF_PATH_ABCD,
	/*   ODM_RF_PATH_MAX,    Max RF number 90 support */
} ODM_RF_RADIO_PATH_E, *PODM_RF_RADIO_PATH_E;

 typedef enum _ODM_RF_CONTENT {
	odm_radioa_txt = 0x1000,
	odm_radiob_txt = 0x1001,
	odm_radioc_txt = 0x1002,
	odm_radiod_txt = 0x1003
} ODM_RF_CONTENT;

typedef enum _ODM_BB_Config_Type {
	CONFIG_BB_PHY_REG,
	CONFIG_BB_AGC_TAB,
	CONFIG_BB_AGC_TAB_2G,
	CONFIG_BB_AGC_TAB_5G,
	CONFIG_BB_PHY_REG_PG,
	CONFIG_BB_PHY_REG_MP,
	CONFIG_BB_AGC_TAB_DIFF,
} ODM_BB_Config_Type, *PODM_BB_Config_Type;

typedef enum _ODM_RF_Config_Type {
	CONFIG_RF_RADIO,
	CONFIG_RF_TXPWR_LMT,
} ODM_RF_Config_Type, *PODM_RF_Config_Type;

typedef enum _ODM_FW_Config_Type {
	CONFIG_FW_NIC,
	CONFIG_FW_NIC_2,
	CONFIG_FW_AP,
	CONFIG_FW_WoWLAN,
	CONFIG_FW_WoWLAN_2,
	CONFIG_FW_AP_WoWLAN,
	CONFIG_FW_BT,
} ODM_FW_Config_Type;

/*  Status code */
typedef enum _RT_STATUS {
	RT_STATUS_SUCCESS,
	RT_STATUS_FAILURE,
	RT_STATUS_PENDING,
	RT_STATUS_RESOURCE,
	RT_STATUS_INVALID_CONTEXT,
	RT_STATUS_INVALID_PARAMETER,
	RT_STATUS_NOT_SUPPORT,
	RT_STATUS_OS_API_FAILED,
} RT_STATUS, *PRT_STATUS;

#ifdef REMOVE_PACK
#pragma pack()
#endif

/* include "odm_function.h" */

/* 3 =========================================================== */
/* 3 DIG */
/* 3 =========================================================== */

/* Remove DIG by Yuchen */

/* 3 =========================================================== */
/* 3 AGC RX High Power Mode */
/* 3 =========================================================== */
#define          LNA_Low_Gain_1                      0x64
#define          LNA_Low_Gain_2                      0x5A
#define          LNA_Low_Gain_3                      0x58

#define          FA_RXHP_TH1                           5000
#define          FA_RXHP_TH2                           1500
#define          FA_RXHP_TH3                             800
#define          FA_RXHP_TH4                             600
#define          FA_RXHP_TH5                             500

/* 3 =========================================================== */
/* 3 EDCA */
/* 3 =========================================================== */

/* 3 =========================================================== */
/* 3 Dynamic Tx Power */
/* 3 =========================================================== */
/* Dynamic Tx Power Control Threshold */

/* 3 =========================================================== */
/* 3 Rate Adaptive */
/* 3 =========================================================== */
#define		DM_RATR_STA_INIT			0
#define		DM_RATR_STA_HIGH			1
#define		DM_RATR_STA_MIDDLE			2
#define		DM_RATR_STA_LOW				3

/* 3 =========================================================== */
/* 3 BB Power Save */
/* 3 =========================================================== */

typedef enum tag_1R_CCA_Type_Definition {
	CCA_1R = 0,
	CCA_2R = 1,
	CCA_MAX = 2,
} DM_1R_CCA_E;

typedef enum tag_RF_Type_Definition {
	RF_Save = 0,
	RF_Normal = 1,
	RF_MAX = 2,
} DM_RF_E;

/* 3 =========================================================== */
/* 3 Antenna Diversity */
/* 3 =========================================================== */
typedef enum tag_SW_Antenna_Switch_Definition {
	Antenna_A = 1,
	Antenna_B = 2,
	Antenna_MAX = 3,
} DM_SWAS_E;

/*  Maximal number of antenna detection mechanism needs to perform, added by Roger, 2011.12.28. */
#define	MAX_ANTENNA_DETECTION_CNT	10

/*  */
/*  Extern Global Variables. */
/*  */
extern	u32 OFDMSwingTable[OFDM_TABLE_SIZE];
extern	u8 CCKSwingTable_Ch1_Ch13[CCK_TABLE_SIZE][8];
extern	u8 CCKSwingTable_Ch14[CCK_TABLE_SIZE][8];

extern	u32 OFDMSwingTable_New[OFDM_TABLE_SIZE];
extern	u8 CCKSwingTable_Ch1_Ch13_New[CCK_TABLE_SIZE][8];
extern	u8 CCKSwingTable_Ch14_New[CCK_TABLE_SIZE][8];

extern  u32 TxScalingTable_Jaguar[TXSCALE_TABLE_SIZE];

/*  */
/*  check Sta pointer valid or not */
/*  */
#define IS_STA_VALID(pSta)		(pSta)
/*  20100514 Joseph: Add definition for antenna switching test after link. */
/*  This indicates two different the steps. */
/*  In SWAW_STEP_PEAK, driver needs to switch antenna and listen to the signal on the air. */
/*  In SWAW_STEP_DETERMINE, driver just compares the signal captured in SWAW_STEP_PEAK */
/*  with original RSSI to determine if it is necessary to switch antenna. */
#define SWAW_STEP_PEAK		0
#define SWAW_STEP_DETERMINE	1

/* Remove DIG by yuchen */

void ODM_SetAntenna(PDM_ODM_T pDM_Odm, u8 Antenna);

/* Remove BB power saving by Yuchen */

#define dm_CheckTXPowerTracking ODM_TXPowerTrackingCheck
void ODM_TXPowerTrackingCheck(PDM_ODM_T pDM_Odm);

bool ODM_RAStateCheck(
	PDM_ODM_T pDM_Odm,
	s32	RSSI,
	bool bForceUpdate,
	u8 *pRATRState
);

#define dm_SWAW_RSSI_Check	ODM_SwAntDivChkPerPktRssi
void ODM_SwAntDivChkPerPktRssi(
	PDM_ODM_T pDM_Odm,
	u8 StationID,
	struct odm_phy_info *pPhyInfo
);

u32 ODM_Get_Rate_Bitmap(
	PDM_ODM_T pDM_Odm,
	u32 macid,
	u32 ra_mask,
	u8 rssi_level
);

#if (BEAMFORMING_SUPPORT == 1)
BEAMFORMING_CAP Beamforming_GetEntryBeamCapByMacId(PMGNT_INFO pMgntInfo, u8 MacId);
#endif

void odm_TXPowerTrackingInit(PDM_ODM_T pDM_Odm);

void ODM_DMInit(PDM_ODM_T pDM_Odm);

void ODM_DMWatchdog(PDM_ODM_T pDM_Odm); /*  For common use in the future */

void ODM_CmnInfoInit(PDM_ODM_T pDM_Odm, ODM_CMNINFO_E CmnInfo, u32 Value);

void ODM_CmnInfoHook(PDM_ODM_T pDM_Odm, ODM_CMNINFO_E CmnInfo, void *pValue);

void ODM_CmnInfoPtrArrayHook(
	PDM_ODM_T pDM_Odm,
	ODM_CMNINFO_E CmnInfo,
	u16 Index,
	void *pValue
);

void ODM_CmnInfoUpdate(PDM_ODM_T pDM_Odm, u32 CmnInfo, u64 Value);

void ODM_InitAllTimers(PDM_ODM_T pDM_Odm);

void ODM_CancelAllTimers(PDM_ODM_T pDM_Odm);

void ODM_ReleaseAllTimers(PDM_ODM_T pDM_Odm);

void ODM_AntselStatistics_88C(
	PDM_ODM_T pDM_Odm,
	u8 MacId,
	u32 PWDBAll,
	bool isCCKrate
);

void ODM_DynamicARFBSelect(PDM_ODM_T pDM_Odm, u8 rate, bool Collision_State);

#endif
