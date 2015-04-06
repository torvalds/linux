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
 *
 ******************************************************************************/

#include "drv_types.h"
#include "rtl8192d_hal.h"
#include <rtl8192d_led.h>

/*  */
/*	Constant. */
/*  */

/*  */
/*  Default LED behavior. */
/*  */
#define LED_BLINK_NORMAL_INTERVAL	100
#define LED_BLINK_SLOWLY_INTERVAL	200
#define LED_BLINK_LONG_INTERVAL	400

#define LED_BLINK_NO_LINK_INTERVAL_ALPHA		1000
#define LED_BLINK_LINK_INTERVAL_ALPHA			500		/* 500 */
#define LED_BLINK_SCAN_INTERVAL_ALPHA		180	/* 150 */
#define LED_BLINK_FASTER_INTERVAL_ALPHA		50
#define LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA	5000

/*  */
/*  LED object. */
/*  */

/*  */
/*	Prototype of protected function. */
/*  */

static void BlinkTimerCallback(unsigned long data);

static void
BlinkWorkItemCallback(
	struct work_struct *work
	);

static void
ResetLedStatus(struct LED_871X *	pLed) {
	pLed->CurrLedState = RTW_LED_OFF; /*  Current LED state. */
	pLed->bLedOn = false; /*  true if LED is ON, false if LED is OFF. */

	pLed->bLedBlinkInProgress = false; /*  true if it is blinking, false o.w.. */
	pLed->bLedNoLinkBlinkInProgress = false;
	pLed->bLedLinkBlinkInProgress = false;
	pLed->bLedStartToLinkBlinkInProgress = false;
	pLed->bLedScanBlinkInProgress = false;
	pLed->bLedWPSBlinkInProgress = false;
	pLed->BlinkTimes = 0; /*  Number of times to toggle led state for blinking. */
	pLed->BlinkingLedState = LED_UNKNOWN; /*  Next state for blinking, either RTW_LED_ON or RTW_LED_OFF are. */
}

/*  */
/*  LED_819xUsb routines. */
/*  */

/*  */
/*	Description: */
/*		Initialize an struct LED_871X object. */
/*  */

static void InitLed871x(struct rtw_adapter *padapter,
	struct LED_871X *		pLed,
	enum LED_PIN_871X	LedPin
	)
{
	pLed->padapter = padapter;

	pLed->LedPin = LedPin;

	pLed->CurrLedState = RTW_LED_OFF;
	pLed->bLedOn = false;

	pLed->bLedBlinkInProgress = false;
	pLed->BlinkTimes = 0;
	pLed->BlinkingLedState = LED_UNKNOWN;

	_init_timer(&(pLed->BlinkTimer), padapter->pnetdev, BlinkTimerCallback, pLed);

	_init_workitem(&(pLed->BlinkWorkItem), BlinkWorkItemCallback, pLed);
}

/*  */
/*	Description: */
/*		DeInitialize an struct LED_871X object. */
/*  */
static void DeInitLed871x(struct LED_871X *pLed)
{
	/* call _cancel_workitem_sync(&(pLed->BlinkWorkItem)) */
    /* before _cancel_timer_ex(&(pLed->BlinkTimer)) to */
    /* avoid led timer restarting when driver is removed */

	_cancel_workitem_sync(&(pLed->BlinkWorkItem));
	_cancel_timer_ex(&(pLed->BlinkTimer));
	/*  We should reset bLedBlinkInProgress if we cancel the LedControlTimer, 2005.03.10, by rcnjko. */
	ResetLedStatus(pLed);
}

/*  */
/*	Description: */
/*		Turn on LED according to LedPin specified. */
/*  */

static void SwLedOn(struct rtw_adapter *padapter, struct LED_871X *pLed)
{
	u8	LedCfg;

	if ((padapter->bSurpriseRemoved == true) || (padapter->bDriverStopped == true))
	{
		return;
	}

	switch (pLed->LedPin)
	{
		case LED_PIN_GPIO0:
			break;

		case LED_PIN_LED0:
			LedCfg = rtw_read8(padapter, REG_LEDCFG2);
			rtw_write8(padapter, REG_LEDCFG2, LedCfg&0xf0); /*  SW control led0 on. */

			break;

		case LED_PIN_LED1:
			LedCfg = rtw_read8(padapter, (REG_LEDCFG2 + 1));
			rtw_write8(padapter, (REG_LEDCFG2 + 1), LedCfg&0x0f); /*  SW control led1 on. */

			break;

		default:
			break;
	}

	pLed->bLedOn = true;
}

