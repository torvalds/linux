/*
	Copyright (C) 2004 - 2008 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00rfkill
	Abstract: rt2x00 rfkill routines.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rfkill.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

static int rt2x00rfkill_toggle_radio(void *data, enum rfkill_state state)
{
	struct rt2x00_dev *rt2x00dev = data;
	int retval = 0;

	if (unlikely(!rt2x00dev))
		return 0;

	/*
	 * Only continue if there are enabled interfaces.
	 */
	if (!test_bit(DEVICE_STATE_STARTED, &rt2x00dev->flags))
		return 0;

	if (state == RFKILL_STATE_UNBLOCKED) {
		INFO(rt2x00dev, "RFKILL event: enabling radio.\n");
		clear_bit(DEVICE_STATE_DISABLED_RADIO_HW, &rt2x00dev->flags);
		retval = rt2x00lib_enable_radio(rt2x00dev);
	} else if (state == RFKILL_STATE_SOFT_BLOCKED) {
		INFO(rt2x00dev, "RFKILL event: disabling radio.\n");
		set_bit(DEVICE_STATE_DISABLED_RADIO_HW, &rt2x00dev->flags);
		rt2x00lib_disable_radio(rt2x00dev);
	} else {
		WARNING(rt2x00dev, "RFKILL event: unknown state %d.\n", state);
	}

	return retval;
}

static int rt2x00rfkill_get_state(void *data, enum rfkill_state *state)
{
	struct rt2x00_dev *rt2x00dev = data;

	/*
	 * rfkill_poll reports 1 when the key has been pressed and the
	 * radio should be blocked.
	 */
	*state = rt2x00dev->ops->lib->rfkill_poll(rt2x00dev) ?
	    RFKILL_STATE_SOFT_BLOCKED : RFKILL_STATE_UNBLOCKED;

	return 0;
}

static void rt2x00rfkill_poll(struct work_struct *work)
{
	struct rt2x00_dev *rt2x00dev =
	    container_of(work, struct rt2x00_dev, rfkill_work.work);
	enum rfkill_state state;

	if (!test_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state) ||
	    !test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags))
		return;

	/*
	 * Poll latest state and report it to rfkill who should sort
	 * out if the state should be toggled or not.
	 */
	if (!rt2x00rfkill_get_state(rt2x00dev, &state))
		rfkill_force_state(rt2x00dev->rfkill, state);

	queue_delayed_work(rt2x00dev->hw->workqueue,
			   &rt2x00dev->rfkill_work, RFKILL_POLL_INTERVAL);
}

void rt2x00rfkill_register(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state) ||
	    test_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state))
		return;

	if (rfkill_register(rt2x00dev->rfkill)) {
		ERROR(rt2x00dev, "Failed to register rfkill handler.\n");
		return;
	}

	__set_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state);

	/*
	 * Force initial poll which will detect the initial device state,
	 * and correctly sends the signal to the rfkill layer about this
	 * state.
	 */
	rt2x00rfkill_poll(&rt2x00dev->rfkill_work.work);
}

void rt2x00rfkill_unregister(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state) ||
	    !test_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state))
		return;

	cancel_delayed_work_sync(&rt2x00dev->rfkill_work);

	rfkill_unregister(rt2x00dev->rfkill);

	__clear_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state);
}

void rt2x00rfkill_allocate(struct rt2x00_dev *rt2x00dev)
{
	struct device *dev = wiphy_dev(rt2x00dev->hw->wiphy);

	if (test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state))
		return;

	rt2x00dev->rfkill = rfkill_allocate(dev, RFKILL_TYPE_WLAN);
	if (!rt2x00dev->rfkill) {
		ERROR(rt2x00dev, "Failed to allocate rfkill handler.\n");
		return;
	}

	__set_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state);

	rt2x00dev->rfkill->name = rt2x00dev->ops->name;
	rt2x00dev->rfkill->data = rt2x00dev;
	rt2x00dev->rfkill->toggle_radio = rt2x00rfkill_toggle_radio;
	if (test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags)) {
		rt2x00dev->rfkill->get_state = rt2x00rfkill_get_state;
		rt2x00dev->rfkill->state =
			rt2x00dev->ops->lib->rfkill_poll(rt2x00dev) ?
			    RFKILL_STATE_SOFT_BLOCKED : RFKILL_STATE_UNBLOCKED;
	} else {
		rt2x00dev->rfkill->state = RFKILL_STATE_UNBLOCKED;
	}

	INIT_DELAYED_WORK(&rt2x00dev->rfkill_work, rt2x00rfkill_poll);

	return;
}

void rt2x00rfkill_free(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->flags))
		return;

	cancel_delayed_work_sync(&rt2x00dev->rfkill_work);

	rfkill_free(rt2x00dev->rfkill);
	rt2x00dev->rfkill = NULL;
}
