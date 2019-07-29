// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * rtl8712_led.c
 *
 * Copyright(c) 2007 - 2010  Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#include "drv_types.h"

/*===========================================================================
 *	Constant.
 *===========================================================================

 *
 * Default LED behavior.
 */
#define LED_BLINK_NORMAL_INTERVAL	100
#define LED_BLINK_SLOWLY_INTERVAL	200
#define LED_BLINK_LONG_INTERVAL	400

#define LED_BLINK_NO_LINK_INTERVAL_ALPHA	1000
#define LED_BLINK_LINK_INTERVAL_ALPHA		500
#define LED_BLINK_SCAN_INTERVAL_ALPHA		180
#define LED_BLINK_FASTER_INTERVAL_ALPHA		50
#define LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA	5000

/*===========================================================================
 * LED object.
 *===========================================================================
 */
enum _LED_STATE_871x {
	LED_UNKNOWN = 0,
	LED_STATE_ON = 1,
	LED_STATE_OFF = 2,
	LED_BLINK_NORMAL = 3,
	LED_BLINK_SLOWLY = 4,
	LED_POWER_ON_BLINK = 5,
	LED_SCAN_BLINK = 6, /* LED is blinking during scanning period,
			     * the # of times to blink is depend on time
			     * for scanning.
			     */
	LED_NO_LINK_BLINK = 7, /* LED is blinking during no link state. */
	LED_BLINK_StartToBlink = 8,/* Customized for Sercomm Printer
				    * Server case
				    */
	LED_BLINK_WPS = 9,	/* LED is blinkg during WPS communication */
	LED_TXRX_BLINK = 10,
	LED_BLINK_WPS_STOP = 11,	/*for ALPHA */
	LED_BLINK_WPS_STOP_OVERLAP = 12,	/*for BELKIN */
};

/*===========================================================================
 *	Prototype of protected function.
 *===========================================================================
 */
static void BlinkTimerCallback(struct timer_list *t);

static void BlinkWorkItemCallback(struct work_struct *work);
/*===========================================================================
 * LED_819xUsb routines.
 *===========================================================================
 *
 *
 *
 *	Description:
 *		Initialize an LED_871x object.
 */
static void InitLed871x(struct _adapter *padapter, struct LED_871x *pLed,
		 enum LED_PIN_871x	LedPin)
{
	pLed->padapter = padapter;
	pLed->LedPin = LedPin;
	pLed->CurrLedState = LED_STATE_OFF;
	pLed->bLedOn = false;
	pLed->bLedBlinkInProgress = false;
	pLed->BlinkTimes = 0;
	pLed->BlinkingLedState = LED_UNKNOWN;
	timer_setup(&pLed->BlinkTimer, BlinkTimerCallback, 0);
	INIT_WORK(&pLed->BlinkWorkItem, BlinkWorkItemCallback);
}

/*
 *	Description:
 *		DeInitialize an LED_871x object.
 */
static void DeInitLed871x(struct LED_871x *pLed)
{
	del_timer_sync(&pLed->BlinkTimer);
	/* We should reset bLedBlinkInProgress if we cancel
	 * the LedControlTimer,
	 */
	pLed->bLedBlinkInProgress = false;
}

/*
 *	Description:
 *		Turn on LED according to LedPin specified.
 */
static void SwLedOn(struct _adapter *padapter, struct LED_871x *pLed)
{
	u8	LedCfg;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;
	LedCfg = r8712_read8(padapter, LEDCFG);
	switch (pLed->LedPin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		/* SW control led0 on.*/
		r8712_write8(padapter, LEDCFG, LedCfg & 0xf0);
		break;
	case LED_PIN_LED1:
		/* SW control led1 on.*/
		r8712_write8(padapter, LEDCFG, LedCfg & 0x0f);
		break;
	default:
		break;
	}
	pLed->bLedOn = true;
}

/*
 *	Description:
 *		Turn off LED according to LedPin specified.
 */
