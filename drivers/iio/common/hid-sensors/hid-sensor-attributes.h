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
#ifndef _HID_SENSORS_ATTRIBUTES_H
#define _HID_SENSORS_ATTRIBUTES_H

/* Common hid sensor iio structure */
struct hid_sensor_iio_common {
	struct hid_sensor_hub_device *hsdev;
	struct platform_device *pdev;
	unsigned usage_id;
	bool data_ready;
	struct hid_sensor_hub_attribute_info poll;
	struct hid_sensor_hub_attribute_info report_state;
	struct hid_sensor_hub_attribute_info power_state;
	struct hid_sensor_hub_attribute_info sensitivity;
};

/*Convert from hid unit expo to regular exponent*/
static inline int hid_sensor_convert_exponent(int unit_expo)
{
	if (unit_expo < 0x08)
		return unit_expo;
	else if (unit_expo <= 0x0f)
		return -(0x0f-unit_expo+1);
	else
		return 0;
}

int hid_sensor_parse_common_attributes(struct hid_sensor_hub_device *hsdev,
					u32 usage_id,
					struct hid_sensor_iio_common *st);
int hid_sensor_write_raw_hyst_value(struct hid_sensor_iio_common *st,
					int val1, int val2);
int hid_sensor_read_raw_hyst_value(struct hid_sensor_iio_common *st,
					int *val1, int *val2);
int hid_sensor_write_samp_freq_value(struct hid_sensor_iio_common *st,
					int val1, int val2);
int hid_sensor_read_samp_freq_value(struct hid_sensor_iio_common *st,
					int *val1, int *val2);

#endif
