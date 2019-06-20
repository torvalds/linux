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