static void SwLedOff(struct _adapter *padapter, struct LED_871x *pLed)
{
	u8	LedCfg;

	if (padapter->bSurpriseRemoved || padapter->bDriverStopped)
		return;
	LedCfg = r8712_read8(padapter, LEDCFG);
	switch (pLed->LedPin) {
	case LED_PIN_GPIO0:
		break;
	case LED_PIN_LED0:
		LedCfg &= 0xf0; /* Set to software control.*/
		r8712_write8(padapter, LEDCFG, (LedCfg | BIT(3)));
		break;
	case LED_PIN_LED1:
		LedCfg &= 0x0f; /* Set to software control.*/
		r8712_write8(padapter, LEDCFG, (LedCfg | BIT(7)));
		break;
	default:
		break;
	}
	pLed->bLedOn = false;
}

/*===========================================================================
 * Interface to manipulate LED objects.
 *===========================================================================
 *
 *	Description:
 *		Initialize all LED_871x objects.
 */
void r8712_InitSwLeds(struct _adapter *padapter)
{
	struct led_priv	*pledpriv = &padapter->ledpriv;

	pledpriv->LedControlHandler = LedControl871x;
	InitLed871x(padapter, &pledpriv->SwLed0, LED_PIN_LED0);
	InitLed871x(padapter, &pledpriv->SwLed1, LED_PIN_LED1);
}

/*	Description:
 *		DeInitialize all LED_819xUsb objects.
 */
void r8712_DeInitSwLeds(struct _adapter *padapter)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;

	DeInitLed871x(&ledpriv->SwLed0);
	DeInitLed871x(&ledpriv->SwLed1);
}

/*	Description:
 *		Implementation of LED blinking behavior.
 *		It toggle off LED and schedule corresponding timer if necessary.
 */
static void SwLedBlink(struct LED_871x *pLed)
{
	struct _adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bStopBlinking = false;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == LED_STATE_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);
	/* Determine if we shall change LED state again. */
	pLed->BlinkTimes--;
	switch (pLed->CurrLedState) {
	case LED_BLINK_NORMAL:
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		break;
	case LED_BLINK_StartToBlink:
		if (check_fwstate(pmlmepriv, _FW_LINKED) &&
		    (pmlmepriv->fw_state & WIFI_STATION_STATE))
			bStopBlinking = true;
		if (check_fwstate(pmlmepriv, _FW_LINKED) &&
		    ((pmlmepriv->fw_state & WIFI_ADHOC_STATE) ||
		    (pmlmepriv->fw_state & WIFI_ADHOC_MASTER_STATE)))
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
		if (check_fwstate(pmlmepriv, _FW_LINKED) &&
		    !pLed->bLedOn)
			SwLedOn(padapter, pLed);
		else if (check_fwstate(pmlmepriv, _FW_LINKED) &&  pLed->bLedOn)
			SwLedOff(padapter, pLed);
		pLed->BlinkTimes = 0;
		pLed->bLedBlinkInProgress = false;
	} else {
		/* Assign LED state to toggle. */
		if (pLed->BlinkingLedState == LED_STATE_ON)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;

		/* Schedule a timer to toggle LED state. */
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

static void SwLedBlink1(struct LED_871x *pLed)
{
	struct _adapter *padapter = pLed->padapter;
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct eeprom_priv *peeprompriv = &padapter->eeprompriv;
	struct LED_871x *pLed1 = &ledpriv->SwLed1;
	u8 bStopBlinking = false;

	if (peeprompriv->CustomerID == RT_CID_819x_CAMEO)
		pLed = &ledpriv->SwLed1;
	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == LED_STATE_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);
	if (peeprompriv->CustomerID == RT_CID_DEFAULT) {
		if (check_fwstate(pmlmepriv, _FW_LINKED)) {
			if (!pLed1->bSWLedCtrl) {
				SwLedOn(padapter, pLed1);
				pLed1->bSWLedCtrl = true;
			} else if (!pLed1->bLedOn) {
				SwLedOn(padapter, pLed1);
			}
		} else {
			if (!pLed1->bSWLedCtrl) {
				SwLedOff(padapter, pLed1);
				pLed1->bSWLedCtrl = true;
			} else if (pLed1->bLedOn) {
				SwLedOff(padapter, pLed1);
			}
		}
	}
	switch (pLed->CurrLedState) {
	case LED_BLINK_SLOWLY:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		break;
	case LED_BLINK_NORMAL:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
		break;
	case LED_SCAN_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = LED_STATE_OFF;
				else
					pLed->BlinkingLedState = LED_STATE_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = LED_STATE_OFF;
				else
					pLed->BlinkingLedState = LED_STATE_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_TXRX_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_NORMAL;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = LED_STATE_OFF;
				else
					pLed->BlinkingLedState = LED_STATE_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->bLedNoLinkBlinkInProgress = true;
				pLed->CurrLedState = LED_BLINK_SLOWLY;
				if (pLed->bLedOn)
					pLed->BlinkingLedState = LED_STATE_OFF;
				else
					pLed->BlinkingLedState = LED_STATE_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
			}
			pLed->BlinkTimes = 0;
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_BLINK_WPS:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		break;
	case LED_BLINK_WPS_STOP:	/* WPS success */
		if (pLed->BlinkingLedState == LED_STATE_ON) {
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA));
			bStopBlinking = false;
		} else {
			bStopBlinking = true;
		}
		if (bStopBlinking) {
			pLed->bLedLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_NORMAL;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
		}
		pLed->bLedWPSBlinkInProgress = false;
		break;
	default:
		break;
	}
}

