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
#ifndef __INC_MP_BB_HW_IMG_8812A_H
#define __INC_MP_BB_HW_IMG_8812A_H


/******************************************************************************
*                           AGC_TAB.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_AGC_TAB( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8812A_AGC_TAB(void);

/******************************************************************************
*                           AGC_TAB_DIFF.TXT
******************************************************************************/

extern u4Byte Array_MP_8812A_AGC_TAB_DIFF_LB[60];
extern u4Byte Array_MP_8812A_AGC_TAB_DIFF_HB[60];
void
ODM_ReadAndConfig_MP_8812A_AGC_TAB_DIFF(
     IN   PDM_ODM_T    pDM_Odm,
 	 IN   u4Byte  	   Array[],
 	 IN   u4Byte  	   ArrayLen 
);
u4Byte ODM_GetVersion_MP_8812A_AGC_TAB_DIFF(void);

/******************************************************************************
*                           PHY_REG.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_PHY_REG( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8812A_PHY_REG(void);

/******************************************************************************
*                           PHY_REG_MP.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_MP( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8812A_PHY_REG_MP(void);

/******************************************************************************
*                           PHY_REG_PG.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_PG( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8812A_PHY_REG_PG(void);

/******************************************************************************
*                           PHY_REG_PG_ASUS.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_PG_ASUS( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8812A_PHY_REG_PG_ASUS(void);

/******************************************************************************
*                           PHY_REG_PG_NEC.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_PHY_REG_PG_NEC( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8812A_PHY_REG_PG_NEC(void);

#endif
#endif // end of HWIMG_SUPPORT

