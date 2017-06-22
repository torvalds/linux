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

/*Image2HeaderVersion: 2.26*/
#if (RTL8188F_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8188F_H
#define __INC_MP_RF_HW_IMG_8188F_H


/******************************************************************************
*                           RadioA.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188F_RadioA(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188F_RadioA(void);

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188F_TxPowerTrack_AP(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188F_TxPowerTrack_AP(void);

/******************************************************************************
*                           TxPowerTrack_SDIO.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188F_TxPowerTrack_SDIO(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188F_TxPowerTrack_SDIO(void);

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188F_TxPowerTrack_USB(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188F_TxPowerTrack_USB(void);

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188F_TXPWR_LMT(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188F_TXPWR_LMT(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

