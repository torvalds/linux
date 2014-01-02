/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/hid-sensor-hub.h>
#include <linux/iio/iio.h>
#include <linux/iio/trigger.h>
#include <linux/iio/sysfs.h>
#include "hid-sensor-trigger.h"

static int hid_sensor_data_rdy_trigger_set_state(struct iio_trigger *trig,
						bool state)
{
	struct hid_sensor_common *st = iio_trigger_get_drvdata(trig);
	int state_val;

	if (state) {
		if (sensor_hub_device_open(st->hsdev))
			return -EIO;
	} else
		sensor_hub_device_close(st->hsdev);

	state_val = state ? 1 : 0;
	if (IS_ENABLED(CONFIG_HID_SENSOR_ENUM_BASE_QUIRKS))
		++state_val;
	st->data_ready = state;
	sensor_hub_set_feature(st->hsdev, st->power_state.report_id,
					st->power_state.index,
					(s32)state_val);

	sensor_hub_set_feature(st->hsdev, st->report_state.report_id,
					st->report_state.index,
					(s32)state_val);

	return 0;
}

void hid_sensor_remove_trigger(struct iio_dev *indio_dev)
{
	iio_trigger_unregister(indio_dev->trig);
	iio_trigger_free(indio_dev->trig);
	indio_dev->trig = NULL;
}
EXPORT_SYMBOL(hid_sensor_remove_trigger);

static const struct iio_trigger_ops hid_sensor_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = &hid_sensor_data_rdy_trigger_set_state,
};

int hid_sensor_setup_trigger(struct iio_dev *indio_dev, const char *name,
				struct hid_sensor_common *attrb)
{
	int ret;
	struct iio_trigger *trig;

	trig = iio_trigger_alloc("%s-dev%d", name, indio_dev->id);
	if (trig == NULL) {
		dev_err(&indio_dev->dev, "Trigger Allocate Failed\n");
		ret = -ENOMEM;
		goto error_ret;
	}

	trig->dev.parent = indio_dev->dev.parent;
	iio_trigger_set_drvdata(trig, attrb);
	trig->ops = &hid_sensor_trigger_ops;
	ret = iio_trigger_register(trig);

	if (ret) {
		dev_err(&indio_dev->dev, "Trigger Register Failed\n");
		goto error_free_trig;
	}
	indio_dev->trig = trig;

	return ret;

error_free_trig:
	iio_trigger_free(trig);
error_ret:
	return ret;
}
EXPORT_SYMBOL(hid_sensor_setup_trigger);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_DESCRIPTION("HID Sensor trigger processing");
MODULE_LICENSE("GPL");
