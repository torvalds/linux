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
#ifndef __INC_HAL8192CE_FW_IMG_H
#define __INC_HAL8192CE_FW_IMG_H

#include <basic_types.h>

/*Created on  2011/ 6/15,  5:45*/

#ifdef CONFIG_BT_COEXISTENCE
#define TSMCImgArrayLength 15706 //v84 TSMC COMMON 2012-04-13
#else //#ifdef CONFIG_P2P
#define TSMCImgArrayLength 16126 //v88 TSMC P2PPS with CCX report C2H 2012-12-05
#endif
extern u8 Rtl8192CEFwTSMCImgArray[TSMCImgArrayLength];

#ifdef CONFIG_BT_COEXISTENCE
#define UMCACutImgArrayLength 16248 //v79 UMC A Cut COMMON 2011-10-06
#else //#ifdef CONFIG_P2P
#define UMCACutImgArrayLength 16126 //v88 UMC A Cut P2PPS with CCX report C2H 2012-12-05
#endif
extern u8 Rtl8192CEFwUMCACutImgArray[UMCACutImgArrayLength];

#ifdef CONFIG_BT_COEXISTENCE
#define UMCBCutImgArrayLength 15686 //v84 UMC B Cut COMMON 2012-04-13
#else //#ifdef CONFIG_P2P
#define UMCBCutImgArrayLength 16096 //v88 UMC B Cut P2PPS with CCX report C2H 2012-12-05
#endif
extern u8 Rtl8192CEFwUMCBCutImgArray[UMCBCutImgArrayLength];

//8192C_Formal_92CE_PHYforMP_110804 2011-11-23
//8188C_Formal_88CE_PHYforMP_111117 2011-11-23

#define PHY_REG_2TArrayLength 374
extern u32 Rtl8192CEPHY_REG_2TArray[PHY_REG_2TArrayLength];
#define PHY_REG_1TArrayLength 374
extern u32 Rtl8192CEPHY_REG_1TArray[PHY_REG_1TArrayLength];
#define PHY_ChangeTo_1T1RArrayLength 1
extern u32 Rtl8192CEPHY_ChangeTo_1T1RArray[PHY_ChangeTo_1T1RArrayLength];
#define PHY_ChangeTo_1T2RArrayLength 1
extern u32 Rtl8192CEPHY_ChangeTo_1T2RArray[PHY_ChangeTo_1T2RArrayLength];
#define PHY_ChangeTo_2T2RArrayLength 1
extern u32 Rtl8192CEPHY_ChangeTo_2T2RArray[PHY_ChangeTo_2T2RArrayLength];
#define PHY_REG_Array_PGLength 336
extern u32 Rtl8192CEPHY_REG_Array_PG[PHY_REG_Array_PGLength];
#define PHY_REG_Array_MPLength 4
extern u32 Rtl8192CEPHY_REG_Array_MP[PHY_REG_Array_MPLength];
#define RadioA_2TArrayLength 282
extern u32 Rtl8192CERadioA_2TArray[RadioA_2TArrayLength];
#define RadioB_2TArrayLength 78
extern u32 Rtl8192CERadioB_2TArray[RadioB_2TArrayLength];
#define RadioA_1TArrayLength 282
extern u32 Rtl8192CERadioA_1TArray[RadioA_1TArrayLength];
#define RadioB_1TArrayLength 1
extern u32 Rtl8192CERadioB_1TArray[RadioB_1TArrayLength];
#define RadioB_GM_ArrayLength 1
extern u32 Rtl8192CERadioB_GM_Array[RadioB_GM_ArrayLength];
// MAC reg V14 - 2011-11-23
#define MAC_2T_ArrayLength 174
extern u32 Rtl8192CEMAC_2T_Array[MAC_2T_ArrayLength];
#define MACPHY_Array_PGLength 1
extern u32 Rtl8192CEMACPHY_Array_PG[MACPHY_Array_PGLength];
#define AGCTAB_2TArrayLength 320
extern u32 Rtl8192CEAGCTAB_2TArray[AGCTAB_2TArrayLength];
#define AGCTAB_1TArrayLength 320
extern u32 Rtl8192CEAGCTAB_1TArray[AGCTAB_1TArrayLength];

#endif //__INC_HAL8192CE_FW_IMG_H