/*  */
/*	Description: */
/*		Turn off LED according to LedPin specified. */
/*  */
static void SwLedOff(struct rtw_adapter *padapter, struct LED_871X *pLed)
{
	u8	LedCfg;

	if ((padapter->bSurpriseRemoved == true) || (padapter->bDriverStopped == true))
	{
             return;
	}

	switch (pLed->LedPin)
	{
		case LED_PIN_GPIO0:
			break;

		case LED_PIN_LED0:
			LedCfg = rtw_read8(padapter, REG_LEDCFG2);
			LedCfg &= 0xf0; /*  Set to software control. */
			rtw_write8(padapter, REG_LEDCFG2, (LedCfg|BIT3));

			break;

		case LED_PIN_LED1:
			LedCfg = rtw_read8(padapter, (REG_LEDCFG2+1));
			LedCfg &= 0x0f; /*  Set to software control. */
			rtw_write8(padapter, (REG_LEDCFG2+1), (LedCfg|BIT3));

			break;

		default:
			break;
	}

	pLed->bLedOn = false;
}

/*  */
/*	Description: */
/*		Implementation of LED blinking behavior. */
/*		It toggle off LED and schedule corresponding timer if necessary. */
/*  */
static void SwLedBlink(struct LED_871X *pLed)
{
	struct rtw_adapter			*padapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	u8				bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	/*  Determine if we shall change LED state again. */
	pLed->BlinkTimes--;
	switch (pLed->CurrLedState)
	{

	case LED_BLINK_NORMAL:
		if (pLed->BlinkTimes == 0)
		{
			bStopBlinking = true;
		}
		break;

	case LED_BLINK_StartToBlink:
		if (check_fwstate(pmlmepriv, _FW_LINKED) && check_fwstate(pmlmepriv, WIFI_STATION_STATE))
		{
			bStopBlinking = true;
		}
		if (check_fwstate(pmlmepriv, _FW_LINKED) &&
			(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
		{
			bStopBlinking = true;
		}
		else if (pLed->BlinkTimes == 0)
		{
			bStopBlinking = true;
		}
		break;

	case LED_BLINK_WPS:
		if (pLed->BlinkTimes == 0)
		{
			bStopBlinking = true;
		}
		break;

	default:
		bStopBlinking = true;
		break;

	}

	if (bStopBlinking)
	{
		if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
		{
			SwLedOff(padapter, pLed);
		}
		else if ((check_fwstate(pmlmepriv, _FW_LINKED)== true) && (pLed->bLedOn == false))
		{
			SwLedOn(padapter, pLed);
		}
		else if ((check_fwstate(pmlmepriv, _FW_LINKED)== true) &&  pLed->bLedOn == true)
		{
			SwLedOff(padapter, pLed);
		}

		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = false;
	}
	else
	{
		/*  Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/*  Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState)
		{
		case LED_BLINK_NORMAL:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		case LED_BLINK_SLOWLY:
		case LED_BLINK_StartToBlink:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			break;

		case LED_BLINK_WPS:
			{
				if (pLed->BlinkingLedState == RTW_LED_ON)
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
				else
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
			}
			break;

		default:
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			break;
		}
	}
}

static void SwLedBlink1(struct LED_871X *pLed)
{
	struct rtw_adapter				*padapter = pLed->padapter;
	struct hal_data_8192du		*pHalData = GET_HAL_DATA(padapter);
	struct led_priv		*ledpriv = &(padapter->ledpriv);
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	struct LED_871X *			pLed1 = &(ledpriv->SwLed1);
	u8					bStopBlinking = false;

	if (pHalData->CustomerID == RT_CID_819x_CAMEO)
		pLed = &(ledpriv->SwLed1);

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	if (pHalData->CustomerID == RT_CID_DEFAULT)
	{
		if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
		{
			if (!pLed1->bSWLedCtrl)
			{
				SwLedOn(padapter, pLed1);
				pLed1->bSWLedCtrl = true;
			}
			else if (!pLed1->bLedOn)
				SwLedOn(padapter, pLed1);
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (): turn on pLed1\n"));
		}
		else
		{
			if (!pLed1->bSWLedCtrl)
			{
				SwLedOff(padapter, pLed1);
				pLed1->bSWLedCtrl = true;
			}
			else if (pLed1->bLedOn)
				SwLedOff(padapter, pLed1);
			RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (): turn off pLed1\n"));
		}
	}

	switch (pLed->CurrLedState)
	{
		case LED_BLINK_SLOWLY:
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			break;

		case LED_BLINK_NORMAL:
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
			break;

		case LED_SCAN_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
				{
					pLed->bLedLinkBlinkInProgress = true;
					pLed->CurrLedState = LED_BLINK_NORMAL;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));

				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== false)
				{
					pLed->bLedNoLinkBlinkInProgress = true;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedScanBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_TXRX_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}
			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
				{
					pLed->bLedLinkBlinkInProgress = true;
					pLed->CurrLedState = LED_BLINK_NORMAL;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== false)
				{
					pLed->bLedNoLinkBlinkInProgress = true;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->BlinkTimes = 0;
				pLed->bLedBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_WPS:
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			break;

		case LED_BLINK_WPS_STOP:	/* WPS success */
			if (pLed->BlinkingLedState == RTW_LED_ON)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
				bStopBlinking = false;
			}
			else
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->bLedLinkBlinkInProgress = true;
					pLed->CurrLedState = LED_BLINK_NORMAL;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedWPSBlinkInProgress = false;
			}
			break;

		default:
			break;
	}
}

