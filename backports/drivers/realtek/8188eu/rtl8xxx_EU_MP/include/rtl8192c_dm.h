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

//============================================================
// function prototype
//============================================================
#define DYNAMIC_FUNC_BT BIT(0)

enum{
	UP_LINK,
	DOWN_LINK,	
};
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

//============================================================
// structure and define
//============================================================

//###### duplicate code,will move to ODM #########
#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM			9
#define HP_THERMAL_NUM		8
//###### duplicate code,will move to ODM #########
struct 	dm_priv	
{
	u8	DM_Type;
	u8	DMFlag;
	u8	InitDMFlag;
	u32	InitODMFlag;

	//* Upper and Lower Signal threshold for Rate Adaptive*/
	int	UndecoratedSmoothedPWDB;
	int	UndecoratedSmoothedCCK;
	int	EntryMinUndecoratedSmoothedPWDB;
	int	EntryMaxUndecoratedSmoothedPWDB;
	int	MinUndecoratedPWDBForDM;
	int	LastMinUndecoratedPWDBForDM;

//###### duplicate code,will move to ODM #########
/*
	//for DIG
	u8	bDMInitialGainEnable;
	u8	binitialized; // for dm_initial_gain_Multi_STA use.
	DIG_T	DM_DigTable;

	PS_T	DM_PSTable;

	FALSE_ALARM_STATISTICS FalseAlmCnt;
	
	//for rate adaptive, in fact,  88c/92c fw will handle this
	u8 bUseRAMask;
	RATE_ADAPTIVE RateAdaptive;
*/
	//for High Power
	u8 bDynamicTxPowerEnable;
	u8 LastDTPLvl;
	u8 DynamicTxHighPowerLvl;//Add by Jacken Tx Power Control for Near/Far Range 2008/03/06
		
	//for tx power tracking
	u8	bTXPowerTracking;
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

	u8	CCK_index;
	u8	OFDM_index[2];

	u8	bDoneTxpower;
	u8	CCK_index_HP;
	u8	OFDM_index_HP[2];
	u8	ThermalValue_HP[HP_THERMAL_NUM];
	u8	ThermalValue_HP_index;

	//for TxPwrTracking
	s32	RegE94;
	s32     RegE9C;
	s32	RegEB4;
	s32	RegEBC;

	u32	TXPowerTrackingCallbackCnt;	//cosa add for debug

	u32	prv_traffic_idx; // edca turbo

/*
	// for dm_RF_Saving
	u8	initialize;
	u32	rf_saving_Reg874;
	u32	rf_saving_RegC70;
	u32	rf_saving_Reg85C;
	u32	rf_saving_RegA74;
*/
	//for Antenna diversity
#ifdef CONFIG_ANTENNA_DIVERSITY
//	SWAT_T DM_SWAT_Table;
#endif	
#ifdef CONFIG_SW_ANTENNA_DIVERSITY
//	_timer SwAntennaSwitchTimer;
/*	
	u64	lastTxOkCnt;
	u64	lastRxOkCnt;
	u64	TXByteCnt_A;
	u64	TXByteCnt_B;
	u64	RXByteCnt_A;
	u64	RXByteCnt_B;
	u8	DoubleComfirm;
	u8	TrafficLoad;
*/
#endif

	s32	OFDM_Pkt_Cnt;
	u8	RSSI_Select;
//	u8 	DIG_Dynamic_MIN ;
//###### duplicate code,will move to ODM #########
	// Add for Reading Initial Data Rate SEL Register 0x484 during watchdog. Using for fill tx desc. 2011.3.21 by Thomas
	u8	INIDATA_RATE[32];
};


//============================================================
// function prototype
//============================================================
#ifdef CONFIG_BT_COEXIST
void rtl8192c_set_dm_bt_coexist(_adapter *padapter, u8 bStart);
void rtl8192c_issue_delete_ba(_adapter *padapter, u8 dir);
#endif

void rtl8192c_init_dm_priv(IN PADAPTER Adapter);
void rtl8192c_deinit_dm_priv(IN PADAPTER Adapter);

void rtl8192c_InitHalDm(	IN	PADAPTER	Adapter);
void rtl8192c_HalDmWatchDog(IN PADAPTER Adapter);

#endif	//__HAL8190PCIDM_H__

