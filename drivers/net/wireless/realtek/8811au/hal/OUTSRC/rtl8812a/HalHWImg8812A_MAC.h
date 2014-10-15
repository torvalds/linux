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

#if (RTL8812A_SUPPORT == 1)
#ifndef __INC_MP_MAC_HW_IMG_8812A_H
#define __INC_MP_MAC_HW_IMG_8812A_H

//static BOOLEAN CheckPositive(PDM_ODM_T pDM_Odm, const u4Byte Condition1, const u4Byte Condition2);
//static BOOLEAN CheckNegative(PDM_ODM_T pDM_Odm, const u4Byte Condition1, const u4Byte Condition2);

/******************************************************************************
*                           MAC_REG.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_MAC_REG( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_MAC_REG(VOID);

#endif
#endif // end of HWIMG_SUPPORT

