/* SPDX-License-Identifier: GPL-2.0 */
#ifndef IMG_H
#define IMG_H

#define MACPHY_Array_PGLength 30
#define PHY_REG_1T2RArrayLength 296
#define AGCTAB_ArrayLength 384
#define MACPHY_ArrayLength 18

#define RadioA_ArrayLength 246
#define RadioB_ArrayLength 78
#define RadioC_ArrayLength 1
#define RadioD_ArrayLength 1
#define PHY_REGArrayLength 1

extern u32 Rtl8192UsbPHY_REGArray[];
extern u32 Rtl8192UsbPHY_REG_1T2RArray[];
extern u32 Rtl8192UsbRadioA_Array[];
extern u32 Rtl8192UsbRadioB_Array[];
extern u32 Rtl8192UsbRadioC_Array[];
extern u32 Rtl8192UsbRadioD_Array[];
extern u32 Rtl8192UsbMACPHY_Array[];
extern u32 Rtl8192UsbMACPHY_Array_PG[];
extern u32 Rtl8192UsbAGCTAB_Array[];

#endif
