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
#define _RTL8189ES_LED_C_

#include "drv_types.h"
#include "rtl8188e_hal.h"

//================================================================================
// LED object.
//================================================================================


//================================================================================
//	Prototype of protected function.
//================================================================================

//================================================================================
// LED_819xUsb routines.
//================================================================================

//
//	Description:
//		Turn on LED according to LedPin specified.
//
static void
SwLedOn_8188ES(
	_adapter			*padapter,
	PLED_SDIO		pLed
)
{
	u8	LedCfg;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (RTW_CANNOT_RUN(padapter))
		return;

	pLed->bLedOn = _TRUE;
}


//
//	Description:
//		Turn off LED according to LedPin specified.
//
static void
SwLedOff_8188ES(
	_adapter			*padapter,
	PLED_SDIO		pLed
)
{
	u8	LedCfg;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (RTW_CANNOT_RUN(padapter))
		goto exit;

exit:
	pLed->bLedOn = _FALSE;

}

//================================================================================
// Default LED behavior.
//================================================================================

//
//	Description:
//		Initialize all LED_871x objects.
//
void
rtl8188es_InitSwLeds(
	_adapter	*padapter
	)
{
#if 0
	struct led_priv *pledpriv = &(padapter->ledpriv);

	pledpriv->LedControlHandler = LedControlSDIO;

	pledpriv->SwLedOn = SwLedOn_8188ES;
	pledpriv->SwLedOff = SwLedOff_8188ES;

	InitLed(padapter, &(pledpriv->SwLed0), LED_PIN_LED0);

	InitLed(padapter, &(pledpriv->SwLed1), LED_PIN_LED1);
#endif
}


//
//	Description:
//		DeInitialize all LED_819xUsb objects.
//
void
rtl8188es_DeInitSwLeds(
	_adapter	*padapter
	)
{
#if 0
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	DeInitLed( &(ledpriv->SwLed0) );
	DeInitLed( &(ledpriv->SwLed1) );
#endif
}

