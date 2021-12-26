/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_LED_H_
#define __RTW_LED_H_

#include "osdep_service.h"
#include "drv_types.h"

enum LED_CTL_MODE {
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
	LED_CTL_START_WPS_BOTTON = 11,
	LED_CTL_STOP_WPS_FAIL = 12,
};

enum LED_STATE_871x {
	LED_UNKNOWN = 0,
	RTW_LED_ON = 1,
	RTW_LED_OFF = 2,
	LED_BLINK_NORMAL = 3,
	LED_BLINK_SLOWLY = 4,
	LED_BLINK_SCAN = 6, /*  LED is blinking during scanning period,
			     * the # of times to blink is depend on time
			     * for scanning. */
	LED_BLINK_StartToBlink = 8,/*  Customzied for Sercomm Printer
				    * Server case */
	LED_BLINK_TXRX = 9,
	LED_BLINK_WPS = 10,	/*  LED is blinkg during WPS communication */
	LED_BLINK_WPS_STOP = 11,
	LED_BLINK_RUNTOP = 13, /*  Customized for RunTop */
};

struct LED_871x {
	struct adapter *padapter;

	enum LED_STATE_871x	CurrLedState; /*  Current LED state. */
	enum LED_STATE_871x	BlinkingLedState; /*  Next state for blinking,
				   * either RTW_LED_ON or RTW_LED_OFF are. */

	bool bLedOn; /*  true if LED is ON, false if LED is OFF. */

	bool bLedBlinkInProgress; /*  true if it is blinking, false o.w.. */

	bool bLedWPSBlinkInProgress;

	u32 BlinkTimes; /*  Number of times to toggle led state for blinking. */

	bool bLedNoLinkBlinkInProgress;
	bool bLedLinkBlinkInProgress;
	bool bLedScanBlinkInProgress;
	struct delayed_work blink_work;
};

void LedControl8188eu(struct adapter *padapter, enum LED_CTL_MODE	LedAction);

struct led_priv{
	struct LED_871x			SwLed0;
	bool	bRegUseLed;
	void (*LedControlHandler)(struct adapter *padapter,
				  enum LED_CTL_MODE LedAction);
};

#define rtw_led_control(adapt, action) \
	do { \
		if ((adapt)->ledpriv.LedControlHandler) \
			(adapt)->ledpriv.LedControlHandler((adapt), (action)); \
	} while (0)

void rtl8188eu_InitSwLeds(struct adapter *padapter);
void rtl8188eu_DeInitSwLeds(struct adapter *padapter);

#endif /* __RTW_LED_H_ */