static void SwLedBlink2(struct LED_871x *pLed)
{
	struct _adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &(padapter->mlmepriv);
	u8 bStopBlinking = false;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == LED_STATE_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);
	switch (pLed->CurrLedState) {
	case LED_SCAN_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_ON;
				pLed->BlinkingLedState = LED_STATE_ON;
				SwLedOn(padapter, pLed);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_OFF;
				pLed->BlinkingLedState = LED_STATE_OFF;
				SwLedOff(padapter, pLed);
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_TXRX_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_ON;
				pLed->BlinkingLedState = LED_STATE_ON;
				SwLedOn(padapter, pLed);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_OFF;
				pLed->BlinkingLedState = LED_STATE_OFF;
				SwLedOff(padapter, pLed);
			}
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	default:
		break;
	}
}

static void SwLedBlink3(struct LED_871x *pLed)
{
	struct _adapter *padapter = pLed->padapter;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 bStopBlinking = false;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == LED_STATE_ON)
		SwLedOn(padapter, pLed);
	else
		if (pLed->CurrLedState != LED_BLINK_WPS_STOP)
			SwLedOff(padapter, pLed);
	switch (pLed->CurrLedState) {
	case LED_SCAN_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_ON;
				pLed->BlinkingLedState = LED_STATE_ON;
				if (!pLed->bLedOn)
					SwLedOn(padapter, pLed);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_OFF;
				pLed->BlinkingLedState = LED_STATE_OFF;
				if (pLed->bLedOn)
					SwLedOff(padapter, pLed);
			}
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_TXRX_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			if (check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_ON;
				pLed->BlinkingLedState = LED_STATE_ON;
				if (!pLed->bLedOn)
					SwLedOn(padapter, pLed);
			} else if (!check_fwstate(pmlmepriv, _FW_LINKED)) {
				pLed->CurrLedState = LED_STATE_OFF;
				pLed->BlinkingLedState = LED_STATE_OFF;
				if (pLed->bLedOn)
					SwLedOff(padapter, pLed);
			}
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_BLINK_WPS:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		break;
	case LED_BLINK_WPS_STOP:	/*WPS success*/
		if (pLed->BlinkingLedState == LED_STATE_ON) {
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA));
			bStopBlinking = false;
		} else {
			bStopBlinking = true;
		}
		if (bStopBlinking) {
			pLed->CurrLedState = LED_STATE_ON;
			pLed->BlinkingLedState = LED_STATE_ON;
			SwLedOn(padapter, pLed);
			pLed->bLedWPSBlinkInProgress = false;
		}
		break;
	default:
		break;
	}
}

