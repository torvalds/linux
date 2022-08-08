/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_LED_H_
#define __RTW_LED_H_

#include <osdep_service.h>
#include <drv_types.h>

#define LED_BLINK_NO_LINK_INTERVAL_ALPHA	1000
#define LED_BLINK_LINK_INTERVAL_ALPHA		500	/* 500 */
#define LED_BLINK_SCAN_INTERVAL_ALPHA		180	/* 150 */
#define LED_BLINK_FASTER_INTERVAL_ALPHA		50
#define LED_BLINK_WPS_SUCCESS_INTERVAL_ALPHA	5000

enum LED_CTL_MODE {
	LED_CTL_POWER_ON,
	LED_CTL_LINK,
	LED_CTL_NO_LINK,
	LED_CTL_TX,
	LED_CTL_RX,
	LED_CTL_SITE_SURVEY,
	LED_CTL_POWER_OFF,
	LED_CTL_START_TO_LINK,
	LED_CTL_START_WPS,
	LED_CTL_STOP_WPS,
	LED_CTL_START_WPS_BOTTON,
	LED_CTL_STOP_WPS_FAIL
};

enum LED_STATE_871x {
	LED_UNKNOWN,
	RTW_LED_ON,
	RTW_LED_OFF,
	LED_BLINK_NORMAL,
	LED_BLINK_SLOWLY,
	LED_BLINK_POWER_ON,
	LED_BLINK_SCAN,
	LED_BLINK_TXRX,
	LED_BLINK_WPS,
	LED_BLINK_WPS_STOP
};

struct LED_871x {
	struct adapter *padapter;

	enum LED_STATE_871x	CurrLedState; /*  Current LED state. */
	enum LED_STATE_871x	BlinkingLedState; /*  Next state for blinking,
						   * either RTW_LED_ON or RTW_LED_OFF are.
						   */

	u8 led_on; /*  true if LED is ON, false if LED is OFF. */

	u8 bLedBlinkInProgress; /*  true if it is blinking, false o.w.. */

	u8 bLedWPSBlinkInProgress;

	u32 BlinkTimes; /*  Number of times to toggle led state for blinking. */

	struct timer_list BlinkTimer; /*  Timer object for led blinking. */

	/*  ALPHA, added by chiyoko, 20090106 */
	u8 bLedNoLinkBlinkInProgress;
	u8 bLedLinkBlinkInProgress;
	u8 bLedScanBlinkInProgress;
	struct work_struct BlinkWorkItem; /* Workitem used by BlinkTimer to
					   * manipulate H/W to blink LED.
					   */
};

#define IS_LED_WPS_BLINKING(_LED_871x)					\
	(((struct LED_871x *)_LED_871x)->CurrLedState == LED_BLINK_WPS || \
	((struct LED_871x *)_LED_871x)->CurrLedState == LED_BLINK_WPS_STOP || \
	((struct LED_871x *)_LED_871x)->bLedWPSBlinkInProgress)

void led_control_8188eu(struct adapter *padapter, enum LED_CTL_MODE LedAction);

struct led_priv {
	struct LED_871x sw_led;
};

void BlinkWorkItemCallback(struct work_struct *work);

void ResetLedStatus(struct LED_871x *pLed);

void InitLed871x(struct adapter *padapter, struct LED_871x *pLed);

void DeInitLed871x(struct LED_871x *pLed);

/* hal... */
void blink_handler(struct LED_871x *pLed);
void sw_led_on(struct adapter *padapter, struct LED_871x *pLed);
void sw_led_off(struct adapter *padapter, struct LED_871x *pLed);

#endif /* __RTW_LED_H_ */
