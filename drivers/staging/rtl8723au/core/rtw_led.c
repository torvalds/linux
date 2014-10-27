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
 ******************************************************************************/

#include <drv_types.h>
#include <rtl8723a_led.h>

/*  */
/*	Description: */
/*		Callback function of LED BlinkTimer, */
/*		it just schedules to corresponding BlinkWorkItem/led_blink_hdl23a */
/*  */
static void BlinkTimerCallback(unsigned long data)
{
	struct led_8723a *pLed = (struct led_8723a *)data;
	struct rtw_adapter *padapter = pLed->padapter;

	/* DBG_8723A("%s\n", __func__); */

	if ((padapter->bSurpriseRemoved == true) || (padapter->bDriverStopped == true))
	{
		/* DBG_8723A("%s bSurpriseRemoved:%d, bDriverStopped:%d\n", __func__, padapter->bSurpriseRemoved, padapter->bDriverStopped); */
		return;
	}
	schedule_work(&pLed->BlinkWorkItem);
}

/*  */
/*	Description: */
/*		Callback function of LED BlinkWorkItem. */
/*		We dispatch acture LED blink action according to LedStrategy. */
/*  */
void BlinkWorkItemCallback23a(struct work_struct *work)
{
	struct led_8723a *pLed = container_of(work, struct led_8723a, BlinkWorkItem);
	BlinkHandler23a(pLed);
}

/*  */
/*	Description: */
/*		Reset status of led_8723a object. */
/*  */
void ResetLedStatus23a(struct led_8723a * pLed) {

	pLed->CurrLedState = RTW_LED_OFF; /*  Current LED state. */
	pLed->bLedOn = false; /*  true if LED is ON, false if LED is OFF. */

	pLed->bLedBlinkInProgress = false; /*  true if it is blinking, false o.w.. */
	pLed->bLedWPSBlinkInProgress = false;

	pLed->BlinkTimes = 0; /*  Number of times to toggle led state for blinking. */
	pLed->BlinkingLedState = LED_UNKNOWN; /*  Next state for blinking, either RTW_LED_ON or RTW_LED_OFF are. */

	pLed->bLedNoLinkBlinkInProgress = false;
	pLed->bLedLinkBlinkInProgress = false;
	pLed->bLedStartToLinkBlinkInProgress = false;
	pLed->bLedScanBlinkInProgress = false;
}

 /*  */
/*	Description: */
/*		Initialize an led_8723a object. */
/*  */
void
InitLed871x23a(struct rtw_adapter *padapter, struct led_8723a *pLed, enum led_pin_8723a LedPin)
{
	pLed->padapter = padapter;
	pLed->LedPin = LedPin;

	ResetLedStatus23a(pLed);

	setup_timer(&pLed->BlinkTimer, BlinkTimerCallback, (unsigned long)pLed);

	INIT_WORK(&pLed->BlinkWorkItem, BlinkWorkItemCallback23a);
}

/*  */
/*	Description: */
/*		DeInitialize an led_8723a object. */
/*  */
void
DeInitLed871x23a(struct led_8723a *pLed)
{
	cancel_work_sync(&pLed->BlinkWorkItem);
	del_timer_sync(&pLed->BlinkTimer);
	ResetLedStatus23a(pLed);
}

/*	Description: */
/*		Implementation of LED blinking behavior. */
/*		It toggle off LED and schedule corresponding timer if necessary. */

