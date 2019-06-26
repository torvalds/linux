/*

  Broadcom B43 wireless driver
  LED control

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
  Copyright (c) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2007 Michael Buesch <m@bues.ch>
  Copyright (c) 2005 Danny van Dyk <kugelfang@gentoo.org>
  Copyright (c) 2005 Andreas Jaggi <andreas.jaggi@waterwave.ch>

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
#include "rfkill.h"


static void b43_led_turn_on(struct b43_wldev *dev, u8 led_index,
			    bool activelow)
{
	u16 ctl;

	ctl = b43_read16(dev, B43_MMIO_GPIO_CONTROL);
	if (activelow)
		ctl &= ~(1 << led_index);
	else
		ctl |= (1 << led_index);
	b43_write16(dev, B43_MMIO_GPIO_CONTROL, ctl);
}

static void b43_led_turn_off(struct b43_wldev *dev, u8 led_index,
			     bool activelow)
{
	u16 ctl;

	ctl = b43_read16(dev, B43_MMIO_GPIO_CONTROL);
	if (activelow)
		ctl |= (1 << led_index);
	else
		ctl &= ~(1 << led_index);
	b43_write16(dev, B43_MMIO_GPIO_CONTROL, ctl);
}

static void b43_led_update(struct b43_wldev *dev,
			   struct b43_led *led)
{
	bool radio_enabled;
	bool turn_on;

	if (!led->wl)
		return;

	radio_enabled = (dev->phy.radio_on && dev->radio_hw_enable);

	/* The led->state read is racy, but we don't care. In case we raced
	 * with the brightness_set handler, we will be called again soon
	 * to fixup our state. */
	if (radio_enabled)
		turn_on = atomic_read(&led->state) != LED_OFF;
	else
		turn_on = false;
	if (turn_on == led->hw_state)
		return;
	led->hw_state = turn_on;

	if (turn_on)
		b43_led_turn_on(dev, led->index, led->activelow);
	else
		b43_led_turn_off(dev, led->index, led->activelow);
}

static void b43_leds_work(struct work_struct *work)
{
	struct b43_leds *leds = container_of(work, struct b43_leds, work);
	struct b43_wl *wl = container_of(leds, struct b43_wl, leds);
	struct b43_wldev *dev;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (unlikely(!dev || b43_status(dev) < B43_STAT_STARTED))
		goto out_unlock;

	b43_led_update(dev, &wl->leds.led_tx);
	b43_led_update(dev, &wl->leds.led_rx);
	b43_led_update(dev, &wl->leds.led_radio);
	b43_led_update(dev, &wl->leds.led_assoc);

out_unlock:
	mutex_unlock(&wl->mutex);
}

/* Callback from the LED subsystem. */
static void b43_led_brightness_set(struct led_classdev *led_dev,
				   enum led_brightness brightness)
{
	struct b43_led *led = container_of(led_dev, struct b43_led, led_dev);
	struct b43_wl *wl = led->wl;

	if (likely(!wl->leds.stop)) {
		atomic_set(&led->state, brightness);
		ieee80211_queue_work(wl->hw, &wl->leds.work);
	}
}

static int b43_register_led(struct b43_wldev *dev, struct b43_led *led,
			    const char *name, const char *default_trigger,
			    u8 led_index, bool activelow)
{
	int err;

	if (led->wl)
		return -EEXIST;
	if (!default_trigger)
		return -EINVAL;
	led->wl = dev->wl;
	led->index = led_index;
	led->activelow = activelow;
	strlcpy(led->name, name, sizeof(led->name));
	atomic_set(&led->state, 0);

	led->led_dev.name = led->name;
	led->led_dev.default_trigger = default_trigger;
	led->led_dev.brightness_set = b43_led_brightness_set;

	err = led_classdev_register(dev->dev->dev, &led->led_dev);
	if (err) {
		b43warn(dev->wl, "LEDs: Failed to register %s\n", name);
		led->wl = NULL;
		return err;
	}

	return 0;
}

static void b43_unregister_led(struct b43_led *led)
{
	if (!led->wl)
		return;
	led_classdev_unregister(&led->led_dev);
	led->wl = NULL;
}

static void b43_map_led(struct b43_wldev *dev,
			u8 led_index,
			enum b43_led_behaviour behaviour,
			bool activelow)
{
	struct ieee80211_hw *hw = dev->wl->hw;
	char name[B43_LED_MAX_NAME_LEN + 1];

	/* Map the b43 specific LED behaviour value to the
	 * generic LED triggers. */
	switch (behaviour) {
	case B43_LED_INACTIVE:
	case B43_LED_OFF:
	case B43_LED_ON:
		break;
	case B43_LED_ACTIVITY:
	case B43_LED_TRANSFER:
	case B43_LED_APTRANSFER:
		snprintf(name, sizeof(name),
			 "b43-%s::tx", wiphy_name(hw->wiphy));
		b43_register_led(dev, &dev->wl->leds.led_tx, name,
				 ieee80211_get_tx_led_name(hw),
				 led_index, activelow);
		snprintf(name, sizeof(name),
			 "b43-%s::rx", wiphy_name(hw->wiphy));
		b43_register_led(dev, &dev->wl->leds.led_rx, name,
				 ieee80211_get_rx_led_name(hw),
				 led_index, activelow);
		break;
	case B43_LED_RADIO_ALL:
	case B43_LED_RADIO_A:
	case B43_LED_RADIO_B:
	case B43_LED_MODE_BG:
		snprintf(name, sizeof(name),
			 "b43-%s::radio", wiphy_name(hw->wiphy));
		b43_register_led(dev, &dev->wl->leds.led_radio, name,
				 ieee80211_get_radio_led_name(hw),
				 led_index, activelow);
		break;
	case B43_LED_WEIRD:
	case B43_LED_ASSOC:
		snprintf(name, sizeof(name),
			 "b43-%s::assoc", wiphy_name(hw->wiphy));
		b43_register_led(dev, &dev->wl->leds.led_assoc, name,
				 ieee80211_get_assoc_led_name(hw),
				 led_index, activelow);
		break;
	default:
		b43warn(dev->wl, "LEDs: Unknown behaviour 0x%02X\n",
			behaviour);
		break;
	}
}

