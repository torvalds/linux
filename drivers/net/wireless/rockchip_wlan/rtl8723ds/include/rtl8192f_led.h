/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
#ifndef __RTL8192F_LED_H__
#define __RTL8192F_LED_H__

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#ifdef CONFIG_RTW_SW_LED
/* ********************************************************************************
 * Interface to manipulate LED objects.
 * ******************************************************************************** */
#ifdef CONFIG_USB_HCI
/** REG_LED_CFG (0x4C) **/
/* LED0 GPIO Enable, 0: disable, 1: enable*/
#define LED0_GPIO_ENABLE_8192FU (BIT21)
/* LED0 Disabled for analog signal usage, 0:Enable (output mode), 1: disable (input mode) */
#define LED0_DISABLE_ANALOGSIGNAL_8192FU (BIT7)
/* LED0 software value, 0: turn off, 1:turn on */
#define LED0_SW_VALUE_8192FU (BIT3)

/** REG_GPIO_MUXCFG (0x40) **/
/* Enable LED[1:0] for RFE CTRL[7:6], 0: BT, 1: Wi-Fi */
#define ENABLE_LED0_AND_LED1_CTRL_BY_WIFI_8192FU (BIT3)

/** REG_SW_GPIO_SHARE_CTRL_0 (0x1038) **/
/* LED Output PIN Location, 0: GPIOA_0, 1:GPIOB_4*/
#define LED_OUTPUT_PIN_LOCATION_8192FU (BIT16)

u8 rtl8192fu_CfgLed0Hw(PADAPTER padapter);
void rtl8192fu_InitSwLeds(PADAPTER padapter);
void rtl8192fu_DeInitSwLeds(PADAPTER padapter);
#endif

#ifdef CONFIG_SDIO_HCI
void rtl8192fs_InitSwLeds(PADAPTER padapter);
void rtl8192fs_DeInitSwLeds(PADAPTER padapter);
#endif

#ifdef CONFIG_PCI_HCI
void rtl8192fe_InitSwLeds(PADAPTER padapter);
void rtl8192fe_DeInitSwLeds(PADAPTER padapter);
#endif
#endif /*#ifdef CONFIG_RTW_SW_LED*/

#endif
