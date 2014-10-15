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
#ifndef	__RTL8192C_DM_H__
#define __RTL8192C_DM_H__
//============================================================
// Description:
//
// This file is for 92CE/92CU dynamic mechanism only
//
//
//============================================================

#define	RSSI_CCK		0
#define	RSSI_OFDM		1
#define	RSSI_DEFAULT	2

//============================================================
// structure and define
//============================================================

typedef struct _FALSE_ALARM_STATISTICS{
	u32	Cnt_Parity_Fail;
	u32	Cnt_Rate_Illegal;
	u32	Cnt_Crc8_fail;
	u32	Cnt_Mcs_fail;
	u32	Cnt_Ofdm_fail;
	u32	Cnt_Cck_fail;
	u32	Cnt_all;
	u32	Cnt_Fast_Fsync;
	u32	Cnt_SB_Search_fail;
}FALSE_ALARM_STATISTICS, *PFALSE_ALARM_STATISTICS;

typedef struct _Dynamic_Power_Saving_
{
	u8		PreCCAState;
	u8		CurCCAState;

	u8		PreRFState;
	u8		CurRFState;

	s32		Rssi_val_min;
	
}PS_T;

typedef struct _Dynamic_Initial_Gain_Threshold_
{
	u8		Dig_Enable_Flag;
	u8		Dig_Ext_Port_Stage;

	int		RssiLowThresh;
	int		RssiHighThresh;

	u32		FALowThresh;
	u32		FAHighThresh;

	u8		CurSTAConnectState;
	u8		PreSTAConnectState;
	u8		CurMultiSTAConnectState;

	u8		PreIGValue;
	u8		CurIGValue;
	u8		BackupIGValue;

	char		BackoffVal;
	char		BackoffVal_range_max;
	char		BackoffVal_range_min;
	u8		rx_gain_range_max;
	u8		rx_gain_range_min;
	u8		Rssi_val_min;

	u8		PreCCKPDState;
	u8		CurCCKPDState;
        u8		PreCCKFAState;
	u8		CurCCKFAState;
	u8		PreCCAState;
	u8		CurCCAState;

	u8		LargeFAHit;
	u8		ForbiddenIGI;
	u32		Recover_cnt;
	u8		rx_gain_range_min_nolink;

}DIG_T;

typedef enum tag_Dynamic_Init_Gain_Operation_Type_Definition
{
	DIG_TYPE_THRESH_HIGH	= 0,
	DIG_TYPE_THRESH_LOW	= 1,
	DIG_TYPE_BACKOFF		= 2,
	DIG_TYPE_RX_GAIN_MIN	= 3,
	DIG_TYPE_RX_GAIN_MAX	= 4,
	DIG_TYPE_ENABLE 		= 5,
	DIG_TYPE_DISABLE 		= 6,
	DIG_OP_TYPE_MAX
}DM_DIG_OP_E;

typedef enum tag_CCK_Packet_Detection_Threshold_Type_Definition
{
	CCK_PD_STAGE_LowRssi = 0,
	CCK_PD_STAGE_HighRssi = 1,
	CCK_PD_STAGE_MAX = 3,
}DM_CCK_PDTH_E;

typedef enum tag_1R_CCA_Type_Definition
{
	CCA_1R =0,
	CCA_2R = 1,
	CCA_MAX = 2,
}DM_1R_CCA_E;

typedef enum tag_RF_Type_Definition
{
	RF_Save =0,
	RF_Normal = 1,
	RF_MAX = 2,
}DM_RF_E;

typedef enum tag_DIG_EXT_PORT_ALGO_Definition
{
	DIG_EXT_PORT_STAGE_0 = 0,
	DIG_EXT_PORT_STAGE_1 = 1,
	DIG_EXT_PORT_STAGE_2 = 2,
	DIG_EXT_PORT_STAGE_3 = 3,
	DIG_EXT_PORT_STAGE_MAX = 4,
}DM_DIG_EXT_PORT_ALG_E;


typedef enum tag_DIG_Connect_Definition
{
	DIG_STA_DISCONNECT = 0,	
	DIG_STA_CONNECT = 1,
	DIG_STA_BEFORE_CONNECT = 2,
	DIG_MultiSTA_DISCONNECT = 3,
	DIG_MultiSTA_CONNECT = 4,
	DIG_CONNECT_MAX
}DM_DIG_CONNECT_E;



