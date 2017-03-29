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

//============================================================
// include files
//============================================================

#include "odm_precomp.h"

//======================================================
// when antenna test utility is on or some testing
// need to disable antenna diversity
// call this function to disable all ODM related mechanisms
// which will switch antenna.
//======================================================
void ODM_StopAntennaSwitchDm(PDM_ODM_T pDM_Odm)
{
	// disable ODM antenna diversity
	pDM_Odm->SupportAbility &= ~ODM_BB_ANT_DIV;
	ODM_RT_TRACE(
		pDM_Odm,
		ODM_COMP_ANT_DIV,
		ODM_DBG_LOUD,
		("STOP Antenna Diversity\n")
	);
}

void ODM_SetAntConfig(PDM_ODM_T pDM_Odm, u8 antSetting)// 0=A, 1=B, 2=C, ....
{
	if (antSetting == 0) // ant A
		PHY_SetBBReg(pDM_Odm->Adapter, 0x948, bMaskDWord, 0x00000000);
	else if (antSetting == 1)
		PHY_SetBBReg(pDM_Odm->Adapter, 0x948, bMaskDWord, 0x00000280);
}

//======================================================


void ODM_SwAntDivRestAfterLink(PDM_ODM_T pDM_Odm)
{
	pSWAT_T pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	pFAT_T pDM_FatTable = &pDM_Odm->DM_FatTable;
	u32 i;

	pDM_Odm->RSSI_test = false;
	pDM_SWAT_Table->try_flag = 0xff;
	pDM_SWAT_Table->RSSI_Trying = 0;
	pDM_SWAT_Table->Double_chk_flag = 0;

	pDM_FatTable->RxIdleAnt = MAIN_ANT;

	for (i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++) {
		pDM_FatTable->MainAnt_Sum[i] = 0;
		pDM_FatTable->AuxAnt_Sum[i] = 0;
		pDM_FatTable->MainAnt_Cnt[i] = 0;
		pDM_FatTable->AuxAnt_Cnt[i] = 0;
	}
}
