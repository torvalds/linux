/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef __INC_HAL8192PciE_FW_IMG_H
#define __INC_HAL8192PciE_FW_IMG_H

/*Created on  2008/11/18,  3: 7*/

#include <linux/types.h>

#define PHY_REGArrayLengthPciE 1
extern u32 Rtl8192PciEPHY_REGArray[PHY_REGArrayLengthPciE];
#define PHY_REG_1T2RArrayLengthPciE 296
extern u32 Rtl8192PciEPHY_REG_1T2RArray[PHY_REG_1T2RArrayLengthPciE];
#define RadioA_ArrayLengthPciE 246
extern u32 Rtl8192PciERadioA_Array[RadioA_ArrayLengthPciE];
#define RadioB_ArrayLengthPciE 78
extern u32 Rtl8192PciERadioB_Array[RadioB_ArrayLengthPciE];
#define RadioC_ArrayLengthPciE 2
extern u32 Rtl8192PciERadioC_Array[RadioC_ArrayLengthPciE];
#define RadioD_ArrayLengthPciE 2
extern u32 Rtl8192PciERadioD_Array[RadioD_ArrayLengthPciE];
#define MACPHY_ArrayLengthPciE 18
extern u32 Rtl8192PciEMACPHY_Array[MACPHY_ArrayLengthPciE];
#define MACPHY_Array_PGLengthPciE 30
extern u32 Rtl8192PciEMACPHY_Array_PG[MACPHY_Array_PGLengthPciE];
#define AGCTAB_ArrayLengthPciE 384
extern u32 Rtl8192PciEAGCTAB_Array[AGCTAB_ArrayLengthPciE];

#endif
