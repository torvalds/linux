/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * HID Sensors Driver
 * Copyright (c) 2012, Intel Corporation.
 */
#ifndef _HID_SENSOR_TRIGGER_H
#define _HID_SENSOR_TRIGGER_H

#include <linux/pm.h>
#include <linux/pm_runtime.h>

extern const struct dev_pm_ops hid_sensor_pm_ops;

int hid_sensor_setup_trigger(struct iio_dev *indio_dev, const char *name,
				struct hid_sensor_common *attrb);
void hid_sensor_remove_trigger(struct iio_dev *indio_dev,
			       struct hid_sensor_common *attrb);
int hid_sensor_power_state(struct hid_sensor_common *st, bool state);

#endif
