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
#include <linux/iio/sysfs.h>

static struct {
	u32 usage_id;
	int unit; /* 0 for default others from HID sensor spec */
	int scale_val0; /* scale, whole number */
	int scale_val1; /* scale, fraction in nanos */
} unit_conversion[] = {
	{HID_USAGE_SENSOR_ACCEL_3D, 0, 9, 806650000},
	{HID_USAGE_SENSOR_ACCEL_3D,
		HID_USAGE_SENSOR_UNITS_METERS_PER_SEC_SQRD, 1, 0},
	{HID_USAGE_SENSOR_ACCEL_3D,
		HID_USAGE_SENSOR_UNITS_G, 9, 806650000},

	{HID_USAGE_SENSOR_GYRO_3D, 0, 0, 17453293},
	{HID_USAGE_SENSOR_GYRO_3D,
		HID_USAGE_SENSOR_UNITS_RADIANS_PER_SECOND, 1, 0},
	{HID_USAGE_SENSOR_GYRO_3D,
		HID_USAGE_SENSOR_UNITS_DEGREES_PER_SECOND, 0, 17453293},

	{HID_USAGE_SENSOR_COMPASS_3D, 0, 0, 1000000},
	{HID_USAGE_SENSOR_COMPASS_3D, HID_USAGE_SENSOR_UNITS_GAUSS, 1, 0},

	{HID_USAGE_SENSOR_INCLINOMETER_3D, 0, 0, 17453293},
	{HID_USAGE_SENSOR_INCLINOMETER_3D,
		HID_USAGE_SENSOR_UNITS_DEGREES, 0, 17453293},
	{HID_USAGE_SENSOR_INCLINOMETER_3D,
		HID_USAGE_SENSOR_UNITS_RADIANS, 1, 0},

	{HID_USAGE_SENSOR_ALS, 0, 1, 0},
	{HID_USAGE_SENSOR_ALS, HID_USAGE_SENSOR_UNITS_LUX, 1, 0},

	{HID_USAGE_SENSOR_PRESSURE, 0, 100, 0},
	{HID_USAGE_SENSOR_PRESSURE, HID_USAGE_SENSOR_UNITS_PASCAL, 0, 1000000},
};

static int pow_10(unsigned power)
{
	int i;
	int ret = 1;
	for (i = 0; i < power; ++i)
		ret = ret * 10;

	return ret;
}

static void simple_div(int dividend, int divisor, int *whole,
				int *micro_frac)
{
	int rem;
	int exp = 0;

	*micro_frac = 0;
	if (divisor == 0) {
		*whole = 0;
		return;
	}
	*whole = dividend/divisor;
	rem = dividend % divisor;
	if (rem) {
		while (rem <= divisor) {
			rem *= 10;
			exp++;
		}
		*micro_frac = (rem / divisor) * pow_10(6-exp);
	}
}

static void split_micro_fraction(unsigned int no, int exp, int *val1, int *val2)
{
	*val1 = no/pow_10(exp);
	*val2 = no%pow_10(exp) * pow_10(6-exp);
}

/*
VTF format uses exponent and variable size format.
For example if the size is 2 bytes
0x0067 with VTF16E14 format -> +1.03
To convert just change to 0x67 to decimal and use two decimal as E14 stands
for 10^-2.
Negative numbers are 2's complement
*/
static void convert_from_vtf_format(u32 value, int size, int exp,
					int *val1, int *val2)
{
	int sign = 1;

	if (value & BIT(size*8 - 1)) {
		value =  ((1LL << (size * 8)) - value);
		sign = -1;
	}
	exp = hid_sensor_convert_exponent(exp);
	if (exp >= 0) {
		*val1 = sign * value * pow_10(exp);
		*val2 = 0;
	} else {
		split_micro_fraction(value, -exp, val1, val2);
		if (*val1)
			*val1 = sign * (*val1);
		else
			*val2 = sign * (*val2);
	}
}

static u32 convert_to_vtf_format(int size, int exp, int val1, int val2)
{
	u32 value;
	int sign = 1;

	if (val1 < 0 || val2 < 0)
		sign = -1;
	exp = hid_sensor_convert_exponent(exp);
	if (exp < 0) {
		value = abs(val1) * pow_10(-exp);
		value += abs(val2) / pow_10(6+exp);
	} else
		value = abs(val1) / pow_10(exp);
	if (sign < 0)
		value =  ((1LL << (size * 8)) - value);

	return value;
}

