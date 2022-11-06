// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2012 Realtek Corporation. */

#include "../include/drv_types.h"
#include "../include/rtw_led.h"
#include "../include/rtl8188e_spec.h"

#define LED_BLINK_NO_LINK_INTVL			msecs_to_jiffies(1000)
#define LED_BLINK_LINK_INTVL			msecs_to_jiffies(500)
#define LED_BLINK_SCAN_INTVL			msecs_to_jiffies(180)
#define LED_BLINK_FASTER_INTVL			msecs_to_jiffies(50)
#define LED_BLINK_WPS_SUCESS_INTVL		msecs_to_jiffies(5000)

#define IS_LED_WPS_BLINKING(l) \
	((l)->CurrLedState == LED_BLINK_WPS || \
	(l)->CurrLedState == LED_BLINK_WPS_STOP || \
	(l)->bLedWPSBlinkInProgress)

static void ResetLedStatus(struct led_priv *pLed)
{
	pLed->CurrLedState = RTW_LED_OFF; /*  Current LED state. */
	pLed->bLedOn = false; /*  true if LED is ON, false if LED is OFF. */

	pLed->bLedBlinkInProgress = false; /*  true if it is blinking, false o.w.. */
	pLed->bLedWPSBlinkInProgress = false;

	pLed->BlinkTimes = 0; /*  Number of times to toggle led state for blinking. */

	pLed->bLedScanBlinkInProgress = false;
}

static void SwLedOn(struct led_priv *pLed)
{
	struct adapter *padapter = container_of(pLed, struct adapter, ledpriv);

	if (padapter->bDriverStopped)
		return;

	if (rtw_write8(padapter, REG_LEDCFG2, BIT(5)) != _SUCCESS)
		return;

	pLed->bLedOn = true;
}

static void SwLedOff(struct led_priv *pLed)
{
	struct adapter *padapter = container_of(pLed, struct adapter, ledpriv);

	if (padapter->bDriverStopped)
		return;

	if (rtw_write8(padapter, REG_LEDCFG2, BIT(5) | BIT(3)) != _SUCCESS)
		return;

	pLed->bLedOn = false;
}

static void blink_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct led_priv *pLed = container_of(dwork, struct led_priv, blink_work);
	struct adapter *padapter = container_of(pLed, struct adapter, ledpriv);
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
		SwLedOff(pLed);
		ResetLedStatus(pLed);
		return;
	}

	if (pLed->bLedOn)
		SwLedOff(pLed);
	else
		SwLedOn(pLed);

	switch (pLed->CurrLedState) {
	case LED_BLINK_SLOWLY:
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
		break;
	case LED_BLINK_NORMAL:
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
		break;
	case LED_BLINK_SCAN:
	case LED_BLINK_TXRX:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_BLINK_NORMAL;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
			} else {
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
			}
			pLed->bLedBlinkInProgress = false;
			pLed->bLedScanBlinkInProgress = false;
		} else {
			schedule_delayed_work(&pLed->blink_work,
					      pLed->CurrLedState == LED_BLINK_SCAN ?
					      LED_BLINK_SCAN_INTVL : LED_BLINK_FASTER_INTVL);
		}
		break;
	case LED_BLINK_WPS:
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
		break;
	case LED_BLINK_WPS_STOP:	/* WPS success */
		if (!pLed->bLedOn) {
			pLed->CurrLedState = LED_BLINK_NORMAL;
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);

			pLed->bLedWPSBlinkInProgress = false;
		} else {
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_WPS_SUCESS_INTVL);
		}
		break;
	default:
		break;
	}
}

void rtl8188eu_InitSwLeds(struct adapter *padapter)
{
	struct led_priv *pledpriv = &padapter->ledpriv;

	ResetLedStatus(pledpriv);
	INIT_DELAYED_WORK(&pledpriv->blink_work, blink_work);
}

void rtl8188eu_DeInitSwLeds(struct adapter *padapter)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;

	cancel_delayed_work_sync(&ledpriv->blink_work);
	ResetLedStatus(ledpriv);
	SwLedOff(ledpriv);
}

void rtw_led_control(struct adapter *padapter, enum LED_CTL_MODE LedAction)
{
	struct led_priv *pLed = &padapter->ledpriv;
	struct registry_priv *registry_par;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if (!padapter->hw_init_completed)
		return;

	if (!pLed->bRegUseLed)
		return;

	registry_par = &padapter->registrypriv;
	if (!registry_par->led_enable)
		return;

	switch (LedAction) {
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		if (pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
			return;

		cancel_delayed_work(&pLed->blink_work);

		pLed->bLedBlinkInProgress = false;

		pLed->CurrLedState = LED_BLINK_SLOWLY;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
		break;
	case LED_CTL_LINK:
		if (pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
			return;

		cancel_delayed_work(&pLed->blink_work);

		pLed->bLedBlinkInProgress = false;

		pLed->CurrLedState = LED_BLINK_NORMAL;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
		break;
	case LED_CTL_SITE_SURVEY:
		if ((pmlmepriv->LinkDetectInfo.bBusyTraffic) && (check_fwstate(pmlmepriv, _FW_LINKED)))
			return;

		if (pLed->bLedScanBlinkInProgress)
			return;

		if (IS_LED_WPS_BLINKING(pLed))
			return;

		cancel_delayed_work(&pLed->blink_work);

		pLed->bLedBlinkInProgress = false;
		pLed->bLedScanBlinkInProgress = true;

		pLed->CurrLedState = LED_BLINK_SCAN;
		pLed->BlinkTimes = 24;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (pLed->bLedBlinkInProgress)
			return;

		if (pLed->CurrLedState == LED_BLINK_SCAN || IS_LED_WPS_BLINKING(pLed))
			return;

		cancel_delayed_work(&pLed->blink_work);

		pLed->bLedBlinkInProgress = true;

		pLed->CurrLedState = LED_BLINK_TXRX;
		pLed->BlinkTimes = 2;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_FASTER_INTVL);
		break;
	case LED_CTL_START_WPS: /* wait until xinpin finish */
		if (pLed->bLedWPSBlinkInProgress)
			return;

		cancel_delayed_work(&pLed->blink_work);

		pLed->bLedBlinkInProgress = false;
		pLed->bLedScanBlinkInProgress = false;
		pLed->bLedWPSBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_WPS;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
		break;
	case LED_CTL_STOP_WPS:
		cancel_delayed_work(&pLed->blink_work);

		pLed->bLedBlinkInProgress = false;
		pLed->bLedScanBlinkInProgress = false;
		pLed->bLedWPSBlinkInProgress = true;

		pLed->CurrLedState = LED_BLINK_WPS_STOP;
		if (pLed->bLedOn) {
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_WPS_SUCESS_INTVL);
		} else {
			schedule_delayed_work(&pLed->blink_work, 0);
		}
		break;
	case LED_CTL_STOP_WPS_FAIL:
		cancel_delayed_work(&pLed->blink_work);
		pLed->bLedWPSBlinkInProgress = false;
		pLed->CurrLedState = LED_BLINK_SLOWLY;
		schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = RTW_LED_OFF;
		pLed->bLedBlinkInProgress = false;
		pLed->bLedWPSBlinkInProgress = false;
		pLed->bLedScanBlinkInProgress = false;
		cancel_delayed_work(&pLed->blink_work);
		SwLedOff(pLed);
		break;
	default:
		break;
	}
}
