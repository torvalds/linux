/*
 * via-pmu event device for reporting some events that come through the PMU
 *
 * Copyright 2006 Johannes Berg <johannes@sipsolutions.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#include <linux/input.h>
#include <linux/adb.h>
#include <linux/pmu.h>
#include "via-pmu-event.h"

static struct input_dev *pmu_input_dev;

static int __init via_pmu_event_init(void)
{
	int err;

	/* do other models report button/lid status? */
	if (pmu_get_model() != PMU_KEYLARGO_BASED)
		return -ENODEV;

	pmu_input_dev = input_allocate_device();
	if (!pmu_input_dev)
		return -ENOMEM;

	pmu_input_dev->name = "PMU";
	pmu_input_dev->id.bustype = BUS_HOST;
	pmu_input_dev->id.vendor = 0x0001;
	pmu_input_dev->id.product = 0x0001;
	pmu_input_dev->id.version = 0x0100;

	set_bit(EV_KEY, pmu_input_dev->evbit);
	set_bit(EV_SW, pmu_input_dev->evbit);
	set_bit(KEY_POWER, pmu_input_dev->keybit);
	set_bit(SW_LID, pmu_input_dev->swbit);

	err = input_register_device(pmu_input_dev);
	if (err)
		input_free_device(pmu_input_dev);
	return err;
}

void via_pmu_event(int key, int down)
{

	if (unlikely(!pmu_input_dev))
		return;

	switch (key) {
	case PMU_EVT_POWER:
		input_report_key(pmu_input_dev, KEY_POWER, down);
		break;
	case PMU_EVT_LID:
		input_report_switch(pmu_input_dev, SW_LID, down);
		break;
	default:
		/* no such key handled */
		return;
	}

	input_sync(pmu_input_dev);
}

late_initcall(via_pmu_event_init);
