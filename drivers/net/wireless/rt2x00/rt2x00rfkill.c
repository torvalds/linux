/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
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

#include <linux/input-polldev.h>
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
	if (!test_bit(DEVICE_STARTED, &rt2x00dev->flags))
		return 0;

	if (state == RFKILL_STATE_ON) {
		INFO(rt2x00dev, "Hardware button pressed, enabling radio.\n");
		__clear_bit(DEVICE_DISABLED_RADIO_HW, &rt2x00dev->flags);
		retval = rt2x00lib_enable_radio(rt2x00dev);
	} else if (state == RFKILL_STATE_OFF) {
		INFO(rt2x00dev, "Hardware button pressed, disabling radio.\n");
		__set_bit(DEVICE_DISABLED_RADIO_HW, &rt2x00dev->flags);
		rt2x00lib_disable_radio(rt2x00dev);
	}

	return retval;
}

static void rt2x00rfkill_poll(struct input_polled_dev *poll_dev)
{
	struct rt2x00_dev *rt2x00dev = poll_dev->private;
	int state = rt2x00dev->ops->lib->rfkill_poll(rt2x00dev);

	if (rt2x00dev->rfkill->state != state) {
		input_report_key(poll_dev->input, KEY_WLAN, 1);
		input_report_key(poll_dev->input, KEY_WLAN, 0);
	}
}

void rt2x00rfkill_register(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags) ||
	    !test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state))
		return;

	if (rfkill_register(rt2x00dev->rfkill)) {
		ERROR(rt2x00dev, "Failed to register rfkill handler.\n");
		return;
	}

	if (input_register_polled_device(rt2x00dev->poll_dev)) {
		ERROR(rt2x00dev, "Failed to register polled device.\n");
		rfkill_unregister(rt2x00dev->rfkill);
		return;
	}

	__set_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state);

	/*
	 * Force initial poll which will detect the initial device state,
	 * and correctly sends the signal to the rfkill layer about this
	 * state.
	 */
	rt2x00rfkill_poll(rt2x00dev->poll_dev);
}

void rt2x00rfkill_unregister(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags) ||
	    !test_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state))
		return;

	input_unregister_polled_device(rt2x00dev->poll_dev);
	rfkill_unregister(rt2x00dev->rfkill);

	__clear_bit(RFKILL_STATE_REGISTERED, &rt2x00dev->rfkill_state);
}

static struct input_polled_dev *
rt2x00rfkill_allocate_polldev(struct rt2x00_dev *rt2x00dev)
{
	struct input_polled_dev *poll_dev;

	poll_dev = input_allocate_polled_device();
	if (!poll_dev)
		return NULL;

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
	poll_dev->input->evbit[0] = BIT(EV_KEY);
	set_bit(KEY_WLAN, poll_dev->input->keybit);

	return poll_dev;
}

void rt2x00rfkill_allocate(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags))
		return;

	rt2x00dev->rfkill =
	    rfkill_allocate(wiphy_dev(rt2x00dev->hw->wiphy), RFKILL_TYPE_WLAN);
	if (!rt2x00dev->rfkill) {
		ERROR(rt2x00dev, "Failed to allocate rfkill handler.\n");
		return;
	}

	rt2x00dev->rfkill->name = rt2x00dev->ops->name;
	rt2x00dev->rfkill->data = rt2x00dev;
	rt2x00dev->rfkill->state = -1;
	rt2x00dev->rfkill->toggle_radio = rt2x00rfkill_toggle_radio;

	rt2x00dev->poll_dev = rt2x00rfkill_allocate_polldev(rt2x00dev);
	if (!rt2x00dev->poll_dev) {
		ERROR(rt2x00dev, "Failed to allocate polled device.\n");
		rfkill_free(rt2x00dev->rfkill);
		rt2x00dev->rfkill = NULL;
		return;
	}

	return;
}

void rt2x00rfkill_free(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags) ||
	    !test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state))
		return;

	input_free_polled_device(rt2x00dev->poll_dev);
	rt2x00dev->poll_dev = NULL;

	rfkill_free(rt2x00dev->rfkill);
	rt2x00dev->rfkill = NULL;
}

void rt2x00rfkill_suspend(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags) ||
	    !test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state))
		return;

	input_free_polled_device(rt2x00dev->poll_dev);
	rt2x00dev->poll_dev = NULL;
}

void rt2x00rfkill_resume(struct rt2x00_dev *rt2x00dev)
{
	if (!test_bit(CONFIG_SUPPORT_HW_BUTTON, &rt2x00dev->flags) ||
	    !test_bit(RFKILL_STATE_ALLOCATED, &rt2x00dev->rfkill_state))
		return;

	rt2x00dev->poll_dev = rt2x00rfkill_allocate_polldev(rt2x00dev);
	if (!rt2x00dev->poll_dev) {
		ERROR(rt2x00dev, "Failed to allocate polled device.\n");
		return;
	}
}
