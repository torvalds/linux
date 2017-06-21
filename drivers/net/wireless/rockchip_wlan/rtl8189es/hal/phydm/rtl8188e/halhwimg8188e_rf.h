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

/*Image2HeaderVersion: 2.18*/
#if (RTL8188E_SUPPORT == 1)
#ifndef __INC_MP_RF_HW_IMG_8188E_H
#define __INC_MP_RF_HW_IMG_8188E_H


/******************************************************************************
*                           RadioA.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_RadioA(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_RadioA(void);

/******************************************************************************
*                           TxPowerTrack_AP.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_AP(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TxPowerTrack_AP(void);

/******************************************************************************
*                           TxPowerTrack_PCIE.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_PCIE(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TxPowerTrack_PCIE(void);

/******************************************************************************
*                           TxPowerTrack_PCIE_ICUT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_PCIE_ICUT(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TxPowerTrack_PCIE_ICUT(void);

/******************************************************************************
*                           TxPowerTrack_SDIO.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_SDIO(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TxPowerTrack_SDIO(void);

/******************************************************************************
*                           TxPowerTrack_SDIO_ICUT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_SDIO_ICUT(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TxPowerTrack_SDIO_ICUT(void);

/******************************************************************************
*                           TxPowerTrack_USB.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_USB(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TxPowerTrack_USB(void);

/******************************************************************************
*                           TxPowerTrack_USB_ICUT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TxPowerTrack_USB_ICUT(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TxPowerTrack_USB_ICUT(void);

/******************************************************************************
*                           TXPWR_LMT.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TXPWR_LMT(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TXPWR_LMT(void);

/******************************************************************************
*                           TXPWR_LMT_88EE_M2_for_MSI.TXT
******************************************************************************/

void
ODM_ReadAndConfig_MP_8188E_TXPWR_LMT_88EE_M2_for_MSI(/* TC: Test Chip, MP: MP Chip*/
	IN   PDM_ODM_T  pDM_Odm
);
u4Byte ODM_GetVersion_MP_8188E_TXPWR_LMT_88EE_M2_for_MSI(void);

#endif
#endif /* end of HWIMG_SUPPORT*/

