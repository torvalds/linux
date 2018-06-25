/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTL8188E_LED_H__
#define __RTL8188E_LED_H__

#include <osdep_service.h>
#include <drv_types.h>


/*  */
/*  Interface to manipulate LED objects. */
/*  */
void SwLedOn(struct adapter *padapter, struct LED_871x *pLed);
void SwLedOff(struct adapter *padapter, struct LED_871x *pLed);

#endif