static void SwLedBlink2(struct LED_871X *pLed)
{
	struct rtw_adapter				*padapter = pLed->padapter;
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);
	u8					bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch (pLed->CurrLedState)
	{
		case LED_SCAN_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop scan blink CurrLedState %d\n", pLed->CurrLedState));

				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== false)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					SwLedOff(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop scan blink CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedScanBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_TXRX_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}
			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop CurrLedState %d\n", pLed->CurrLedState));

				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== false)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					SwLedOff(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("stop CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			break;
	}
}

static void SwLedBlink3(struct LED_871X *pLed)
{
	struct rtw_adapter			*padapter = pLed->padapter;
	struct mlme_priv	*pmlmepriv = &(padapter->mlmepriv);
	u8				bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		if (pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch (pLed->CurrLedState)
	{
		case LED_SCAN_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if (!pLed->bLedOn)
						SwLedOn(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== false)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if (pLed->bLedOn)
						SwLedOff(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedScanBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_TXRX_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}
			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== true)
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;

					if (!pLed->bLedOn)
						SwLedOn(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)== false)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;

					if (pLed->bLedOn)
						SwLedOff(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_WPS:
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			break;

		case LED_BLINK_WPS_STOP:	/* WPS success */
			if (pLed->BlinkingLedState == RTW_LED_ON)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
				bStopBlinking = false;
			}
			else
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedWPSBlinkInProgress = false;
			}
			break;

		default:
			break;
	}
}

static void SwLedBlink4(struct LED_871X *pLed)
{
	struct rtw_adapter			*padapter = pLed->padapter;
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct LED_871X *		pLed1 = &(ledpriv->SwLed1);
	u8				bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	if (!pLed1->bLedWPSBlinkInProgress && pLed1->BlinkingLedState == LED_UNKNOWN)
	{
		pLed1->BlinkingLedState = RTW_LED_OFF;
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff(padapter, pLed1);
	}

	switch (pLed->CurrLedState)
	{
		case LED_BLINK_SLOWLY:
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			break;

		case LED_BLINK_StartToBlink:
			if (pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		case LED_SCAN_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = false;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->bLedNoLinkBlinkInProgress = false;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}
				pLed->bLedScanBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_TXRX_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}
			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->bLedNoLinkBlinkInProgress = true;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
				}
				pLed->bLedBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_BLINK_WPS:
			if (pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		case LED_BLINK_WPS_STOP:	/* WPS authentication fail */
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			break;

		case LED_BLINK_WPS_STOP_OVERLAP:	/* WPS session overlap */
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				if (pLed->bLedOn)
				{
					pLed->BlinkTimes = 1;
				}
				else
				{
					bStopBlinking = true;
				}
			}

			if (bStopBlinking)
			{
				pLed->BlinkTimes = 10;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
			}
			else
			{
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;

				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink4 CurrLedState %d\n", pLed->CurrLedState));

}

