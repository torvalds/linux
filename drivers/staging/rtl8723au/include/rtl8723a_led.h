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
 ******************************************************************************/
#ifndef __RTL8723A_LED_H__
#define __RTL8723A_LED_H__

#include <osdep_service.h>
#include <drv_types.h>


/*  */
/*  Interface to manipulate LED objects. */
/*  */
void rtl8723au_InitSwLeds(struct rtw_adapter *padapter);
void rtl8723au_DeInitSwLeds(struct rtw_adapter *padapter);
void SwLedOn23a(struct rtw_adapter *padapter, struct led_8723a * pLed);
void SwLedOff23a(struct rtw_adapter *padapter, struct led_8723a * pLed);

#endif
