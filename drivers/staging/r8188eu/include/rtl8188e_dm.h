/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

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
	u32	InitODMFlag;

	/*  Upper and Lower Signal threshold for Rate Adaptive*/
	int	EntryMinUndecoratedSmoothedPWDB;
	int	EntryMaxUndecoratedSmoothedPWDB;
	int	MinUndecoratedPWDBForDM;
};

void rtl8188e_init_dm_priv(struct adapter *adapt);
void rtl8188e_InitHalDm(struct adapter *adapt);
void rtl8188e_HalDmWatchDog(struct adapter *adapt);

void AntDivCompare8188E(struct adapter *adapt, struct wlan_bssid_ex *dst,
			struct wlan_bssid_ex *src);
u8 AntDivBeforeLink8188E(struct adapter *adapt);

#endif
