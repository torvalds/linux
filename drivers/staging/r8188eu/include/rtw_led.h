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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTW_LED_H_
#define __RTW_LED_H_

#include <osdep_service.h>
#include <drv_types.h>

#define MSECS(t)        (HZ * ((t) / 1000) + (HZ * ((t) % 1000)) / 1000)

#define LED_BLINK_NORMAL_INTERVAL		100
#define LED_BLINK_SLOWLY_INTERVAL		200
#define LED_BLINK_LONG_INTERVAL			400

#define LED_BLINK_NO_LINK_INTERVAL_ALPHA	1000
#define LED_BLINK_LINK_INTERVAL_ALPHA		500	/* 500 */
#define LED_BLINK_SCAN_INTERVAL_ALPHA		180	/* 150 */
#define LED_BLINK_FASTER_INTERVAL_ALPHA		50
#define LED_BLINK_WPS_SUCESS_INTERVAL_ALPHA	5000

#define LED_BLINK_NORMAL_INTERVAL_NETTRONIX	100
#define LED_BLINK_SLOWLY_INTERVAL_NETTRONIX	2000

#define LED_BLINK_SLOWLY_INTERVAL_PORNET	1000
#define LED_BLINK_NORMAL_INTERVAL_PORNET	100

#define LED_BLINK_FAST_INTERVAL_BITLAND		30

/*  060403, rcnjko: Customized for AzWave. */
#define LED_CM2_BLINK_ON_INTERVAL		250
#define LED_CM2_BLINK_OFF_INTERVAL		4750

#define LED_CM8_BLINK_INTERVAL			500	/* for QMI */
#define LED_CM8_BLINK_OFF_INTERVAL		3750	/* for QMI */

/*  080124, lanhsin: Customized for RunTop */
#define LED_RunTop_BLINK_INTERVAL		300

/*  060421, rcnjko: Customized for Sercomm Printer Server case. */
#define LED_CM3_BLINK_INTERVAL			1500

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
	LED_CTL_START_WPS_BOTTON = 11, /* added for runtop */
	LED_CTL_STOP_WPS_FAIL = 12, /* added for ALPHA */
	LED_CTL_STOP_WPS_FAIL_OVERLAP = 13, /* added for BELKIN */
	LED_CTL_CONNECTION_NO_TRANSFER = 14,
};

enum LED_STATE_871x {
	LED_UNKNOWN = 0,
	RTW_LED_ON = 1,
	RTW_LED_OFF = 2,
	LED_BLINK_NORMAL = 3,
	LED_BLINK_SLOWLY = 4,
	LED_BLINK_POWER_ON = 5,
	LED_BLINK_SCAN = 6, /*  LED is blinking during scanning period,
			     * the # of times to blink is depend on time
			     * for scanning. */
	LED_BLINK_NO_LINK = 7, /*  LED is blinking during no link state. */
	LED_BLINK_StartToBlink = 8,/*  Customzied for Sercomm Printer
				    * Server case */
	LED_BLINK_TXRX = 9,
	LED_BLINK_WPS = 10,	/*  LED is blinkg during WPS communication */
	LED_BLINK_WPS_STOP = 11,	/* for ALPHA */
	LED_BLINK_WPS_STOP_OVERLAP = 12,	/* for BELKIN */
	LED_BLINK_RUNTOP = 13, /*  Customized for RunTop */
	LED_BLINK_CAMEO = 14,
	LED_BLINK_XAVI = 15,
	LED_BLINK_ALWAYS_ON = 16,
};

enum LED_PIN_871x {
	LED_PIN_NULL = 0,
	LED_PIN_LED0 = 1,
	LED_PIN_LED1 = 2,
	LED_PIN_LED2 = 3,
	LED_PIN_GPIO0 = 4,
};

struct LED_871x {
	struct adapter *padapter;

	enum LED_PIN_871x	LedPin;	/* Identify how to implement this
					 * SW led. */
	enum LED_STATE_871x	CurrLedState; /*  Current LED state. */
	enum LED_STATE_871x	BlinkingLedState; /*  Next state for blinking,
				   * either RTW_LED_ON or RTW_LED_OFF are. */

