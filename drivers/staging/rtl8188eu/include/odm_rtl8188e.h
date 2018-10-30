/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef	__ODM_RTL8188E_H__
#define __ODM_RTL8188E_H__

#define	MAIN_ANT	0
#define	AUX_ANT	1
#define	MAIN_ANT_CG_TRX	1
#define	AUX_ANT_CG_TRX	0
#define	MAIN_ANT_CGCS_RX	0
#define	AUX_ANT_CGCS_RX	1

void ODM_DIG_LowerBound_88E(struct odm_dm_struct *pDM_Odm);

void rtl88eu_dm_antenna_div_init(struct odm_dm_struct *dm_odm);

void rtl88eu_dm_antenna_diversity(struct odm_dm_struct *dm_odm);

void rtl88eu_dm_set_tx_ant_by_tx_info(struct odm_dm_struct *dm_odm, u8 *desc,
				      u8 mac_id);

void rtl88eu_dm_update_rx_idle_ant(struct odm_dm_struct *dm_odm, u8 ant);

void rtl88eu_dm_ant_sel_statistics(struct odm_dm_struct *dm_odm, u8 antsel_tr_mux,
				   u32 mac_id, u8 rx_pwdb_all);

void odm_FastAntTraining(struct odm_dm_struct *pDM_Odm);

void odm_FastAntTrainingCallback(struct odm_dm_struct *pDM_Odm);

void odm_FastAntTrainingWorkItemCallback(struct odm_dm_struct *pDM_Odm);

bool ODM_DynamicPrimaryCCA_DupRTS(struct odm_dm_struct *pDM_Odm);

#endif
