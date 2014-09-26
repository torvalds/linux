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
#define _RTL8812AU_LED_C_

//#include <drv_types.h>
#include <rtl8812a_hal.h>

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
SwLedOn_8812AU(
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
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("In SwLedOn,LedAddr:%X LEDPIN=%d\n",REG_LEDCFG2, pLed->LedPin));
				LedCfg = rtw_read8(padapter, REG_LEDCFG2);
				rtw_write8(padapter, REG_LEDCFG2, (LedCfg&0xf0)|BIT5|BIT6); // SW control led0 on.
				break;

			case LED_PIN_LED1:
				rtw_write8(padapter, REG_LEDCFG2, (LedCfg&0x0f)|BIT5); // SW control led1 on.
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

			case LED_PIN_LED0:
				if(pHalData->AntDivCfg==0)
				{
					LedCfg = rtw_read8(padapter, REG_LEDCFG0);
					rtw_write8(padapter, REG_LEDCFG0, (LedCfg&0x70)|BIT5); // SW control led0 on.
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOn LED0 0x%x\n", rtw_read32(padapter, REG_LEDCFG0)));
				}
				else
				{
					LedCfg = rtw_read8(padapter, REG_LEDCFG2) & 0xe0;
					rtw_write8(padapter, REG_LEDCFG2, LedCfg|BIT7|BIT6|BIT5); // SW control led0 on.
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOn LED0 Addr 0x%x,  0x%x\n", REG_LEDCFG2, rtw_read32(padapter, REG_LEDCFG2)));
				}
				break;

			case LED_PIN_LED1:
				LedCfg = rtw_read8(padapter, (REG_LEDCFG1));
				rtw_write8(padapter, (REG_LEDCFG1), (LedCfg&0x70)|BIT5); // SW control led1 on.
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOn LED1 0x%x\n", rtw_read32(padapter, REG_LEDCFG1)));
				break;

			case LED_PIN_LED2:
				LedCfg = rtw_read8(padapter, (REG_LEDCFG2));
				rtw_write8(padapter, (REG_LEDCFG2), (LedCfg&0x70)|BIT5); // SW control led1 on.
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
SwLedOff_8812AU(
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

			case LED_PIN_LED0:
				if(pHalData->bLedOpenDrain == _TRUE)					
				{
					LedCfg &= 0x90; // Set to software control.				
					rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3));				
					LedCfg = rtw_read8(padapter, REG_MAC_PINMUX_CFG);
					LedCfg &= 0xFE;
					rtw_write8(padapter, REG_MAC_PINMUX_CFG, LedCfg);									
				}
				else
				{
					rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3|BIT5|BIT6));
				}
				break;

			case LED_PIN_LED1:
				LedCfg &= 0x0f; // Set to software control.
				rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3));
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

			case LED_PIN_LED0:
				 if(pHalData->AntDivCfg==0)
				{
					LedCfg = rtw_read8(padapter, REG_LEDCFG0);
					LedCfg &= 0x70; // Set to software control.
					rtw_write8(padapter, REG_LEDCFG0, (LedCfg|BIT3|BIT5));
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOff LED0 0x%x\n", rtw_read32(padapter, REG_LEDCFG0)));
				}
				else
				{
					LedCfg = rtw_read8(padapter, REG_LEDCFG2);
					LedCfg &= 0xe0; // Set to software control. 			
					if(IS_HARDWARE_TYPE_8723A(padapter))
						rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3|BIT7|BIT5));
					else
						rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3|BIT7|BIT6|BIT5));
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOff LED0 0x%x\n", rtw_read32(padapter, REG_LEDCFG2)));
				}
				break;

			case LED_PIN_LED1:
				LedCfg = rtw_read8(padapter, REG_LEDCFG1);
				LedCfg &= 0x70; // Set to software control.
				rtw_write8(padapter, REG_LEDCFG1, (LedCfg|BIT3|BIT5));
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOff LED1 0x%x\n", rtw_read32(padapter, REG_LEDCFG1)));
				break;

			case LED_PIN_LED2:
				LedCfg = rtw_read8(padapter, REG_LEDCFG2);
				LedCfg &= 0x70; // Set to software control.
				rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3|BIT5));
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedOff LED1 0x%x\n", rtw_read32(padapter, REG_LEDCFG2)));
				break;

			default:
				break;
		}
	}

	pLed->bLedOn = _FALSE;
}

