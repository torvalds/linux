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
*
******************************************************************************/

#ifndef __INC_BB_8723A_HW_IMG_H
#define __INC_BB_8723A_HW_IMG_H

/******************************************************************************
*                           AGC_TAB_1T.TXT
******************************************************************************/

void ODM_ReadAndConfig_AGC_TAB_1T_8723A(struct dm_odm_t *pDM_Odm);

/******************************************************************************
*                           PHY_REG_1T.TXT
******************************************************************************/

void ODM_ReadAndConfig_PHY_REG_1T_8723A(struct dm_odm_t *pDM_Odm);

/******************************************************************************
*                           PHY_REG_MP.TXT
******************************************************************************/

void ODM_ReadAndConfig_PHY_REG_MP_8723A(struct dm_odm_t *pDM_Odm);

#endif /*  end of HWIMG_SUPPORT */