s32 hid_sensor_read_poll_value(struct hid_sensor_common *st)
{
	s32 value = 0;
	int ret;

	ret = sensor_hub_get_feature(st->hsdev,
				     st->poll.report_id,
				     st->poll.index, sizeof(value), &value);

	if (ret < 0 || value < 0) {
		return -EINVAL;
	} else {
		if (st->poll.units == HID_USAGE_SENSOR_UNITS_SECOND)
			value = value * 1000;
	}

	return value;
}
EXPORT_SYMBOL(hid_sensor_read_poll_value);

int hid_sensor_read_samp_freq_value(struct hid_sensor_common *st,
				int *val1, int *val2)
{
	s32 value;
	int ret;

	ret = sensor_hub_get_feature(st->hsdev,
				     st->poll.report_id,
				     st->poll.index, sizeof(value), &value);
	if (ret < 0 || value < 0) {
		*val1 = *val2 = 0;
		return -EINVAL;
	} else {
		if (st->poll.units == HID_USAGE_SENSOR_UNITS_MILLISECOND)
			simple_div(1000, value, val1, val2);
		else if (st->poll.units == HID_USAGE_SENSOR_UNITS_SECOND)
			simple_div(1, value, val1, val2);
		else {
			*val1 = *val2 = 0;
			return -EINVAL;
		}
	}

	return IIO_VAL_INT_PLUS_MICRO;
}
EXPORT_SYMBOL(hid_sensor_read_samp_freq_value);