static void SwLedBlink4(struct LED_871x *pLed)
{
	struct _adapter *padapter = pLed->padapter;
	struct led_priv	*ledpriv = &padapter->ledpriv;
	struct LED_871x *pLed1 = &ledpriv->SwLed1;
	u8 bStopBlinking = false;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == LED_STATE_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);
	if (!pLed1->bLedWPSBlinkInProgress &&
	    pLed1->BlinkingLedState == LED_UNKNOWN) {
		pLed1->BlinkingLedState = LED_STATE_OFF;
		pLed1->CurrLedState = LED_STATE_OFF;
		SwLedOff(padapter, pLed1);
	}
	switch (pLed->CurrLedState) {
	case LED_BLINK_SLOWLY:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		break;
	case LED_BLINK_StartToBlink:
		if (pLed->bLedOn) {
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
		} else {
			pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
		}
		break;
	case LED_SCAN_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_TXRX_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_BLINK_WPS:
		if (pLed->bLedOn) {
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
		} else {
			pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
		}
		break;
	case LED_BLINK_WPS_STOP:	/*WPS authentication fail*/
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
		break;
	case LED_BLINK_WPS_STOP_OVERLAP:	/*WPS session overlap */
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0) {
			if (pLed->bLedOn)
				pLed->BlinkTimes = 1;
			else
				bStopBlinking = true;
		}
		if (bStopBlinking) {
			pLed->BlinkTimes = 10;
			pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
		}
		break;
	default:
		break;
	}
}

static void SwLedBlink5(struct LED_871x *pLed)
{
	struct _adapter *padapter = pLed->padapter;
	u8 bStopBlinking = false;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == LED_STATE_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);
	switch (pLed->CurrLedState) {
	case LED_SCAN_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			pLed->CurrLedState = LED_STATE_ON;
			pLed->BlinkingLedState = LED_STATE_ON;
			if (!pLed->bLedOn)
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
			pLed->bLedScanBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_TXRX_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			pLed->CurrLedState = LED_STATE_ON;
			pLed->BlinkingLedState = LED_STATE_ON;
			if (!pLed->bLedOn)
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	default:
		break;
	}
}

static void SwLedBlink6(struct LED_871x *pLed)
{
	struct _adapter *padapter = pLed->padapter;
	u8 bStopBlinking = false;

	/* Change LED according to BlinkingLedState specified. */
	if (pLed->BlinkingLedState == LED_STATE_ON)
		SwLedOn(padapter, pLed);
	else
		SwLedOff(padapter, pLed);
	switch (pLed->CurrLedState) {
	case LED_TXRX_BLINK:
		pLed->BlinkTimes--;
		if (pLed->BlinkTimes == 0)
			bStopBlinking = true;
		if (bStopBlinking) {
			pLed->CurrLedState = LED_STATE_ON;
			pLed->BlinkingLedState = LED_STATE_ON;
			if (!pLed->bLedOn)
				SwLedOn(padapter, pLed);
			pLed->bLedBlinkInProgress = false;
		} else {
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_BLINK_WPS:
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		break;

	default:
		break;
	}
}

/*	Description:
 *		Callback function of LED BlinkTimer,
 *		it just schedules to corresponding BlinkWorkItem.
 */
static void BlinkTimerCallback(struct timer_list *t)
{
	struct LED_871x  *pLed = from_timer(pLed, t, BlinkTimer);

	/* This fixed the crash problem on Fedora 12 when trying to do the
	 * insmod;ifconfig up;rmmod commands.
	 */
	if (pLed->padapter->bSurpriseRemoved || pLed->padapter->bDriverStopped)
		return;
	schedule_work(&pLed->BlinkWorkItem);
}

/*	Description:
 *		Callback function of LED BlinkWorkItem.
 *		We dispatch actual LED blink action according to LedStrategy.
 */
static void BlinkWorkItemCallback(struct work_struct *work)
{
	struct LED_871x *pLed = container_of(work, struct LED_871x,
				BlinkWorkItem);
	struct led_priv	*ledpriv = &pLed->padapter->ledpriv;

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
		SwLedBlink(pLed);
		break;
	}
}

/*============================================================================
 * Default LED behavior.
 *============================================================================
 *
 *	Description:
 *		Implement each led action for SW_LED_MODE0.
 *		This is default strategy.
 */

static void SwLedControlMode1(struct _adapter *padapter,
			      enum LED_CTL_MODE LedAction)
{
	struct led_priv *ledpriv = &padapter->ledpriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct sitesurvey_ctrl *psitesurveyctrl = &pmlmepriv->sitesurveyctrl;

