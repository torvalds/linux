// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <osdep_service.h>
#include <drv_types.h>
#include <rtl8188e_hal.h>
#include <usb_ops_linux.h>

void sw_led_on(struct adapter *padapter, struct LED_871x *pLed)
{
	u8 led_cfg;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;
	led_cfg = usb_read8(padapter, REG_LEDCFG2);
	usb_write8(padapter, REG_LEDCFG2, (led_cfg & 0xf0) | BIT(5) | BIT(6));
	pLed->bLedOn = true;
}

void sw_led_off(struct adapter *padapter, struct LED_871x *pLed)
{
	u8 led_cfg;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		goto exit;

	led_cfg = usb_read8(padapter, REG_LEDCFG2);/* 0x4E */

	/*  Open-drain arrangement for controlling the LED) */
	led_cfg &= 0x90; /*  Set to software control. */
	usb_write8(padapter, REG_LEDCFG2, (led_cfg | BIT(3)));
	led_cfg = usb_read8(padapter, REG_MAC_PINMUX_CFG);
	led_cfg &= 0xFE;
	usb_write8(padapter, REG_MAC_PINMUX_CFG, led_cfg);
exit:
	pLed->bLedOn = false;
}

void rtw_hal_sw_led_init(struct adapter *padapter)
{
	struct led_priv *pledpriv = &padapter->ledpriv;

	InitLed871x(padapter, &pledpriv->sw_led);
}

void rtw_hal_sw_led_deinit(struct adapter *padapter)
{
	struct led_priv *ledpriv = &padapter->ledpriv;

	DeInitLed871x(&ledpriv->sw_led);
}
