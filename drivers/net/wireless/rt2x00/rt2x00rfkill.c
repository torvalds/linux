/*
	Copyright (C) 2004 - 2009 rt2x00 SourceForge Project
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

#include "rt2x00.h"
#include "rt2x00lib.h"

static void rt2x00rfkill_poll(struct input_polled_dev *poll_dev)
{
	struct rt2x00_dev *rt2x00dev = poll_dev->private;
	int state, old_state;

	if (!test_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state) ||
	    !test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags))
		return;

	/*
	 * Poll latest state, if the state is different then the previous state,
	 * we should generate an input event.
	 */
	state = !!rt2x00dev->ops->lib->rfkill_poll(rt2x00dev);
	old_state = !!test_bit(RFKILL_STATE_BLOCKED, &rt2x00dev->rfkill_state);

	if (old_state != state) {
		input_report_switch(poll_dev->input, SW_RFKILL_ALL, state);
		change_bit(RFKILL_STATE_BLOCKED, &rt2x00dev->rfkill_state);
	}
}

void rt2x00rfkill_register(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state) ||
	    test_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state))
		return;

	if (input_register_polled_device(rt2x00dev->rfkill_poll_dev)) {
		ERROR(rt2x00dev, "Failed to register polled device.\n");
		return;
	}

	__set_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state);

	/*
	 * Force initial poll which will detect the initial device state,
	 * and correctly sends the signal to the input layer about this
	 * state.
	 */
	rt2x00rfkill_poll(rt2x00dev->rfkill_poll_dev);
}

void rt2x00rfkill_unregister(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state) ||
	    !test_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state))
		return;

	input_unregister_polled_device(rt2x00dev->rfkill_poll_dev);

	__clear_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state);
}

void rt2x00rfkill_allocate(struct rt2x00_dev *rt2x00dev)
{
	struct input_polled_dev *poll_dev;

	if (test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state) ||
	    !test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags))
		return;

	poll_dev = input_allocate_polled_device();
	if (!poll_dev) {
		ERROR(rt2x00dev, "Failed to allocate polled device.\n");
		return;
	}

	poll_dev->private = rt2x00dev;
	poll_dev->poll = rt2x00rfkill_poll;
	poll_dev->poll_interval = RFKILL_POLL_INTERVAL;

	poll_dev->input->name = rt2x00dev->ops->name;
	poll_dev->input->phys = wiphy_name(rt2x00dev->hw->wiphy);
	poll_dev->input->id.bustype = BUS_HOST;
	poll_dev->input->id.vendor = 0x1814;
	poll_dev->input->id.product = rt2x00dev->chip.rt;
	poll_dev->input->id.version = rt2x00dev->chip.rev;
	poll_dev->input->dev.parent = wiphy_dev(rt2x00dev->hw->wiphy);
	poll_dev->input->evbit[0] = BIT(EV_SW);
	poll_dev->input->swbit[0] = BIT(SW_RFKILL_ALL);

	rt2x00dev->rfkill_poll_dev = poll_dev;

	__set_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state);
}

void rt2x00rfkill_free(struct rt2x00_dev *rt2x00dev)
{
	if (!__test_and_clear_bit(RFKILL_STATE_ALLOCATED,
				  &rt2x00dev->rfkill_state))
		return;

	input_free_polled_device(rt2x00dev->rfkill_poll_dev);
	rt2x00dev->rfkill_poll_dev = NULL;
}