	if (padapter->eeprompriv.CustomerID == RT_CID_819x_CAMEO)
		pLed = &ledpriv->SwLed1;
	switch (LedAction) {
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		if (!pLed->bLedNoLinkBlinkInProgress) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			  IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_LINK:
		if (!pLed->bLedLinkBlinkInProgress) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			    IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_NORMAL;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_LINK_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_SITE_SURVEY:
		if (psitesurveyctrl->traffic_busy &&
		    check_fwstate(pmlmepriv, _FW_LINKED))
			; /* dummy branch */
		else if (!pLed->bLedScanBlinkInProgress) {
			if (IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				 pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedScanBlinkInProgress = true;
			pLed->CurrLedState = LED_SCAN_BLINK;
			pLed->BlinkTimes = 24;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (!pLed->bLedBlinkInProgress) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			    IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedLinkBlinkInProgress = false;
			}
			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_TXRX_BLINK;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;

	case LED_CTL_START_WPS: /*wait until xinpin finish */
	case LED_CTL_START_WPS_BOTTON:
		if (!pLed->bLedWPSBlinkInProgress) {
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				 pLed->bLedLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			pLed->bLedWPSBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_WPS;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_STOP_WPS:
		if (pLed->bLedNoLinkBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedNoLinkBlinkInProgress = false;
		}
		if (pLed->bLedLinkBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			 pLed->bLedLinkBlinkInProgress = false;
		}
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress)
			del_timer(&pLed->BlinkTimer);
		else
			pLed->bLedWPSBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_WPS_STOP;
		if (pLed->bLedOn) {
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA));
		} else {
			pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer,
				  jiffies + msecs_to_jiffies(0));
		}
		break;
	case LED_CTL_STOP_WPS_FAIL:
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		pLed->bLedNoLinkBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_SLOWLY;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		if (pLed->bLedNoLinkBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedNoLinkBlinkInProgress = false;
		}
		if (pLed->bLedLinkBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedLinkBlinkInProgress = false;
		}
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;
	default:
		break;
	}
}

static void SwLedControlMode2(struct _adapter *padapter,
			      enum LED_CTL_MODE LedAction)
{
	struct led_priv	 *ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;

	switch (LedAction) {
	case LED_CTL_SITE_SURVEY:
		if (pmlmepriv->sitesurveyctrl.traffic_busy)
			; /* dummy branch */
		else if (!pLed->bLedScanBlinkInProgress) {
			if (IS_LED_WPS_BLINKING(pLed))
				return;

			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedScanBlinkInProgress = true;
			pLed->CurrLedState = LED_SCAN_BLINK;
			pLed->BlinkTimes = 24;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;

	case LED_CTL_TX:
	case LED_CTL_RX:
		if (!pLed->bLedBlinkInProgress &&
		    check_fwstate(pmlmepriv, _FW_LINKED)) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			   IS_LED_WPS_BLINKING(pLed))
				return;
			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_TXRX_BLINK;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;

	case LED_CTL_LINK:
		pLed->CurrLedState = LED_STATE_ON;
		pLed->BlinkingLedState = LED_STATE_ON;
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}

		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;

	case LED_CTL_START_WPS: /*wait until xinpin finish*/
	case LED_CTL_START_WPS_BOTTON:
		if (!pLed->bLedWPSBlinkInProgress) {
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			pLed->bLedWPSBlinkInProgress = true;
			pLed->CurrLedState = LED_STATE_ON;
			pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer,
				  jiffies + msecs_to_jiffies(0));
		}
		break;

	case LED_CTL_STOP_WPS:
		pLed->bLedWPSBlinkInProgress = false;
		pLed->CurrLedState = LED_STATE_ON;
		pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;

	case LED_CTL_STOP_WPS_FAIL:
		pLed->bLedWPSBlinkInProgress = false;
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;

	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		if (!IS_LED_BLINKING(pLed)) {
			pLed->CurrLedState = LED_STATE_OFF;
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer,
				  jiffies + msecs_to_jiffies(0));
		}
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;
	default:
		break;
	}
}

