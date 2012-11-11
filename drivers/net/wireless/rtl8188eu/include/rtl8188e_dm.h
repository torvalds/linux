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
#ifndef __RTL8188E_DM_H__
#define __RTL8188E_DM_H__
enum{
	UP_LINK,
	DOWN_LINK,	
};
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
	u8	PowerIndex_backup[6];
#if 0		
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
#endif
	// Add for Reading Initial Data Rate SEL Register 0x484 during watchdog. Using for fill tx desc. 2011.3.21 by Thomas
	//u8	INIDATA_RATE[32];
};


void rtl8188e_init_dm_priv(IN PADAPTER Adapter);
void rtl8188e_deinit_dm_priv(IN PADAPTER Adapter);
void rtl8188e_InitHalDm(IN PADAPTER Adapter);
void rtl8188e_HalDmWatchDog(IN PADAPTER Adapter);

//VOID rtl8192c_dm_CheckTXPowerTracking(IN PADAPTER Adapter);

//void rtl8192c_dm_RF_Saving(IN PADAPTER pAdapter, IN u8 bForceInNormal);

#ifdef CONFIG_ANTENNA_DIVERSITY
void	AntDivCompare8188E(PADAPTER Adapter, WLAN_BSSID_EX *dst, WLAN_BSSID_EX *src);
u8 AntDivBeforeLink8188E(PADAPTER Adapter );
#endif
#endif

