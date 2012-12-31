/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#ifndef __RTL8712_LED_H
#define __RTL8712_LED_H

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#define MSECS(t)        (HZ * ((t) / 1000) + (HZ * ((t) % 1000)) / 1000)

//================================================================================
// LED customization.
//================================================================================
typedef enum _LED_CTL_MODE {
	LED_CTL_POWER_ON = 1,
	LED_CTL_LINK = 2,
	LED_CTL_NO_LINK = 3,
	LED_CTL_TX = 4,
	LED_CTL_RX = 5,
	LED_CTL_SITE_SURVEY = 6,
	LED_CTL_POWER_OFF = 7,
	LED_CTL_START_TO_LINK = 8,
	LED_CTL_START_WPS = 9,
	LED_CTL_STOP_WPS = 10,
	LED_CTL_START_WPS_BOTTON = 11, //added for runtop
	LED_CTL_STOP_WPS_FAIL = 12, //added for ALPHA	
	LED_CTL_STOP_WPS_FAIL_OVERLAP = 13, //added for BELKIN
}LED_CTL_MODE;



#define IS_LED_WPS_BLINKING(_LED_871x)	(((PLED_871x)_LED_871x)->CurrLedState==LED_BLINK_WPS \
					|| ((PLED_871x)_LED_871x)->CurrLedState==LED_BLINK_WPS_STOP \
					|| ((PLED_871x)_LED_871x)->bLedWPSBlinkInProgress)

#define IS_LED_BLINKING(_LED_871x) 	(((PLED_871x)_LED_871x)->bLedWPSBlinkInProgress \
					||((PLED_871x)_LED_871x)->bLedScanBlinkInProgress)

typedef enum _LED_PIN_871x{
	LED_PIN_GPIO0,
	LED_PIN_LED0,
	LED_PIN_LED1
}LED_PIN_871x;

//================================================================================
// LED customization.
//================================================================================
typedef	enum _LED_STRATEGY_871x{
	SW_LED_MODE0, // SW control 1 LED via GPIO0. It is default option.
	SW_LED_MODE1, // 2 LEDs, through LED0 and LED1. For ALPHA.
	SW_LED_MODE2, // SW control 1 LED via GPIO0, customized for AzWave 8187 minicard.
	SW_LED_MODE3, // SW control 1 LED via GPIO0, customized for Sercomm Printer Server case.
	SW_LED_MODE4, //for Edimax / Belkin
	SW_LED_MODE5, //for Sercomm / Belkin
	SW_LED_MODE6, //for WNC / Corega
	HW_LED, // HW control 2 LEDs, LED0 and LED1 (there are 4 different control modes, see MAC.CONFIG1 for details.)
}LED_STRATEGY_871x, *PLED_STRATEGY_871x;

typedef struct _LED_871x{
	_adapter		*padapter;
	LED_PIN_871x		LedPin;	// Identify how to implement this SW led.
	u32			CurrLedState; // Current LED state.
	u8			bLedOn; // _TRUE if LED is ON, _FALSE if LED is OFF.

	u8			bSWLedCtrl;

	u8			bLedBlinkInProgress; // true if it is blinking, false o.w..
	// ALPHA, added by chiyoko, 20090106
	u8			bLedNoLinkBlinkInProgress;
	u8			bLedLinkBlinkInProgress;
	u8			bLedStartToLinkBlinkInProgress;
	u8			bLedScanBlinkInProgress;
	u8			bLedWPSBlinkInProgress;
	
	u32			BlinkTimes; // Number of times to toggle led state for blinking.
	u32			BlinkingLedState; // Next state for blinking, either LED_ON or LED_OFF are.

	_timer			BlinkTimer; // Timer object for led blinking.
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,5,0)
	_workitem		BlinkWorkItem; // Workitem used by BlinkTimer to manipulate H/W to blink LED. 
#endif
} LED_871x, *PLED_871x;

struct led_priv{
	/* add for led controll */
	LED_871x		SwLed0;
	LED_871x		SwLed1;
	LED_STRATEGY_871x	LedStrategy;
	u8			bRegUseLed;
	void (*LedControlHandler)(_adapter *padapter, LED_CTL_MODE LedAction);
	/* add for led controll */
};

//================================================================================
// Interface to manipulate LED objects.
//================================================================================
void InitSwLeds(_adapter *padapter);
void DeInitSwLeds(_adapter *padapter);
void LedControl871x(_adapter *padapter,LED_CTL_MODE LedAction);

#endif