static void SwLedBlink(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	/*  Determine if we shall change LED state again. */
	pLed->BlinkTimes--;
	switch (pLed->CurrLedState) {

	case LED_BLINK_NORMAL:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		break;
	case LED_BLINK_StartToBlink:
		if (check_fwstate(pmlmepriv, _FW_LINKED) &&
		    check_fwstate(pmlmepriv, WIFI_STATION_STATE))
			bStopBlinking = true;
		if (check_fwstate(pmlmepriv, _FW_LINKED) &&
		    (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
		    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)))
			bStopBlinking = true;
		else if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		break;
	case LED_BLINK_WPS:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		break;
	default:
		bStopBlinking = true;
		break;
	}

	if (bStopBlinking) {
		if ((check_fwstate(pmlmepriv, _FW_LINKED)) && !pLed->bLedOn)
			SwLedOn23a(padapter, pLed);
		else if ((check_fwstate(pmlmepriv, _FW_LINKED)) &&  pLed->bLedOn)
			SwLedOff23a(padapter, pLed);

		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = false;
	} else {
		/*  Assign LED state to toggle. */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;

		/*  Schedule a timer to toggle LED state. */
		switch (pLed->CurrLedState) {
		case LED_BLINK_NORMAL:
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
			break;
		case LED_BLINK_SLOWLY:
		case LED_BLINK_StartToBlink:
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
			break;
		case LED_BLINK_WPS:
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_LONG_INTERVAL));
			break;
		default:
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
			break;
		}
	}
}

static void SwLedBlink1(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	unsigned long delay = 0;
	u8 bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
			 ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
			 ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
		SwLedOff23a(padapter, pLed);
		ResetLedStatus23a(pLed);
		return;
	}
	switch (pLed->CurrLedState) {
	case LED_BLINK_SLOWLY:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
		break;
	case LED_BLINK_NORMAL:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		delay = LED_BLINK_LINK_INTERVAL_ALPHA;
		break;
	case LED_BLINK_SCAN:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_LINK_INTERVAL_ALPHA;
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			} else {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
		}
		break;
	case LED_BLINK_TXRX:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_LINK_INTERVAL_ALPHA;
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			} else {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			}
			pLed->BlinkTimes = 0;
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			delay = LED_BLINK_FASTER_INTERVAL_ALPHA;
		}
		break;
	case LED_BLINK_WPS:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
		break;
	case LED_BLINK_WPS_STOP:	/* WPS success */
		if (pLed->BlinkingLedState == RTW_LED_ON)
			bStopBlinking = false;
		else
			bStopBlinking = true;
		if (bStopBlinking) {
			pLed->bLedLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_NORMAL;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			delay = LED_BLINK_LINK_INTERVAL_ALPHA;
			RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));

			pLed->bLedWPSBlinkInProgress = false;
		} else {
			pLed->BlinkingLedState = RTW_LED_OFF;
			delay = LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA;
		}
		break;
	default:
		break;
	}
	if (delay)
		mod_timer(&pLed->BlinkTimer, jiffies + msecs_to_jiffies(delay));
}

static void SwLedBlink2(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
			 ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
			 ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}
	switch (pLed->CurrLedState) {
	case LED_BLINK_SCAN:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
				SwLedOff23a(padapter, pLed);
			} else if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				SwLedOn23a(padapter, pLed);
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
					 ("stop scan blink CurrLedState %d\n",
					 pLed->CurrLedState));
			} else {
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				SwLedOff23a(padapter, pLed);
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
					 ("stop scan blink CurrLedState %d\n",
					 pLed->CurrLedState));
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
				SwLedOff23a(padapter, pLed);
			} else {
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer,
					  jiffies + msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
			}
		}
		break;
	case LED_BLINK_TXRX:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
				SwLedOff23a(padapter, pLed);
			} else if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = RTW_LED_ON;
				pLed->BlinkingLedState = RTW_LED_ON;
				SwLedOn23a(padapter, pLed);
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
					 ("stop CurrLedState %d\n", pLed->CurrLedState));

			} else {
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				SwLedOff23a(padapter, pLed);
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
					 ("stop CurrLedState %d\n", pLed->CurrLedState));
			}
			pLed->bLedBlinkInProgress = false;
		} else {
			if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
				SwLedOff23a(padapter, pLed);
			} else {
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer,
					  jiffies + msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
			}
		}
		break;
	default:
		break;
	}
}