typedef	enum _BT_Ant_NUM{
	Ant_x2	= 0,		
	Ant_x1	= 1
} BT_Ant_NUM, *PBT_Ant_NUM;

typedef	enum _BT_CoType{
	BT_2Wire		= 0,		
	BT_ISSC_3Wire	= 1,
	BT_Accel		= 2,
	BT_CSR_BC4		= 3,
	BT_CSR_BC8		= 4,
	BT_RTL8756		= 5,
} BT_CoType, *PBT_CoType;

typedef	enum _BT_CurState{
	BT_OFF		= 0,	
	BT_ON		= 1,
} BT_CurState, *PBT_CurState;

typedef	enum _BT_ServiceType{
	BT_SCO		= 0,	
	BT_A2DP		= 1,
	BT_HID		= 2,
	BT_HID_Idle	= 3,
	BT_Scan		= 4,
	BT_Idle		= 5,
	BT_OtherAction	= 6,
	BT_Busy			= 7,
	BT_OtherBusy		= 8,
	BT_PAN			= 9,
} BT_ServiceType, *PBT_ServiceType;

typedef	enum _BT_RadioShared{
	BT_Radio_Shared 	= 0,	
	BT_Radio_Individual	= 1,
} BT_RadioShared, *PBT_RadioShared;

struct btcoexist_priv	{
	u8					BT_Coexist;
	u8					BT_Ant_Num;
	u8					BT_CoexistType;
	u8					BT_State;
	u8					BT_CUR_State;		//0:on, 1:off
	u8					BT_Ant_isolation;	//0:good, 1:bad
	u8					BT_PapeCtrl;		//0:SW, 1:SW/HW dynamic
	u8					BT_Service;
	u8					BT_Ampdu;	// 0:Disable BT control A-MPDU, 1:Enable BT control A-MPDU.
	u8					BT_RadioSharedType;
	u32					Ratio_Tx;
	u32					Ratio_PRI;
	u8					BtRfRegOrigin1E;
	u8					BtRfRegOrigin1F;
	u8					BtRssiState;
	u32					BtEdcaUL;
	u32					BtEdcaDL;
	u32					BT_EDCA[2];
	u8					bCOBT;

	u8					bInitSet;
	u8					bBTBusyTraffic;
	u8					bBTTrafficModeSet;
	u8					bBTNonTrafficModeSet;
	//BTTraffic				BT21TrafficStatistics;
	u32					CurrentState;
	u32					PreviousState;
	u8					BtPreRssiState;
	u8					bFWCoexistAllOff;
	u8					bSWCoexistAllOff;
};

#define		BW_AUTO_SWITCH_HIGH_LOW	25
#define		BW_AUTO_SWITCH_LOW_HIGH	30

#define		DM_DIG_THRESH_HIGH			40
#define		DM_DIG_THRESH_LOW			35

#define		DM_FALSEALARM_THRESH_LOW	400
#define		DM_FALSEALARM_THRESH_HIGH	1000

#define		DM_DIG_MAX					0x3e
#define		DM_DIG_MIN					0x1e //0x22//0x1c

#define		DM_DIG_FA_UPPER				0x3e
#define		DM_DIG_FA_LOWER				0x20
#define		DM_DIG_FA_TH0					0x20
#define		DM_DIG_FA_TH1					0x100
#define		DM_DIG_FA_TH2					0x200

#define		DM_DIG_BACKOFF_MAX			12
#define		DM_DIG_BACKOFF_MIN			(-4)
#define		DM_DIG_BACKOFF_DEFAULT		10

#define		RxPathSelection_SS_TH_low		30
#define		RxPathSelection_diff_TH			18

#define		DM_RATR_STA_INIT			0
#define		DM_RATR_STA_HIGH			1
#define 		DM_RATR_STA_MIDDLE		2
#define 		DM_RATR_STA_LOW			3

#define		CTSToSelfTHVal					30
#define		RegC38_TH						20

#define		WAIotTHVal						25

//Dynamic Tx Power Control Threshold
#define		TX_POWER_NEAR_FIELD_THRESH_LVL2	74
#define		TX_POWER_NEAR_FIELD_THRESH_LVL1	67

