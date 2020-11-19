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
#ifndef __RTL8188F_LED_H__
#define __RTL8188F_LED_H__
#ifdef CONFIG_RTW_SW_LED

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


/* ********************************************************************************
 * Interface to manipulate LED objects.
 * ******************************************************************************** */
#ifdef CONFIG_USB_HCI
void rtl8188fu_InitSwLeds(PADAPTER padapter);
void rtl8188fu_DeInitSwLeds(PADAPTER padapter);
#endif
#ifdef CONFIG_SDIO_HCI
void rtl8188fs_InitSwLeds(PADAPTER padapter);
void rtl8188fs_DeInitSwLeds(PADAPTER padapter);
#endif
#ifdef CONFIG_GSPI_HCI
void rtl8188fs_InitSwLeds(PADAPTER padapter);
void rtl8188fs_DeInitSwLeds(PADAPTER padapter);
#endif
#ifdef CONFIG_PCI_HCI
void rtl8188fe_InitSwLeds(PADAPTER padapter);
void rtl8188fe_DeInitSwLeds(PADAPTER padapter);
#endif

#endif
#endif/*CONFIG_RTW_SW_LED*/