static void SwLedBlink5(struct LED_871X *pLed)
{
	struct rtw_adapter			*padapter = pLed->padapter;
	u8				bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		SwLedOff(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch (pLed->CurrLedState)
	{
		case LED_SCAN_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if (pLed->bLedOn)
						SwLedOff(padapter, pLed);
				}
				else
				{		pLed->CurrLedState = RTW_LED_ON;
						pLed->BlinkingLedState = RTW_LED_ON;
						if (!pLed->bLedOn)
							_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}

				pLed->bLedScanBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
				}
			}
			break;

		case LED_TXRX_BLINK:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if (pLed->bLedOn)
						SwLedOff(padapter, pLed);
				}
				else
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if (!pLed->bLedOn)
						_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}

				pLed->bLedBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS)
				{
					SwLedOff(padapter, pLed);
				}
				else
				{
					 if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
				}
			}
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("SwLedBlink5 CurrLedState %d\n", pLed->CurrLedState));

}

/*  */
/*	Description: */
/*		Callback function of LED BlinkTimer, */
/*		it just schedules to corresponding BlinkWorkItem. */
/*  */
static void
BlinkTimerCallback(
	unsigned long data
	)
{
	struct LED_871X *	 pLed = (struct LED_871X *)data;
	struct rtw_adapter		*padapter = pLed->padapter;

	 if ((padapter->bSurpriseRemoved == true) || (padapter->bDriverStopped == true))
       {
             return;
       }

	_set_workitem(&(pLed->BlinkWorkItem));
}

/*  */
/*	Description: */
/*		Callback function of LED BlinkWorkItem. */
/*		We dispatch acture LED blink action according to LedStrategy. */
/*  */
static void BlinkWorkItemCallback(struct work_struct *work)
{
	struct LED_871X *	 pLed = container_of(work, struct LED_871X, BlinkWorkItem);
	struct led_priv	*ledpriv = &(pLed->padapter->ledpriv);
	struct rtw_adapter		*padapter = pLed->padapter;

	 if ((padapter->bSurpriseRemoved == true) || (padapter->bDriverStopped == true))
       {
             return;
       }

	switch (ledpriv->LedStrategy)
	{
		case SW_LED_MODE0:
			SwLedBlink(pLed);
			break;

		case SW_LED_MODE1:
			SwLedBlink1(pLed);
			break;

		case SW_LED_MODE2:
			SwLedBlink2(pLed);
			break;

		case SW_LED_MODE3:
			SwLedBlink3(pLed);
			break;

		case SW_LED_MODE4:
			SwLedBlink4(pLed);
			break;

		case SW_LED_MODE5:
			SwLedBlink5(pLed);
			break;

		default:
			SwLedBlink(pLed);
			break;
	}
}

/*  */
/*  Default LED behavior. */
/*  */

