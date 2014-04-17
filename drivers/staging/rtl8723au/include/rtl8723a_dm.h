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
#ifndef __RTL8723A_DM_H__
#define __RTL8723A_DM_H__
/*  */
/*  Description: */
/*  */
/*  This file is for 8723A dynamic mechanism only */
/*  */
/*  */
/*  */
#define DYNAMIC_FUNC_BT BIT(0)

enum{
	UP_LINK,
	DOWN_LINK,
};
/*  */
/*  structure and define */
/*  */

/*  duplicate code,will move to ODM ######### */
#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM			9
#define HP_THERMAL_NUM		8
/*  duplicate code,will move to ODM ######### */
struct dm_priv
{
	u8	DM_Type;
	u8	DMFlag;
	u8	InitDMFlag;
	u32	InitODMFlag;

	/*  Upper and Lower Signal threshold for Rate Adaptive*/
	int	UndecoratedSmoothedPWDB;
	int	UndecoratedSmoothedCCK;
	int	EntryMinUndecoratedSmoothedPWDB;
	int	EntryMaxUndecoratedSmoothedPWDB;
	int	MinUndecoratedPWDBForDM;
	int	LastMinUndecoratedPWDBForDM;

	s32	UndecoratedSmoothedBeacon;
	#ifdef CONFIG_8723AU_BT_COEXIST
	s32 BT_EntryMinUndecoratedSmoothedPWDB;
	s32 BT_EntryMaxUndecoratedSmoothedPWDB;
	#endif

	/* for High Power */
	u8 bDynamicTxPowerEnable;
	u8 LastDTPLvl;
	u8 DynamicTxHighPowerLvl;/* Add by Jacken Tx Power Control for Near/Far Range 2008/03/06 */

	/* for tx power tracking */
	u8	bTXPowerTracking;
	u8	TXPowercount;
	u8	bTXPowerTrackingInit;
	u8	TxPowerTrackControl;	/* for mp mode, turn off txpwrtracking as default */
	u8	TM_Trigger;

	u8	ThermalMeter[2];				/*  ThermalMeter, index 0 for RFIC0, and 1 for RFIC1 */
	u8	ThermalValue;
	u8	ThermalValue_LCK;
	u8	ThermalValue_IQK;
	u8	ThermalValue_DPK;

	u8	bRfPiEnable;

	/* for APK */
	u32	APKoutput[2][2];	/* path A/B; output1_1a/output1_2a */
	u8	bAPKdone;
	u8	bAPKThermalMeterIgnore;
	u8	bDPdone;
	u8	bDPPathAOK;
	u8	bDPPathBOK;

	/* for IQK */
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

	/* for TxPwrTracking */
	s32	RegE94;
	s32     RegE9C;
	s32	RegEB4;
	s32	RegEBC;

	u32	TXPowerTrackingCallbackCnt;	/* cosa add for debug */

	u32	prv_traffic_idx; /*  edca turbo */

	s32	OFDM_Pkt_Cnt;
	u8	RSSI_Select;
/*	u8	DIG_Dynamic_MIN ; */
/*  duplicate code,will move to ODM ######### */
	/*  Add for Reading Initial Data Rate SEL Register 0x484 during watchdog. Using for fill tx desc. 2011.3.21 by Thomas */
	u8	INIDATA_RATE[32];
};


/*  */
/*  function prototype */
/*  */

void rtl8723a_init_dm_priv(struct rtw_adapter *padapter);
void rtl8723a_deinit_dm_priv(struct rtw_adapter *padapter);

void rtl8723a_InitHalDm(struct rtw_adapter *padapter);
void rtl8723a_HalDmWatchDog(struct rtw_adapter *padapter);

#endif
