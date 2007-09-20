/*

  Broadcom B43 wireless driver

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
                     Stefano Brivio <st3@riseup.net>
                     Michael Buesch <mb@bu3sch.de>
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

#include "b43.h"
#include "leds.h"
#include "main.h"

static void b43_led_changestate(struct b43_led *led)
{
	struct b43_wldev *dev = led->dev;
	const int index = led->index;
	u16 ledctl;

	B43_WARN_ON(!(index >= 0 && index < B43_NR_LEDS));
	B43_WARN_ON(!led->blink_interval);
	ledctl = b43_read16(dev, B43_MMIO_GPIO_CONTROL);
	ledctl ^= (1 << index);
	b43_write16(dev, B43_MMIO_GPIO_CONTROL, ledctl);
}

static void b43_led_blink(unsigned long d)
{
	struct b43_led *led = (struct b43_led *)d;
	struct b43_wldev *dev = led->dev;
	unsigned long flags;

	spin_lock_irqsave(&dev->wl->leds_lock, flags);
	if (led->blink_interval) {
		b43_led_changestate(led);
		mod_timer(&led->blink_timer, jiffies + led->blink_interval);
	}
	spin_unlock_irqrestore(&dev->wl->leds_lock, flags);
}

static void b43_led_blink_start(struct b43_led *led, unsigned long interval)
{
	if (led->blink_interval)
		return;
	led->blink_interval = interval;
	b43_led_changestate(led);
	led->blink_timer.expires = jiffies + interval;
	add_timer(&led->blink_timer);
}

static void b43_led_blink_stop(struct b43_led *led, int sync)
{
	struct b43_wldev *dev = led->dev;
	const int index = led->index;
	u16 ledctl;

	if (!led->blink_interval)
		return;
	if (unlikely(sync))
		del_timer_sync(&led->blink_timer);
	else
		del_timer(&led->blink_timer);
	led->blink_interval = 0;

	/* Make sure the LED is turned off. */
	B43_WARN_ON(!(index >= 0 && index < B43_NR_LEDS));
	ledctl = b43_read16(dev, B43_MMIO_GPIO_CONTROL);
	if (led->activelow)
		ledctl |= (1 << index);
	else
		ledctl &= ~(1 << index);
	b43_write16(dev, B43_MMIO_GPIO_CONTROL, ledctl);
}

static void b43_led_init_hardcoded(struct b43_wldev *dev,
				   struct b43_led *led, int led_index)
{
	struct ssb_bus *bus = dev->dev->bus;

	/* This function is called, if the behaviour (and activelow)
	 * information for a LED is missing in the SPROM.
	 * We hardcode the behaviour values for various devices here.
	 * Note that the B43_LED_TEST_XXX behaviour values can
	 * be used to figure out which led is mapped to which index.
	 */

	switch (led_index) {
	case 0:
		led->behaviour = B43_LED_ACTIVITY;
		led->activelow = 1;
		if (bus->boardinfo.vendor == PCI_VENDOR_ID_COMPAQ)
			led->behaviour = B43_LED_RADIO_ALL;
		break;
	case 1:
		led->behaviour = B43_LED_RADIO_B;
		if (bus->boardinfo.vendor == PCI_VENDOR_ID_ASUSTEK)
			led->behaviour = B43_LED_ASSOC;
		break;
	case 2:
		led->behaviour = B43_LED_RADIO_A;
		break;
	case 3:
		led->behaviour = B43_LED_OFF;
		break;
	default:
		B43_WARN_ON(1);
	}
}

int b43_leds_init(struct b43_wldev *dev)
{
	struct b43_led *led;
	u8 sprom[4];
	int i;

	sprom[0] = dev->dev->bus->sprom.r1.gpio0;
	sprom[1] = dev->dev->bus->sprom.r1.gpio1;
	sprom[2] = dev->dev->bus->sprom.r1.gpio2;
	sprom[3] = dev->dev->bus->sprom.r1.gpio3;

	for (i = 0; i < B43_NR_LEDS; i++) {
		led = &(dev->leds[i]);
		led->index = i;
		led->dev = dev;
		setup_timer(&led->blink_timer,
			    b43_led_blink, (unsigned long)led);

		if (sprom[i] == 0xFF) {
			b43_led_init_hardcoded(dev, led, i);
		} else {
			led->behaviour = sprom[i] & B43_LED_BEHAVIOUR;
			led->activelow = !!(sprom[i] & B43_LED_ACTIVELOW);
		}
	}

	return 0;
}

void b43_leds_exit(struct b43_wldev *dev)
{
	struct b43_led *led;
	int i;

	for (i = 0; i < B43_NR_LEDS; i++) {
		led = &(dev->leds[i]);
		b43_led_blink_stop(led, 1);
	}
	b43_leds_switch_all(dev, 0);
}

