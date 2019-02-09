/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTL8188E_DM_H__
#define __RTL8188E_DM_H__
enum{
	UP_LINK,
	DOWN_LINK,
};
/*  duplicate code,will move to ODM ######### */
#define IQK_MAC_REG_NUM		4
#define IQK_ADDA_REG_NUM		16
#define IQK_BB_REG_NUM			9
#define HP_THERMAL_NUM		8
/*  duplicate code,will move to ODM ######### */
struct	dm_priv {
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

	/* for High Power */
	u8 bDynamicTxPowerEnable;
	u8 LastDTPLvl;
	u8 DynamicTxHighPowerLvl;/* Tx Power Control for Near/Far Range */
	u8	PowerIndex_backup[6];
};

void rtl8188e_InitHalDm(struct adapter *adapt);

void AntDivCompare8188E(struct adapter *adapt, struct wlan_bssid_ex *dst,
			struct wlan_bssid_ex *src);

#endif
