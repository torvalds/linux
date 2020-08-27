// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include <drv_types.h>
#include "rtw_led.h"

/*  */
/*	Description: */
/*		Callback function of LED BlinkTimer, */
/*		it just schedules to corresponding BlinkWorkItem/led_blink_hdl */
/*  */
static void BlinkTimerCallback(struct timer_list *t)
{
	struct LED_871x *pLed = from_timer(pLed, t, BlinkTimer);
	struct adapter *padapter = pLed->padapter;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;

	schedule_work(&pLed->BlinkWorkItem);
}

/*  */
/*	Description: */
/*		Callback function of LED BlinkWorkItem. */
/*  */
void BlinkWorkItemCallback(struct work_struct *work)
{
	struct LED_871x *pLed = container_of(work, struct LED_871x,
						BlinkWorkItem);

	blink_handler(pLed);
}

/*  */
/*	Description: */
/*		Reset status of LED_871x object. */
/*  */
void ResetLedStatus(struct LED_871x *pLed)
{
	pLed->CurrLedState = RTW_LED_OFF; /*  Current LED state. */
	pLed->bLedOn = false; /*  true if LED is ON, false if LED is OFF. */

	pLed->bLedBlinkInProgress = false; /*  true if it is blinking, false o.w.. */
	pLed->bLedWPSBlinkInProgress = false;

	pLed->BlinkTimes = 0; /*  Number of times to toggle led state for blinking. */
	pLed->BlinkingLedState = LED_UNKNOWN; /*  Next state for blinking, either RTW_LED_ON or RTW_LED_OFF are. */

	pLed->bLedNoLinkBlinkInProgress = false;
	pLed->bLedLinkBlinkInProgress = false;
	pLed->bLedScanBlinkInProgress = false;
}

/*Description: */
/*		Initialize an LED_871x object. */
void InitLed871x(struct adapter *padapter, struct LED_871x *pLed)
{
	pLed->padapter = padapter;

	ResetLedStatus(pLed);

	timer_setup(&pLed->BlinkTimer, BlinkTimerCallback, 0);

	INIT_WORK(&pLed->BlinkWorkItem, BlinkWorkItemCallback);
}

/*  */
/*	Description: */
/*		DeInitialize an LED_871x object. */
/*  */
void DeInitLed871x(struct LED_871x *pLed)
{
	cancel_work_sync(&pLed->BlinkWorkItem);
	del_timer_sync(&pLed->BlinkTimer);
	ResetLedStatus(pLed);
}

/*  */
/*	Description: */
/*		Implementation of LED blinking behavior. */
/*		It toggle off LED and schedule corresponding timer if necessary. */
/*  */

static void SwLedBlink1(struct LED_871x *pLed)
{
	struct adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON) {
		sw_led_on(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
			 ("Blinktimes (%d): turn on\n", pLed->BlinkTimes));
	} else {
		sw_led_off(padapter, pLed);
		RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
			 ("Blinktimes (%d): turn off\n", pLed->BlinkTimes));
	}

	if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
		sw_led_off(padapter, pLed);
		ResetLedStatus(pLed);
		return;
	}

	switch (pLed->CurrLedState) {
	case LED_BLINK_SLOWLY:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		break;
	case LED_BLINK_NORMAL:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
		break;
	case LED_BLINK_SCAN:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_BLINK_TXRX:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
				RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));
			}
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
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
		if (pLed->BlinkingLedState != RTW_LED_ON) {
			pLed->bLedLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_NORMAL;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
			RT_TRACE(_module_rtl8712_led_c_, _drv_info_, ("CurrLedState %d\n", pLed->CurrLedState));

			pLed->bLedWPSBlinkInProgress = false;
		} else {
			pLed->BlinkingLedState = RTW_LED_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA));
		}
		break;
	default:
		break;
	}
}

 /* ALPHA, added by chiyoko, 20090106 */
static void SwLedControlMode1(struct adapter *padapter, enum LED_CTL_MODE LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct LED_871x *pLed = &ledpriv->sw_led;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		if (pLed->bLedNoLinkBlinkInProgress)
			break;
		if (pLed->CurrLedState == LED_BLINK_SCAN ||
		    IS_LED_WPS_BLINKING(pLed))
			return;
		if (pLed->bLedLinkBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedLinkBlinkInProgress = false;
		}
		if (pLed->bLedBlinkInProgress) {
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
		break;
	case LED_CTL_LINK:
		if (pLed->bLedLinkBlinkInProgress)
			break;
		if (pLed->CurrLedState == LED_BLINK_SCAN ||
		    IS_LED_WPS_BLINKING(pLed))
			return;
		if (pLed->bLedNoLinkBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedNoLinkBlinkInProgress = false;
		}
		if (pLed->bLedBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		pLed->bLedLinkBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_NORMAL;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
		break;
	case LED_CTL_SITE_SURVEY:
		if (pmlmepriv->LinkDetectInfo.bBusyTraffic &&
		    check_fwstate(pmlmepriv, _FW_LINKED))
			break;
		if (pLed->bLedScanBlinkInProgress)
			break;
		if (IS_LED_WPS_BLINKING(pLed))
			return;
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
		pLed->bLedScanBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_SCAN;
		pLed->BlinkTimes = 24;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed->bLedBlinkInProgress)
			break;
		if (pLed->CurrLedState == LED_BLINK_SCAN ||
		    IS_LED_WPS_BLINKING(pLed))
			return;
		if (pLed->bLedNoLinkBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedNoLinkBlinkInProgress = false;
		}
		if (pLed->bLedLinkBlinkInProgress) {
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
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		break;
	case LED_CTL_START_WPS: /* wait until xinpin finish */
	case LED_CTL_START_WPS_BOTTON:
		if (pLed->bLedWPSBlinkInProgress)
			break;
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
		if (pLed->bLedScanBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		pLed->bLedWPSBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_WPS;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		break;
	case LED_CTL_STOP_WPS:
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
		if (pLed->bLedScanBlinkInProgress) {
			del_timer_sync(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress)
			del_timer_sync(&pLed->BlinkTimer);
		else
			pLed->bLedWPSBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_WPS_STOP;
		if (pLed->bLedOn) {
			pLed->BlinkingLedState = RTW_LED_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA));
		} else {
			pLed->BlinkingLedState = RTW_LED_ON;
			mod_timer(&pLed->BlinkTimer,
				  jiffies + msecs_to_jiffies(0));
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
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
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
		sw_led_off(padapter, pLed);
		break;
	default:
		break;
	}

	RT_TRACE(_module_rtl8712_led_c_, _drv_info_,
		 ("Led %d\n", pLed->CurrLedState));
}

void blink_handler(struct LED_871x *pLed)
{
	struct adapter *padapter = pLed->padapter;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;

	SwLedBlink1(pLed);
}

void led_control_8188eu(struct adapter *padapter, enum LED_CTL_MODE LedAction)
{
	if (padapter->bSurpriseRemoved || padapter->bDriverStopped ||
	    !padapter->hw_init_completed)
		return;

	if ((padapter->pwrctrlpriv.rf_pwrstate != rf_on &&
	     padapter->pwrctrlpriv.rfoff_reason > RF_CHANGE_BY_PS) &&
	    (LedAction == LED_CTL_TX || LedAction == LED_CTL_RX ||
	     LedAction == LED_CTL_SITE_SURVEY ||
	     LedAction == LED_CTL_LINK ||
	     LedAction == LED_CTL_NO_LINK ||
	     LedAction == LED_CTL_POWER_ON))
		return;

	SwLedControlMode1(padapter, LedAction);
}
