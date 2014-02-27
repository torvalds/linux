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

#if (RTL8188E_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8188E_H
#define __INC_MP_RF_HW_IMG_8188E_H

//static BOOLEAN CheckCondition(const u4Byte Condition, const u4Byte Hex);

/******************************************************************************
*                           RadioA_1T.TXT
******************************************************************************/

HAL_STATUS
ODM_ReadAndConfig_MP_8188E_RadioA_1T( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);

/******************************************************************************
*                           RadioA_1T_ICUT.TXT
******************************************************************************/

HAL_STATUS
ODM_ReadAndConfig_MP_8188E_RadioA_1T_ICUT( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_AP( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);

/******************************************************************************
*                           TxPowerTrack_PCIE.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_PCIE( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_USB( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TXPWR_LMT( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);

#endif
#endif // end of HWIMG_SUPPORT

