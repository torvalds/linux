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
#ifndef __RTL8188E_LED_H__
#define __RTL8188E_LED_H__

#include <osdep_service.h>
#include <drv_types.h>


/*  */
/*  Interface to manipulate LED objects. */
/*  */
void rtl8188eu_InitSwLeds(struct adapter *padapter);
void rtl8188eu_DeInitSwLeds(struct adapter *padapter);
void SwLedOn(struct adapter *padapter, struct LED_871x *pLed);
void SwLedOff(struct adapter *padapter, struct LED_871x *pLed);

#endif