static void SwLedBlink3(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	}
	else
	{
		if (pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch (pLed->CurrLedState)
	{
		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff23a(padapter, pLed);
				}
				else if (check_fwstate(pmlmepriv, _FW_LINKED)) {
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if (!pLed->bLedOn)
						SwLedOn23a(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
				} else {
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if (pLed->bLedOn)
						SwLedOff23a(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedScanBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff23a(padapter, pLed);
				}
				else
				{
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					mod_timer(&pLed->BlinkTimer,
						  jiffies + msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0)
			{
				bStopBlinking = true;
			}
			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff23a(padapter, pLed);
				} else if (check_fwstate(pmlmepriv,
							 _FW_LINKED)) {
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;

					if (!pLed->bLedOn)
						SwLedOn23a(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
				} else {
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;

					if (pLed->bLedOn)
						SwLedOff23a(padapter, pLed);

					RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedBlinkInProgress = false;
			}
			else
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff23a(padapter, pLed);
				}
				else
				{
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					mod_timer(&pLed->BlinkTimer,
						  jiffies + msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
				}
			}
			break;

		case LED_BLINK_WPS:
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
			break;

		case LED_BLINK_WPS_STOP:	/* WPS success */
			if (pLed->BlinkingLedState == RTW_LED_ON)
			{
				pLed->BlinkingLedState = RTW_LED_OFF;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA));
				bStopBlinking = false;
			} else {
				bStopBlinking = true;
			}

			if (bStopBlinking)
			{
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on)
				{
					SwLedOff23a(padapter, pLed);
				}
				else
				{
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					SwLedOn23a(padapter, pLed);
					RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
				}
				pLed->bLedWPSBlinkInProgress = false;
			}
			break;

		default:
			break;
	}
}

static void SwLedBlink4(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct led_8723a *pLed1 = &ledpriv->SwLed1;
	u8 bStopBlinking = false;
	unsigned long delay = 0;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
	{
		SwLedOn23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	if (!pLed1->bLedWPSBlinkInProgress && pLed1->BlinkingLedState == LED_UNKNOWN)
	{
		pLed1->BlinkingLedState = RTW_LED_OFF;
		pLed1->CurrLedState = RTW_LED_OFF;
		SwLedOff23a(padapter, pLed1);
	}

	switch (pLed->CurrLedState)
	{
		case LED_BLINK_SLOWLY:
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
			break;

		case LED_BLINK_StartToBlink:
			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				delay = LED_BLINK_SLOWLY_INTERVAL;
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_NORMAL_INTERVAL;
			}
			break;

		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0) {
				bStopBlinking = false;
			}

			if (bStopBlinking) {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					SwLedOff23a(padapter, pLed);
				} else {
					pLed->bLedNoLinkBlinkInProgress = false;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
				}
				pLed->bLedScanBlinkInProgress = false;
			} else {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					SwLedOff23a(padapter, pLed);
				} else {
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0) {
				bStopBlinking = true;
			}
			if (bStopBlinking) {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					SwLedOff23a(padapter, pLed);
				} else {
					pLed->bLedNoLinkBlinkInProgress = true;
					pLed->CurrLedState = LED_BLINK_SLOWLY;
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
				}
				pLed->bLedBlinkInProgress = false;
			} else {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					SwLedOff23a(padapter, pLed);
				} else {
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					delay = LED_BLINK_FASTER_INTERVAL_ALPHA;
				}
			}
			break;

		case LED_BLINK_WPS:
			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				delay = LED_BLINK_SLOWLY_INTERVAL;
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_NORMAL_INTERVAL;
			}
			break;

		case LED_BLINK_WPS_STOP:	/* WPS authentication fail */
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;

			delay = LED_BLINK_NORMAL_INTERVAL;
			break;

		case LED_BLINK_WPS_STOP_OVERLAP:	/* WPS session overlap */
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0) {
				if (pLed->bLedOn) {
					pLed->BlinkTimes = 1;
				} else {
					bStopBlinking = true;
				}
			}

			if (bStopBlinking) {
				pLed->BlinkTimes = 10;
				pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_LINK_INTERVAL_ALPHA;
			} else {
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;

				delay = LED_BLINK_NORMAL_INTERVAL;
			}
			break;

		default:
			break;
	}
	if (delay)
		mod_timer(&pLed->BlinkTimer, jiffies + msecs_to_jiffies(delay));

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink4 CurrLedState %d\n", pLed->CurrLedState));
}