static void SwLedControlMode3(struct _adapter *padapter,
			      enum LED_CTL_MODE LedAction)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;

	switch (LedAction) {
	case LED_CTL_SITE_SURVEY:
		if (pmlmepriv->sitesurveyctrl.traffic_busy)
			; /* dummy branch */
		else if (!pLed->bLedScanBlinkInProgress) {
			if (IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedScanBlinkInProgress = true;
			pLed->CurrLedState = LED_SCAN_BLINK;
			pLed->BlinkTimes = 24;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (!pLed->bLedBlinkInProgress &&
		    check_fwstate(pmlmepriv, _FW_LINKED)) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			    IS_LED_WPS_BLINKING(pLed))
				return;
			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_TXRX_BLINK;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_LINK:
		if (IS_LED_WPS_BLINKING(pLed))
			return;
		pLed->CurrLedState = LED_STATE_ON;
		pLed->BlinkingLedState = LED_STATE_ON;
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;
	case LED_CTL_START_WPS: /* wait until xinpin finish */
	case LED_CTL_START_WPS_BOTTON:
		if (!pLed->bLedWPSBlinkInProgress) {
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			pLed->bLedWPSBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_WPS;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_STOP_WPS:
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		} else {
			pLed->bLedWPSBlinkInProgress = true;
		}
		pLed->CurrLedState = LED_BLINK_WPS_STOP;
		if (pLed->bLedOn) {
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA));
		} else {
			pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer,
				  jiffies + msecs_to_jiffies(0));
		}
		break;
	case LED_CTL_STOP_WPS_FAIL:
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;
	case LED_CTL_START_TO_LINK:
	case LED_CTL_NO_LINK:
		if (!IS_LED_BLINKING(pLed)) {
			pLed->CurrLedState = LED_STATE_OFF;
			pLed->BlinkingLedState = LED_STATE_OFF;
			mod_timer(&pLed->BlinkTimer,
				  jiffies + msecs_to_jiffies(0));
		}
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;
	default:
		break;
	}
}

