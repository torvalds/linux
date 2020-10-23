/******************************************************************************
 *
 * Copyright(c) 2012 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef __RTL8192F_RF_H__
#define __RTL8192F_RF_H__

/*default*/
/*#define CONFIG_8192F_DRV_DIS*/
/*AP*/
#define CONFIG_8192F_TYPE3_DRV_DIS
#define CONFIG_8192F_TYPE4_DRV_DIS
/*unused*/
#define CONFIG_8192F_TYPE18_DRV_DIS
#define CONFIG_8192F_TYPE19_DRV_DIS
#define CONFIG_8192F_TYPE20_DRV_DIS
#define CONFIG_8192F_TYPE21_DRV_DIS
#define CONFIG_8192F_TYPE22_DRV_DIS
#define CONFIG_8192F_TYPE23_DRV_DIS
#define CONFIG_8192F_TYPE24_DRV_DIS
#define CONFIG_8192F_TYPE25_DRV_DIS
#define CONFIG_8192F_TYPE26_DRV_DIS
#define CONFIG_8192F_TYPE27_DRV_DIS
#define CONFIG_8192F_TYPE28_DRV_DIS
#define CONFIG_8192F_TYPE29_DRV_DIS
#define CONFIG_8192F_TYPE30_DRV_DIS
#define CONFIG_8192F_TYPE31_DRV_DIS


#ifdef CONFIG_SDIO_HCI /**/
/*usb*/
#define CONFIG_8192F_TYPE1_DRV_DIS
#define CONFIG_8192F_TYPE5_DRV_DIS
#define CONFIG_8192F_TYPE10_DRV_DIS
#define CONFIG_8192F_TYPE13_DRV_DIS
#define CONFIG_8192F_TYPE14_DRV_DIS
/*pcie*/
#define CONFIG_8192F_TYPE0_DRV_DIS
#define CONFIG_8192F_TYPE6_DRV_DIS
#define CONFIG_8192F_TYPE7_DRV_DIS
#define CONFIG_8192F_TYPE8_DRV_DIS
#define CONFIG_8192F_TYPE9_DRV_DIS
#define CONFIG_8192F_TYPE12_DRV_DIS
#define CONFIG_8192F_TYPE15_DRV_DIS
#define CONFIG_8192F_TYPE16_DRV_DIS
#define CONFIG_8192F_TYPE17_DRV_DIS
#endif/*CONFIG_SDIO_HCI*/

#ifdef CONFIG_USB_HCI
/*sdio*/
#define CONFIG_8192F_TYPE2_DRV_DIS
#define CONFIG_8192F_TYPE11_DRV_DIS
/*pcie*/
#define CONFIG_8192F_TYPE0_DRV_DIS
#define CONFIG_8192F_TYPE6_DRV_DIS
#define CONFIG_8192F_TYPE7_DRV_DIS
#define CONFIG_8192F_TYPE8_DRV_DIS
#define CONFIG_8192F_TYPE9_DRV_DIS
#define CONFIG_8192F_TYPE12_DRV_DIS
#define CONFIG_8192F_TYPE15_DRV_DIS
#define CONFIG_8192F_TYPE16_DRV_DIS
#define CONFIG_8192F_TYPE17_DRV_DIS
#endif/*CONFIG_USB_HCI*/

#ifdef CONFIG_PCI_HCI
/*sdio*/
#define CONFIG_8192F_TYPE2_DRV_DIS
#define CONFIG_8192F_TYPE11_DRV_DIS
/*usb*/
#define CONFIG_8192F_TYPE1_DRV_DIS
#define CONFIG_8192F_TYPE5_DRV_DIS
#define CONFIG_8192F_TYPE10_DRV_DIS
#define CONFIG_8192F_TYPE13_DRV_DIS
#define CONFIG_8192F_TYPE14_DRV_DIS
#endif/*CONFIG_PCI_HCI*/

int PHY_RF6052_Config8192F(PADAPTER pdapter);

void PHY_RF6052SetBandwidth8192F(PADAPTER Adapter, enum channel_width Bandwidth);

#endif/* __RTL8192F_RF_H__ */
