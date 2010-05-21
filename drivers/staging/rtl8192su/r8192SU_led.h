/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/
#ifndef __INC_HAL8192USBLED_H
#define __INC_HAL8192USBLED_H

#include <linux/types.h>
#include <linux/timer.h>

typedef	enum _LED_STATE_819xUsb{
	LED_UNKNOWN = 0,
	LED_ON = 1,
	LED_OFF = 2,
	LED_BLINK_NORMAL = 3,
	LED_BLINK_SLOWLY = 4,
	LED_POWER_ON_BLINK = 5,
	LED_SCAN_BLINK = 6, 
	LED_NO_LINK_BLINK = 7, 
	LED_BLINK_StartToBlink = 8,
	LED_BLINK_WPS = 9,	
	LED_TXRX_BLINK = 10,
	LED_BLINK_WPS_STOP = 11,	
	LED_BLINK_WPS_STOP_OVERLAP = 12,	
	
}LED_STATE_819xUsb;

#define IS_LED_WPS_BLINKING(_LED_819xUsb)	(((PLED_819xUsb)_LED_819xUsb)->CurrLedState==LED_BLINK_WPS \
												|| ((PLED_819xUsb)_LED_819xUsb)->CurrLedState==LED_BLINK_WPS_STOP \
												|| ((PLED_819xUsb)_LED_819xUsb)->bLedWPSBlinkInProgress)

#define IS_LED_BLINKING(_LED_819xUsb) 	(((PLED_819xUsb)_LED_819xUsb)->bLedWPSBlinkInProgress \
											||((PLED_819xUsb)_LED_819xUsb)->bLedScanBlinkInProgress)

typedef enum _LED_PIN_819xUsb{
	LED_PIN_GPIO0,
	LED_PIN_LED0,
	LED_PIN_LED1
}LED_PIN_819xUsb;

typedef	enum _LED_STRATEGY_819xUsb{
	SW_LED_MODE0, /* SW control 1 LED via GPIO0. It is default option. */
	SW_LED_MODE1, /* SW control for PCI Express */
	SW_LED_MODE2, /* SW control for Cameo. */
	SW_LED_MODE3, /* SW contorl for RunTop. */
	SW_LED_MODE4, /* SW control for Netcore */
	SW_LED_MODE5,
	HW_LED, /* HW control 2 LEDs, LED0 and LED1 (there are 4 different control modes) */
}LED_STRATEGY_819xUsb, *PLED_STRATEGY_819xUsb;

typedef struct _LED_819xUsb{
	struct net_device 		*dev;

	LED_PIN_819xUsb		LedPin;	

	LED_STATE_819xUsb	CurrLedState; 
	bool					bLedOn; 

	bool					bSWLedCtrl;

	bool					bLedBlinkInProgress; 
	bool					bLedNoLinkBlinkInProgress;
	bool					bLedLinkBlinkInProgress;
	bool					bLedStartToLinkBlinkInProgress;
	bool					bLedScanBlinkInProgress;
	bool					bLedWPSBlinkInProgress;
	
	u32					BlinkTimes; 
	LED_STATE_819xUsb	BlinkingLedState; 

	struct timer_list		BlinkTimer; 
} LED_819xUsb, *PLED_819xUsb;

void InitSwLeds(struct net_device *dev);
void DeInitSwLeds(struct net_device *dev);
void LedControl8192SUsb(struct net_device *dev,LED_CTL_MODE LedAction);

#endif