static void SwLedControlMode4(struct _adapter *padapter,
			      enum LED_CTL_MODE LedAction)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;
	struct LED_871x *pLed1 = &ledpriv->SwLed1;

	switch (LedAction) {
	case LED_CTL_START_TO_LINK:
		if (pLed1->bLedWPSBlinkInProgress) {
			pLed1->bLedWPSBlinkInProgress = false;
			del_timer(&pLed1->BlinkTimer);
			pLed1->BlinkingLedState = LED_STATE_OFF;
			pLed1->CurrLedState = LED_STATE_OFF;
			if (pLed1->bLedOn)
				mod_timer(&pLed->BlinkTimer,
					  jiffies + msecs_to_jiffies(0));
		}
		if (!pLed->bLedStartToLinkBlinkInProgress) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			    IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			pLed->bLedStartToLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_StartToBlink;
			if (pLed->bLedOn) {
				pLed->BlinkingLedState = LED_STATE_OFF;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
			} else {
				pLed->BlinkingLedState = LED_STATE_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
			}
		}
		break;
	case LED_CTL_LINK:
	case LED_CTL_NO_LINK:
		/*LED1 settings*/
		if (LedAction == LED_CTL_LINK) {
			if (pLed1->bLedWPSBlinkInProgress) {
				pLed1->bLedWPSBlinkInProgress = false;
				del_timer(&pLed1->BlinkTimer);
				pLed1->BlinkingLedState = LED_STATE_OFF;
				pLed1->CurrLedState = LED_STATE_OFF;
				if (pLed1->bLedOn)
					mod_timer(&pLed->BlinkTimer,
						  jiffies + msecs_to_jiffies(0));
			}
		}
		if (!pLed->bLedNoLinkBlinkInProgress) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			    IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedNoLinkBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_SLOWLY;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_SITE_SURVEY:
		if (pmlmepriv->sitesurveyctrl.traffic_busy &&
		    check_fwstate(pmlmepriv, _FW_LINKED))
			;
		else if (!pLed->bLedScanBlinkInProgress) {
			if (IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedScanBlinkInProgress = true;
			pLed->CurrLedState = LED_SCAN_BLINK;
			pLed->BlinkTimes = 24;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (!pLed->bLedBlinkInProgress) {
			if (pLed->CurrLedState == LED_SCAN_BLINK ||
			    IS_LED_WPS_BLINKING(pLed))
				return;
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_TXRX_BLINK;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_START_WPS: /*wait until xinpin finish*/
	case LED_CTL_START_WPS_BOTTON:
		if (pLed1->bLedWPSBlinkInProgress) {
			pLed1->bLedWPSBlinkInProgress = false;
			del_timer(&pLed1->BlinkTimer);
			pLed1->BlinkingLedState = LED_STATE_OFF;
			pLed1->CurrLedState = LED_STATE_OFF;
			if (pLed1->bLedOn)
				mod_timer(&pLed->BlinkTimer,
					  jiffies + msecs_to_jiffies(0));
		}
		if (!pLed->bLedWPSBlinkInProgress) {
			if (pLed->bLedNoLinkBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedNoLinkBlinkInProgress = false;
			}
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			if (pLed->bLedScanBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedScanBlinkInProgress = false;
			}
			pLed->bLedWPSBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_WPS;
			if (pLed->bLedOn) {
				pLed->BlinkingLedState = LED_STATE_OFF;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_SLOWLY_INTERVAL));
			} else {
				pLed->BlinkingLedState = LED_STATE_ON;
				mod_timer(&pLed->BlinkTimer, jiffies +
					  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
			}
		}
		break;
	case LED_CTL_STOP_WPS:	/*WPS connect success*/
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		pLed->bLedNoLinkBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_SLOWLY;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		break;
	case LED_CTL_STOP_WPS_FAIL:	/*WPS authentication fail*/
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		pLed->bLedNoLinkBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_SLOWLY;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		/*LED1 settings*/
		if (pLed1->bLedWPSBlinkInProgress)
			del_timer(&pLed1->BlinkTimer);
		else
			pLed1->bLedWPSBlinkInProgress = true;
		pLed1->CurrLedState = LED_BLINK_WPS_STOP;
		if (pLed1->bLedOn)
			pLed1->BlinkingLedState = LED_STATE_OFF;
		else
			pLed1->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
		break;
	case LED_CTL_STOP_WPS_FAIL_OVERLAP:	/*WPS session overlap*/
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		pLed->bLedNoLinkBlinkInProgress = true;
		pLed->CurrLedState = LED_BLINK_SLOWLY;
		if (pLed->bLedOn)
			pLed->BlinkingLedState = LED_STATE_OFF;
		else
			pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NO_LINK_INTERVAL_ALPHA));
		/*LED1 settings*/
		if (pLed1->bLedWPSBlinkInProgress)
			del_timer(&pLed1->BlinkTimer);
		else
			pLed1->bLedWPSBlinkInProgress = true;
		pLed1->CurrLedState = LED_BLINK_WPS_STOP_OVERLAP;
		pLed1->BlinkTimes = 10;
		if (pLed1->bLedOn)
			pLed1->BlinkingLedState = LED_STATE_OFF;
		else
			pLed1->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer, jiffies +
			  msecs_to_jiffies(LED_BLINK_NORMAL_INTERVAL));
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		if (pLed->bLedNoLinkBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedNoLinkBlinkInProgress = false;
		}
		if (pLed->bLedLinkBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedLinkBlinkInProgress = false;
		}
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		if (pLed->bLedScanBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedScanBlinkInProgress = false;
		}
		if (pLed->bLedStartToLinkBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedStartToLinkBlinkInProgress = false;
		}
		if (pLed1->bLedWPSBlinkInProgress) {
			del_timer(&pLed1->BlinkTimer);
			pLed1->bLedWPSBlinkInProgress = false;
		}
		pLed1->BlinkingLedState = LED_UNKNOWN;
		SwLedOff(padapter, pLed);
		SwLedOff(padapter, pLed1);
		break;
	default:
		break;
	}
}

