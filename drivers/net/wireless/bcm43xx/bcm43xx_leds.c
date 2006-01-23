/*

  Broadcom BCM43xx wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mbuesch@freenet.de>
                     Danny van Dyk <kugelfang@gentoo.org>
                     Andreas Jaggi <andreas.jaggi@waterwave.ch>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; see the file COPYING.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Steet, Fifth Floor,
  Boston, MA 02110-1301, USA.

*/

#include "bcm43xx_leds.h"
#include "bcm43xx.h"

#include <asm/bitops.h>


static void bcm43xx_led_changestate(struct bcm43xx_led *led)
{
	struct bcm43xx_private *bcm = led->bcm;
	const int index = bcm43xx_led_index(led);
	u16 ledctl;

	assert(index >= 0 && index < BCM43xx_NR_LEDS);
	assert(led->blink_interval);
	ledctl = bcm43xx_read16(bcm, BCM43xx_MMIO_GPIO_CONTROL);
	__change_bit(index, (unsigned long *)(&ledctl));
	bcm43xx_write16(bcm, BCM43xx_MMIO_GPIO_CONTROL, ledctl);
}

static void bcm43xx_led_blink(unsigned long d)
{
	struct bcm43xx_led *led = (struct bcm43xx_led *)d;
	struct bcm43xx_private *bcm = led->bcm;
	unsigned long flags;

	spin_lock_irqsave(&bcm->lock, flags);
	if (led->blink_interval) {
		bcm43xx_led_changestate(led);
		mod_timer(&led->blink_timer, jiffies + led->blink_interval);
	}
	spin_unlock_irqrestore(&bcm->lock, flags);
}

static void bcm43xx_led_blink_start(struct bcm43xx_led *led,
				    unsigned long interval)
{
	led->blink_interval = interval;
	bcm43xx_led_changestate(led);
	led->blink_timer.expires = jiffies + interval;
	add_timer(&led->blink_timer);
}

static void bcm43xx_led_blink_stop(struct bcm43xx_led *led, int sync)
{
	struct bcm43xx_private *bcm = led->bcm;
	const int index = bcm43xx_led_index(led);
	u16 ledctl;

	if (!led->blink_interval)
		return;
	if (unlikely(sync))
		del_timer_sync(&led->blink_timer);
	else
		del_timer(&led->blink_timer);
	led->blink_interval = 0;

	/* Make sure the LED is turned off. */
	assert(index >= 0 && index < BCM43xx_NR_LEDS);
	ledctl = bcm43xx_read16(bcm, BCM43xx_MMIO_GPIO_CONTROL);
	if (led->activelow)
		ledctl |= (1 << index);
	else
		ledctl &= ~(1 << index);
	bcm43xx_write16(bcm, BCM43xx_MMIO_GPIO_CONTROL, ledctl);
}

int bcm43xx_leds_init(struct bcm43xx_private *bcm)
{
	struct bcm43xx_led *led;
	u8 sprom[4];
	int i;

	sprom[0] = bcm->sprom.wl0gpio0;
	sprom[1] = bcm->sprom.wl0gpio1;
	sprom[2] = bcm->sprom.wl0gpio2;
	sprom[3] = bcm->sprom.wl0gpio3;

	for (i = 0; i < BCM43xx_NR_LEDS; i++) {
		led = &(bcm->leds[i]);
		led->bcm = bcm;
		init_timer(&led->blink_timer);
		led->blink_timer.data = (unsigned long)led;
		led->blink_timer.function = bcm43xx_led_blink;

		if (sprom[i] == 0xFF) {
			/* SPROM information not set. */
			switch (i) {
			case 0:
				if (bcm->board_vendor == PCI_VENDOR_ID_COMPAQ)
					led->behaviour = BCM43xx_LED_RADIO_ALL;
				else
					led->behaviour = BCM43xx_LED_ACTIVITY;
				break;
			case 1:
				led->behaviour = BCM43xx_LED_RADIO_B;
				break;
			case 2:
				led->behaviour = BCM43xx_LED_RADIO_A;
				break;
			case 3:
				led->behaviour = BCM43xx_LED_OFF;
				break;
			default:
				assert(0);
			}
		} else {
			led->behaviour = sprom[i] & BCM43xx_LED_BEHAVIOUR;
			led->activelow = !!(sprom[i] & BCM43xx_LED_ACTIVELOW);
		}
	}

	return 0;
}

