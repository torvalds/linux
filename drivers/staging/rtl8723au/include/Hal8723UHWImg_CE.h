#ifndef __INC_HAL8723U_FW_IMG_H
#define __INC_HAL8723U_FW_IMG_H

/*Created on  2013/01/14, 15:51*/

/* FW v16 enable usb interrupt */
#define Rtl8723UImgArrayLength 22172
extern u8 Rtl8723UFwImgArray[Rtl8723UImgArrayLength];
#define Rtl8723UBTImgArrayLength 1
extern u8 Rtl8723UFwBTImgArray[Rtl8723UBTImgArrayLength];

#define Rtl8723UUMCBCutImgArrayWithBTLength 24118
#define Rtl8723UUMCBCutImgArrayWithoutBTLength 19200

extern u8 Rtl8723UFwUMCBCutImgArrayWithBT[Rtl8723UUMCBCutImgArrayWithBTLength];
extern u8 Rtl8723UFwUMCBCutImgArrayWithoutBT[Rtl8723UUMCBCutImgArrayWithoutBTLength];

#define Rtl8723SUMCBCutMPImgArrayLength 24174
extern const u8 Rtl8723SFwUMCBCutMPImgArray[Rtl8723SUMCBCutMPImgArrayLength];

#define Rtl8723EBTImgArrayLength 15276
extern u8 Rtl8723EFwBTImgArray[Rtl8723EBTImgArrayLength];

#define Rtl8723UPHY_REG_Array_PGLength 336
extern u32 Rtl8723UPHY_REG_Array_PG[Rtl8723UPHY_REG_Array_PGLength];
#define Rtl8723UMACPHY_Array_PGLength 1
extern u32 Rtl8723UMACPHY_Array_PG[Rtl8723UMACPHY_Array_PGLength];

#endif /* ifndef __INC_HAL8723U_FW_IMG_H */