static void SwLedControlMode5(struct _adapter *padapter,
			      enum LED_CTL_MODE LedAction)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;

	if (padapter->eeprompriv.CustomerID == RT_CID_819x_CAMEO)
		pLed = &ledpriv->SwLed1;

	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_NO_LINK:
	case LED_CTL_LINK:	/* solid blue */
		if (pLed->CurrLedState == LED_SCAN_BLINK)
			return;
		pLed->CurrLedState = LED_STATE_ON;
		pLed->BlinkingLedState = LED_STATE_ON;
		pLed->bLedBlinkInProgress = false;
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;
	case LED_CTL_SITE_SURVEY:
		if (pmlmepriv->sitesurveyctrl.traffic_busy &&
		    check_fwstate(pmlmepriv, _FW_LINKED))
			; /* dummy branch */
		else if (!pLed->bLedScanBlinkInProgress) {
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedScanBlinkInProgress = true;
			pLed->CurrLedState = LED_SCAN_BLINK;
			pLed->BlinkTimes = 24;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (!pLed->bLedBlinkInProgress) {
			if (pLed->CurrLedState == LED_SCAN_BLINK)
				return;
			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_TXRX_BLINK;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		SwLedOff(padapter, pLed);
		break;
	default:
		break;
	}
}


static void SwLedControlMode6(struct _adapter *padapter,
			      enum LED_CTL_MODE LedAction)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct LED_871x *pLed = &ledpriv->SwLed0;

	switch (LedAction) {
	case LED_CTL_POWER_ON:
	case LED_CTL_NO_LINK:
	case LED_CTL_LINK:	/*solid blue*/
	case LED_CTL_SITE_SURVEY:
		if (IS_LED_WPS_BLINKING(pLed))
			return;
		pLed->CurrLedState = LED_STATE_ON;
		pLed->BlinkingLedState = LED_STATE_ON;
		pLed->bLedBlinkInProgress = false;
		mod_timer(&pLed->BlinkTimer, jiffies + msecs_to_jiffies(0));
		break;
	case LED_CTL_TX:
	case LED_CTL_RX:
		if (!pLed->bLedBlinkInProgress &&
		    check_fwstate(pmlmepriv, _FW_LINKED)) {
			if (IS_LED_WPS_BLINKING(pLed))
				return;
			pLed->bLedBlinkInProgress = true;
			pLed->CurrLedState = LED_TXRX_BLINK;
			pLed->BlinkTimes = 2;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_FASTER_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_START_WPS: /*wait until xinpin finish*/
	case LED_CTL_START_WPS_BOTTON:
		if (!pLed->bLedWPSBlinkInProgress) {
			if (pLed->bLedBlinkInProgress) {
				del_timer(&pLed->BlinkTimer);
				pLed->bLedBlinkInProgress = false;
			}
			pLed->bLedWPSBlinkInProgress = true;
			pLed->CurrLedState = LED_BLINK_WPS;
			if (pLed->bLedOn)
				pLed->BlinkingLedState = LED_STATE_OFF;
			else
				pLed->BlinkingLedState = LED_STATE_ON;
			mod_timer(&pLed->BlinkTimer, jiffies +
				  msecs_to_jiffies(LED_BLINK_SCAN_INTERVAL_ALPHA));
		}
		break;
	case LED_CTL_STOP_WPS_FAIL:
	case LED_CTL_STOP_WPS:
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		pLed->CurrLedState = LED_STATE_ON;
		pLed->BlinkingLedState = LED_STATE_ON;
		mod_timer(&pLed->BlinkTimer,
			  jiffies + msecs_to_jiffies(0));
		break;
	case LED_CTL_POWER_OFF:
		pLed->CurrLedState = LED_STATE_OFF;
		pLed->BlinkingLedState = LED_STATE_OFF;
		if (pLed->bLedBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedBlinkInProgress = false;
		}
		if (pLed->bLedWPSBlinkInProgress) {
			del_timer(&pLed->BlinkTimer);
			pLed->bLedWPSBlinkInProgress = false;
		}
		SwLedOff(padapter, pLed);
		break;
	default:
		break;
	}
}

/*	Description:
 *		Dispatch LED action according to pHalData->LedStrategy.
 */
void LedControl871x(struct _adapter *padapter, enum LED_CTL_MODE LedAction)
{
	struct led_priv	*ledpriv = &padapter->ledpriv;

	if (!ledpriv->bRegUseLed)
		return;
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
}