#define		TxHighPwrLevel_Normal		0	
#define		TxHighPwrLevel_Level1		1
#define		TxHighPwrLevel_Level2		2
#define		TxHighPwrLevel_BT1			3
#define		TxHighPwrLevel_BT2			4
#define		TxHighPwrLevel_15			5
#define		TxHighPwrLevel_35			6
#define		TxHighPwrLevel_50			7
#define		TxHighPwrLevel_70			8
#define		TxHighPwrLevel_100			9

#define		DM_Type_ByFW			0
#define		DM_Type_ByDriver		1


typedef struct _RATE_ADAPTIVE
{
	u8				RateAdaptiveDisabled;
	u8				RATRState;
	u16				reserve;	
	
	u32				HighRSSIThreshForRA;
	u32				High2LowRSSIThreshForRA;
	u8				Low2HighRSSIThreshForRA40M;
	u32				LowRSSIThreshForRA40M;	
	u8				Low2HighRSSIThreshForRA20M;
	u32				LowRSSIThreshForRA20M;	
	u32				UpperRSSIThresholdRATR;
	u32				MiddleRSSIThresholdRATR;
	u32				LowRSSIThresholdRATR;
	u32				LowRSSIThresholdRATR40M;
	u32				LowRSSIThresholdRATR20M;
	u8				PingRSSIEnable;	//cosa add for Netcore long range ping issue
	u32				PingRSSIRATR;	//cosa add for Netcore long range ping issue
	u32				PingRSSIThreshForRA;//cosa add for Netcore long range ping issue
	u32				LastRATR;
	u8				PreRATRState;
	
} RATE_ADAPTIVE, *PRATE_ADAPTIVE;

typedef enum tag_SW_Antenna_Switch_Definition
{
	Antenna_B = 1,
	Antenna_A = 2,
	Antenna_MAX = 3,
}DM_SWAS_E;

#ifdef CONFIG_ANTENNA_DIVERSITY
// This indicates two different the steps. 
// In SWAW_STEP_PEAK, driver needs to switch antenna and listen to the signal on the air.
// In SWAW_STEP_DETERMINE, driver just compares the signal captured in SWAW_STEP_PEAK
// with original RSSI to determine if it is necessary to switch antenna.
#define SWAW_STEP_PEAK		0
#define SWAW_STEP_DETERMINE	1

#define	TP_MODE		0
#define	RSSI_MODE		1
#define	TRAFFIC_LOW	0
#define	TRAFFIC_HIGH	1

typedef struct _SW_Antenna_Switch_
{
	u8		try_flag;
	s32		PreRSSI;
	u8		CurAntenna;
	u8		PreAntenna;
	u8		RSSI_Trying;
	u8		TestMode;
	u8		bTriggerAntennaSwitch;
	u8		SelectAntennaMap;
	// Before link Antenna Switch check
	u8		SWAS_NoLink_State;

}SWAT_T;


#endif


struct 	dm_priv	
{
	u8	DM_Type;
	u8	DMFlag, DMFlag_tmp;
	

	//for DIG
	u8	bDMInitialGainEnable;
	u8	binitialized; // for dm_initial_gain_Multi_STA use.
	DIG_T	DM_DigTable;

	PS_T	DM_PSTable;

	FALSE_ALARM_STATISTICS FalseAlmCnt;
	
	//for rate adaptive, in fact,  88c/92c fw will handle this
	u8 bUseRAMask;
	RATE_ADAPTIVE RateAdaptive;

	//* Upper and Lower Signal threshold for Rate Adaptive*/
	int	UndecoratedSmoothedPWDB;
	int	UndecoratedSmoothedCCK;
	int	EntryMinUndecoratedSmoothedPWDB;
	int	EntryMaxUndecoratedSmoothedPWDB;


	//for High Power
	u8 bDynamicTxPowerEnable;
	u8 LastDTPLvl;
	u8 DynamicTxHighPowerLvl;//Add by Jacken Tx Power Control for Near/Far Range 2008/03/06
		
	//for tx power tracking
	//u8	bTXPowerTracking;
	u8	TXPowercount;
	u8	bTXPowerTrackingInit;	
	u8	TxPowerTrackControl;	//for mp mode, turn off txpwrtracking as default
	u8	TM_Trigger;