/*  */
/*	Description: */
/*		Implement each led action for SW_LED_MODE0. */
/*		This is default strategy. */
/*  */
static void SwLedControlMode0(
	struct rtw_adapter		*padapter,
	enum LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct LED_871X *	pLed = &(ledpriv->SwLed1);

	/*  Decide led state */
	switch (LedAction)
	{
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed->bLedBlinkInProgress == false)
		{
			pLed->bLedBlinkInProgress = true;

			pLed->CurrLedState = LED_BLINK_NORMAL;
			pLed->BlinkTimes = 2;

			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
		}
		break;

	case LED_CTL_START_TO_LINK:
		if (pLed->bLedBlinkInProgress == false)
		{
			pLed->bLedBlinkInProgress = true;

			pLed->CurrLedState = LED_BLINK_StartToBlink;
			pLed->BlinkTimes = 24;

			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
		}
		else
		{
			pLed->CurrLedState = LED_BLINK_StartToBlink;
		}
		break;

	case LED_CTL_LINK:
		pLed->CurrLedState = RTW_LED_ON;
		if (pLed->bLedBlinkInProgress == false)
		{
			SwLedOn(padapter, pLed);
		}
		break;

	case LED_CTL_NO_LINK:
		pLed->CurrLedState = RTW_LED_OFF;
		if (pLed->bLedBlinkInProgress == false)
		{
			SwLedOff(padapter, pLed);
		}
		break;

	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		pLed->BlinkingLedState = RTW_LED_OFF;

		if (pLed->bLedBlinkInProgress)
		{
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = false;
		}
		SwLedOff(padapter, pLed);
		break;

	case LED_CTL_START_WPS:
		if (pLed->bLedBlinkInProgress == false || pLed->CurrLedState == RTW_LED_ON)
		{
			pLed->bLedBlinkInProgress = true;

			pLed->CurrLedState = LED_BLINK_WPS;
			pLed->BlinkTimes = 20;

			if (pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LONG_INTERVAL);
			}
		}
		break;

	case LED_CTL_STOP_WPS:
		if (pLed->bLedBlinkInProgress)
		{
			pLed->CurrLedState = RTW_LED_OFF;
			_cancel_timer_ex(&(pLed->BlinkTimer));
			pLed->bLedBlinkInProgress = false;
		}
		break;

	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));
}

 /* ALPHA, added by chiyoko, 20090106 */
static void SwLedControlMode1(
	struct rtw_adapter		*padapter,
	enum LED_CTL_MODE		LedAction
)
{
	struct hal_data_8192du		*pHalData = GET_HAL_DATA(padapter);
	struct led_priv		*ledpriv = &(padapter->ledpriv);
	struct LED_871X *			pLed = &(ledpriv->SwLed0);
	struct mlme_priv		*pmlmepriv = &(padapter->mlmepriv);

	if (pHalData->CustomerID == RT_CID_819x_CAMEO)
		pLed = &(ledpriv->SwLed1);

	switch (LedAction)
	{
		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if (pLed->bLedNoLinkBlinkInProgress == false)
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if (pLed->bLedLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}

				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_LINK:
			if (pLed->bLedLinkBlinkInProgress == false)
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if (pLed->bLedNoLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_LINK_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_SITE_SURVEY:
			 if ((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)== true))
				;
			 else if (pLed->bLedScanBlinkInProgress ==false)
			 {
				if (IS_LED_WPS_BLINKING(pLed))
					return;

				if (pLed->bLedNoLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					 pLed->bLedLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_SCAN_BLINK;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			 }
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if (pLed->bLedBlinkInProgress ==false)
			{
                            if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
                            {
					return;
                            }
                            if (pLed->bLedNoLinkBlinkInProgress == true)
                            {
                                _cancel_timer_ex(&(pLed->BlinkTimer));
                                pLed->bLedNoLinkBlinkInProgress = false;
                            }
                            if (pLed->bLedLinkBlinkInProgress == true)
                            {
                                _cancel_timer_ex(&(pLed->BlinkTimer));
                                pLed->bLedLinkBlinkInProgress = false;
                            }
                            pLed->bLedBlinkInProgress = true;
                            pLed->CurrLedState = LED_TXRX_BLINK;
                            pLed->BlinkTimes = 2;
                            if (pLed->bLedOn)
                                pLed->BlinkingLedState = RTW_LED_OFF;
                            else
                                pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_START_WPS: /* wait until xinpin finish */
		case LED_CTL_START_WPS_BOTTON:
			 if (pLed->bLedWPSBlinkInProgress ==false)
			 {
				if (pLed->bLedNoLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					 pLed->bLedLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedScanBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = false;
				}
				pLed->bLedWPSBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_WPS;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			 }
			break;

		case LED_CTL_STOP_WPS:
			if (pLed->bLedNoLinkBlinkInProgress == true)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress == true)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				 pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress ==true)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress ==true)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
			}
			else
			{
				pLed->bLedWPSBlinkInProgress = true;
			}

			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if (pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		case LED_CTL_STOP_WPS_FAIL:
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if (pLed->bLedNoLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = false;
			}

			SwLedOff(padapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));
}

 /* Arcadyan/Sitecom , added by chiyoko, 20090216 */
