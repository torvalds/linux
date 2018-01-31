/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __INC_HAL8723U_FW_IMG_H
#define __INC_HAL8723U_FW_IMG_H

/*Created on  2013/01/14, 15:51*/

//FW v16 enable usb interrupt
#define Rtl8723UImgArrayLength 22172
extern u8 Rtl8723UFwImgArray[Rtl8723UImgArrayLength];
#define Rtl8723UBTImgArrayLength 1
extern u8 Rtl8723UFwBTImgArray[Rtl8723UBTImgArrayLength];

#define Rtl8723UUMCBCutImgArrayWithBTLength 24124
#define Rtl8723UUMCBCutImgArrayWithoutBTLength 19200

extern u8 Rtl8723UFwUMCBCutImgArrayWithBT[Rtl8723UUMCBCutImgArrayWithBTLength];
extern u8 Rtl8723UFwUMCBCutImgArrayWithoutBT[Rtl8723UUMCBCutImgArrayWithoutBTLength];

#define Rtl8723SUMCBCutMPImgArrayLength 24174
extern const u8 Rtl8723SFwUMCBCutMPImgArray[Rtl8723SUMCBCutMPImgArrayLength];

#define Rtl8723EBTImgArrayLength 16404
extern u8 Rtl8723EFwBTImgArray[Rtl8723EBTImgArrayLength] ;


#ifndef CONFIG_PHY_SETTING_WITH_ODM
#define Rtl8723UPHY_REG_2TArrayLength 1
extern u32 Rtl8723UPHY_REG_2TArray[Rtl8723UPHY_REG_2TArrayLength];
#define Rtl8723UPHY_REG_1TArrayLength 372
extern u32 Rtl8723UPHY_REG_1TArray[Rtl8723UPHY_REG_1TArrayLength];
#define Rtl8723UPHY_ChangeTo_1T1RArrayLength 1
extern u32 Rtl8723UPHY_ChangeTo_1T1RArray[Rtl8723UPHY_ChangeTo_1T1RArrayLength];
#define Rtl8723UPHY_ChangeTo_1T2RArrayLength 1
extern u32 Rtl8723UPHY_ChangeTo_1T2RArray[Rtl8723UPHY_ChangeTo_1T2RArrayLength];
#define Rtl8723UPHY_ChangeTo_2T2RArrayLength 1
extern u32 Rtl8723UPHY_ChangeTo_2T2RArray[Rtl8723UPHY_ChangeTo_2T2RArrayLength];

#define Rtl8723UPHY_REG_1T_mCardArrayLength 372
extern u32 Rtl8723UPHY_REG_1T_mCardArray[Rtl8723UPHY_REG_1T_mCardArrayLength];
#define Rtl8723UPHY_REG_1T_SDIOArrayLength 372
extern u32 Rtl8723UPHY_REG_1T_SDIOArray[Rtl8723UPHY_REG_1T_SDIOArrayLength];
#define Rtl8723URadioA_2TArrayLength 1
extern u32 Rtl8723URadioA_2TArray[Rtl8723URadioA_2TArrayLength];
#define Rtl8723URadioB_2TArrayLength 1
extern u32 Rtl8723URadioB_2TArray[Rtl8723URadioB_2TArrayLength];
#define Rtl8723URadioA_1TArrayLength 282
extern u32 Rtl8723URadioA_1TArray[Rtl8723URadioA_1TArrayLength];
#define Rtl8723URadioB_1TArrayLength 1
extern u32 Rtl8723URadioB_1TArray[Rtl8723URadioB_1TArrayLength];
#define Rtl8723URadioA_1T_mCardArrayLength 282
extern u32 Rtl8723URadioA_1T_mCardArray[Rtl8723URadioA_1T_mCardArrayLength];
#define Rtl8723URadioA_1T_SDIOArrayLength 282
extern u32 Rtl8723URadioA_1T_SDIOArray[Rtl8723URadioA_1T_SDIOArrayLength];
#define Rtl8723URadioB_GM_ArrayLength 1
extern u32 Rtl8723URadioB_GM_Array[Rtl8723URadioB_GM_ArrayLength];
#define Rtl8723UMAC_2T_ArrayLength 172
extern u32 Rtl8723UMAC_2T_Array[Rtl8723UMAC_2T_ArrayLength];

#define Rtl8723UAGCTAB_2TArrayLength 1
extern u32 Rtl8723UAGCTAB_2TArray[Rtl8723UAGCTAB_2TArrayLength];
#define Rtl8723UAGCTAB_1TArrayLength 320
extern u32 Rtl8723UAGCTAB_1TArray[Rtl8723UAGCTAB_1TArrayLength];
#endif//#ifndef CONFIG_PHY_SETTING_WITH_ODM

#define Rtl8723UPHY_REG_Array_PGLength 336
extern u32 Rtl8723UPHY_REG_Array_PG[Rtl8723UPHY_REG_Array_PGLength];
#define Rtl8723UMACPHY_Array_PGLength 1
extern u32 Rtl8723UMACPHY_Array_PG[Rtl8723UMACPHY_Array_PGLength];

#if MP_DRIVER == 1
#define Rtl8723UPHY_REG_Array_MPLength 4
extern u32 Rtl8723UPHY_REG_Array_MP[Rtl8723UPHY_REG_Array_MPLength];
#endif //#if MP_DRIVER == 1

#endif //#ifndef __INC_HAL8723U_FW_IMG_H

