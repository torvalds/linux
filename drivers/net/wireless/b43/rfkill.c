/*

  Broadcom B43 wireless driver
  RFKILL support

  Copyright (c) 2007 Michael Buesch <mb@bu3sch.de>

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

#include "rfkill.h"
#include "b43.h"
#include "phy_common.h"

#include <linux/kmod.h>


/* Returns TRUE, if the radio is enabled in hardware. */
static bool b43_is_hw_radio_enabled(struct b43_wldev *dev)
{
	if (dev->phy.rev >= 3) {
		if (!(b43_read32(dev, B43_MMIO_RADIO_HWENABLED_HI)
		      & B43_MMIO_RADIO_HWENABLED_HI_MASK))
			return 1;
	} else {
		if (b43_read16(dev, B43_MMIO_RADIO_HWENABLED_LO)
		    & B43_MMIO_RADIO_HWENABLED_LO_MASK)
			return 1;
	}
	return 0;
}

/* The poll callback for the hardware button. */
static void b43_rfkill_poll(struct rfkill *rfkill, void *data)
{
	struct b43_wldev *dev = data;
	struct b43_wl *wl = dev->wl;
	bool enabled;

	mutex_lock(&wl->mutex);
	if (unlikely(b43_status(dev) < B43_STAT_INITIALIZED)) {
		mutex_unlock(&wl->mutex);
		return;
	}
	enabled = b43_is_hw_radio_enabled(dev);
	if (unlikely(enabled != dev->radio_hw_enable)) {
		dev->radio_hw_enable = enabled;
		b43info(wl, "Radio hardware status changed to %s\n",
			enabled ? "ENABLED" : "DISABLED");
		enabled = !rfkill_set_hw_state(rfkill, !enabled);
		if (enabled != dev->phy.radio_on)
			b43_software_rfkill(dev, !enabled);
	}
	mutex_unlock(&wl->mutex);
}

/* Called when the RFKILL toggled in software. */
static int b43_rfkill_soft_set(void *data, bool blocked)
{
	struct b43_wldev *dev = data;
	struct b43_wl *wl = dev->wl;
	int err = -EINVAL;

	if (WARN_ON(!wl->rfkill.registered))
		return -EINVAL;

	mutex_lock(&wl->mutex);

	if (b43_status(dev) < B43_STAT_INITIALIZED)
		goto out_unlock;

	if (!dev->radio_hw_enable)
		goto out_unlock;

	if (!blocked != dev->phy.radio_on)
		b43_software_rfkill(dev, blocked);
	err = 0;
out_unlock:
	mutex_unlock(&wl->mutex);
	return err;
}

const char *b43_rfkill_led_name(struct b43_wldev *dev)
{
	struct b43_rfkill *rfk = &(dev->wl->rfkill);

	if (!rfk->registered)
		return NULL;
	return rfkill_get_led_trigger_name(rfk->rfkill);
}

static const struct rfkill_ops b43_rfkill_ops = {
	.set_block = b43_rfkill_soft_set,
	.poll = b43_rfkill_poll,
};

void b43_rfkill_init(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	struct b43_rfkill *rfk = &(wl->rfkill);
	int err;

	rfk->registered = 0;

	snprintf(rfk->name, sizeof(rfk->name),
		 "b43-%s", wiphy_name(wl->hw->wiphy));

	rfk->rfkill = rfkill_alloc(rfk->name,
				   dev->dev->dev,
				   RFKILL_TYPE_WLAN,
				   &b43_rfkill_ops, dev);
	if (!rfk->rfkill)
		goto out_error;

	err = rfkill_register(rfk->rfkill);
	if (err)
		goto err_free;

	rfk->registered = 1;

	return;
 err_free:
	rfkill_destroy(rfk->rfkill);
 out_error:
	rfk->registered = 0;
	b43warn(wl, "RF-kill button init failed\n");
}

void b43_rfkill_exit(struct b43_wldev *dev)
{
	struct b43_rfkill *rfk = &(dev->wl->rfkill);

	if (!rfk->registered)
		return;
	rfk->registered = 0;

	rfkill_unregister(rfk->rfkill);
	rfkill_destroy(rfk->rfkill);
	rfk->rfkill = NULL;
}