static void SwLedControlMode2(
	struct rtw_adapter				*padapter,
	enum LED_CTL_MODE		LedAction
)
{
	struct led_priv	 *ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct LED_871X *		pLed = &(ledpriv->SwLed0);

	switch (LedAction)
	{
		case LED_CTL_SITE_SURVEY:
			 if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
				;
			 else if (pLed->bLedScanBlinkInProgress ==false)
			 {
				if (IS_LED_WPS_BLINKING(pLed))
					return;

				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_SCAN_BLINK;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			 }
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if ((pLed->bLedBlinkInProgress ==false) && (check_fwstate(pmlmepriv, _FW_LINKED)== true))
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}

				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_TXRX_BLINK;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_LINK:
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			if (pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = false;
			}

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_WPS: /* wait until xinpin finish */
		case LED_CTL_START_WPS_BOTTON:
			if (pLed->bLedWPSBlinkInProgress ==false)
			{
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedScanBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = false;
				}
				pLed->bLedWPSBlinkInProgress = true;
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
			 }
			break;

		case LED_CTL_STOP_WPS:
			pLed->bLedWPSBlinkInProgress = false;
			if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
			{
				SwLedOff(padapter, pLed);
			}
			else
			{
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
			}
			break;

		case LED_CTL_STOP_WPS_FAIL:
			pLed->bLedWPSBlinkInProgress = false;
			if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
			{
				SwLedOff(padapter, pLed);
			}
			else
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), 0);
				RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
			}
			break;

		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if (!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if (pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
}

  /* COREGA, added by chiyoko, 20090316 */
static void SwLedControlMode3(
	struct rtw_adapter				*padapter,
	enum LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct LED_871X *		pLed = &(ledpriv->SwLed0);

	switch (LedAction)
	{
		case LED_CTL_SITE_SURVEY:
			if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
				;
			else if (pLed->bLedScanBlinkInProgress ==false)
			{
				if (IS_LED_WPS_BLINKING(pLed))
					return;

				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_SCAN_BLINK;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if ((pLed->bLedBlinkInProgress ==false) && (check_fwstate(pmlmepriv, _FW_LINKED)== true))
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}

				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_TXRX_BLINK;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_LINK:
			if (IS_LED_WPS_BLINKING(pLed))
				return;

			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			if (pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = false;
			}

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_WPS: /* wait until xinpin finish */
		case LED_CTL_START_WPS_BOTTON:
			if (pLed->bLedWPSBlinkInProgress ==false)
			{
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedScanBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = false;
				}
				pLed->bLedWPSBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_WPS;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_STOP_WPS:
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}
			else
			{
				pLed->bLedWPSBlinkInProgress = true;
			}

			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if (pLed->bLedOn)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA);
			}
			else
			{
				pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), 0);
			}

			break;

		case LED_CTL_STOP_WPS_FAIL:
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if (!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				_set_timer(&(pLed->BlinkTimer), 0);
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if (pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("CurrLedState %d\n", pLed->CurrLedState));
}

 /* Edimax-Belkin, added by chiyoko, 20090413 */
