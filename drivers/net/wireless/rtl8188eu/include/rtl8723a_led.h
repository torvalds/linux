/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTL8723A_LED_H__
#define __RTL8723A_LED_H__

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


//================================================================================
// Interface to manipulate LED objects.
//================================================================================
#ifdef CONFIG_USB_HCI
void rtl8723au_InitSwLeds(PADAPTER padapter);
void rtl8723au_DeInitSwLeds(PADAPTER padapter);
#endif
#ifdef CONFIG_PCI_HCI
void rtl8723ae_gen_RefreshLedState(PADAPTER Adapter);
void rtl8723ae_InitSwLeds(PADAPTER padapter);
void rtl8723ae_DeInitSwLeds(PADAPTER padapter);
#endif
#ifdef CONFIG_SDIO_HCI
void rtl8723as_InitSwLeds(PADAPTER padapter);
void rtl8723as_DeInitSwLeds(PADAPTER padapter);
#endif

#endif

