#ifndef __INC_HAL8192SU_FW_IMG_H
#define __INC_HAL8192SU_FW_IMG_H

#include <linux/types.h>

/*Created on  2009/ 3/ 6,  5:29*/

#define ImgArrayLength 68368
extern u8 Rtl8192SUFwImgArray[ImgArrayLength];
#define MainArrayLength 1
extern u8 Rtl8192SUFwMainArray[MainArrayLength];
#define DataArrayLength 1
extern u8 Rtl8192SUFwDataArray[DataArrayLength];
#define PHY_REG_2T2RArrayLength 372
extern u32 Rtl8192SUPHY_REG_2T2RArray[PHY_REG_2T2RArrayLength];
#define PHY_REG_1T2RArrayLength 1
extern u32 Rtl8192SUPHY_REG_1T2RArray[PHY_REG_1T2RArrayLength];
#define PHY_ChangeTo_1T1RArrayLength 48
extern u32 Rtl8192SUPHY_ChangeTo_1T1RArray[PHY_ChangeTo_1T1RArrayLength];
#define PHY_ChangeTo_1T2RArrayLength 45
extern u32 Rtl8192SUPHY_ChangeTo_1T2RArray[PHY_ChangeTo_1T2RArrayLength];
#define PHY_ChangeTo_2T2RArrayLength 45
extern u32 Rtl8192SUPHY_ChangeTo_2T2RArray[PHY_ChangeTo_2T2RArrayLength];
#define PHY_REG_Array_PGLength 21
extern u32 Rtl8192SUPHY_REG_Array_PG[PHY_REG_Array_PGLength];
#define RadioA_1T_ArrayLength 202
extern u32 Rtl8192SURadioA_1T_Array[RadioA_1T_ArrayLength];
#define RadioB_ArrayLength 22
extern u32 Rtl8192SURadioB_Array[RadioB_ArrayLength];
#define RadioA_to1T_ArrayLength 2
extern u32 Rtl8192SURadioA_to1T_Array[RadioA_to1T_ArrayLength];
#define RadioA_to2T_ArrayLength 2
extern u32 Rtl8192SURadioA_to2T_Array[RadioA_to2T_ArrayLength];
#define RadioB_GM_ArrayLength 16
extern u32 Rtl8192SURadioB_GM_Array[RadioB_GM_ArrayLength];
#define MAC_2T_ArrayLength 106
extern u32 Rtl8192SUMAC_2T_Array[MAC_2T_ArrayLength];
#define MACPHY_Array_PGLength 1
extern u32 Rtl8192SUMACPHY_Array_PG[MACPHY_Array_PGLength];
#define AGCTAB_ArrayLength 320
extern u32 Rtl8192SUAGCTAB_Array[AGCTAB_ArrayLength];

#endif //__INC_HAL8192SU_FW_IMG_H

