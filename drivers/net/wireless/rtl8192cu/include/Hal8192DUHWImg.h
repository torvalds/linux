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
#ifndef __INC_HAL8192DU_FW_IMG_H
#define __INC_HAL8192DU_FW_IMG_H

#include <basic_types.h>

/*Created on  2011/ 6/22,  2:27*/

#define ImgArrayLength 29642
extern u8 Rtl8192DUFwImgArray[ImgArrayLength];
#define MainArrayLength 1
extern u8 Rtl8192DUFwMainArray[MainArrayLength];
#define DataArrayLength 1
extern u8 Rtl8192DUFwDataArray[DataArrayLength];
#define PHY_REG_2TArrayLength 380
extern u32 Rtl8192DUPHY_REG_2TArray[PHY_REG_2TArrayLength];
#define PHY_REG_1TArrayLength 1
extern u32 Rtl8192DUPHY_REG_1TArray[PHY_REG_1TArrayLength];
#define PHY_REG_Array_PGLength 624
extern u32 Rtl8192DUPHY_REG_Array_PG[PHY_REG_Array_PGLength];
#define PHY_REG_Array_MPLength 10
extern u32 Rtl8192DUPHY_REG_Array_MP[PHY_REG_Array_MPLength];
#define RadioA_2TArrayLength 378
extern u32 Rtl8192DURadioA_2TArray[RadioA_2TArrayLength];
#define RadioB_2TArrayLength 384
extern u32 Rtl8192DURadioB_2TArray[RadioB_2TArrayLength];
#define RadioA_1TArrayLength 1
extern u32 Rtl8192DURadioA_1TArray[RadioA_1TArrayLength];
#define RadioB_1TArrayLength 1
extern u32 Rtl8192DURadioB_1TArray[RadioB_1TArrayLength];
#define RadioA_2T_intPAArrayLength 378
extern u32 Rtl8192DURadioA_2T_intPAArray[RadioA_2T_intPAArrayLength];
#define RadioB_2T_intPAArrayLength 384
extern u32 Rtl8192DURadioB_2T_intPAArray[RadioB_2T_intPAArrayLength];
#define MAC_2TArrayLength 160
extern u32 Rtl8192DUMAC_2TArray[MAC_2TArrayLength];
#define AGCTAB_ArrayLength 386
extern u32 Rtl8192DUAGCTAB_Array[AGCTAB_ArrayLength];
#define AGCTAB_5GArrayLength 194
extern u32 Rtl8192DUAGCTAB_5GArray[AGCTAB_5GArrayLength];
#define AGCTAB_2GArrayLength 194
extern u32 Rtl8192DUAGCTAB_2GArray[AGCTAB_2GArrayLength];
#define AGCTAB_2TArrayLength 1
extern u32 Rtl8192DUAGCTAB_2TArray[AGCTAB_2TArrayLength];
#define AGCTAB_1TArrayLength 1
extern u32 Rtl8192DUAGCTAB_1TArray[AGCTAB_1TArrayLength];

#endif //__INC_HAL8192CU_FW_IMG_H
