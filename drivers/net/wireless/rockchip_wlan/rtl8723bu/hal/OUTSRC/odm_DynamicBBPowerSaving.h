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
 
#ifndef	__ODMDYNAMICBBPOWERSAVING_H__
#define    __ODMDYNAMICBBPOWERSAVING_H__

typedef struct _Dynamic_Power_Saving_
{
	u1Byte		PreCCAState;
	u1Byte		CurCCAState;

	u1Byte		PreRFState;
	u1Byte		CurRFState;

	int		    Rssi_val_min;
	
	u1Byte		initialize;
	u4Byte		Reg874,RegC70,Reg85C,RegA74;
	
}PS_T,*pPS_T;

#define dm_RF_Saving	ODM_RF_Saving

void ODM_RF_Saving(
	IN		PVOID					pDM_VOID,
	IN	u1Byte		bForceInNormal 
	);

VOID 
odm_DynamicBBPowerSavingInit(
	IN		PVOID					pDM_VOID
	);

VOID 
odm_DynamicBBPowerSaving(
	IN		PVOID					pDM_VOID
	);

VOID
odm_1R_CCA(
	IN		PVOID					pDM_VOID
	);

#endif