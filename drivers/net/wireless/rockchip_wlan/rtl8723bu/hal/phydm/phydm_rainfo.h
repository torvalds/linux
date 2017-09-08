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

#ifndef	__PHYDMRAINFO_H__
#define    __PHYDMRAINFO_H__

/*#define RAINFO_VERSION	"2.0"  //2014.11.04*/
/*#define RAINFO_VERSION	"3.0"  //2015.01.13 Dino*/
/*#define RAINFO_VERSION	"3.1"  //2015.01.14 Dino*/
#define RAINFO_VERSION	"3.2"  /*2015.01.14 Dino*/

#define HIGH_RSSI_THRESH	50
#define LOW_RSSI_THRESH	20

#define	ACTIVE_TP_THRESHOLD	150
#define	RA_RETRY_DESCEND_NUM	2
#define	RA_RETRY_LIMIT_LOW	4
#define	RA_RETRY_LIMIT_HIGH	32

#define PHYDM_IC_8051_SERIES		(ODM_RTL8881A|ODM_RTL8812|ODM_RTL8821|ODM_RTL8192S|ODM_RTL8192C|ODM_RTL8192D|ODM_RTL8723A|ODM_RTL8188E|ODM_RTL8192E|ODM_RTL8723B|ODM_RTL8703B|ODM_RTL8188F)
#define PHYDM_IC_3081_SERIES		(ODM_RTL8814A|ODM_RTL8821B|ODM_RTL8822B)

#define RAINFO_BE_RX_STATE			BIT0  // 1:RX    //ULDL
#define RAINFO_STBC_STATE			BIT1
//#define RAINFO_LDPC_STATE 			BIT2
#define RAINFO_NOISY_STATE 			BIT2    // set by Noisy_Detection
#define RAINFO_SHURTCUT_STATE 		BIT3
#define RAINFO_SHURTCUT_FLAG 		BIT4
#define RAINFO_INIT_RSSI_RATE_STATE  BIT5
#define RAINFO_BF_STATE 				BIT6
#define RAINFO_BE_TX_STATE 			BIT7 // 1:TX

#define	RA_MASK_CCK		0xf
#define	RA_MASK_OFDM		0xff0
#define	RA_MASK_HT1SS		0xff000
#define	RA_MASK_HT2SS		0xff00000
/*#define	RA_MASK_MCS3SS	*/
#define	RA_MASK_HT4SS		0xff0
#define	RA_MASK_VHT1SS	0x3ff000
#define	RA_MASK_VHT2SS	0xffc00000

#if(DM_ODM_SUPPORT_TYPE == ODM_AP)
#define	EXT_RA_INFO_SUPPORT_IC (ODM_RTL8881A |ODM_RTL8192E |ODM_RTL8812 |ODM_RTL8814A|ODM_RTL8822B)
#define		RA_FIRST_MACID 	1
#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)
#define	EXT_RA_INFO_SUPPORT_IC (ODM_RTL8192E | ODM_RTL8812 | ODM_RTL8821 | ODM_RTL8723B | ODM_RTL8814A | ODM_RTL8822B | ODM_RTL8703B)
#define		RA_FIRST_MACID 	0
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)
/*#define	EXT_RA_INFO_SUPPORT_IC (ODM_RTL8192E|ODM_RTL8812|ODM_RTL8821|ODM_RTL8723B|ODM_RTL8814A|ODM_RTL8822B|ODM_RTL8703B) */
#define		RA_FIRST_MACID 	0
#endif


#define AP_InitRateAdaptiveState	ODM_RateAdaptiveStateApInit

#define		DM_RATR_STA_INIT			0
#define		DM_RATR_STA_HIGH			1
#define 		DM_RATR_STA_MIDDLE		2
#define 		DM_RATR_STA_LOW			3
#if(DM_ODM_SUPPORT_TYPE & ODM_AP)
#define		DM_RATR_STA_ULTRA_LOW	4
#endif

#define		DM_RA_RATE_UP				1
#define		DM_RA_RATE_DOWN			2

