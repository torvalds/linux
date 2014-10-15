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
#define _RTL8192E_LED_C_

//#include <drv_types.h>
#include <rtl8192e_hal.h>


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
SwLedOn_8192EU(
	PADAPTER		padapter, 
	PLED_USB		pLed
)
{
	u8	LedCfg;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if( (padapter->bSurpriseRemoved == _TRUE) || ( padapter->bDriverStopped == _TRUE))
	{
		return;
	}

	if(	RT_GetInterfaceSelection(padapter) == INTF_SEL2_MINICARD ||
	 	RT_GetInterfaceSelection(padapter) == INTF_SEL3_USB_Solo ||
		RT_GetInterfaceSelection(padapter) == INTF_SEL4_USB_Combo)
	{
		LedCfg = rtw_read8(padapter, REG_LEDCFG2);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("In SwLedON,LedAddr:%X LEDPIN=%d\n",REG_LEDCFG2, pLed->LedPin));
		switch(pLed->LedPin)
		{
			case LED_PIN_GPIO0:
				break;

			case LED_PIN_LED0:
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("In SwLedOn,LedAddr:%X LEDPIN=%d\n",REG_LEDCFG1, pLed->LedPin));
				LedCfg = rtw_read8(padapter, REG_LEDCFG0);						
				rtw_write8(padapter, REG_LEDCFG1, (LedCfg&~BIT7)); // SW control led1 on.
				break;

			case LED_PIN_LED1:
				PHY_SetMacReg(padapter, 0x64, BIT1, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT15, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT10|BIT9|BIT8, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT11, 0);
				break;

			default:
				break;
		}
	}
	else
	{
		switch(pLed->LedPin)
		{
			case LED_PIN_GPIO0:
				break;

			case LED_PIN_LED0: //8192EU don't have LED0, so we jump this case to set LED1
				LedCfg = rtw_read8(padapter, (REG_LEDCFG1));
				rtw_write8(padapter, (REG_LEDCFG1), (LedCfg&~BIT7)); // SW control led1 on.
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOn LED1 0x%x\n", rtw_read32(padapter, REG_LEDCFG1)));
				break;

			case LED_PIN_LED1:
				PHY_SetMacReg(padapter, 0x64, BIT1, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT15, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT10|BIT9|BIT8, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT11, 0);
				break;

			case LED_PIN_LED2:
				LedCfg = rtw_read8(padapter, (REG_LEDCFG2));
				rtw_write8(padapter, (REG_LEDCFG2), (LedCfg&~BIT7)); // SW control led1 on.
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOn LED2 0x%x\n", rtw_read32(padapter, REG_LEDCFG2)));
				break;
				
			default:
				break;
		}
	}

	pLed->bLedOn = _TRUE;
}


//
//	Description:
//		Turn off LED according to LedPin specified.
//
static void
SwLedOff_8192EU(
	PADAPTER		padapter, 
	PLED_USB		pLed
)
{
	u8	LedCfg;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if(padapter->bSurpriseRemoved == _TRUE)
	{
		return;
	}

	if(	RT_GetInterfaceSelection(padapter) == INTF_SEL2_MINICARD ||
		RT_GetInterfaceSelection(padapter) == INTF_SEL3_USB_Solo ||
		RT_GetInterfaceSelection(padapter) == INTF_SEL4_USB_Combo)
	{		
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("In SwLedOff,LedAddr:%X LEDPIN=%d\n",REG_LEDCFG2, pLed->LedPin));
		LedCfg = rtw_read8(padapter, REG_LEDCFG2);
		
		// 2009/10/23 MH Issau eed to move the LED GPIO from bit  0 to bit3.
		// 2009/10/26 MH Issau if tyhe device is 8c DID is 0x8176, we need to enable bit6 to
		// enable GPIO8 for controlling LED.	
		// 2010/07/02 Supprt Open-drain arrangement for controlling the LED. Added by Roger.
		//
		switch(pLed->LedPin)
		{

			case LED_PIN_GPIO0:
				break;

			case LED_PIN_LED0:	//8192EU don't have LED0, so we jump this case to set LED1
				LedCfg = rtw_read8(padapter, REG_LEDCFG1);
				rtw_write8(padapter, REG_LEDCFG1, LedCfg|BIT7);
				break;

			case LED_PIN_LED1:
				PHY_SetMacReg(padapter, 0x64, BIT1, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT15, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT10|BIT9|BIT8, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT11, 1);
				break;

			default:
				break;
		}
	}
	else
	{
		switch(pLed->LedPin)
		{
			case LED_PIN_GPIO0:
				break;

			case LED_PIN_LED0://8192EU don't have LED0, so we jump this case to set LED1
				LedCfg = rtw_read8(padapter, REG_LEDCFG1);
				rtw_write8(padapter, REG_LEDCFG1, LedCfg|BIT7);
				break;

			case LED_PIN_LED1:
				PHY_SetMacReg(padapter, 0x64, BIT1, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT15, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT10|BIT9|BIT8, 0);
				PHY_SetMacReg(padapter, 0x4C, BIT11, 1);
				break;

			case LED_PIN_LED2:
				LedCfg = rtw_read8(padapter, REG_LEDCFG2);
				rtw_write8(padapter, REG_LEDCFG2, LedCfg|BIT7);
				break;

			default:
				break;
		}
	}

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
rtl8192eu_InitSwLeds(
	_adapter	*padapter
	)
{
	struct led_priv *pledpriv = &(padapter->ledpriv);

	pledpriv->LedControlHandler = LedControlUSB;

	pledpriv->SwLedOn = SwLedOn_8192EU;
	pledpriv->SwLedOff = SwLedOff_8192EU;

	InitLed(padapter, &(pledpriv->SwLed0), LED_PIN_LED0);

	InitLed(padapter, &(pledpriv->SwLed1), LED_PIN_LED1);
}


//
//	Description:
//		DeInitialize all LED_819xUsb objects.
//
void
rtl8192eu_DeInitSwLeds(
	_adapter	*padapter
	)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	DeInitLed( &(ledpriv->SwLed0) );
	DeInitLed( &(ledpriv->SwLed1) );
}

