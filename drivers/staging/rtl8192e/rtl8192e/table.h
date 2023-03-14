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

#define PHY_REG_1T2RArrayLengthPciE 296
extern u32 Rtl8192PciEPHY_REG_1T2RArray[PHY_REG_1T2RArrayLengthPciE];
#define RTL8192E_RADIO_A_ARR_LEN 246
extern u32 Rtl8192PciERadioA_Array[RTL8192E_RADIO_A_ARR_LEN];
#define RTL8192E_RADIO_B_ARR_LEN 78
extern u32 Rtl8192PciERadioB_Array[RTL8192E_RADIO_B_ARR_LEN];
#define RTL8192E_MACPHY_ARR_LEN 18
extern u32 Rtl8192PciEMACPHY_Array[RTL8192E_MACPHY_ARR_LEN];
#define MACPHY_Array_PGLengthPciE 30
extern u32 Rtl8192PciEMACPHY_Array_PG[MACPHY_Array_PGLengthPciE];
#define RTL8192E_AGCTAB_ARR_LEN 384
extern u32 Rtl8192PciEAGCTAB_Array[RTL8192E_AGCTAB_ARR_LEN];

#endif
