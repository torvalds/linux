/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __INC_BB_8188E_HW_IMG_H
#define __INC_BB_8188E_HW_IMG_H

/* static bool CheckCondition(const u32 Condition, const u32 Hex); */

/******************************************************************************
*                           AGC_TAB_1T.TXT
******************************************************************************/

int ODM_ReadAndConfig_AGC_TAB_1T_8188E(struct odm_dm_struct *odm);

/******************************************************************************
*                           PHY_REG_1T.TXT
******************************************************************************/

int ODM_ReadAndConfig_PHY_REG_1T_8188E(struct odm_dm_struct *odm);

/******************************************************************************
*                           PHY_REG_PG.TXT
******************************************************************************/

void ODM_ReadAndConfig_PHY_REG_PG_8188E(struct odm_dm_struct *dm_odm);

#endif