static void SwLedBlink5(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;
	u8 bStopBlinking = false;
	unsigned long delay = 0;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	switch (pLed->CurrLedState)
	{
		case LED_BLINK_SCAN:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0) {
				bStopBlinking = true;
			}

			if (bStopBlinking) {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if (pLed->bLedOn)
						SwLedOff23a(padapter, pLed);
				} else {
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if (!pLed->bLedOn)
						delay = LED_BLINK_FASTER_INTERVAL_ALPHA;
				}

				pLed->bLedScanBlinkInProgress = false;
			} else {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					SwLedOff23a(padapter, pLed);
				} else {
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
				}
			}
			break;

		case LED_BLINK_TXRX:
			pLed->BlinkTimes--;
			if (pLed->BlinkTimes == 0) {
				bStopBlinking = true;
			}

			if (bStopBlinking) {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					pLed->CurrLedState = RTW_LED_OFF;
					pLed->BlinkingLedState = RTW_LED_OFF;
					if (pLed->bLedOn)
						SwLedOff23a(padapter, pLed);
				} else {
					pLed->CurrLedState = RTW_LED_ON;
					pLed->BlinkingLedState = RTW_LED_ON;
					if (!pLed->bLedOn)
						delay = LED_BLINK_FASTER_INTERVAL_ALPHA;
				}

				pLed->bLedBlinkInProgress = false;
			} else {
				if (padapter->pwrctrlpriv.rf_pwrstate != rf_on && padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) {
					SwLedOff23a(padapter, pLed);
				} else {
					if (pLed->bLedOn)
						pLed->BlinkingLedState = RTW_LED_OFF;
					else
						pLed->BlinkingLedState = RTW_LED_ON;
					delay = LED_BLINK_FASTER_INTERVAL_ALPHA;
				}
			}
			break;

		default:
			break;
	}

	if (delay)
		mod_timer(&pLed->BlinkTimer, jiffies + msecs_to_jiffies(delay));

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("SwLedBlink5 CurrLedState %d\n", pLed->CurrLedState));
}

static void SwLedBlink6(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		SwLedOn23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		SwLedOff23a(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}
	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("<==== blink6\n"));
}

/* ALPHA, added by chiyoko, 20090106 */
static void
SwLedControlMode1(struct rtw_adapter *padapter, enum led_ctl_mode LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct led_8723a *pLed = &ledpriv->SwLed0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	long delay = -1;

	switch (LedAction)
	{
		case LED_CTL_POWER_ON:
		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if (pLed->bLedNoLinkBlinkInProgress == false) {
				if (pLed->CurrLedState == LED_BLINK_SCAN ||
				    IS_LED_WPS_BLINKING(pLed)) {
					return;
				}
				if (pLed->bLedLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}

				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
			}
			break;

		case LED_CTL_LINK:
			if (pLed->bLedLinkBlinkInProgress == false) {
				if (pLed->CurrLedState == LED_BLINK_SCAN ||
				    IS_LED_WPS_BLINKING(pLed)) {
					return;
				}
				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_LINK_INTERVAL_ALPHA;
			}
			break;

		case LED_CTL_SITE_SURVEY:
			 if (pmlmepriv->LinkDetectInfo.bBusyTraffic &&
			     check_fwstate(pmlmepriv, _FW_LINKED))
				;
			 else if (pLed->bLedScanBlinkInProgress == false) {
				if (IS_LED_WPS_BLINKING(pLed))
					return;

				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
			 }
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if (pLed->bLedBlinkInProgress == false) {
				if (pLed->CurrLedState == LED_BLINK_SCAN ||
				    IS_LED_WPS_BLINKING(pLed)) {
					return;
				}
				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedLinkBlinkInProgress = false;
				}
				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_FASTER_INTERVAL_ALPHA;
			}
			break;

		case LED_CTL_START_WPS: /* wait until xinpin finish */
		case LED_CTL_START_WPS_BOTTON:
			if (pLed->bLedWPSBlinkInProgress == false) {
				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedScanBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedScanBlinkInProgress = false;
				}
				pLed->bLedWPSBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_WPS;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
			 }
			break;

		case LED_CTL_STOP_WPS:
			if (pLed->bLedNoLinkBlinkInProgress == true) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress == true) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress == true) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress == true) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
			} else {
				pLed->bLedWPSBlinkInProgress = true;
			}

			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				delay = LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA;
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				delay = 0;
			}
			break;

		case LED_CTL_STOP_WPS_FAIL:
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			delay = LED_BLINK_NO_LINK_INTERVAL_ALPHA;
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}

			SwLedOff23a(padapter, pLed);
			break;

		default:
			break;

	}

	if (delay != -1)
		mod_timer(&pLed->BlinkTimer, jiffies + msecs_to_jiffies(delay));

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led %d\n", pLed->CurrLedState));
}

 /* Arcadyan/Sitecom , added by chiyoko, 20090216 */
