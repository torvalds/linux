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

#include "odm_precomp.h"

void odm_PathDiversityInit(void *pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

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
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	if (!(pDM_Odm->SupportAbility & ODM_BB_PATH_DIV))
		ODM_RT_TRACE(
			pDM_Odm,
			ODM_COMP_PATH_DIV,
			ODM_DBG_LOUD,
			("Return: Not Support PathDiv\n")
		);
}
