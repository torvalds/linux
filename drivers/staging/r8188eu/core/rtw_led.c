// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#include "../include/drv_types.h"
#include "../include/rtw_led.h"

void BlinkWorkItemCallback(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct LED_871x *pLed = container_of(dwork, struct LED_871x, blink_work);
	BlinkHandler(pLed);
}

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
	pLed->bLedStartToLinkBlinkInProgress = false;
	pLed->bLedScanBlinkInProgress = false;
}

void InitLed871x(struct adapter *padapter, struct LED_871x *pLed, enum LED_PIN_871x LedPin)
{
	pLed->padapter = padapter;
	pLed->LedPin = LedPin;

	ResetLedStatus(pLed);

	INIT_DELAYED_WORK(&pLed->blink_work, BlinkWorkItemCallback);
}

void DeInitLed871x(struct LED_871x *pLed)
{
	cancel_delayed_work_sync(&pLed->blink_work);
	ResetLedStatus(pLed);
}

static void SwLedBlink1(struct LED_871x *pLed)
{
	struct adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bStopBlinking = false;

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);

	if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
		SwLedOff(padapter, pLed);
		ResetLedStatus(pLed);
		return;
	}

	switch (pLed->CurrLedState) {
	case LED_BLINK_SLOWLY:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
		break;
	case LED_BLINK_NORMAL:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
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
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
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
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = RTW_LED_OFF;
				else
					pLed->BlinkingLedState = RTW_LED_ON;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
			}
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_FASTER_INTVL);
		}
		break;
	case LED_BLINK_WPS:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
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
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);

			pLed->bLedWPSBlinkInProgress = false;
		} else {
			pLed->BlinkingLedState = RTW_LED_OFF;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_WPS_SUCESS_INTVL);
		}
		break;
	default:
		break;
	}
}

static void SwLedControlMode1(struct adapter *padapter, enum LED_CTL_MODE LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		if (!pLed->bLedNoLinkBlinkInProgress) {
			if (pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedBlinkInProgress = false;
			}

			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
		}
		break;
	case LED_CTL_LINK:
		if (!pLed->bLedLinkBlinkInProgress) {
			if (pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_NORMAL;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
		}
		break;
	case LED_CTL_SITE_SURVEY:
		if ((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED))) {
			;
		} else if (!pLed->bLedScanBlinkInProgress) {
			if (IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedScanBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SCAN;
			pLed->BlinkTimes = 24;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
		 }
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (!pLed->bLedBlinkInProgress) {
			if (pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedLinkBlinkInProgress = false;
			}
			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_TXRX;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_FASTER_INTVL);
		}
		break;
	case LED_CTL_START_WPS: /* wait until xinpin finish */
	case LED_CTL_START_WPS_BOTTON:
		 if (!pLed->bLedWPSBlinkInProgress) {
			if (pLed->bLedNoLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				cancel_delayed_work(&pLed->blink_work);
				pLed->bLedScanBlinkInProgress = false;
			}
			pLed->bLedWPSBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_WPS;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = RTW_LED_OFF;
			else
				pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
		 }
		break;
	case LED_CTL_STOP_WPS:
		if (pLed->bLedNoLinkBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedNoLinkBlinkInProgress = false;
		}
		if (pLed->bLedLinkBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedLinkBlinkInProgress = false;
		}
		if (pLed->bLedBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedScanBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress)
			cancel_delayed_work(&pLed->blink_work);
		else
			pLed->bLedWPSBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_WPS_STOP;
		if (pLed->bLedOn) {
			pLed->BlinkingLedState = RTW_LED_OFF;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_WPS_SUCESS_INTVL);
		} else {
			pLed->BlinkingLedState = RTW_LED_ON;
			schedule_delayed_work(&pLed->blink_work, 0);
		}
		break;
	case LED_CTL_STOP_WPS_FAIL:
		if (pLed->bLedWPSBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedWPSBlinkInProgress = false;
		}
		pLed->bLedNoLinkBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_SLOWLY;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		pLed->BlinkingLedState = RTW_LED_OFF;
		if (pLed->bLedNoLinkBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedNoLinkBlinkInProgress = false;
		}
		if (pLed->bLedLinkBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedLinkBlinkInProgress = false;
		}
		if (pLed->bLedBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedWPSBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			cancel_delayed_work(&pLed->blink_work);
			pLed->bLedScanBlinkInProgress = false;
		}
		SwLedOff(padapter, pLed);
		break;
	default:
		break;
	}
}

void BlinkHandler(struct LED_871x *pLed)
{
	struct adapter *padapter = pLed->padapter;

	if ((padapter->bSurpriseRemoved) || (padapter->bDriverStopped))
		return;

	SwLedBlink1(pLed);
}

void LedControl8188eu(struct adapter *padapter, enum LED_CTL_MODE LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct registry_priv *registry_par;

	if ((padapter->bSurpriseRemoved) || (padapter->bDriverStopped) ||
	    (!padapter->hw_init_completed))
		return;

	if (!ledpriv->bRegUseLed)
		return;

	registry_par = &padapter->registrypriv;
	if (!registry_par->led_enable)
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