	u8	ThermalMeter[2];				// ThermalMeter, index 0 for RFIC0, and 1 for RFIC1
	u8	ThermalValue;
	u8	ThermalValue_LCK;
	u8	ThermalValue_IQK;
	u8	ThermalValue_DPK;

	u8	bRfPiEnable;

	//for APK
	u32	APKoutput[2][2];	//path A/B; output1_1a/output1_2a
	u8	bAPKdone;
	u8	bAPKThermalMeterIgnore;
	u8	bDPdone;
	u8	bDPPathAOK;
	u8	bDPPathBOK;

	//for IQK
	u32	RegC04;
	u32	Reg874;
	u32	RegC08;
	u32	RegB68;
	u32	RegB6C;
	u32	Reg870;
	u32	Reg860;
	u32	Reg864;
	u32	ADDA_backup[IQK_ADDA_REG_NUM];
	u32	IQK_MAC_backup[IQK_MAC_REG_NUM];
	u32	IQK_BB_backup_recover[9];
	u32	IQK_BB_backup[IQK_BB_REG_NUM];
	u8	PowerIndex_backup[6];

	u8	bCCKinCH14;

	char	CCK_index;
	char	OFDM_index[2];

	BOOLEAN		bDoneTxpower;
	char	CCK_index_HP;
	char	OFDM_index_HP[2];
	u8	ThermalValue_HP[HP_THERMAL_NUM];
	u8	ThermalValue_HP_index;

	//for TxPwrTracking
	int	RegE94;
	int 	RegE9C;
	int	RegEB4;
	int	RegEBC;

	u32	TXPowerTrackingCallbackCnt;	//cosa add for debug

	u32	prv_traffic_idx; // edca turbo

	// for dm_RF_Saving
	u8	initialize;
	u32	rf_saving_Reg874;
	u32	rf_saving_RegC70;
	u32	rf_saving_Reg85C;
	u32	rf_saving_RegA74;

	//for Antenna diversity
#ifdef CONFIG_ANTENNA_DIVERSITY
	SWAT_T DM_SWAT_Table;
#endif	
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
	_timer SwAntennaSwitchTimer;
	
	u64	lastTxOkCnt;
	u64	lastRxOkCnt;
	u64	TXByteCnt_A;
	u64	TXByteCnt_B;
	u64	RXByteCnt_A;
	u64	RXByteCnt_B;
	u8	DoubleComfirm;
	u8	TrafficLoad;
#endif

	s32	OFDM_Pkt_Cnt;
	u8	RSSI_Select;
	u8 	DIG_Dynamic_MIN ;

	// Add for Reading Initial Data Rate SEL Register 0x484 during watchdog. Using for fill tx desc. 2011.3.21 by Thomas
	u8	INIDATA_RATE[32];
};


/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/
/*------------------------Export Marco Definition---------------------------*/
//#define DM_MultiSTA_InitGainChangeNotify(Event) {DM_DigTable.CurMultiSTAConnectState = Event;}


//============================================================
// function prototype
//============================================================
void rtl8192c_init_dm_priv(IN PADAPTER Adapter);
void rtl8192c_deinit_dm_priv(IN PADAPTER Adapter);
void rtl8192c_InitHalDm(IN PADAPTER Adapter);
void rtl8192c_HalDmWatchDog(IN PADAPTER Adapter);

VOID rtl8192c_dm_CheckTXPowerTracking(IN PADAPTER Adapter);

void rtl8192c_dm_RF_Saving(IN PADAPTER pAdapter, IN u8 bForceInNormal);

#ifdef CONFIG_BT_COEXIST
void rtl8192c_set_dm_bt_coexist(_adapter *padapter, u8 bStart);
void rtl8192c_issue_delete_ba(_adapter *padapter, u8 dir);
#endif

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
void SwAntDivRSSICheck8192C(_adapter *padapter ,u32 RxPWDBAll); 
void SwAntDivRestAfterLink8192C(IN	PADAPTER Adapter);
#endif
#ifdef CONFIG_ANTENNA_DIVERSITY
void	SwAntDivCompare8192C(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
u8 SwAntDivBeforeLink8192C(IN PADAPTER Adapter);
#endif

#endif	//__HAL8190PCIDM_H__