static void SwLedControlMode4(
	struct rtw_adapter				*padapter,
	enum LED_CTL_MODE		LedAction
)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct LED_871X *		pLed = &(ledpriv->SwLed0);
	struct LED_871X *		pLed1 = &(ledpriv->SwLed1);

	switch (LedAction)
	{
		case LED_CTL_START_TO_LINK:
			if (pLed1->bLedWPSBlinkInProgress)
			{
				pLed1->bLedWPSBlinkInProgress = false;
				_cancel_timer_ex(&(pLed1->BlinkTimer));

				pLed1->BlinkingLedState = RTW_LED_OFF;
				pLed1->CurrLedState = RTW_LED_OFF;

				if (pLed1->bLedOn)
					_set_timer(&(pLed->BlinkTimer), 0);
			}

			if (pLed->bLedStartToLinkBlinkInProgress == false)
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedNoLinkBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = false;
				}

				pLed->bLedStartToLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_StartToBlink;
				if (pLed->bLedOn)
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
				}
			}
			break;

		case LED_CTL_LINK:
		case LED_CTL_NO_LINK:
			/* LED1 settings */
			if (LedAction == LED_CTL_LINK)
			{
				if (pLed1->bLedWPSBlinkInProgress)
				{
					pLed1->bLedWPSBlinkInProgress = false;
					_cancel_timer_ex(&(pLed1->BlinkTimer));

					pLed1->BlinkingLedState = RTW_LED_OFF;
					pLed1->CurrLedState = RTW_LED_OFF;

					if (pLed1->bLedOn)
						_set_timer(&(pLed->BlinkTimer), 0);
				}
			}

			if (pLed->bLedNoLinkBlinkInProgress == false)
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}

				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_SITE_SURVEY:
			if ((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)== true))
				;
			else if (pLed->bLedScanBlinkInProgress ==false)
			{
				if (IS_LED_WPS_BLINKING(pLed))
					return;

				if (pLed->bLedNoLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_SCAN_BLINK;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if (pLed->bLedBlinkInProgress ==false)
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK || IS_LED_WPS_BLINKING(pLed))
				{
					return;
				}
				if (pLed->bLedNoLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_TXRX_BLINK;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_START_WPS: /* wait until xinpin finish */
		case LED_CTL_START_WPS_BOTTON:
			if (pLed1->bLedWPSBlinkInProgress)
			{
				pLed1->bLedWPSBlinkInProgress = false;
				_cancel_timer_ex(&(pLed1->BlinkTimer));

				pLed1->BlinkingLedState = RTW_LED_OFF;
				pLed1->CurrLedState = RTW_LED_OFF;

				if (pLed1->bLedOn)
					_set_timer(&(pLed->BlinkTimer), 0);
			}

			if (pLed->bLedWPSBlinkInProgress ==false)
			{
				if (pLed->bLedNoLinkBlinkInProgress == true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedScanBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedScanBlinkInProgress = false;
				}
				pLed->bLedWPSBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_WPS;
				if (pLed->bLedOn)
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_SLOWLY_INTERVAL);
				}
				else
				{
					pLed->BlinkingLedState = RTW_LED_ON;
					_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);
				}
			}
			break;

		case LED_CTL_STOP_WPS:	/* WPS connect success */
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);

			break;

		case LED_CTL_STOP_WPS_FAIL:		/* WPS authentication fail */
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);

			/* LED1 settings */
			if (pLed1->bLedWPSBlinkInProgress)
				_cancel_timer_ex(&(pLed1->BlinkTimer));
			else
				pLed1->bLedWPSBlinkInProgress = true;

			pLed1->CurrLedState = LED_BLINK_WPS_STOP;
			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);

			break;

		case LED_CTL_STOP_WPS_FAIL_OVERLAP:	/* WPS session overlap */
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NO_LINK_INTERVAL_ALPHA);

			/* LED1 settings */
			if (pLed1->bLedWPSBlinkInProgress)
				_cancel_timer_ex(&(pLed1->BlinkTimer));
			else
				pLed1->bLedWPSBlinkInProgress = true;

			pLed1->CurrLedState = LED_BLINK_WPS_STOP_OVERLAP;
			pLed1->BlinkTimes = 10;
			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			_set_timer(&(pLed->BlinkTimer), LED_BLINK_NORMAL_INTERVAL);

			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if (pLed->bLedNoLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedWPSBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedScanBlinkInProgress = false;
			}
			if (pLed->bLedStartToLinkBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedStartToLinkBlinkInProgress = false;
			}

			if (pLed1->bLedWPSBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed1->BlinkTimer));
				pLed1->bLedWPSBlinkInProgress = false;
			}

			pLed1->BlinkingLedState = LED_UNKNOWN;
			SwLedOff(padapter, pLed);
			SwLedOff(padapter, pLed1);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));
}

