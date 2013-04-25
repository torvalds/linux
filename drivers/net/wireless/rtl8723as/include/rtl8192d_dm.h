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
#ifndef	__RTL8192D_DM_H__
#define __RTL8192D_DM_H__
//============================================================
// Description:
//
// This file is for 92CE/92CU dynamic mechanism only
//
//
//============================================================
enum{
	UP_LINK,
	DOWN_LINK,	
};
/*------------------------Export global variable----------------------------*/
/*------------------------Export global variable----------------------------*/
/*------------------------Export Marco Definition---------------------------*/
//#define DM_MultiSTA_InitGainChangeNotify(Event) {DM_DigTable.CurMultiSTAConnectState = Event;}
//============================================================
// structure and define
//============================================================

//###### duplicate code,will move to ODM #########
#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM			10
#define IQK_BB_REG_NUM_92C	9
#define IQK_BB_REG_NUM_92D	10
#define IQK_BB_REG_NUM_test	6
#define index_mapping_NUM		13
#define Rx_index_mapping_NUM	15
#define AVG_THERMAL_NUM		8
#define IQK_Matrix_REG_NUM	8
#define IQK_Matrix_Settings_NUM	1+24+21
//###### duplicate code,will move to ODM #########
struct 	dm_priv	
{
	u8	DM_Type;
	u8	DMFlag;
	u8	InitDMFlag;
	u32	InitODMFlag;
	
	//* Upper and Lower Signal threshold for Rate Adaptive*/
	int	UndecoratedSmoothedPWDB;
	int	EntryMinUndecoratedSmoothedPWDB;
	int	EntryMaxUndecoratedSmoothedPWDB;
	int	MinUndecoratedPWDBForDM;
	int	LastMinUndecoratedPWDBForDM;
//###### duplicate code,will move to ODM #########
/*
	//for DIG
	u8	bDMInitialGainEnable;
	//u8	binitialized; // for dm_initial_gain_Multi_STA use.
	DIG_T	DM_DigTable;

	PS_T	DM_PSTable;

	FALSE_ALARM_STATISTICS	FalseAlmCnt;	
	
	//for rate adaptive, in fact,  88c/92c fw will handle this
	u8	bUseRAMask;
	RATE_ADAPTIVE	RateAdaptive;
*/
	
	//for High Power
	u8	bDynamicTxPowerEnable;
	u8	LastDTPLvl;
	u8	DynamicTxHighPowerLvl;//Add by Jacken Tx Power Control for Near/Far Range 2008/03/06
		
	//for tx power tracking
	u8	bTXPowerTracking;
	u8	TXPowercount;
	u8	bTXPowerTrackingInit;	
	u8	TxPowerTrackControl;	//for mp mode, turn off txpwrtracking as default
	u8	TM_Trigger;

	u8	ThermalMeter[2];	// ThermalMeter, index 0 for RFIC0, and 1 for RFIC1
	u8	ThermalValue;
	u8	ThermalValue_LCK;
	u8	ThermalValue_IQK;
	u8	ThermalValue_AVG[AVG_THERMAL_NUM];
	u8	ThermalValue_AVG_index;
	u8	ThermalValue_RxGain;
	u8	ThermalValue_Crystal;
	u8	Delta_IQK;
	u8	Delta_LCK;
	u8	bRfPiEnable;
	u8	bReloadtxpowerindex;
	u8	bDoneTxpower;

	//for APK
	u32	APKoutput[2][2];	//path A/B; output1_1a/output1_2a
	u8	bAPKdone;
	u8	bAPKThermalMeterIgnore;
	u32	RegA24;
	
	//for IQK
	u32	Reg874;
	u32	RegC08;
	u32	Reg88C;
	u8	Reg522;
	u8	Reg550;
	u8	Reg551;
	u32	Reg870;
	u32	ADDA_backup[IQK_ADDA_REG_NUM];
	u32	IQK_MAC_backup[IQK_MAC_REG_NUM];
	u32	IQK_BB_backup[IQK_BB_REG_NUM];

	u8	bCCKinCH14;
	
	char	CCK_index;
	//u8 Record_CCK_20Mindex;
	//u8 Record_CCK_40Mindex;
	char	OFDM_index[2];
	
	BOOLEAN	bDPKdone[2];

	u8	PowerIndex_backup[6];
	
	//for Antenna diversity
//#ifdef CONFIG_ANTENNA_DIVERSITY
	//SWAT_T DM_SWAT_Table;
//#endif
       //Neil Chen----2011--06--23-----
       //3 Path Diversity 
	BOOLEAN		bPathDiv_Enable;	//For 92D Non-interrupt Antenna Diversity by Neil ,add by wl.2011.07.19
	BOOLEAN		RSSI_test;
	s32			RSSI_sum_A;
	s32			RSSI_cnt_A;
	s32			RSSI_sum_B;
	s32			RSSI_cnt_B;
	struct sta_info	*RSSI_target;
	_timer		PathDivSwitchTimer;

	//for TxPwrTracking
	int	RegE94;
	int 	RegE9C;
	int	RegEB4;
	int	RegEBC;
#if MP_DRIVER == 1
	u8	RegC04_MP;
	u32	RegD04_MP;
#endif
	u32	TXPowerTrackingCallbackCnt;	//cosa add for debug

	u32	prv_traffic_idx; // edca turbo

	u32	RegRF3C[2];	//pathA / pathB
//###### duplicate code,will move to ODM #########
	// Add for Reading Initial Data Rate SEL Register 0x484 during watchdog. Using for fill tx desc. 2011.3.21 by Thomas
	u8	INIDATA_RATE[32];
};


//============================================================
// function prototype
//============================================================
void rtl8192d_init_dm_priv(IN PADAPTER Adapter);
void rtl8192d_deinit_dm_priv(IN PADAPTER Adapter);

void	rtl8192d_InitHalDm(IN PADAPTER Adapter);
void	rtl8192d_HalDmWatchDog(IN PADAPTER Adapter);

#endif	//__HAL8190PCIDM_H__

