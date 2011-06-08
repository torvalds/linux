#ifndef __RTL8712_LED_H
#define __RTL8712_LED_H

#include "osdep_service.h"
#include "drv_types.h"

/*===========================================================================
 * LED customization.
 *===========================================================================
 */
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
	LED_CTL_STOP_WPS_FAIL_OVERLAP = 13,
};

#define IS_LED_WPS_BLINKING(_LED_871x)	\
	(((struct LED_871x *)_LED_871x)->CurrLedState == LED_BLINK_WPS \
	|| ((struct LED_871x *)_LED_871x)->CurrLedState == LED_BLINK_WPS_STOP \
	|| ((struct LED_871x *)_LED_871x)->bLedWPSBlinkInProgress)

#define IS_LED_BLINKING(_LED_871x)	\
		(((struct LED_871x *)_LED_871x)->bLedWPSBlinkInProgress \
		|| ((struct LED_871x *)_LED_871x)->bLedScanBlinkInProgress)

enum LED_PIN_871x {
	LED_PIN_GPIO0,
	LED_PIN_LED0,
	LED_PIN_LED1
};

/*===========================================================================
 * LED customization.
 *===========================================================================
 */
enum LED_STRATEGY_871x {
	SW_LED_MODE0, /* SW control 1 LED via GPIO0. It is default option. */
	SW_LED_MODE1, /* 2 LEDs, through LED0 and LED1. For ALPHA. */
	SW_LED_MODE2, /* SW control 1 LED via GPIO0,
		       * custom for AzWave 8187 minicard. */
	SW_LED_MODE3, /* SW control 1 LED via GPIO0,
		       *  customized for Sercomm Printer Server case.*/
	SW_LED_MODE4, /*for Edimax / Belkin*/
	SW_LED_MODE5, /*for Sercomm / Belkin*/
	SW_LED_MODE6, /*for WNC / Corega*/
	HW_LED, /* HW control 2 LEDs, LED0 and LED1 (there are 4 different
		 * control modes, see MAC.CONFIG1 for details.)*/
};

struct LED_871x {
	struct _adapter		*padapter;
	enum LED_PIN_871x	LedPin;	/* Implementation for this SW led. */
	u32			CurrLedState; /* Current LED state. */
	u8			bLedOn; /* true if LED is ON */
	u8			bSWLedCtrl;
	u8			bLedBlinkInProgress; /*true if blinking */
	u8			bLedNoLinkBlinkInProgress;
	u8			bLedLinkBlinkInProgress;
	u8			bLedStartToLinkBlinkInProgress;
	u8			bLedScanBlinkInProgress;
	u8			bLedWPSBlinkInProgress;
	u32			BlinkTimes; /* No. times to toggle for blink.*/
	u32			BlinkingLedState; /* Next state for blinking,
						   * either LED_ON or OFF.*/

	struct timer_list	BlinkTimer; /* Timer object for led blinking.*/
	_workitem		BlinkWorkItem; /* Workitem used by BlinkTimer */
};

struct led_priv {
	/* add for led control */
	struct LED_871x		SwLed0;
	struct LED_871x		SwLed1;
	enum LED_STRATEGY_871x	LedStrategy;
	u8			bRegUseLed;
	void (*LedControlHandler)(struct _adapter *padapter,
				  enum LED_CTL_MODE LedAction);
	/* add for led control */
};

/*===========================================================================
 * Interface to manipulate LED objects.
 *===========================================================================*/
void r8712_InitSwLeds(struct _adapter *padapter);
void r8712_DeInitSwLeds(struct _adapter *padapter);
void LedControl871x(struct _adapter *padapter, enum LED_CTL_MODE LedAction);

#endif