typedef enum _phydm_arfr_num {
	ARFR_0_RATE_ID	=	0x9,
	ARFR_1_RATE_ID	=	0xa,
	ARFR_2_RATE_ID	=	0xb,
	ARFR_3_RATE_ID	=	0xc,
	ARFR_4_RATE_ID	=	0xd,
	ARFR_5_RATE_ID	=	0xe
} PHYDM_RA_ARFR_NUM_E;

typedef enum _Phydm_ra_dbg_para {
	RADBG_RTY_PENALTY			=	1,  //u8
	RADBG_N_HIGH 				=	2,
	RADBG_N_LOW				=	3,
	RADBG_TRATE_UP_TABLE		=	4,
	RADBG_TRATE_DOWN_TABLE	=	5,
	RADBG_TRYING_NECESSARY	=	6,
	RADBG_TDROPING_NECESSARY =	7,
	RADBG_RATE_UP_RTY_RATIO	=	8, //u8
	RADBG_RATE_DOWN_RTY_RATIO =	9, //u8

	RADBG_DEBUG_MONITOR1 = 0xc,
	RADBG_DEBUG_MONITOR2 = 0xd,
	RADBG_DEBUG_MONITOR3 = 0xe,
	RADBG_DEBUG_MONITOR4 = 0xf,
	NUM_RA_PARA
} PHYDM_RA_DBG_PARA_E;


#if (RATE_ADAPTIVE_SUPPORT == 1)//88E RA
typedef struct _ODM_RA_Info_ {
	u1Byte RateID;
	u4Byte RateMask;
	u4Byte RAUseRate;
	u1Byte RateSGI;
	u1Byte RssiStaRA;
	u1Byte PreRssiStaRA;
	u1Byte SGIEnable;
	u1Byte DecisionRate;
	u1Byte PreRate;
	u1Byte HighestRate;
	u1Byte LowestRate;
	u4Byte NscUp;
	u4Byte NscDown;
	u2Byte RTY[5];
	u4Byte TOTAL;
	u2Byte DROP;
	u1Byte Active;
	u2Byte RptTime;
	u1Byte RAWaitingCounter;
	u1Byte RAPendingCounter;
#if 1 //POWER_TRAINING_ACTIVE == 1 // For compile  pass only~!
	u1Byte PTActive;  // on or off
	u1Byte PTTryState;  // 0 trying state, 1 for decision state
	u1Byte PTStage;  // 0~6
	u1Byte PTStopCount; //Stop PT counter
	u1Byte PTPreRate;  // if rate change do PT
	u1Byte PTPreRssi; // if RSSI change 5% do PT
	u1Byte PTModeSS;  // decide whitch rate should do PT
	u1Byte RAstage;  // StageRA, decide how many times RA will be done between PT
	u1Byte PTSmoothFactor;
#endif
#if (DM_ODM_SUPPORT_TYPE == ODM_AP) && ((DEV_BUS_TYPE == RT_USB_INTERFACE) || (DEV_BUS_TYPE == RT_SDIO_INTERFACE))
	u1Byte RateDownCounter;
	u1Byte RateUpCounter;
	u1Byte RateDirection;
	u1Byte BoundingType;
	u1Byte BoundingCounter;
	u1Byte BoundingLearningTime;
	u1Byte RateDownStartTime;
#endif
} ODM_RA_INFO_T, *PODM_RA_INFO_T;
#endif


