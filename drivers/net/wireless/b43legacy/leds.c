/*

  Broadcom B43 wireless driver
  LED control

  Copyright (c) 2005 Martin Langer <martin-langer@gmx.de>,
  Copyright (c) 2005 Stefano Brivio <stefano.brivio@polimi.it>
  Copyright (c) 2005-2007 Michael Buesch <mb@bu3sch.de>
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

#include "b43legacy.h"
#include "leds.h"
#include "rfkill.h"


static void b43legacy_led_turn_on(struct b43legacy_wldev *dev, u8 led_index,
			    bool activelow)
{
	struct b43legacy_wl *wl = dev->wl;
	unsigned long flags;
	u16 ctl;

	spin_lock_irqsave(&wl->leds_lock, flags);
	ctl = b43legacy_read16(dev, B43legacy_MMIO_GPIO_CONTROL);
	if (activelow)
		ctl &= ~(1 << led_index);
	else
		ctl |= (1 << led_index);
	b43legacy_write16(dev, B43legacy_MMIO_GPIO_CONTROL, ctl);
	spin_unlock_irqrestore(&wl->leds_lock, flags);
}

static void b43legacy_led_turn_off(struct b43legacy_wldev *dev, u8 led_index,
			     bool activelow)
{
	struct b43legacy_wl *wl = dev->wl;
	unsigned long flags;
	u16 ctl;

	spin_lock_irqsave(&wl->leds_lock, flags);
	ctl = b43legacy_read16(dev, B43legacy_MMIO_GPIO_CONTROL);
	if (activelow)
		ctl |= (1 << led_index);
	else
		ctl &= ~(1 << led_index);
	b43legacy_write16(dev, B43legacy_MMIO_GPIO_CONTROL, ctl);
	spin_unlock_irqrestore(&wl->leds_lock, flags);
}

/* Callback from the LED subsystem. */
static void b43legacy_led_brightness_set(struct led_classdev *led_dev,
				   enum led_brightness brightness)
{
	struct b43legacy_led *led = container_of(led_dev, struct b43legacy_led,
				    led_dev);
	struct b43legacy_wldev *dev = led->dev;
	bool radio_enabled;

	/* Checking the radio-enabled status here is slightly racy,
	 * but we want to avoid the locking overhead and we don't care
	 * whether the LED has the wrong state for a second. */
	radio_enabled = (dev->phy.radio_on && dev->radio_hw_enable);

	if (brightness == LED_OFF || !radio_enabled)
		b43legacy_led_turn_off(dev, led->index, led->activelow);
	else
		b43legacy_led_turn_on(dev, led->index, led->activelow);
}

static int b43legacy_register_led(struct b43legacy_wldev *dev,
				  struct b43legacy_led *led,
				  const char *name,
				  const char *default_trigger,
				  u8 led_index, bool activelow)
{
	int err;

	b43legacy_led_turn_off(dev, led_index, activelow);
	if (led->dev)
		return -EEXIST;
	if (!default_trigger)
		return -EINVAL;
	led->dev = dev;
	led->index = led_index;
	led->activelow = activelow;
	strncpy(led->name, name, sizeof(led->name));

	led->led_dev.name = led->name;
	led->led_dev.default_trigger = default_trigger;
	led->led_dev.brightness_set = b43legacy_led_brightness_set;

	err = led_classdev_register(dev->dev->dev, &led->led_dev);
	if (err) {
		b43legacywarn(dev->wl, "LEDs: Failed to register %s\n", name);
		led->dev = NULL;
		return err;
	}
	return 0;
}

static void b43legacy_unregister_led(struct b43legacy_led *led)
{
	if (!led->dev)
		return;
	led_classdev_unregister(&led->led_dev);
	b43legacy_led_turn_off(led->dev, led->index, led->activelow);
	led->dev = NULL;
}

