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

static void ResetLedStatus(struct LED_871x *pLed)
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

static void SwLedOn(struct adapter *padapter, struct LED_871x *pLed)
{
	u8	LedCfg;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;

	LedCfg = rtw_read8(padapter, REG_LEDCFG2);
	rtw_write8(padapter, REG_LEDCFG2, (LedCfg & 0xf0) | BIT(5) | BIT(6)); /*  SW control led0 on. */
	pLed->bLedOn = true;
}

static void SwLedOff(struct adapter *padapter, struct LED_871x *pLed)
{
	u8	LedCfg;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		goto exit;

	LedCfg = rtw_read8(padapter, REG_LEDCFG2);/* 0x4E */

	LedCfg &= 0x90; /*  Set to software control. */
	rtw_write8(padapter, REG_LEDCFG2, (LedCfg | BIT(3)));
	LedCfg = rtw_read8(padapter, REG_MAC_PINMUX_CFG);
	LedCfg &= 0xFE;
	rtw_write8(padapter, REG_MAC_PINMUX_CFG, LedCfg);
exit:
	pLed->bLedOn = false;
}

static void blink_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct LED_871x *pLed = container_of(dwork, struct LED_871x, blink_work);
	struct adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if ((padapter->bSurpriseRemoved) || (padapter->bDriverStopped))
		return;

	if (padapter->pwrctrlpriv.rf_pwrstate != rf_on) {
		SwLedOff(padapter, pLed);
		ResetLedStatus(pLed);
		return;
	}

	/*  Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == RTW_LED_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);

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
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			schedule_delayed_work(&pLed->blink_work, LED_BLINK_SCAN_INTVL);
		}
		break;
	case LED_BLINK_TXRX:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = RTW_LED_OFF;
		else
			pLed->BlinkingLedState = RTW_LED_ON;
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_LINK_INTVL);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				schedule_delayed_work(&pLed->blink_work, LED_BLINK_NO_LINK_INTVL);
			}
			pLed->bLedBlinkInProgress = false;
		} else {
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
		if (pLed->BlinkingLedState != RTW_LED_ON) {
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

void rtl8188eu_InitSwLeds(struct adapter *padapter)
{
	struct led_priv *pledpriv = &padapter->ledpriv;
	struct LED_871x *pLed = &pledpriv->SwLed0;

	pLed->padapter = padapter;
	ResetLedStatus(pLed);
	INIT_DELAYED_WORK(&pLed->blink_work, blink_work);
}

void rtl8188eu_DeInitSwLeds(struct adapter *padapter)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;

	cancel_delayed_work_sync(&pLed->blink_work);
	ResetLedStatus(pLed);
	SwLedOff(padapter, pLed);
}

void rtw_led_control(struct adapter *padapter, enum LED_CTL_MODE LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct registry_priv *registry_par;
	struct LED_871x *pLed = &ledpriv->SwLed0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	if ((padapter->bSurpriseRemoved) || (padapter->bDriverStopped) ||
	    (!padapter->hw_init_completed))
		return;

	if (!ledpriv->bRegUseLed)
		return;

	registry_par = &padapter->registrypriv;
	if (!registry_par->led_enable)
		return;

	switch (LedAction) {
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
