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
#ifndef __RTL8192C_LED_H_
#define __RTL8192C_LED_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


//================================================================================
// Interface to manipulate LED objects.
//================================================================================
#ifdef CONFIG_USB_HCI
void rtl8192cu_InitSwLeds(_adapter *padapter);
void rtl8192cu_DeInitSwLeds(_adapter *padapter);
#endif
#ifdef CONFIG_PCI_HCI
void rtl8192ce_gen_RefreshLedState(PADAPTER Adapter);
void rtl8192ce_InitSwLeds(_adapter *padapter);
void rtl8192ce_DeInitSwLeds(_adapter *padapter);
#endif

#endif

