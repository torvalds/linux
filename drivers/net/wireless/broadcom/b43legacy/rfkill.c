// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver
  RFKILL support

  Copyright (c) 2007 Michael Buesch <m@bues.ch>


*/

#include "radio.h"
#include "b43legacy.h"


/* Returns TRUE, if the radio is enabled in hardware. */
bool b43legacy_is_hw_radio_enabled(struct b43legacy_wldev *dev)
{
	if (dev->dev->id.revision >= 3) {
		if (!(b43legacy_read32(dev, B43legacy_MMIO_RADIO_HWENABLED_HI)
		      & B43legacy_MMIO_RADIO_HWENABLED_HI_MASK))
			return true;
	} else {
		/* To prevent CPU fault on PPC, do not read a register
		 * unless the interface is started; however, on resume
		 * for hibernation, this routine is entered early. When
		 * that happens, unconditionally return TRUE.
		 */
		if (b43legacy_status(dev) < B43legacy_STAT_STARTED)
			return true;
		if (b43legacy_read16(dev, B43legacy_MMIO_RADIO_HWENABLED_LO)
		    & B43legacy_MMIO_RADIO_HWENABLED_LO_MASK)
			return true;
	}
	return false;
}

/* The poll callback for the hardware button. */
void b43legacy_rfkill_poll(struct ieee80211_hw *hw)
{
	struct b43legacy_wl *wl = hw_to_b43legacy_wl(hw);
	struct b43legacy_wldev *dev = wl->current_dev;
	struct ssb_bus *bus = dev->dev->bus;
	bool enabled;
	bool brought_up = false;

	mutex_lock(&wl->mutex);
	if (unlikely(b43legacy_status(dev) < B43legacy_STAT_INITIALIZED)) {
		if (ssb_bus_powerup(bus, 0)) {
			mutex_unlock(&wl->mutex);
			return;
		}
		ssb_device_enable(dev->dev, 0);
		brought_up = true;
	}

	enabled = b43legacy_is_hw_radio_enabled(dev);

	if (unlikely(enabled != dev->radio_hw_enable)) {
		dev->radio_hw_enable = enabled;
		b43legacyinfo(wl, "Radio hardware status changed to %s\n",
			enabled ? "ENABLED" : "DISABLED");
		wiphy_rfkill_set_hw_state(hw->wiphy, !enabled);
		if (enabled != dev->phy.radio_on) {
			if (enabled)
				b43legacy_radio_turn_on(dev);
			else
				b43legacy_radio_turn_off(dev, 0);
		}
	}

	if (brought_up) {
		ssb_device_disable(dev->dev, 0);
		ssb_bus_may_powerdown(bus);
	}

	mutex_unlock(&wl->mutex);
}
