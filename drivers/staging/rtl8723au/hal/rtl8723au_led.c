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

#include "drv_types.h"
#include "rtl8723a_hal.h"
#include "rtl8723a_led.h"
#include "usb_ops_linux.h"

/*  */
/*  LED object. */
/*  */

/*  */
/*	Prototype of protected function. */
/*  */

/*  */
/*  LED_819xUsb routines. */
/*  */

/*	Description: */
/*		Turn on LED according to LedPin specified. */
void SwLedOn23a(struct rtw_adapter *padapter, struct led_8723a *pLed)
{
	u8	LedCfg = 0;

	if ((padapter->bSurpriseRemoved == true) || (padapter->bDriverStopped == true))
		return;
	switch (pLed->LedPin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		/*  SW control led0 on. */
		rtl8723au_write8(padapter, REG_LEDCFG0,
				 (LedCfg&0xf0)|BIT(5)|BIT(6));
		break;
	case LED_PIN_LED1:
		 /*  SW control led1 on. */
		rtl8723au_write8(padapter, REG_LEDCFG1, (LedCfg&0x00)|BIT(6));
		break;
	case LED_PIN_LED2:
		LedCfg = rtl8723au_read8(padapter, REG_LEDCFG2);
		 /*  SW control led1 on. */
		rtl8723au_write8(padapter, REG_LEDCFG2, (LedCfg&0x80)|BIT(5));
		break;
	default:
		break;
	}
	pLed->bLedOn = true;
}

/*	Description: */
/*		Turn off LED according to LedPin specified. */
void SwLedOff23a(struct rtw_adapter *padapter, struct led_8723a *pLed)
{
	u8	LedCfg = 0;
	/* struct hal_data_8723a	*pHalData = GET_HAL_DATA(padapter); */

	if ((padapter->bSurpriseRemoved) || (padapter->bDriverStopped))
		goto exit;

	switch (pLed->LedPin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		/*  SW control led0 on. */
		rtl8723au_write8(padapter, REG_LEDCFG0,
				 (LedCfg&0xf0)|BIT(5)|BIT(6));
		break;
	case LED_PIN_LED1:
		/*  SW control led1 on. */
		rtl8723au_write8(padapter, REG_LEDCFG1,
				 (LedCfg&0x00)|BIT(5)|BIT(6));
		break;
	case LED_PIN_LED2:
		LedCfg = rtl8723au_read8(padapter, REG_LEDCFG2);
		/*  SW control led1 on. */
		rtl8723au_write8(padapter, REG_LEDCFG2,
				 (LedCfg&0x80)|BIT(3)|BIT(5));
		break;
	default:
		break;
	}
exit:
	pLed->bLedOn = false;
}

/*  Interface to manipulate LED objects. */

/*	Description: */
/*		Initialize all LED_871x objects. */
void
rtl8723au_InitSwLeds(struct rtw_adapter	*padapter)
{
	struct led_priv *pledpriv = &padapter->ledpriv;

	pledpriv->LedControlHandler = LedControl871x23a;
	/* 8723as-vau wifi used led2 */
	InitLed871x23a(padapter, &pledpriv->SwLed0, LED_PIN_LED2);

/*	InitLed871x23a(padapter,&pledpriv->SwLed1, LED_PIN_LED2); */
}

/*	Description: */
/*		DeInitialize all LED_819xUsb objects. */
void
rtl8723au_DeInitSwLeds(struct rtw_adapter *padapter)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;

	DeInitLed871x23a(&ledpriv->SwLed0);
}
