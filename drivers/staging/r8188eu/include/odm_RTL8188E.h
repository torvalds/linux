/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef	__ODM_RTL8188E_H__
#define __ODM_RTL8188E_H__

#define	MAIN_ANT	0
#define	AUX_ANT	1
#define	MAIN_ANT_CG_TRX	1
#define	AUX_ANT_CG_TRX	0
#define	MAIN_ANT_CGCS_RX	0
#define	AUX_ANT_CGCS_RX	1

void ODM_DIG_LowerBound_88E(struct odm_dm_struct *pDM_Odm);

void ODM_AntennaDiversityInit_88E(struct odm_dm_struct *pDM_Odm);

void ODM_AntennaDiversity_88E(struct odm_dm_struct *pDM_Odm);

void ODM_SetTxAntByTxInfo_88E(struct odm_dm_struct *pDM_Odm, u8 *pDesc,
			      u8 macId);

void ODM_UpdateRxIdleAnt_88E(struct odm_dm_struct *pDM_Odm, u8 Ant);

void ODM_AntselStatistics_88E(struct odm_dm_struct *pDM_Odm, u8	antsel_tr_mux,
			      u32 MacId, u8 RxPWDBAll);

void odm_FastAntTraining(struct odm_dm_struct *pDM_Odm);

void odm_FastAntTrainingCallback(struct odm_dm_struct *pDM_Odm);

void odm_FastAntTrainingWorkItemCallback(struct odm_dm_struct *pDM_Odm);

void odm_PrimaryCCA_Init(struct odm_dm_struct *pDM_Odm);

#endif
