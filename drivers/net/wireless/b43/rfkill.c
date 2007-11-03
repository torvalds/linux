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
static void b43_rfkill_poll(struct input_polled_dev *poll_dev)
{
	struct b43_wldev *dev = poll_dev->private;
	struct b43_wl *wl = dev->wl;
	bool enabled;
	bool report_change = 0;

	mutex_lock(&wl->mutex);
	B43_WARN_ON(b43_status(dev) < B43_STAT_INITIALIZED);
	enabled = b43_is_hw_radio_enabled(dev);
	if (unlikely(enabled != dev->radio_hw_enable)) {
		dev->radio_hw_enable = enabled;
		report_change = 1;
		b43info(wl, "Radio hardware status changed to %s\n",
			enabled ? "ENABLED" : "DISABLED");
	}
	mutex_unlock(&wl->mutex);

	if (unlikely(report_change))
		input_report_key(poll_dev->input, KEY_WLAN, enabled);
}

/* Called when the RFKILL toggled in software. */
static int b43_rfkill_soft_toggle(void *data, enum rfkill_state state)
{
	struct b43_wldev *dev = data;
	struct b43_wl *wl = dev->wl;
	int err = 0;

	if (!wl->rfkill.registered)
		return 0;

	mutex_lock(&wl->mutex);
	B43_WARN_ON(b43_status(dev) < B43_STAT_INITIALIZED);
	switch (state) {
	case RFKILL_STATE_ON:
		if (!dev->radio_hw_enable) {
			/* No luck. We can't toggle the hardware RF-kill
			 * button from software. */
			err = -EBUSY;
			goto out_unlock;
		}
		if (!dev->phy.radio_on)
			b43_radio_turn_on(dev);
		break;
	case RFKILL_STATE_OFF:
		if (dev->phy.radio_on)
			b43_radio_turn_off(dev, 0);
		break;
	}
out_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

char * b43_rfkill_led_name(struct b43_wldev *dev)
{
	struct b43_rfkill *rfk = &(dev->wl->rfkill);

	if (!rfk->registered)
		return NULL;
	return rfkill_get_led_name(rfk->rfkill);
}

void b43_rfkill_init(struct b43_wldev *dev)
{
	struct b43_wl *wl = dev->wl;
	struct b43_rfkill *rfk = &(wl->rfkill);
	int err;

	rfk->registered = 0;

	rfk->rfkill = rfkill_allocate(dev->dev->dev, RFKILL_TYPE_WLAN);
	if (!rfk->rfkill)
		goto out_error;
	snprintf(rfk->name, sizeof(rfk->name),
		 "b43-%s", wiphy_name(wl->hw->wiphy));
	rfk->rfkill->name = rfk->name;
	rfk->rfkill->state = RFKILL_STATE_ON;
	rfk->rfkill->data = dev;
	rfk->rfkill->toggle_radio = b43_rfkill_soft_toggle;
	rfk->rfkill->user_claim_unsupported = 1;

	rfk->poll_dev = input_allocate_polled_device();
	if (!rfk->poll_dev)
		goto err_free_rfk;
	rfk->poll_dev->private = dev;
	rfk->poll_dev->poll = b43_rfkill_poll;
	rfk->poll_dev->poll_interval = 1000; /* msecs */

	err = rfkill_register(rfk->rfkill);
	if (err)
		goto err_free_polldev;
	err = input_register_polled_device(rfk->poll_dev);
	if (err)
		goto err_unreg_rfk;

	rfk->registered = 1;

	return;
err_unreg_rfk:
	rfkill_unregister(rfk->rfkill);
err_free_polldev:
	input_free_polled_device(rfk->poll_dev);
	rfk->poll_dev = NULL;
err_free_rfk:
	rfkill_free(rfk->rfkill);
	rfk->rfkill = NULL;
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

	input_unregister_polled_device(rfk->poll_dev);
	rfkill_unregister(rfk->rfkill);
	input_free_polled_device(rfk->poll_dev);
	rfk->poll_dev = NULL;
	rfkill_free(rfk->rfkill);
	rfk->rfkill = NULL;
}
