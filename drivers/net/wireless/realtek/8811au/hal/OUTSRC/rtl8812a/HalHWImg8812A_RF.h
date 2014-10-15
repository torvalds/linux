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
#ifndef __INC_MP_RF_HW_IMG_8812A_H
#define __INC_MP_RF_HW_IMG_8812A_H

//static BOOLEAN CheckPositive(PDM_ODM_T pDM_Odm, const u4Byte Condition1, const u4Byte Condition2);
//static BOOLEAN CheckNegative(PDM_ODM_T pDM_Odm, const u4Byte Condition1, const u4Byte Condition2);

/******************************************************************************
*                           RadioA.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_RadioA( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_RadioA(VOID);

/******************************************************************************
*                           RadioB.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_RadioB( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_RadioB(VOID);

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_AP( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_TxPowerTrack_AP(VOID);

/******************************************************************************
*                           TxPowerTrack_PCIE.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_PCIE( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_TxPowerTrack_PCIE(VOID);

/******************************************************************************
*                           TxPowerTrack_RFE3.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_RFE3( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_TxPowerTrack_RFE3(VOID);

/******************************************************************************
*                           TxPowerTrack_RFE4.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_RFE4( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_TxPowerTrack_RFE4(VOID);

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_TxPowerTrack_USB( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_TxPowerTrack_USB(VOID);

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8812A_TXPWR_LMT( // TC: Test Chip, MP: MP Chip
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte
ODM_GetVersion_MP_8812A_TXPWR_LMT(VOID);

#endif
#endif // end of HWIMG_SUPPORT