static void
SwLedControlMode2(struct rtw_adapter *padapter, enum led_ctl_mode LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct led_8723a *pLed = &ledpriv->SwLed0;
	long delay = -1;

	switch (LedAction) {
	case LED_CTL_SITE_SURVEY:
		 if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
			;
		 else if (pLed->bLedScanBlinkInProgress == false) {
			if (IS_LED_WPS_BLINKING(pLed))
				return;

			if (pLed->bLedBlinkInProgress == true) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedScanBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SCAN;
			pLed->BlinkTimes = 24;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
		 }
		 break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed->bLedBlinkInProgress == false &&
		    check_fwstate(pmlmepriv, _FW_LINKED)) {
			if (pLed->CurrLedState == LED_BLINK_SCAN ||
			    IS_LED_WPS_BLINKING(pLed)) {
				return;
			}

			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_TXRX;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			delay = LED_BLINK_FASTER_INTERVAL_ALPHA;
		}
		break;
	case LED_CTL_LINK:
		pLed->CurrLedState = RTW_LED_ON;
		pLed->BlinkingLedState = RTW_LED_ON;
		if (pLed->bLedBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}

		delay = 0;
		break;
	case LED_CTL_START_WPS: /* wait until xinpin finish */
	case LED_CTL_START_WPS_BOTTON:
		if (pLed->bLedWPSBlinkInProgress == false) {
			if (pLed->bLedBlinkInProgress == true) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress == true) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			pLed->bLedWPSBlinkInProgress = true;
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			delay = 0;
		 }
		break;
	case LED_CTL_STOP_WPS:
		pLed->bLedWPSBlinkInProgress = false;
		if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
			SwLedOff23a(padapter, pLed);
		} else {
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			delay = 0;
			RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
		}
		break;
	case LED_CTL_STOP_WPS_FAIL:
		pLed->bLedWPSBlinkInProgress = false;
		if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
			SwLedOff23a(padapter, pLed);
		} else {
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			delay = 0;
			RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
		}
		break;
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		if (!IS_LED_BLINKING(pLed))
		{
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			delay = 0;
		}
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		pLed->BlinkingLedState = RTW_LED_OFF;
		if (pLed->bLedBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}

		delay = 0;
		break;
	default:
		break;

	}

	if (delay != -1)
		mod_timer(&pLed->BlinkTimer, jiffies + msecs_to_jiffies(delay));

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
}

  /* COREGA, added by chiyoko, 20090316 */
