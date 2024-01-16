// SPDX-License-Identifier: GPL-2.0-or-later
/*

  Broadcom B43 wireless driver
  RFKILL support

  Copyright (c) 2007 Michael Buesch <m@bues.ch>


*/

#include "b43.h"


/* Returns TRUE, if the radio is enabled in hardware. */
bool b43_is_hw_radio_enabled(struct b43_wldev *dev)
{
	return !(b43_read32(dev, B43_MMIO_RADIO_HWENABLED_HI)
		& B43_MMIO_RADIO_HWENABLED_HI_MASK);
}

/* The poll callback for the hardware button. */
void b43_rfkill_poll(struct ieee80211_hw *hw)
{
	struct b43_wl *wl = hw_to_b43_wl(hw);
	struct b43_wldev *dev = wl->current_dev;
	bool enabled;
	bool brought_up = false;

	mutex_lock(&wl->mutex);
	if (unlikely(b43_status(dev) < B43_STAT_INITIALIZED)) {
		if (b43_bus_powerup(dev, 0)) {
			mutex_unlock(&wl->mutex);
			return;
		}
		b43_device_enable(dev, 0);
		brought_up = true;
	}

	enabled = b43_is_hw_radio_enabled(dev);

	if (unlikely(enabled != dev->radio_hw_enable)) {
		dev->radio_hw_enable = enabled;
		b43info(wl, "Radio hardware status changed to %s\n",
			enabled ? "ENABLED" : "DISABLED");
		wiphy_rfkill_set_hw_state(hw->wiphy, !enabled);
		if (enabled != dev->phy.radio_on)
			b43_software_rfkill(dev, !enabled);
	}

	if (brought_up) {
		b43_device_disable(dev, 0);
		b43_bus_may_powerdown(dev);
	}

	mutex_unlock(&wl->mutex);
}
