/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTW_LED_H_
#define __RTW_LED_H_

#include "osdep_service.h"
#include "drv_types.h"

enum LED_CTL_MODE {
	LED_CTL_LINK = 2,
	LED_CTL_NO_LINK = 3,
	LED_CTL_TX = 4,
	LED_CTL_RX = 5,
	LED_CTL_SITE_SURVEY = 6,
	LED_CTL_POWER_OFF = 7,
	LED_CTL_START_TO_LINK = 8,
	LED_CTL_START_WPS = 9,
	LED_CTL_STOP_WPS = 10,
	LED_CTL_STOP_WPS_FAIL = 12,
};

enum LED_STATE_871x {
	RTW_LED_OFF = 2,
	LED_BLINK_NORMAL = 3,
	LED_BLINK_SLOWLY = 4,
	LED_BLINK_SCAN = 6, /*  LED is blinking during scanning period,
			     * the # of times to blink is depend on time
			     * for scanning. */
	LED_BLINK_TXRX = 9,
	LED_BLINK_WPS = 10,	/*  LED is blinkg during WPS communication */
	LED_BLINK_WPS_STOP = 11,
};

struct led_priv {
	bool bRegUseLed;

	enum LED_STATE_871x	CurrLedState; /*  Current LED state. */

	bool bLedOn; /*  true if LED is ON, false if LED is OFF. */

	bool bLedBlinkInProgress; /*  true if it is blinking, false o.w.. */

	bool bLedWPSBlinkInProgress;

	u32 BlinkTimes; /*  Number of times to toggle led state for blinking. */

	bool bLedScanBlinkInProgress;
	struct delayed_work blink_work;
};

void rtl8188eu_InitSwLeds(struct adapter *padapter);
void rtl8188eu_DeInitSwLeds(struct adapter *padapter);

void rtw_led_control(struct adapter *padapter, enum LED_CTL_MODE LedAction);

#endif /* __RTW_LED_H_ */
