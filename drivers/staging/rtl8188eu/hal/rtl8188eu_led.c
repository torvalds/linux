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

#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8188e_hal.h>
#include <rtl8188e_led.h>

/*  LED object. */

/*  LED_819xUsb routines. */
/*	Description: */
/*		Turn on LED according to LedPin specified. */
void SwLedOn(struct adapter *padapter, struct LED_871x *pLed)
{
	u8	LedCfg;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;
	LedCfg = rtw_read8(padapter, REG_LEDCFG2);
	switch (pLed->LedPin) {
	case LED_PIN_LED0:
		rtw_write8(padapter, REG_LEDCFG2, (LedCfg&0xf0)|BIT5|BIT6); /*  SW control led0 on. */
		break;
	case LED_PIN_LED1:
		rtw_write8(padapter, REG_LEDCFG2, (LedCfg&0x0f)|BIT5); /*  SW control led1 on. */
		break;
	default:
		break;
	}
	pLed->bLedOn = true;
}

/*	Description: */
/*		Turn off LED according to LedPin specified. */
void SwLedOff(struct adapter *padapter, struct LED_871x *pLed)
{
	u8	LedCfg;
	struct hal_data_8188e	*pHalData = GET_HAL_DATA(padapter);

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		goto exit;

	LedCfg = rtw_read8(padapter, REG_LEDCFG2);/* 0x4E */

	switch (pLed->LedPin) {
	case LED_PIN_LED0:
		if (pHalData->bLedOpenDrain) {
			/*  Open-drain arrangement for controlling the LED) */
			LedCfg &= 0x90; /*  Set to software control. */
			rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3));
			LedCfg = rtw_read8(padapter, REG_MAC_PINMUX_CFG);
			LedCfg &= 0xFE;
			rtw_write8(padapter, REG_MAC_PINMUX_CFG, LedCfg);
		} else {
			rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3|BIT5|BIT6));
		}
		break;
	case LED_PIN_LED1:
		LedCfg &= 0x0f; /*  Set to software control. */
		rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3));
		break;
	default:
		break;
	}
exit:
	pLed->bLedOn = false;
}

/*  Interface to manipulate LED objects. */
/*  Default LED behavior. */

/*	Description: */
/*		Initialize all LED_871x objects. */
void rtl8188eu_InitSwLeds(struct adapter *padapter)
{
	struct led_priv *pledpriv = &(padapter->ledpriv);

	pledpriv->LedControlHandler = LedControl8188eu;

	InitLed871x(padapter, &(pledpriv->SwLed0), LED_PIN_LED0);

	InitLed871x(padapter, &(pledpriv->SwLed1), LED_PIN_LED1);
}

/*	Description: */
/*		DeInitialize all LED_819xUsb objects. */
void rtl8188eu_DeInitSwLeds(struct adapter *padapter)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	DeInitLed871x(&(ledpriv->SwLed0));
	DeInitLed871x(&(ledpriv->SwLed1));
}
