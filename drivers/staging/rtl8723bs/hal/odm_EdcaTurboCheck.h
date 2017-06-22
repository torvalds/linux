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

#ifndef __ODMEDCATURBOCHECK_H__
#define __ODMEDCATURBOCHECK_H__

typedef struct _EDCA_TURBO_ {
	bool bCurrentTurboEDCA;
	bool bIsCurRDLState;

	u32 prv_traffic_idx; /*  edca turbo */
} EDCA_T, *pEDCA_T;

void odm_EdcaTurboCheck(void *pDM_VOID);
void ODM_EdcaTurboInit(void *pDM_VOID);

void odm_EdcaTurboCheckCE(void *pDM_VOID);

#endif
