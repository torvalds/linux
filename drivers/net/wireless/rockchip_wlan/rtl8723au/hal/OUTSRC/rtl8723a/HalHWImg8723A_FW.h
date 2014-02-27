/****************************************************************************** 
* 
* Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved. 
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

#if (RTL8723A_SUPPORT == 1)
#ifndef __INC_FW_8723A_HW_IMG_H
#define __INC_FW_8723A_HW_IMG_H


/******************************************************************************
*                           rtl8723fw_B.TXT
******************************************************************************/

void
ODM_ReadFirmware_8723A_rtl8723fw_B(
     IN   PDM_ODM_T    pDM_Odm,
     OUT  u1Byte       *pFirmware,
     OUT  u4Byte       *pFirmwareSize
);

#endif
#endif // end of HWIMG_SUPPORT