void b43_leds_update(struct b43_wldev *dev, int activity)
{
	struct b43_led *led;
	struct b43_phy *phy = &dev->phy;
	const int transferring =
	    (jiffies - dev->stats.last_tx) < B43_LED_XFER_THRES;
	int i, turn_on;
	unsigned long interval = 0;
	u16 ledctl;
	unsigned long flags;

	spin_lock_irqsave(&dev->wl->leds_lock, flags);
	ledctl = b43_read16(dev, B43_MMIO_GPIO_CONTROL);
	for (i = 0; i < B43_NR_LEDS; i++) {
		led = &(dev->leds[i]);

		turn_on = 0;
		switch (led->behaviour) {
		case B43_LED_INACTIVE:
			continue;
		case B43_LED_OFF:
			break;
		case B43_LED_ON:
			turn_on = 1;
			break;
		case B43_LED_ACTIVITY:
			turn_on = activity;
			break;
		case B43_LED_RADIO_ALL:
			turn_on = phy->radio_on && b43_is_hw_radio_enabled(dev);
			break;
		case B43_LED_RADIO_A:
			turn_on = (phy->radio_on && b43_is_hw_radio_enabled(dev)
				   && phy->type == B43_PHYTYPE_A);
			break;
		case B43_LED_RADIO_B:
			turn_on = (phy->radio_on && b43_is_hw_radio_enabled(dev)
				   && (phy->type == B43_PHYTYPE_B
				       || phy->type == B43_PHYTYPE_G));
			break;
		case B43_LED_MODE_BG:
			if (phy->type == B43_PHYTYPE_G
			    && b43_is_hw_radio_enabled(dev)
			    && 1 /*FIXME: using G rates. */ )
				turn_on = 1;
			break;
		case B43_LED_TRANSFER:
			if (transferring)
				b43_led_blink_start(led, B43_LEDBLINK_MEDIUM);
			else
				b43_led_blink_stop(led, 0);
			continue;
		case B43_LED_APTRANSFER:
			if (b43_is_mode(dev->wl, IEEE80211_IF_TYPE_AP)) {
				if (transferring) {
					interval = B43_LEDBLINK_FAST;
					turn_on = 1;
				}
			} else {
				turn_on = 1;
				if (0 /*TODO: not assoc */ )
					interval = B43_LEDBLINK_SLOW;
				else if (transferring)
					interval = B43_LEDBLINK_FAST;
				else
					turn_on = 0;
			}
			if (turn_on)
				b43_led_blink_start(led, interval);
			else
				b43_led_blink_stop(led, 0);
			continue;
		case B43_LED_WEIRD:
			//TODO
			break;
		case B43_LED_ASSOC:
			if (1 /*dev->softmac->associated */ )
				turn_on = 1;
			break;
#ifdef CONFIG_B43_DEBUG
		case B43_LED_TEST_BLINKSLOW:
			b43_led_blink_start(led, B43_LEDBLINK_SLOW);
			continue;
		case B43_LED_TEST_BLINKMEDIUM:
			b43_led_blink_start(led, B43_LEDBLINK_MEDIUM);
			continue;
		case B43_LED_TEST_BLINKFAST:
			b43_led_blink_start(led, B43_LEDBLINK_FAST);
			continue;
#endif /* CONFIG_B43_DEBUG */
		default:
			B43_WARN_ON(1);
		};

		if (led->activelow)
			turn_on = !turn_on;
		if (turn_on)
			ledctl |= (1 << i);
		else
			ledctl &= ~(1 << i);
	}
	b43_write16(dev, B43_MMIO_GPIO_CONTROL, ledctl);
	spin_unlock_irqrestore(&dev->wl->leds_lock, flags);
}

void b43_leds_switch_all(struct b43_wldev *dev, int on)
{
	struct b43_led *led;
	u16 ledctl;
	int i;
	int bit_on;
	unsigned long flags;

	spin_lock_irqsave(&dev->wl->leds_lock, flags);
	ledctl = b43_read16(dev, B43_MMIO_GPIO_CONTROL);
	for (i = 0; i < B43_NR_LEDS; i++) {
		led = &(dev->leds[i]);
		if (led->behaviour == B43_LED_INACTIVE)
			continue;
		if (on)
			bit_on = led->activelow ? 0 : 1;
		else
			bit_on = led->activelow ? 1 : 0;
		if (bit_on)
			ledctl |= (1 << i);
		else
			ledctl &= ~(1 << i);
	}
	b43_write16(dev, B43_MMIO_GPIO_CONTROL, ledctl);
	spin_unlock_irqrestore(&dev->wl->leds_lock, flags);
}
