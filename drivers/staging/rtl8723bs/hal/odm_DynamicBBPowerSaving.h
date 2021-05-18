/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#ifndef	__ODMDYNAMICBBPOWERSAVING_H__
#define    __ODMDYNAMICBBPOWERSAVING_H__

struct ps_t { /* _Dynamic_Power_Saving_ */
	u8 PreCCAState;
	u8 CurCCAState;

	u8 PreRFState;
	u8 CurRFState;

	int Rssi_val_min;

	u8 initialize;
	u32 Reg874, RegC70, Reg85C, RegA74;

};

#define dm_RF_Saving ODM_RF_Saving

void ODM_RF_Saving(void *pDM_VOID, u8 bForceInNormal);

void odm_DynamicBBPowerSavingInit(void *pDM_VOID);

#endif