static void b43_led_get_sprominfo(struct b43_wldev *dev,
				  unsigned int led_index,
				  enum b43_led_behaviour *behaviour,
				  bool *activelow)
{
	u8 sprom[4];

	sprom[0] = dev->dev->bus_sprom->gpio0;
	sprom[1] = dev->dev->bus_sprom->gpio1;
	sprom[2] = dev->dev->bus_sprom->gpio2;
	sprom[3] = dev->dev->bus_sprom->gpio3;

	if ((sprom[0] & sprom[1] & sprom[2] & sprom[3]) == 0xff) {
		/* There is no LED information in the SPROM
		 * for this LED. Hardcode it here. */
		*activelow = false;
		switch (led_index) {
		case 0:
			*behaviour = B43_LED_ACTIVITY;
			*activelow = true;
			if (dev->dev->board_vendor == PCI_VENDOR_ID_COMPAQ)
				*behaviour = B43_LED_RADIO_ALL;
			break;
		case 1:
			*behaviour = B43_LED_RADIO_B;
			if (dev->dev->board_vendor == PCI_VENDOR_ID_ASUSTEK)
				*behaviour = B43_LED_ASSOC;
			break;
		case 2:
			*behaviour = B43_LED_RADIO_A;
			break;
		case 3:
			*behaviour = B43_LED_OFF;
			break;
		default:
			*behaviour = B43_LED_OFF;
			B43_WARN_ON(1);
			return;
		}
	} else {
		/* keep LED disabled if no mapping is defined */
		if (sprom[led_index] == 0xff)
			*behaviour = B43_LED_OFF;
		else
			*behaviour = sprom[led_index] & B43_LED_BEHAVIOUR;
		*activelow = !!(sprom[led_index] & B43_LED_ACTIVELOW);
	}
}

void b43_leds_init(struct b43_wldev *dev)
{
	struct b43_led *led;
	unsigned int i;
	enum b43_led_behaviour behaviour;
	bool activelow;

	/* Sync the RF-kill LED state (if we have one) with radio and switch states. */
	led = &dev->wl->leds.led_radio;
	if (led->wl) {
		if (dev->phy.radio_on && b43_is_hw_radio_enabled(dev)) {
			b43_led_turn_on(dev, led->index, led->activelow);
			led->hw_state = true;
			atomic_set(&led->state, 1);
		} else {
			b43_led_turn_off(dev, led->index, led->activelow);
			led->hw_state = false;
			atomic_set(&led->state, 0);
		}
	}

	/* Initialize TX/RX/ASSOC leds */
	led = &dev->wl->leds.led_tx;
	if (led->wl) {
		b43_led_turn_off(dev, led->index, led->activelow);
		led->hw_state = false;
		atomic_set(&led->state, 0);
	}
	led = &dev->wl->leds.led_rx;
	if (led->wl) {
		b43_led_turn_off(dev, led->index, led->activelow);
		led->hw_state = false;
		atomic_set(&led->state, 0);
	}
	led = &dev->wl->leds.led_assoc;
	if (led->wl) {
		b43_led_turn_off(dev, led->index, led->activelow);
		led->hw_state = false;
		atomic_set(&led->state, 0);
	}

	/* Initialize other LED states. */
	for (i = 0; i < B43_MAX_NR_LEDS; i++) {
		b43_led_get_sprominfo(dev, i, &behaviour, &activelow);
		switch (behaviour) {
		case B43_LED_OFF:
			b43_led_turn_off(dev, i, activelow);
			break;
		case B43_LED_ON:
			b43_led_turn_on(dev, i, activelow);
			break;
		default:
			/* Leave others as-is. */
			break;
		}
	}

	dev->wl->leds.stop = 0;
}

void b43_leds_exit(struct b43_wldev *dev)
{
	struct b43_leds *leds = &dev->wl->leds;

	b43_led_turn_off(dev, leds->led_tx.index, leds->led_tx.activelow);
	b43_led_turn_off(dev, leds->led_rx.index, leds->led_rx.activelow);
	b43_led_turn_off(dev, leds->led_assoc.index, leds->led_assoc.activelow);
	b43_led_turn_off(dev, leds->led_radio.index, leds->led_radio.activelow);
}

void b43_leds_stop(struct b43_wldev *dev)
{
	struct b43_leds *leds = &dev->wl->leds;

	leds->stop = 1;
	cancel_work_sync(&leds->work);
}

void b43_leds_register(struct b43_wldev *dev)
{
	unsigned int i;
	enum b43_led_behaviour behaviour;
	bool activelow;

	INIT_WORK(&dev->wl->leds.work, b43_leds_work);

	/* Register the LEDs to the LED subsystem. */
	for (i = 0; i < B43_MAX_NR_LEDS; i++) {
		b43_led_get_sprominfo(dev, i, &behaviour, &activelow);
		b43_map_led(dev, i, behaviour, activelow);
	}
}

void b43_leds_unregister(struct b43_wl *wl)
{
	struct b43_leds *leds = &wl->leds;

	b43_unregister_led(&leds->led_tx);
	b43_unregister_led(&leds->led_rx);
	b43_unregister_led(&leds->led_assoc);
	b43_unregister_led(&leds->led_radio);
}