typedef struct _Rate_Adaptive_Table_ {
	u1Byte		firstconnect;
#if(DM_ODM_SUPPORT_TYPE==ODM_WIN)
	BOOLEAN		PT_collision_pre;
#endif

#if (defined(CONFIG_RA_DBG_CMD))
	BOOLEAN		is_ra_dbg_init;

	u1Byte	RTY_P[ODM_NUM_RATE_IDX];
	u1Byte	RTY_P_default[ODM_NUM_RATE_IDX];
	BOOLEAN	RTY_P_modify_note[ODM_NUM_RATE_IDX];

	u1Byte	RATE_UP_RTY_RATIO[ODM_NUM_RATE_IDX];
	u1Byte	RATE_UP_RTY_RATIO_default[ODM_NUM_RATE_IDX];
	BOOLEAN	RATE_UP_RTY_RATIO_modify_note[ODM_NUM_RATE_IDX];

	u1Byte	RATE_DOWN_RTY_RATIO[ODM_NUM_RATE_IDX];
	u1Byte	RATE_DOWN_RTY_RATIO_default[ODM_NUM_RATE_IDX];
	BOOLEAN	RATE_DOWN_RTY_RATIO_modify_note[ODM_NUM_RATE_IDX];

	BOOLEAN RA_Para_feedback_req;

	u1Byte   para_idx;
	u1Byte	rate_idx;
	u1Byte	value;
	u2Byte	value_16;
	u1Byte	rate_length;
#endif
	u1Byte	link_tx_rate[ODM_ASSOCIATE_ENTRY_NUM];

	#if (defined(CONFIG_RA_DYNAMIC_RTY_LIMIT))
	u1Byte per_rate_retrylimit_20M[ODM_NUM_RATE_IDX];
	u1Byte per_rate_retrylimit_40M[ODM_NUM_RATE_IDX];	
	u1Byte			retry_descend_num;
	u1Byte			retrylimit_low;
	u1Byte			retrylimit_high;
	#endif


} RA_T, *pRA_T;

typedef struct _ODM_RATE_ADAPTIVE {
	u1Byte				Type;				// DM_Type_ByFW/DM_Type_ByDriver
	u1Byte				HighRSSIThresh;		// if RSSI > HighRSSIThresh	=> RATRState is DM_RATR_STA_HIGH
	u1Byte				LowRSSIThresh;		// if RSSI <= LowRSSIThresh	=> RATRState is DM_RATR_STA_LOW
	u1Byte				RATRState;			// Current RSSI level, DM_RATR_STA_HIGH/DM_RATR_STA_MIDDLE/DM_RATR_STA_LOW

#if(DM_ODM_SUPPORT_TYPE & (ODM_WIN|ODM_CE))
	u1Byte				LdpcThres;			// if RSSI > LdpcThres => switch from LPDC to BCC
	BOOLEAN				bLowerRtsRate;
#endif

#if(DM_ODM_SUPPORT_TYPE & ODM_WIN)
	u1Byte				RtsThres;
#elif(DM_ODM_SUPPORT_TYPE & ODM_CE)
	BOOLEAN				bUseLdpc;
#else
	u1Byte				UltraLowRSSIThresh;
	u4Byte				LastRATR;			// RATR Register Content
#endif

} ODM_RATE_ADAPTIVE, *PODM_RATE_ADAPTIVE;

VOID
ODM_C2HRaParaReportHandler(
	IN	PVOID	pDM_VOID,
	IN pu1Byte   CmdBuf,
	IN u1Byte   CmdLen
);

VOID
odm_RA_ParaAdjust_Send_H2C(
	IN	PVOID	pDM_VOID
);

VOID
odm_RA_debug(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value
);

VOID
odm_RA_ParaAdjust_init(
	IN		PVOID		pDM_VOID
);

VOID
odm_RA_ParaAdjust(
	IN		PVOID		pDM_VOID
);

VOID
phydm_ra_dynamic_retry_count(
	IN	PVOID	pDM_VOID
);

VOID
phydm_ra_dynamic_retry_limit(
	IN	PVOID	pDM_VOID
);

VOID
phydm_ra_dynamic_rate_id_on_assoc(
	IN	PVOID	pDM_VOID,
	IN	u1Byte	wireless_mode,
	IN	u1Byte	init_rate_id
);

VOID
phydm_c2h_ra_report_handler(
	IN PVOID	pDM_VOID,
	IN pu1Byte   CmdBuf,
	IN u1Byte   CmdLen
);

VOID
phydm_ra_info_init(
	IN	PVOID	pDM_VOID
);

VOID
odm_RSSIMonitorInit(
	IN	PVOID	pDM_VOID
);