//
//	Description:
//		Turn on LED according to LedPin specified.
//
static void
SwLedOn_8821AU(
	PADAPTER		Adapter, 
	PLED_USB		pLed
)
{
	u8	LedCfg;

	if( (Adapter->bSurpriseRemoved == _TRUE) || ( Adapter->bDriverStopped == _TRUE))
	{
		return;
	}

	if(	RT_GetInterfaceSelection(Adapter) == INTF_SEL2_MINICARD ||
	 	RT_GetInterfaceSelection(Adapter) == INTF_SEL3_USB_Solo ||
		RT_GetInterfaceSelection(Adapter) == INTF_SEL4_USB_Combo)
	{
		LedCfg = rtw_read8(Adapter, REG_LEDCFG2);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("In SwLedON,LedAddr:%X LEDPIN=%d\n",REG_LEDCFG2, pLed->LedPin));
		switch(pLed->LedPin)
		{	
			case LED_PIN_GPIO0:
				break;

			case LED_PIN_LED0:

				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("In SwLedOn,LedAddr:%X LEDPIN=%d\n",REG_LEDCFG2, pLed->LedPin));				
				LedCfg = rtw_read8(Adapter, REG_LEDCFG2);
				rtw_write8(Adapter, REG_LEDCFG2, (LedCfg&0xf0)|BIT5|BIT6); // SW control led0 on.
				break;

			case LED_PIN_LED1:
				rtw_write8(Adapter, REG_LEDCFG2, (LedCfg&0x0f)|BIT5); // SW control led1 on.
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

			case LED_PIN_LED0:
			case LED_PIN_LED1:
			case LED_PIN_LED2:			
				if(IS_HARDWARE_TYPE_8821U(Adapter))
				{
					LedCfg = rtw_read8(Adapter, REG_LEDCFG2) & 0x20;
					rtw_write8(Adapter, REG_LEDCFG2, (LedCfg & ~(BIT3))|BIT5); // SW control led0 on.
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("8821 SwLedOn LED2 0x%x\n", rtw_read32(Adapter, REG_LEDCFG0)));
					//DBG_871X("8821 SwLedOn LED2 0x%x\n", rtw_read32(Adapter, REG_LEDCFG0));
				}

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
SwLedOff_8821AU(
	PADAPTER		Adapter, 
	PLED_USB		pLed
)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u8	LedCfg;

	if(Adapter->bSurpriseRemoved == _TRUE)
	{
		return;
	}

	if(	RT_GetInterfaceSelection(Adapter) == INTF_SEL2_MINICARD ||
		RT_GetInterfaceSelection(Adapter) == INTF_SEL3_USB_Solo ||
		RT_GetInterfaceSelection(Adapter) == INTF_SEL4_USB_Combo)
	{		
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("In SwLedOff,LedAddr:%X LEDPIN=%d\n",REG_LEDCFG2, pLed->LedPin));
		LedCfg = rtw_read8(Adapter, REG_LEDCFG2);
		
		// 2009/10/23 MH Issau eed to move the LED GPIO from bit  0 to bit3.
		// 2009/10/26 MH Issau if tyhe device is 8c DID is 0x8176, we need to enable bit6 to
		// enable GPIO8 for controlling LED.	
		// 2010/07/02 Supprt Open-drain arrangement for controlling the LED. Added by Roger.
		//
		switch(pLed->LedPin)
		{

			case LED_PIN_GPIO0:
				break;

			case LED_PIN_LED0:
				if(pHalData->bLedOpenDrain == _TRUE)					
				{
					LedCfg &= 0x90; // Set to software control.				
					rtw_write8(Adapter, REG_LEDCFG2, (LedCfg|BIT3));				
					LedCfg = rtw_read8(Adapter, REG_MAC_PINMUX_CFG);
					LedCfg &= 0xFE;
					rtw_write8(Adapter, REG_MAC_PINMUX_CFG, LedCfg);									
				}
				else
				{
					rtw_write8(Adapter, REG_LEDCFG2, (LedCfg|BIT3|BIT5|BIT6));
				}
				break;

			case LED_PIN_LED1:
				LedCfg &= 0x0f; // Set to software control.
				rtw_write8(Adapter, REG_LEDCFG2, (LedCfg|BIT3));
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

			case LED_PIN_LED0:
			case LED_PIN_LED1:
			case LED_PIN_LED2:
				 if(IS_HARDWARE_TYPE_8821U(Adapter))
				 {
					LedCfg = rtw_read8(Adapter, REG_LEDCFG2);
					LedCfg &= 0x20; // Set to software control.
					rtw_write8(Adapter, REG_LEDCFG2, (LedCfg|BIT3|BIT5));
					//DBG_871X("8821 SwLedOn LED2 0x%x\n", rtw_read32(Adapter, REG_LEDCFG0));
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("8821  SwLedOff LED2 0x%x\n", rtw_read32(Adapter, REG_LEDCFG0)));
				 }

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
rtl8812au_InitSwLeds(
	_adapter	*padapter
	)
{
	struct led_priv *pledpriv = &(padapter->ledpriv);

	pledpriv->LedControlHandler = LedControlUSB;

	if(IS_HARDWARE_TYPE_8812(padapter)) {
		pledpriv->SwLedOn = SwLedOn_8812AU;
		pledpriv->SwLedOff = SwLedOff_8812AU;
	} else {
		pledpriv->SwLedOn = SwLedOn_8821AU;
		pledpriv->SwLedOff = SwLedOff_8821AU;
	}

	InitLed(padapter, &(pledpriv->SwLed0), LED_PIN_LED0);

	InitLed(padapter, &(pledpriv->SwLed1), LED_PIN_LED1);

	InitLed(padapter, &(pledpriv->SwLed2), LED_PIN_LED2);
}


//
//	Description:
//		DeInitialize all LED_819xUsb objects.
//
void
rtl8812au_DeInitSwLeds(
	_adapter	*padapter
	)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	DeInitLed( &(ledpriv->SwLed0) );
	DeInitLed( &(ledpriv->SwLed1) );
	DeInitLed( &(ledpriv->SwLed2) );
}

