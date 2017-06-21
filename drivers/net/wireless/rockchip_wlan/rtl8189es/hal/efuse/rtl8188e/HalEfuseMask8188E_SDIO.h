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



/******************************************************************************
*                           MSDIO.TXT
******************************************************************************/


u2Byte
EFUSE_GetArrayLen_MP_8188E_MSDIO(VOID);

VOID
EFUSE_GetMaskArray_MP_8188E_MSDIO(
	IN 	OUT pu1Byte Array
);

BOOLEAN
EFUSE_IsAddressMasked_MP_8188E_MSDIO( // TC: Test Chip, MP: MP Chip
	IN   u2Byte  Offset
);


