/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