/* Sercomm-Belkin, added by chiyoko, 20090415 */
static void SwLedControlMode5(
	struct rtw_adapter				*padapter,
	enum LED_CTL_MODE		LedAction
)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
	struct led_priv	*ledpriv = &(padapter->ledpriv);
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct LED_871X *		pLed = &(ledpriv->SwLed0);

	if (pHalData->EEPROMCustomerID == RT_CID_819x_CAMEO)
		pLed = &(ledpriv->SwLed1);

	switch (LedAction)
	{
		case LED_CTL_POWER_ON:
		case LED_CTL_NO_LINK:
		case LED_CTL_LINK:	/* solid blue */
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;

			_set_timer(&(pLed->BlinkTimer), 0);
			break;

		case LED_CTL_SITE_SURVEY:
			if ((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)== true))
				;
			else if (pLed->bLedScanBlinkInProgress ==false)
			{
				if (pLed->bLedBlinkInProgress ==true)
				{
					_cancel_timer_ex(&(pLed->BlinkTimer));
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_SCAN_BLINK;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_SCAN_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if (pLed->bLedBlinkInProgress ==false)
			{
				if (pLed->CurrLedState == LED_SCAN_BLINK)
				{
					return;
				}
				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_TXRX_BLINK;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				_set_timer(&(pLed->BlinkTimer), LED_BLINK_FASTER_INTERVAL_ALPHA);
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if (pLed->bLedBlinkInProgress)
			{
				_cancel_timer_ex(&(pLed->BlinkTimer));
				pLed->bLedBlinkInProgress = false;
			}

			SwLedOff(padapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("Led %d\n", pLed->CurrLedState));
}

/*  */
/*	Description: */
/*		Dispatch LED action according to pHalData->LedStrategy. */
/*  */
static void LedControl871x(
	struct rtw_adapter				*padapter,
	enum LED_CTL_MODE		LedAction
	)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);

       if ((padapter->bSurpriseRemoved == true) || (padapter->bDriverStopped == true))
       {
             return;
       }

	if (ledpriv->bRegUseLed == false)
		return;

	if (	padapter->pwrctrlpriv.rf_pwrstate != rf_on &&
		(LedAction == LED_CTL_TX || LedAction == LED_CTL_RX ||
		 LedAction == LED_CTL_SITE_SURVEY ||
		 LedAction == LED_CTL_LINK ||
		 LedAction == LED_CTL_NO_LINK ||
		 LedAction == LED_CTL_POWER_ON))
	{
		return;
	}

	switch (ledpriv->LedStrategy)
	{
		case SW_LED_MODE0:
			break;

		case SW_LED_MODE1:
			SwLedControlMode1(padapter, LedAction);
			break;

		case SW_LED_MODE2:
			SwLedControlMode2(padapter, LedAction);
			break;

		case SW_LED_MODE3:
			SwLedControlMode3(padapter, LedAction);
			break;

		case SW_LED_MODE4:
			SwLedControlMode4(padapter, LedAction);
			break;

		case SW_LED_MODE5:
			SwLedControlMode5(padapter, LedAction);
			break;

		default:
			break;
	}

	RT_TRACE(_module_rtl8712_led_c_,_drv_info_,("LedStrategy:%d, LedAction %d\n", ledpriv->LedStrategy,LedAction));
}

/*  */
/*  Interface to manipulate LED objects. */
/*  */

/*  */
/*	Description: */
/*		Initialize all struct LED_871X objects. */
/*  */
void rtl8192du_InitSwLeds(struct rtw_adapter	*padapter)
{
	struct led_priv *pledpriv = &(padapter->ledpriv);

	pledpriv->LedControlHandler = LedControl871x;

	InitLed871x(padapter, &(pledpriv->SwLed0), LED_PIN_LED0);

	InitLed871x(padapter,&(pledpriv->SwLed1), LED_PIN_LED1);
}

/*  */
/*	Description: */
/*		DeInitialize all LED_819xUsb objects. */
/*  */
void rtl8192du_DeInitSwLeds(struct rtw_adapter *padapter)
{
	struct led_priv	*ledpriv = &(padapter->ledpriv);

	DeInitLed871x(&(ledpriv->SwLed0));
	DeInitLed871x(&(ledpriv->SwLed1));
}
