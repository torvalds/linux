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

#define SET_TX_DESC_ANTSEL_A_88E(__ptxdesc, __value)			\
	le32p_replace_bits((__le32 *)(__ptxdesc + 8), __value, BIT(24))
#define SET_TX_DESC_ANTSEL_B_88E(__ptxdesc, __value)			\
	le32p_replace_bits((__le32 *)(__ptxdesc + 8), __value, BIT(25))
#define SET_TX_DESC_ANTSEL_C_88E(__ptxdesc, __value)			\
	le32p_replace_bits((__le32 *)(__ptxdesc + 28), __value, BIT(29))

void ODM_AntennaDiversityInit_88E(struct odm_dm_struct *pDM_Odm);

void ODM_AntennaDiversity_88E(struct odm_dm_struct *pDM_Odm);

void ODM_SetTxAntByTxInfo_88E(struct odm_dm_struct *pDM_Odm, u8 *pDesc,
			      u8 macId);

void ODM_UpdateRxIdleAnt_88E(struct odm_dm_struct *pDM_Odm, u8 Ant);

void ODM_AntselStatistics_88E(struct odm_dm_struct *pDM_Odm, u8	antsel_tr_mux,
			      u32 MacId, u8 RxPWDBAll);

void odm_FastAntTraining(struct odm_dm_struct *pDM_Odm);

#endif
