/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#define _RTL8723AS_LED_C_

#include "drv_types.h"
#include "rtl8723a_hal.h"

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
void
SwLedOn(
	_adapter			*padapter,
	PLED_871x		pLed
)
{
	u8	LedCfg;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if( (padapter->bSurpriseRemoved == _TRUE) || ( padapter->bDriverStopped == _TRUE))
	{
		return;
	}

	pLed->bLedOn = _TRUE;

}


//
//	Description:
//		Turn off LED according to LedPin specified.
//
void
SwLedOff(
	_adapter			*padapter,
	PLED_871x		pLed
)
{
	u8	LedCfg;
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if((padapter->bSurpriseRemoved == _TRUE) || ( padapter->bDriverStopped == _TRUE))
	{
		goto exit;
	}

exit:
	pLed->bLedOn = _FALSE;

}

//================================================================================
// Interface to manipulate LED objects.
//================================================================================

//================================================================================
// Default LED behavior.
//================================================================================

//
//	Description:
//		Initialize all LED_871x objects.
//
void
rtl8723as_InitSwLeds(
	_adapter	*padapter
	)
{
	struct led_priv *pledpriv = &(padapter->ledpriv);

	pledpriv->LedControlHandler = LedControl871x;
#if 0

	InitLed871x(padapter, &(pledpriv->SwLed0), LED_PIN_LED0);

	InitLed871x(padapter,&(pledpriv->SwLed1), LED_PIN_LED1);
#endif
}


//
//	Description:
//		DeInitialize all LED_819xUsb objects.
//
void
rtl8723as_DeInitSwLeds(
	_adapter	*padapter
	)
{
#if 0
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	DeInitLed871x( &(ledpriv->SwLed0) );
	DeInitLed871x( &(ledpriv->SwLed1) );
#endif
}

