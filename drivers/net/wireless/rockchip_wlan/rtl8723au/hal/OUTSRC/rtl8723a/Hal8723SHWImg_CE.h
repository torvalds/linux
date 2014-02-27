#ifndef __INC_HAL8723S_FW_IMG_H
#define __INC_HAL8723S_FW_IMG_H

/*Created on  2013/02/26, 18:59*/

#define Rtl8723SUMCBCutMPImgArrayLength 24174

#define Rtl8723SUMCBCutImgArrayWithBTLength 24348
#define Rtl8723SUMCBCutImgArrayWithoutBTLength 19200

#define Rtl8723SImgArrayLength 20606
extern const u8 Rtl8723SFwImgArray[Rtl8723SImgArrayLength];
#define Rtl8723SBTImgArrayLength 1
extern const u8 Rtl8723SFwBTImgArray[Rtl8723SBTImgArrayLength];

#ifdef CONFIG_MP_INCLUDED
#define Rtl8723EBTImgArrayLength 16404
extern u8 Rtl8723EFwBTImgArray[Rtl8723EBTImgArrayLength] ;
extern const u8 Rtl8723SFwUMCBCutMPImgArray[Rtl8723SUMCBCutMPImgArrayLength];
#endif //CONFIG_MP_INCLUDED

extern const u8 Rtl8723SFwUMCBCutImgArrayWithBT[Rtl8723SUMCBCutImgArrayWithBTLength];
extern const u8 Rtl8723SFwUMCBCutImgArrayWithoutBT[Rtl8723SUMCBCutImgArrayWithoutBTLength];

#ifndef CONFIG_PHY_SETTING_WITH_ODM
#define Rtl8723SPHY_REG_2TArrayLength 1
extern const u32 Rtl8723SPHY_REG_2TArray[Rtl8723SPHY_REG_2TArrayLength];
#define Rtl8723SPHY_REG_1TArrayLength 372
extern const u32 Rtl8723SPHY_REG_1TArray[Rtl8723SPHY_REG_1TArrayLength];
#define Rtl8723SPHY_ChangeTo_1T1RArrayLength 1
extern const u32 Rtl8723SPHY_ChangeTo_1T1RArray[Rtl8723SPHY_ChangeTo_1T1RArrayLength];
#define Rtl8723SPHY_ChangeTo_1T2RArrayLength 1
extern const u32 Rtl8723SPHY_ChangeTo_1T2RArray[Rtl8723SPHY_ChangeTo_1T2RArrayLength];
#define Rtl8723SPHY_ChangeTo_2T2RArrayLength 1
extern const u32 Rtl8723SPHY_ChangeTo_2T2RArray[Rtl8723SPHY_ChangeTo_2T2RArrayLength];


#define Rtl8723SRadioA_2TArrayLength 1
extern const u32 Rtl8723SRadioA_2TArray[Rtl8723SRadioA_2TArrayLength];
#define Rtl8723SRadioB_2TArrayLength 1
extern const u32 Rtl8723SRadioB_2TArray[Rtl8723SRadioB_2TArrayLength];
#define Rtl8723SRadioA_1TArrayLength 282
extern const u32 Rtl8723SRadioA_1TArray[Rtl8723SRadioA_1TArrayLength];
#define Rtl8723SRadioB_1TArrayLength 1
extern const u32 Rtl8723SRadioB_1TArray[Rtl8723SRadioB_1TArrayLength];
#define Rtl8723SRadioB_GM_ArrayLength 1
extern const u32 Rtl8723SRadioB_GM_Array[Rtl8723SRadioB_GM_ArrayLength];
#define Rtl8723SMAC_2T_ArrayLength 172
extern const u32 Rtl8723SMAC_2T_Array[Rtl8723SMAC_2T_ArrayLength];
#define Rtl8723SAGCTAB_2TArrayLength 1
extern const u32 Rtl8723SAGCTAB_2TArray[Rtl8723SAGCTAB_2TArrayLength];
#define Rtl8723SAGCTAB_1TArrayLength 320
extern const u32 Rtl8723SAGCTAB_1TArray[Rtl8723SAGCTAB_1TArrayLength];
#endif//#ifndef CONFIG_PHY_SETTING_WITH_ODM

#define Rtl8723SPHY_REG_Array_PGLength 336
extern const u32 Rtl8723SPHY_REG_Array_PG[Rtl8723SPHY_REG_Array_PGLength];
#define Rtl8723SMACPHY_Array_PGLength 1
extern const u32 Rtl8723SMACPHY_Array_PG[Rtl8723SMACPHY_Array_PGLength];

#if MP_DRIVER == 1
#define Rtl8723SPHY_REG_Array_MPLength 4
extern const u32 Rtl8723SPHY_REG_Array_MP[Rtl8723SPHY_REG_Array_MPLength];
#endif//#if MP_DRIVER == 1

#endif //#ifndef __INC_HAL8723S_FW_IMG_H