VOID
odm_RSSIMonitorCheck(
	IN	PVOID	pDM_VOID
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
s4Byte
phydm_FindMinimumRSSI(
IN		PDM_ODM_T		pDM_Odm,
IN		PADAPTER		pAdapter,
IN OUT	BOOLEAN	*pbLink_temp

	);
#endif

VOID
odm_RSSIMonitorCheckMP(
	IN	PVOID	pDM_VOID
);

VOID
odm_RSSIMonitorCheckCE(
	IN	PVOID	pDM_VOID
);

VOID
odm_RSSIMonitorCheckAP(
	IN	PVOID	pDM_VOID
);


VOID
odm_RateAdaptiveMaskInit(
	IN 	PVOID	pDM_VOID
);

VOID
odm_RefreshRateAdaptiveMask(
	IN		PVOID		pDM_VOID
);

VOID
odm_RefreshRateAdaptiveMaskMP(
	IN		PVOID		pDM_VOID
);

VOID
odm_RefreshRateAdaptiveMaskCE(
	IN		PVOID		pDM_VOID
);

VOID
odm_RefreshRateAdaptiveMaskAPADSL(
	IN		PVOID		pDM_VOID
);

BOOLEAN
ODM_RAStateCheck(
	IN		PVOID		    pDM_VOID,
	IN		s4Byte			RSSI,
	IN		BOOLEAN			bForceUpdate,
	OUT		pu1Byte			pRATRState
);

VOID
odm_RefreshBasicRateMask(
	IN		PVOID		pDM_VOID
);
VOID
ODM_RAPostActionOnAssoc(
	IN		PVOID	pDM_Odm
);

#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN | ODM_CE))

u1Byte
odm_Find_RTS_Rate(
	IN	PVOID		pDM_VOID,
	IN		u1Byte			Tx_Rate,
	IN		BOOLEAN			bErpProtect
);

VOID
ODM_UpdateNoisyState(
	IN	PVOID		pDM_VOID,
	IN	BOOLEAN		bNoisyStateFromC2H
);

u4Byte
Set_RA_DM_Ratrbitmap_by_Noisy(
	IN	PVOID			pDM_VOID,
	IN	WIRELESS_MODE	WirelessMode,
	IN	u4Byte			ratr_bitmap,
	IN	u1Byte			rssi_level
);

VOID
ODM_UpdateInitRate(
	IN	PVOID		pDM_VOID,
	IN	u1Byte		Rate
);

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

VOID
odm_RSSIDumpToRegister(
	IN	PVOID	pDM_VOID
);

VOID
odm_RefreshLdpcRtsMP(
	IN	PADAPTER			pAdapter,
	IN	PDM_ODM_T			pDM_Odm,
	IN	u1Byte				mMacId,
	IN	u1Byte				IOTPeer,
	IN	s4Byte				UndecoratedSmoothedPWDB
);

VOID
ODM_DynamicARFBSelect(
	IN		PVOID		pDM_VOID,
	IN 		u1Byte		rate,
	IN  	BOOLEAN		Collision_State
);

VOID
ODM_RateAdaptiveStateApInit(
	IN	PVOID			PADAPTER_VOID,
	IN	PRT_WLAN_STA  	pEntry
);
#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)

static void
FindMinimumRSSI(
	IN	PADAPTER	pAdapter
);

u8Byte
PhyDM_Get_Rate_Bitmap_Ex(
	IN	PVOID		pDM_VOID,
	IN	u4Byte		macid,
	IN	u8Byte		ra_mask,
	IN	u1Byte		rssi_level,
	OUT		u8Byte	*dm_RA_Mask,
	OUT		u1Byte	*dm_RteID
);
u4Byte
ODM_Get_Rate_Bitmap(
	IN	PVOID	    pDM_VOID,
	IN	u4Byte		macid,
	IN	u4Byte 		ra_mask,
	IN	u1Byte 		rssi_level
);
void phydm_ra_rssi_rpt_wk(PVOID pContext);

#endif/*#elif (DM_ODM_SUPPORT_TYPE == ODM_CE)*/

#endif/*#if (DM_ODM_SUPPORT_TYPE & (ODM_WIN| ODM_CE))*/

#endif /*#ifndef	__ODMRAINFO_H__*/


