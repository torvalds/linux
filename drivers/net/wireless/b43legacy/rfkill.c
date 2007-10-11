/*

  Broadcom B43legacy wireless driver
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
#include "radio.h"
#include "b43legacy.h"


static void b43legacy_notify_rfkill_press(struct work_struct *work)
{
	struct b43legacy_rfkill *rfk = container_of(work,
						    struct b43legacy_rfkill,
						    notify_work);
	struct b43legacy_wl *wl = container_of(rfk, struct b43legacy_wl,
				  rfkill);
	struct b43legacy_wldev *dev;
	enum rfkill_state state;

	mutex_lock(&wl->mutex);
	dev = wl->current_dev;
	if (b43legacy_status(dev) < B43legacy_STAT_INITIALIZED) {
		mutex_unlock(&wl->mutex);
		return;
	}
	if (dev->radio_hw_enable)
		state = RFKILL_STATE_ON;
	else
		state = RFKILL_STATE_OFF;
	b43legacyinfo(wl, "Radio hardware status changed to %s\n",
		dev->radio_hw_enable ? "ENABLED" : "DISABLED");
	mutex_unlock(&wl->mutex);

	if (rfk->rfkill) {
		/* Be careful. This calls back into the software toggle
		 * routines. So we must unlock before calling. */
		rfkill_switch_all(rfk->rfkill->type, state);
	}
}

/* Called when the RFKILL toggled in hardware.
 * This is called with the mutex locked. */
void b43legacy_rfkill_toggled(struct b43legacy_wldev *dev, bool on)
{
	struct b43legacy_wl *wl = dev->wl;

	B43legacy_WARN_ON(b43legacy_status(dev) < B43legacy_STAT_INITIALIZED);
	/* Update the RF status asynchronously, as rfkill will
	 * call back into the software toggle handler.
	 * This would deadlock if done synchronously. */
	queue_work(wl->hw->workqueue, &wl->rfkill.notify_work);
}

/* Called when the RFKILL toggled in software.
 * This is called without locking. */
static int b43legacy_rfkill_soft_toggle(void *data, enum rfkill_state state)
{
	struct b43legacy_wldev *dev = data;
	struct b43legacy_wl *wl = dev->wl;
	int err = 0;

	mutex_lock(&wl->mutex);
	if (b43legacy_status(dev) < B43legacy_STAT_INITIALIZED)
		goto out_unlock;

	switch (state) {
	case RFKILL_STATE_ON:
		if (!dev->radio_hw_enable) {
			/* No luck. We can't toggle the hardware RF-kill
			 * button from software. */
			err = -EBUSY;
			goto out_unlock;
		}
		if (!dev->phy.radio_on)
			b43legacy_radio_turn_on(dev);
		break;
	case RFKILL_STATE_OFF:
		if (dev->phy.radio_on)
			b43legacy_radio_turn_off(dev, 0);
		break;
	}

out_unlock:
	mutex_unlock(&wl->mutex);

	return err;
}

char *b43legacy_rfkill_led_name(struct b43legacy_wldev *dev)
{
	struct b43legacy_wl *wl = dev->wl;

	if (!wl->rfkill.rfkill)
		return NULL;
	return rfkill_get_led_name(wl->rfkill.rfkill);
}

void b43legacy_rfkill_init(struct b43legacy_wldev *dev)
{
	struct b43legacy_wl *wl = dev->wl;
	struct b43legacy_rfkill *rfk = &(wl->rfkill);
	int err;

	snprintf(rfk->name, sizeof(rfk->name),
		 "b43legacy-%s", wiphy_name(wl->hw->wiphy));
	rfk->rfkill = rfkill_allocate(dev->dev->dev, RFKILL_TYPE_WLAN);
	if (!rfk->rfkill)
		goto error;
	rfk->rfkill->name = rfk->name;
	rfk->rfkill->state = RFKILL_STATE_ON;
	rfk->rfkill->data = dev;
	rfk->rfkill->toggle_radio = b43legacy_rfkill_soft_toggle;
	rfk->rfkill->user_claim_unsupported = 1;

	INIT_WORK(&rfk->notify_work, b43legacy_notify_rfkill_press);

	err = rfkill_register(rfk->rfkill);
	if (err)
		goto error;

	return;
error:
	b43legacywarn(dev->wl, "Failed to initialize the RF-kill button\n");
	rfkill_free(rfk->rfkill);
	rfk->rfkill = NULL;
}

void b43legacy_rfkill_exit(struct b43legacy_wldev *dev)
{
	struct b43legacy_rfkill *rfk = &(dev->wl->rfkill);

	if (!rfk->rfkill)
		return;
	cancel_work_sync(&rfk->notify_work);
	rfkill_unregister(rfk->rfkill);
	rfkill_free(rfk->rfkill);
	rfk->rfkill = NULL;
}