static void
SwLedControlMode3(struct rtw_adapter *padapter, enum led_ctl_mode LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct led_8723a *pLed = &ledpriv->SwLed0;
	long delay = -1;

	switch (LedAction)
	{
		case LED_CTL_SITE_SURVEY:
			if (pmlmepriv->LinkDetectInfo.bBusyTraffic)
				;
			else if (pLed->bLedScanBlinkInProgress == false) {
				if (IS_LED_WPS_BLINKING(pLed))
					return;

				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if (pLed->bLedBlinkInProgress == false &&
			    check_fwstate(pmlmepriv, _FW_LINKED)) {
				if (pLed->CurrLedState == LED_BLINK_SCAN ||
				    IS_LED_WPS_BLINKING(pLed)) {
					return;
				}

				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay =  LED_BLINK_FASTER_INTERVAL_ALPHA;
			}
			break;

		case LED_CTL_LINK:
			if (IS_LED_WPS_BLINKING(pLed))
				return;

			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;
			if (pLed->bLedBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}

			delay = 0;
			break;

		case LED_CTL_START_WPS: /* wait until xinpin finish */
		case LED_CTL_START_WPS_BOTTON:
			if (pLed->bLedWPSBlinkInProgress == false) {
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedScanBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedScanBlinkInProgress = false;
				}
				pLed->bLedWPSBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_WPS;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				delay = LED_BLINK_SCAN_INTERVAL_ALPHA;
			}
			break;

		case LED_CTL_STOP_WPS:
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			} else {
				pLed->bLedWPSBlinkInProgress = true;
			}

			pLed->CurrLedState = LED_BLINK_WPS_STOP;
			if (pLed->bLedOn) {
				pLed->BlinkingLedState = RTW_LED_OFF;
				delay = LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA;
			} else {
				pLed->BlinkingLedState = RTW_LED_ON;
				delay = 0;
			}

			break;

		case LED_CTL_STOP_WPS_FAIL:
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			delay = 0;
			break;

		case LED_CTL_START_TO_LINK:
		case LED_CTL_NO_LINK:
			if (!IS_LED_BLINKING(pLed))
			{
				pLed->CurrLedState = RTW_LED_OFF;
				pLed->BlinkingLedState = RTW_LED_OFF;
				delay = 0;
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;
			if (pLed->bLedBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}

			delay = 0;
			break;

		default:
			break;

	}

	if (delay != -1)
		mod_timer(&pLed->BlinkTimer, jiffies + msecs_to_jiffies(delay));

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
}

 /* Edimax-Belkin, added by chiyoko, 20090413 */
