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

/*Image2HeaderVersion: 2.25*/
#if (RTL8822B_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8822B_H
#define __INC_MP_RF_HW_IMG_8822B_H


/******************************************************************************
*                           RadioA.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_RadioA(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_RadioA(void);

/******************************************************************************
*                           RadioB.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_RadioB(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_RadioB(void);

/******************************************************************************
*                           TxPowerTrack.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack(void);

/******************************************************************************
*                           TxPowerTrack_type0.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type0(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type0(void);

/******************************************************************************
*                           TxPowerTrack_type1.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type1(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type1(void);

/******************************************************************************
*                           TxPowerTrack_type10.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type10(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type10(void);

/******************************************************************************
*                           TxPowerTrack_type2.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type2(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type2(void);

/******************************************************************************
*                           TxPowerTrack_Type3_Type5.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_Type3_Type5(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_Type3_Type5(void);

/******************************************************************************
*                           TxPowerTrack_type4.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type4(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type4(void);

/******************************************************************************
*                           TxPowerTrack_type6.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type6(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type6(void);

/******************************************************************************
*                           TxPowerTrack_type7.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type7(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type7(void);

/******************************************************************************
*                           TxPowerTrack_Type8.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_Type8(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_Type8(void);

/******************************************************************************
*                           TxPowerTrack_type9.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TxPowerTrack_type9(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TxPowerTrack_type9(void);

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TXPWR_LMT(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TXPWR_LMT(void);

/******************************************************************************
*                           TXPWR_LMT_type5.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8822B_TXPWR_LMT_type5(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8822B_TXPWR_LMT_type5(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

