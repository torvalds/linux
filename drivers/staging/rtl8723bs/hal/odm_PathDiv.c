// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "odm_precomp.h"

void odm_PathDiversityInit(void *pDM_VOID)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;

	if (!(pDM_Odm->SupportAbility & ODM_BB_PATH_DIV))
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_PATH_DIV,
			ODM_DBG_LOUD,
			("Return: Not Support PathDiv\n")
		);
}

void odm_PathDiversity(void *pDM_VOID)
{
	struct dm_odm_t *pDM_Odm = (struct dm_odm_t *)pDM_VOID;

	if (!(pDM_Odm->SupportAbility & ODM_BB_PATH_DIV))
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_PATH_DIV,
			ODM_DBG_LOUD,
			("Return: Not Support PathDiv\n")
		);
}