static void
SwLedControlMode4(struct rtw_adapter *padapter, enum led_ctl_mode LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct led_8723a *pLed = &ledpriv->SwLed0;
	struct led_8723a *pLed1 = &ledpriv->SwLed1;

	switch (LedAction)
	{
		case LED_CTL_START_TO_LINK:
			if (pLed1->bLedWPSBlinkInProgress) {
				pLed1->bLedWPSBlinkInProgress = false;
				del_timer_sync(&pLed1->BlinkTimer);

				pLed1->BlinkingLedState = RTW_LED_OFF;
				pLed1->CurrLedState = RTW_LED_OFF;

				if (pLed1->bLedOn)
					mod_timer(&pLed->BlinkTimer, jiffies);
			}

			if (pLed->bLedStartToLinkBlinkInProgress == false) {
				if (pLed->CurrLedState == LED_BLINK_SCAN ||
				    IS_LED_WPS_BLINKING(pLed)) {
					return;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}

				pLed->bLedStartToLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_StartToBlink;
				if (pLed->bLedOn) {
					pLed->BlinkingLedState = RTW_LED_OFF;
					mod_timer(&pLed->BlinkTimer,
						  jiffies + msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
				} else {
					pLed->BlinkingLedState = RTW_LED_ON;
					mod_timer(&pLed->BlinkTimer,
						  jiffies + msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
				}
			}
			break;

		case LED_CTL_LINK:
		case LED_CTL_NO_LINK:
			/* LED1 settings */
			if (LedAction == LED_CTL_LINK) {
				if (pLed1->bLedWPSBlinkInProgress) {
					pLed1->bLedWPSBlinkInProgress = false;
					del_timer_sync(&pLed1->BlinkTimer);

					pLed1->BlinkingLedState = RTW_LED_OFF;
					pLed1->CurrLedState = RTW_LED_OFF;

					if (pLed1->bLedOn)
						mod_timer(&pLed->BlinkTimer, jiffies);
				}
			}

			if (pLed->bLedNoLinkBlinkInProgress == false) {
				if (pLed->CurrLedState == LED_BLINK_SCAN ||
				    IS_LED_WPS_BLINKING(pLed)) {
					return;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}

				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
			}
			break;

		case LED_CTL_SITE_SURVEY:
			if (pmlmepriv->LinkDetectInfo.bBusyTraffic &&
			    check_fwstate(pmlmepriv, _FW_LINKED))
				;
			else if (pLed->bLedScanBlinkInProgress == false) {
				if (IS_LED_WPS_BLINKING(pLed))
					return;

				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if (pLed->bLedBlinkInProgress == false) {
				if (pLed->CurrLedState == LED_BLINK_SCAN ||
				    IS_LED_WPS_BLINKING(pLed)) {
					return;
				}
				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
			}
			break;

		case LED_CTL_START_WPS: /* wait until xinpin finish */
		case LED_CTL_START_WPS_BOTTON:
			if (pLed1->bLedWPSBlinkInProgress) {
				pLed1->bLedWPSBlinkInProgress = false;
				del_timer_sync(&pLed1->BlinkTimer);

				pLed1->BlinkingLedState = RTW_LED_OFF;
				pLed1->CurrLedState = RTW_LED_OFF;

				if (pLed1->bLedOn)
					mod_timer(&pLed->BlinkTimer, jiffies);
			}

			if (pLed->bLedWPSBlinkInProgress == false) {
				if (pLed->bLedNoLinkBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedNoLinkBlinkInProgress = false;
				}
				if (pLed->bLedBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				if (pLed->bLedScanBlinkInProgress == true) {
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedScanBlinkInProgress = false;
				}
				pLed->bLedWPSBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_WPS;
				if (pLed->bLedOn)
				{
					pLed->BlinkingLedState = RTW_LED_OFF;
					mod_timer(&pLed->BlinkTimer, jiffies +
						  msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
				} else {
					pLed->BlinkingLedState = RTW_LED_ON;
					mod_timer(&pLed->BlinkTimer, jiffies +
						  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
				}
			}
			break;

		case LED_CTL_STOP_WPS:	/* WPS connect success */
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
			break;

		case LED_CTL_STOP_WPS_FAIL:		/* WPS authentication fail */
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));

			/* LED1 settings */
			if (pLed1->bLedWPSBlinkInProgress)
				del_timer_sync(&pLed1->BlinkTimer);
			else
				pLed1->bLedWPSBlinkInProgress = true;

			pLed1->CurrLedState = LED_BLINK_WPS_STOP;
			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));

			break;

		case LED_CTL_STOP_WPS_FAIL_OVERLAP:	/* WPS session overlap */
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));

			/* LED1 settings */
			if (pLed1->bLedWPSBlinkInProgress)
				del_timer_sync(&pLed1->BlinkTimer);
			else
				pLed1->bLedWPSBlinkInProgress = true;

			pLed1->CurrLedState = LED_BLINK_WPS_STOP_OVERLAP;
			pLed1->BlinkTimes = 10;
			if (pLed1->bLedOn)
				pLed1->BlinkingLedState = RTW_LED_OFF;
			else
				pLed1->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));

			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedWPSBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			if (pLed->bLedStartToLinkBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedStartToLinkBlinkInProgress = false;
			}

			if (pLed1->bLedWPSBlinkInProgress) {
				del_timer_sync(&pLed1->BlinkTimer);
				pLed1->bLedWPSBlinkInProgress = false;
			}

			pLed1->BlinkingLedState = LED_UNKNOWN;
			SwLedOff23a(padapter, pLed);
			SwLedOff23a(padapter, pLed1);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led %d\n", pLed->CurrLedState));
}

 /* Sercomm-Belkin, added by chiyoko, 20090415 */
