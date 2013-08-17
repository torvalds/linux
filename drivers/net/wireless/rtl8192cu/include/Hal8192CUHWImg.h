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
#ifndef __INC_HAL8192CU_FW_IMG_H
#define __INC_HAL8192CU_FW_IMG_H

/*Created on  2011/12/14,  8:38*/

#ifdef CONFIG_BT_COEXISTENCE
#define TSMCImgArrayLength 16248 //v79 TSMC COMMON 2011-10-06
#else
#define TSMCImgArrayLength 16116 //v80 TSMC P2PPS 2011-12-14
#endif
extern u8 Rtl8192CUFwTSMCImgArray[TSMCImgArrayLength];

#ifdef CONFIG_BT_COEXISTENCE
#define UMCACutImgArrayLength 16248 //v79 UMC A Cut COMMON 2011-10-06
#else //#ifdef CONFIG_P2P
#define UMCACutImgArrayLength 16116 //v80 UMC A Cut P2PPS 2011-12-14
#endif
extern u8 Rtl8192CUFwUMCACutImgArray[UMCACutImgArrayLength];

#ifdef CONFIG_BT_COEXISTENCE
#define UMCBCutImgArrayLength 16254 //v79 UMC B Cut COMMON 2011-10-06
#else //#ifdef CONFIG_P2P
#define UMCBCutImgArrayLength 16096 //v80 UMC B Cut P2PPS 2011-12-14
#endif
extern u8 Rtl8192CUFwUMCBCutImgArray[UMCBCutImgArrayLength];

#define UMC8723ImgArrayLength 16288
extern u8 Rtl8192CUFwUMC8723ImgArray[UMC8723ImgArrayLength];
#define PHY_REG_2TArrayLength 374
extern u32  Rtl8192CUPHY_REG_2TArray[PHY_REG_2TArrayLength];
#define PHY_REG_1TArrayLength 374
extern u32 Rtl8192CUPHY_REG_1TArray[PHY_REG_1TArrayLength];
#define PHY_ChangeTo_1T1RArrayLength 1
extern u32 Rtl8192CUPHY_ChangeTo_1T1RArray[PHY_ChangeTo_1T1RArrayLength];
#define PHY_ChangeTo_1T2RArrayLength 1
extern u32 Rtl8192CUPHY_ChangeTo_1T2RArray[PHY_ChangeTo_1T2RArrayLength];
#define PHY_ChangeTo_2T2RArrayLength 1
extern u32 Rtl8192CUPHY_ChangeTo_2T2RArray[PHY_ChangeTo_2T2RArrayLength];
#define PHY_REG_Array_PGLength 336
extern u32 Rtl8192CUPHY_REG_Array_PG[PHY_REG_Array_PGLength];
#define PHY_REG_Array_PG_mCardLength 336
extern u32 Rtl8192CUPHY_REG_Array_PG_mCard[PHY_REG_Array_PG_mCardLength];
#define PHY_REG_Array_MPLength 4
extern u32 Rtl8192CUPHY_REG_Array_MP[PHY_REG_Array_MPLength];
#define PHY_REG_1T_HPArrayLength 378
extern u32 Rtl8192CUPHY_REG_1T_HPArray[PHY_REG_1T_HPArrayLength];
#define PHY_REG_1T_mCardArrayLength 374
extern u32 Rtl8192CUPHY_REG_1T_mCardArray[PHY_REG_1T_mCardArrayLength];
#define PHY_REG_2T_mCardArrayLength 374
extern u32 Rtl8192CUPHY_REG_2T_mCardArray[PHY_REG_2T_mCardArrayLength];
#define PHY_REG_Array_PG_HPLength 336
extern u32 Rtl8192CUPHY_REG_Array_PG_HP[PHY_REG_Array_PG_HPLength];
#define RadioA_2TArrayLength 282
extern u32 Rtl8192CURadioA_2TArray[RadioA_2TArrayLength];
#define RadioB_2TArrayLength 78
extern u32 Rtl8192CURadioB_2TArray[RadioB_2TArrayLength];
#define RadioA_1TArrayLength 282
extern u32 Rtl8192CURadioA_1TArray[RadioA_1TArrayLength];
#define RadioB_1TArrayLength 1
extern u32 Rtl8192CURadioB_1TArray[RadioB_1TArrayLength];
#define RadioA_1T_mCardArrayLength 282
extern u32 Rtl8192CURadioA_1T_mCardArray[RadioA_1T_mCardArrayLength];
#define RadioB_1T_mCardArrayLength 1
extern u32 Rtl8192CURadioB_1T_mCardArray[RadioB_1T_mCardArrayLength];
#define RadioA_1T_HPArrayLength 282
extern u32 Rtl8192CURadioA_1T_HPArray[RadioA_1T_HPArrayLength];
#define RadioB_GM_ArrayLength 1
extern u32 Rtl8192CURadioB_GM_Array[RadioB_GM_ArrayLength];
#define MAC_2T_ArrayLength 172
extern u32 Rtl8192CUMAC_2T_Array[MAC_2T_ArrayLength];
#define MACPHY_Array_PGLength 1
extern u32 Rtl8192CUMACPHY_Array_PG[MACPHY_Array_PGLength];
#define AGCTAB_2TArrayLength 320
extern u32 Rtl8192CUAGCTAB_2TArray[AGCTAB_2TArrayLength];
#define AGCTAB_1TArrayLength 320
extern u32 Rtl8192CUAGCTAB_1TArray[AGCTAB_1TArrayLength];
#define AGCTAB_1T_HPArrayLength 320
extern u32 Rtl8192CUAGCTAB_1T_HPArray[AGCTAB_1T_HPArrayLength];

#endif //__INC_HAL8192CU_FW_IMG_H