	u8 bLedOn; /*  true if LED is ON, false if LED is OFF. */

	u8 bLedBlinkInProgress; /*  true if it is blinking, false o.w.. */

	u8 bLedWPSBlinkInProgress;

	u32 BlinkTimes; /*  Number of times to toggle led state for blinking. */

	struct timer_list BlinkTimer; /*  Timer object for led blinking. */

	u8 bSWLedCtrl;

	/*  ALPHA, added by chiyoko, 20090106 */
	u8 bLedNoLinkBlinkInProgress;
	u8 bLedLinkBlinkInProgress;
	u8 bLedStartToLinkBlinkInProgress;
	u8 bLedScanBlinkInProgress;
	struct work_struct BlinkWorkItem; /* Workitem used by BlinkTimer to
					   * manipulate H/W to blink LED. */
};

#define IS_LED_WPS_BLINKING(_LED_871x)					\
	(((struct LED_871x *)_LED_871x)->CurrLedState == LED_BLINK_WPS || \
	((struct LED_871x *)_LED_871x)->CurrLedState == LED_BLINK_WPS_STOP || \
	((struct LED_871x *)_LED_871x)->bLedWPSBlinkInProgress)

#define IS_LED_BLINKING(_LED_871x)					\
	(((struct LED_871x *)_LED_871x)->bLedWPSBlinkInProgress	||	\
	((struct LED_871x *)_LED_871x)->bLedScanBlinkInProgress)

/*  LED customization. */

enum LED_STRATEGY_871x {
	SW_LED_MODE0 = 0, /* SW control 1 LED via GPIO0. It is default option.*/
	SW_LED_MODE1= 1, /*  2 LEDs, through LED0 and LED1. For ALPHA. */
	SW_LED_MODE2 = 2, /*  SW control 1 LED via GPIO0, customized for AzWave
			   * 8187 minicard. */
	SW_LED_MODE3 = 3, /*  SW control 1 LED via GPIO0, customized for Sercomm
			   * Printer Server case. */
	SW_LED_MODE4 = 4, /* for Edimax / Belkin */
	SW_LED_MODE5 = 5, /* for Sercomm / Belkin */
	SW_LED_MODE6 = 6, /* for 88CU minicard, porting from ce SW_LED_MODE7 */
	HW_LED = 50, /*  HW control 2 LEDs, LED0 and LED1 (there are 4
		      * different control modes, see MAC.CONFIG1 for details.)*/
	LED_ST_NONE = 99,
};

void LedControl8188eu(struct adapter *padapter, enum LED_CTL_MODE	LedAction);

struct led_priv{
	/* add for led control */
	struct LED_871x			SwLed0;
	struct LED_871x			SwLed1;
	enum LED_STRATEGY_871x	LedStrategy;
	u8	bRegUseLed;
	void (*LedControlHandler)(struct adapter *padapter,
				  enum LED_CTL_MODE LedAction);
	/* add for led control */
};

#define rtw_led_control(adapt, action) \
	do { \
		if ((adapt)->ledpriv.LedControlHandler) \
			(adapt)->ledpriv.LedControlHandler((adapt), (action)); \
	} while (0)

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 15, 0)
void BlinkTimerCallback(struct timer_list *t);
#else
void BlinkTimerCallback(void *data);
#endif
void BlinkWorkItemCallback(struct work_struct *work);

void ResetLedStatus(struct LED_871x * pLed);

void InitLed871x(struct adapter *padapter, struct LED_871x *pLed,
		 enum LED_PIN_871x LedPin);

void DeInitLed871x(struct LED_871x *pLed);

/* hal... */
void BlinkHandler(struct LED_871x * pLed);
void SwLedOn(struct adapter *padapter, struct LED_871x *pLed);
void SwLedOff(struct adapter *padapter, struct LED_871x *pLed);

#endif /* __RTW_LED_H_ */