static void
SwLedControlMode5(struct rtw_adapter *padapter, enum led_ctl_mode LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct led_8723a *pLed = &ledpriv->SwLed0;

	switch (LedAction)
	{
		case LED_CTL_POWER_ON:
		case LED_CTL_NO_LINK:
		case LED_CTL_LINK:	/* solid blue */
			pLed->CurrLedState = RTW_LED_ON;
			pLed->BlinkingLedState = RTW_LED_ON;

			mod_timer(&pLed->BlinkTimer, jiffies);
			break;

		case LED_CTL_SITE_SURVEY:
			if (pmlmepriv->LinkDetectInfo.bBusyTraffic &&
			    check_fwstate(pmlmepriv, _FW_LINKED))
				;
			else if (pLed->bLedScanBlinkInProgress == false)
			{
				if (pLed->bLedBlinkInProgress == true)
				{
					del_timer_sync(&pLed->BlinkTimer);
					pLed->bLedBlinkInProgress = false;
				}
				pLed->bLedScanBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SCAN;
				pLed->BlinkTimes = 24;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
			}
			break;

		case LED_CTL_TX:
		case LED_CTL_RX:
			if (pLed->bLedBlinkInProgress == false) {
				if (pLed->CurrLedState == LED_BLINK_SCAN) {
					return;
				}
				pLed->bLedBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_TXRX;
				pLed->BlinkTimes = 2;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
			}
			break;

		case LED_CTL_POWER_OFF:
			pLed->CurrLedState = RTW_LED_OFF;
			pLed->BlinkingLedState = RTW_LED_OFF;

			if (pLed->bLedBlinkInProgress) {
				del_timer_sync(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}

			SwLedOff23a(padapter, pLed);
			break;

		default:
			break;

	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("Led %d\n", pLed->CurrLedState));
}

 /* WNC-Corega, added by chiyoko, 20090902 */
static void SwLedControlMode6(struct rtw_adapter *padapter,
			      enum led_ctl_mode LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct led_8723a *pLed0 = &ledpriv->SwLed0;

	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		del_timer_sync(&pLed0->BlinkTimer);
		pLed0->CurrLedState = RTW_LED_ON;
		pLed0->BlinkingLedState = RTW_LED_ON;
		mod_timer(&pLed0->BlinkTimer, jiffies);
		break;
	case LED_CTL_POWER_OFF:
		SwLedOff23a(padapter, pLed0);
		break;
	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("ledcontrol 6 Led %d\n", pLed0->CurrLedState));
}

/*  */
/*	Description: */
/*		Handler function of LED Blinking. */
/*		We dispatch acture LED blink action according to LedStrategy. */
/*  */
void BlinkHandler23a(struct led_8723a *pLed)
{
	struct rtw_adapter *padapter = pLed->padapter;
	struct led_priv *ledpriv = &padapter->ledpriv;

	/* DBG_8723A("%s (%s:%d)\n", __func__, current->comm, current->pid); */

	if ((padapter->bSurpriseRemoved) || (padapter->bDriverStopped))
		return;

	switch (ledpriv->LedStrategy) {
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
	case SW_LED_MODE6:
		SwLedBlink6(pLed);
		break;
	default:
		break;
	}
}

void
LedControl871x23a(struct rtw_adapter *padapter, enum led_ctl_mode LedAction) {
	struct led_priv *ledpriv = &padapter->ledpriv;

	if ((padapter->bSurpriseRemoved == true) ||
	    (padapter->bDriverStopped == true) ||
	    (padapter->hw_init_completed == false)) {
		return;
	}

	if (ledpriv->bRegUseLed == false)
		return;

	/* if (!priv->up) */
	/*	return; */

	/* if (priv->bInHctTest) */
	/*	return; */

	if ((padapter->pwrctrlpriv.rf_pwrstate != rf_on &&
	     padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) &&
	    (LedAction == LED_CTL_TX || LedAction == LED_CTL_RX ||
	     LedAction == LED_CTL_SITE_SURVEY ||
	     LedAction == LED_CTL_LINK ||
	     LedAction == LED_CTL_NO_LINK ||
	     LedAction == LED_CTL_POWER_ON)) {
		return;
	}

	switch (ledpriv->LedStrategy) {
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
	case SW_LED_MODE6:
		SwLedControlMode6(padapter, LedAction);
		break;
	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("LedStrategy:%d, LedAction %d\n", ledpriv->LedStrategy, LedAction));
}
