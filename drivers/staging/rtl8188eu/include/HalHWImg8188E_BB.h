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
* You should have received a copy of the GNU General Public License along with
* this program; if not, write to the Free Software Foundation, Inc.,
* 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
*
*
******************************************************************************/

#ifndef __INC_BB_8188E_HW_IMG_H
#define __INC_BB_8188E_HW_IMG_H

/* static bool CheckCondition(const u32 Condition, const u32 Hex); */

/******************************************************************************
*                           AGC_TAB_1T.TXT
******************************************************************************/

enum HAL_STATUS ODM_ReadAndConfig_AGC_TAB_1T_8188E(struct odm_dm_struct *odm);

/******************************************************************************
*                           PHY_REG_1T.TXT
******************************************************************************/

enum HAL_STATUS ODM_ReadAndConfig_PHY_REG_1T_8188E(struct odm_dm_struct *odm);

/******************************************************************************
*                           PHY_REG_PG.TXT
******************************************************************************/

void ODM_ReadAndConfig_PHY_REG_PG_8188E(struct odm_dm_struct *dm_odm);

#endif