int hid_sensor_write_samp_freq_value(struct hid_sensor_common *st,
				int val1, int val2)
{
	s32 value;
	int ret;

	if (val1 < 0 || val2 < 0)
		ret = -EINVAL;

	value = val1 * pow_10(6) + val2;
	if (value) {
		if (st->poll.units == HID_USAGE_SENSOR_UNITS_MILLISECOND)
			value = pow_10(9)/value;
		else if (st->poll.units == HID_USAGE_SENSOR_UNITS_SECOND)
			value = pow_10(6)/value;
		else
			value = 0;
	}
	ret = sensor_hub_set_feature(st->hsdev, st->poll.report_id,
				     st->poll.index, sizeof(value), &value);
	if (ret < 0 || value < 0)
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL(hid_sensor_write_samp_freq_value);

int hid_sensor_read_raw_hyst_value(struct hid_sensor_common *st,
				int *val1, int *val2)
{
	s32 value;
	int ret;

	ret = sensor_hub_get_feature(st->hsdev,
				     st->sensitivity.report_id,
				     st->sensitivity.index, sizeof(value),
				     &value);
	if (ret < 0 || value < 0) {
		*val1 = *val2 = 0;
		return -EINVAL;
	} else {
		convert_from_vtf_format(value, st->sensitivity.size,
					st->sensitivity.unit_expo,
					val1, val2);
	}

	return IIO_VAL_INT_PLUS_MICRO;
}
EXPORT_SYMBOL(hid_sensor_read_raw_hyst_value);

int hid_sensor_write_raw_hyst_value(struct hid_sensor_common *st,
					int val1, int val2)
{
	s32 value;
	int ret;

	value = convert_to_vtf_format(st->sensitivity.size,
				st->sensitivity.unit_expo,
				val1, val2);
	ret = sensor_hub_set_feature(st->hsdev, st->sensitivity.report_id,
				     st->sensitivity.index, sizeof(value),
				     &value);
	if (ret < 0 || value < 0)
		ret = -EINVAL;

	return ret;
}
EXPORT_SYMBOL(hid_sensor_write_raw_hyst_value);

/*
 * This fuction applies the unit exponent to the scale.
 * For example:
 * 9.806650000 ->exp:2-> val0[980]val1[665000000]
 * 9.000806000 ->exp:2-> val0[900]val1[80600000]
 * 0.174535293 ->exp:2-> val0[17]val1[453529300]
 * 1.001745329 ->exp:0-> val0[1]val1[1745329]
 * 1.001745329 ->exp:2-> val0[100]val1[174532900]
 * 1.001745329 ->exp:4-> val0[10017]val1[453290000]
 * 9.806650000 ->exp:-2-> val0[0]val1[98066500]
 */
static void adjust_exponent_nano(int *val0, int *val1, int scale0,
				  int scale1, int exp)
{
	int i;
	int x;
	int res;
	int rem;

	if (exp > 0) {
		*val0 = scale0 * pow_10(exp);
		res = 0;
		if (exp > 9) {
			*val1 = 0;
			return;
		}
		for (i = 0; i < exp; ++i) {
			x = scale1 / pow_10(8 - i);
			res += (pow_10(exp - 1 - i) * x);
			scale1 = scale1 % pow_10(8 - i);
		}
		*val0 += res;
			*val1 = scale1 * pow_10(exp);
	} else if (exp < 0) {
		exp = abs(exp);
		if (exp > 9) {
			*val0 = *val1 = 0;
			return;
		}
		*val0 = scale0 / pow_10(exp);
		rem = scale0 % pow_10(exp);
		res = 0;
		for (i = 0; i < (9 - exp); ++i) {
			x = scale1 / pow_10(8 - i);
			res += (pow_10(8 - exp - i) * x);
			scale1 = scale1 % pow_10(8 - i);
		}
		*val1 = rem * pow_10(9 - exp) + res;
	} else {
		*val0 = scale0;
		*val1 = scale1;
	}
}

int hid_sensor_format_scale(u32 usage_id,
			struct hid_sensor_hub_attribute_info *attr_info,
			int *val0, int *val1)
{
	int i;
	int exp;

	*val0 = 1;
	*val1 = 0;

	for (i = 0; i < ARRAY_SIZE(unit_conversion); ++i) {
		if (unit_conversion[i].usage_id == usage_id &&
			unit_conversion[i].unit == attr_info->units) {
			exp  = hid_sensor_convert_exponent(
						attr_info->unit_expo);
			adjust_exponent_nano(val0, val1,
					unit_conversion[i].scale_val0,
					unit_conversion[i].scale_val1, exp);
			break;
		}
	}

	return IIO_VAL_INT_PLUS_NANO;
}
EXPORT_SYMBOL(hid_sensor_format_scale);

static
int hid_sensor_get_reporting_interval(struct hid_sensor_hub_device *hsdev,
					u32 usage_id,
					struct hid_sensor_common *st)
{
	sensor_hub_input_get_attribute_info(hsdev,
					HID_FEATURE_REPORT, usage_id,
					HID_USAGE_SENSOR_PROP_REPORT_INTERVAL,
					&st->poll);
	/* Default unit of measure is milliseconds */
	if (st->poll.units == 0)
		st->poll.units = HID_USAGE_SENSOR_UNITS_MILLISECOND;
	return 0;

}

int hid_sensor_parse_common_attributes(struct hid_sensor_hub_device *hsdev,
					u32 usage_id,
					struct hid_sensor_common *st)
{


	hid_sensor_get_reporting_interval(hsdev, usage_id, st);

	sensor_hub_input_get_attribute_info(hsdev,
					HID_FEATURE_REPORT, usage_id,
					HID_USAGE_SENSOR_PROP_REPORT_STATE,
					&st->report_state);

	sensor_hub_input_get_attribute_info(hsdev,
					HID_FEATURE_REPORT, usage_id,
					HID_USAGE_SENSOR_PROY_POWER_STATE,
					&st->power_state);

	sensor_hub_input_get_attribute_info(hsdev,
			HID_FEATURE_REPORT, usage_id,
			HID_USAGE_SENSOR_PROP_SENSITIVITY_ABS,
			 &st->sensitivity);

	hid_dbg(hsdev->hdev, "common attributes: %x:%x, %x:%x, %x:%x %x:%x\n",
			st->poll.index, st->poll.report_id,
			st->report_state.index, st->report_state.report_id,
			st->power_state.index, st->power_state.report_id,
			st->sensitivity.index, st->sensitivity.report_id);

	return 0;
}
EXPORT_SYMBOL(hid_sensor_parse_common_attributes);

MODULE_AUTHOR("Srinivas Pandruvada <srinivas.pandruvada@intel.com>");
MODULE_DESCRIPTION("HID Sensor common attribute processing");
MODULE_LICENSE("GPL");
