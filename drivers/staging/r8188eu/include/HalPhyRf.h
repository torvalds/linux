/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __HAL_PHY_RF_H__
#define __HAL_PHY_RF_H__

#define ODM_TARGET_CHNL_NUM_2G_5G	59

void ODM_ResetIQKResult(struct odm_dm_struct *pDM_Odm);

u8 ODM_GetRightChnlPlaceforIQK(u8 chnl);

#endif	/*  #ifndef __HAL_PHY_RF_H__ */