void bcm43xx_leds_exit(struct bcm43xx_private *bcm)
{
	struct bcm43xx_led *led;
	int i;

	for (i = 0; i < BCM43xx_NR_LEDS; i++) {
		led = &(bcm->leds[i]);
		bcm43xx_led_blink_stop(led, 1);
	}
	bcm43xx_leds_turn_off(bcm);
}

void bcm43xx_leds_update(struct bcm43xx_private *bcm, int activity)
{
	struct bcm43xx_led *led;
	struct bcm43xx_radioinfo *radio = bcm->current_core->radio;
	struct bcm43xx_phyinfo *phy = bcm->current_core->phy;
	const int transferring = (jiffies - bcm->stats.last_tx) < BCM43xx_LED_XFER_THRES;
	int i, turn_on = 0;
	unsigned long interval = 0;
	u16 ledctl;

	ledctl = bcm43xx_read16(bcm, BCM43xx_MMIO_GPIO_CONTROL);
	for (i = 0; i < BCM43xx_NR_LEDS; i++) {
		led = &(bcm->leds[i]);
		if (led->behaviour == BCM43xx_LED_INACTIVE)
			continue;

		switch (led->behaviour) {
		case BCM43xx_LED_OFF:
			turn_on = 0;
			break;
		case BCM43xx_LED_ON:
			turn_on = 1;
			break;
		case BCM43xx_LED_ACTIVITY:
			turn_on = activity;
			break;
		case BCM43xx_LED_RADIO_ALL:
			turn_on = radio->enabled;
			break;
		case BCM43xx_LED_RADIO_A:
			turn_on = (radio->enabled && phy->type == BCM43xx_PHYTYPE_A);
			break;
		case BCM43xx_LED_RADIO_B:
			turn_on = (radio->enabled &&
				   (phy->type == BCM43xx_PHYTYPE_B ||
				    phy->type == BCM43xx_PHYTYPE_G));
			break;
		case BCM43xx_LED_MODE_BG:
			turn_on = 0;
			if (phy->type == BCM43xx_PHYTYPE_G &&
			    1/*FIXME: using G rates.*/)
				turn_on = 1;
			break;
		case BCM43xx_LED_TRANSFER:
			if (transferring)
				bcm43xx_led_blink_start(led, BCM43xx_LEDBLINK_MEDIUM);
			else
				bcm43xx_led_blink_stop(led, 0);
			continue;
		case BCM43xx_LED_APTRANSFER:
			if (bcm->ieee->iw_mode == IW_MODE_MASTER) {
				if (transferring) {
					interval = BCM43xx_LEDBLINK_FAST;
					turn_on = 1;
				}
			} else {
				turn_on = 1;
				if (0/*TODO: not assoc*/)
					interval = BCM43xx_LEDBLINK_SLOW;
				else if (transferring)
					interval = BCM43xx_LEDBLINK_FAST;
				else
					turn_on = 0;
			}
			if (turn_on)
				bcm43xx_led_blink_start(led, interval);
			else
				bcm43xx_led_blink_stop(led, 0);
			continue;
		case BCM43xx_LED_WEIRD:
			//TODO
			turn_on = 0;
			break;
		case BCM43xx_LED_ASSOC:
			if (1/*TODO: associated*/)
				turn_on = 1;
			break;
		default:
			assert(0);
		};

		if (led->activelow)
			turn_on = !turn_on;
		if (turn_on)
			ledctl |= (1 << i);
		else
			ledctl &= ~(1 << i);
	}
	bcm43xx_write16(bcm, BCM43xx_MMIO_GPIO_CONTROL, ledctl);
}

void bcm43xx_leds_turn_off(struct bcm43xx_private *bcm)
{
	struct bcm43xx_led *led;
	u16 ledctl = 0;
	int i;

	for (i = 0; i < BCM43xx_NR_LEDS; i++) {
		led = &(bcm->leds[i]);
		if (led->behaviour == BCM43xx_LED_INACTIVE)
			continue;
		if (led->activelow)
			ledctl |= (1 << i);
	}
	bcm43xx_write16(bcm, BCM43xx_MMIO_GPIO_CONTROL, ledctl);
}

/* vim: set ts=8 sw=8 sts=8: */
