// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#include "odm_precomp.h"

void ODM_InitDebugSetting(struct odm_dm_struct *pDM_Odm)
{
	pDM_Odm->DebugLevel = ODM_DBG_TRACE;

	pDM_Odm->DebugComponents = 0;
}

u32 GlobalDebugLevel;