static void b43legacy_map_led(struct b43legacy_wldev *dev,
			u8 led_index,
			enum b43legacy_led_behaviour behaviour,
			bool activelow)
{
	struct ieee80211_hw *hw = dev->wl->hw;
	char name[B43legacy_LED_MAX_NAME_LEN + 1];

	/* Map the b43 specific LED behaviour value to the
	 * generic LED triggers. */
	switch (behaviour) {
	case B43legacy_LED_INACTIVE:
		break;
	case B43legacy_LED_OFF:
		b43legacy_led_turn_off(dev, led_index, activelow);
		break;
	case B43legacy_LED_ON:
		b43legacy_led_turn_on(dev, led_index, activelow);
		break;
	case B43legacy_LED_ACTIVITY:
	case B43legacy_LED_TRANSFER:
	case B43legacy_LED_APTRANSFER:
		snprintf(name, sizeof(name),
			 "b43legacy-%s::tx", wiphy_name(hw->wiphy));
		b43legacy_register_led(dev, &dev->led_tx, name,
				 ieee80211_get_tx_led_name(hw),
				 led_index, activelow);
		snprintf(name, sizeof(name),
			 "b43legacy-%s::rx", wiphy_name(hw->wiphy));
		b43legacy_register_led(dev, &dev->led_rx, name,
				 ieee80211_get_rx_led_name(hw),
				 led_index, activelow);
		break;
	case B43legacy_LED_RADIO_ALL:
	case B43legacy_LED_RADIO_A:
	case B43legacy_LED_RADIO_B:
	case B43legacy_LED_MODE_BG:
		snprintf(name, sizeof(name),
			 "b43legacy-%s::radio", wiphy_name(hw->wiphy));
		b43legacy_register_led(dev, &dev->led_radio, name,
				 ieee80211_get_radio_led_name(hw),
				 led_index, activelow);
		/* Sync the RF-kill LED state with radio and switch states. */
		if (dev->phy.radio_on && b43legacy_is_hw_radio_enabled(dev))
			b43legacy_led_turn_on(dev, led_index, activelow);
		break;
	case B43legacy_LED_WEIRD:
	case B43legacy_LED_ASSOC:
		snprintf(name, sizeof(name),
			 "b43legacy-%s::assoc", wiphy_name(hw->wiphy));
		b43legacy_register_led(dev, &dev->led_assoc, name,
				 ieee80211_get_assoc_led_name(hw),
				 led_index, activelow);
		break;
	default:
		b43legacywarn(dev->wl, "LEDs: Unknown behaviour 0x%02X\n",
			behaviour);
		break;
	}
}

void b43legacy_leds_init(struct b43legacy_wldev *dev)
{
	struct ssb_bus *bus = dev->dev->bus;
	u8 sprom[4];
	int i;
	enum b43legacy_led_behaviour behaviour;
	bool activelow;

	sprom[0] = bus->sprom.gpio0;
	sprom[1] = bus->sprom.gpio1;
	sprom[2] = bus->sprom.gpio2;
	sprom[3] = bus->sprom.gpio3;

	for (i = 0; i < 4; i++) {
		if (sprom[i] == 0xFF) {
			/* There is no LED information in the SPROM
			 * for this LED. Hardcode it here. */
			activelow = 0;
			switch (i) {
			case 0:
				behaviour = B43legacy_LED_ACTIVITY;
				activelow = 1;
				if (bus->boardinfo.vendor == PCI_VENDOR_ID_COMPAQ)
					behaviour = B43legacy_LED_RADIO_ALL;
				break;
			case 1:
				behaviour = B43legacy_LED_RADIO_B;
				if (bus->boardinfo.vendor == PCI_VENDOR_ID_ASUSTEK)
					behaviour = B43legacy_LED_ASSOC;
				break;
			case 2:
				behaviour = B43legacy_LED_RADIO_A;
				break;
			case 3:
				behaviour = B43legacy_LED_OFF;
				break;
			default:
				B43legacy_WARN_ON(1);
				return;
			}
		} else {
			behaviour = sprom[i] & B43legacy_LED_BEHAVIOUR;
			activelow = !!(sprom[i] & B43legacy_LED_ACTIVELOW);
		}
		b43legacy_map_led(dev, i, behaviour, activelow);
	}
}

void b43legacy_leds_exit(struct b43legacy_wldev *dev)
{
	b43legacy_unregister_led(&dev->led_tx);
	b43legacy_unregister_led(&dev->led_rx);
	b43legacy_unregister_led(&dev->led_assoc);
	b43legacy_unregister_led(&dev->led_radio);
}
