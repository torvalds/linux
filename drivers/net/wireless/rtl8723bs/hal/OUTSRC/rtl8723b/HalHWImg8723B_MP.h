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

#if (RTL8723B_SUPPORT==1)
#ifndef __INC_MP_HW_IMG_8723B_H
#define __INC_MP_HW_IMG_8723B_H

#ifdef CONFIG_MP_INCLUDED
#define Rtl8723BMPImgArrayLength 18396
#define Rtl8723BFwBTImgArrayLength 19752

#define Rtl8723B_PHYREG_Array_MPLength 4

extern u8 Rtl8723BFwBTImgArray[Rtl8723BFwBTImgArrayLength] ;
extern const u8 Rtl8723BFwMPImgArray[Rtl8723BMPImgArrayLength];
extern const u32 Rtl8723B_PHYREG_Array_MP[Rtl8723B_PHYREG_Array_MPLength];
#endif //CONFIG_MP_INCLUDED

#endif
#endif
