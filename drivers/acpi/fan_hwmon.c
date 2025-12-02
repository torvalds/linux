// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * hwmon interface for the ACPI Fan driver.
 *
 * Copyright (C) 2024 Armin Wolf <W_Armin@gmx.de>
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <linux/units.h>

#include "fan.h"

/* Returned when the ACPI fan does not support speed reporting */
#define FAN_SPEED_UNAVAILABLE	U32_MAX
#define FAN_POWER_UNAVAILABLE	U32_MAX

static struct acpi_fan_fps *acpi_fan_get_current_fps(struct acpi_fan *fan, u64 control)
{
	unsigned int i;

	for (i = 0; i < fan->fps_count; i++) {
		if (fan->fps[i].control == control)
			return &fan->fps[i];
	}

	return NULL;
}

static umode_t acpi_fan_hwmon_is_visible(const void *drvdata, enum hwmon_sensor_types type,
					 u32 attr, int channel)
{
	const struct acpi_fan *fan = drvdata;
	unsigned int i;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			return 0444;
		case hwmon_fan_target:
			/* Only acpi4 fans support fan control. */
			if (!fan->acpi4)
				return 0;

			/*
			 * When in fine grain control mode, not every fan control value
			 * has an associated fan performance state.
			 */
			if (fan->fif.fine_grain_ctrl)
				return 0;

			return 0444;
		default:
			return 0;
		}
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			/* Only acpi4 fans support fan control. */
			if (!fan->acpi4)
				return 0;

			/*
			 * When in fine grain control mode, not every fan control value
			 * has an associated fan performance state.
			 */
			if (fan->fif.fine_grain_ctrl)
				return 0;

			/*
			 * When all fan performance states contain no valid power data,
			 * when the associated attribute should not be created.
			 */
			for (i = 0; i < fan->fps_count; i++) {
				if (fan->fps[i].power != FAN_POWER_UNAVAILABLE)
					return 0444;
			}

			return 0;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

static int acpi_fan_hwmon_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			       int channel, long *val)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	struct acpi_fan_fps *fps;
	struct acpi_fan_fst fst;
	int ret;

	ret = acpi_fan_get_fst(fan->handle, &fst);
	if (ret < 0)
		return ret;

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			if (fst.speed == FAN_SPEED_UNAVAILABLE)
				return -ENODEV;

			if (fst.speed > LONG_MAX)
				return -EOVERFLOW;

			*val = fst.speed;
			return 0;
		case hwmon_fan_target:
			fps = acpi_fan_get_current_fps(fan, fst.control);
			if (!fps)
				return -EIO;

			if (fps->speed > LONG_MAX)
				return -EOVERFLOW;

			*val = fps->speed;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			fps = acpi_fan_get_current_fps(fan, fst.control);
			if (!fps)
				return -EIO;

			if (fps->power == FAN_POWER_UNAVAILABLE)
				return -ENODEV;

			if (fps->power > LONG_MAX / MICROWATT_PER_MILLIWATT)
				return -EOVERFLOW;

			*val = fps->power * MICROWATT_PER_MILLIWATT;
			return 0;
		default:
			return -EOPNOTSUPP;
		}
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_ops acpi_fan_hwmon_ops = {
	.is_visible = acpi_fan_hwmon_is_visible,
	.read = acpi_fan_hwmon_read,
};

static const struct hwmon_channel_info * const acpi_fan_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_TARGET),
	HWMON_CHANNEL_INFO(power, HWMON_P_INPUT),
	NULL
};

static const struct hwmon_chip_info acpi_fan_hwmon_chip_info = {
	.ops = &acpi_fan_hwmon_ops,
	.info = acpi_fan_hwmon_info,
};

int devm_acpi_fan_create_hwmon(struct device *dev)
{
	struct acpi_fan *fan = dev_get_drvdata(dev);
	struct device *hdev;

	hdev = devm_hwmon_device_register_with_info(dev, "acpi_fan", fan, &acpi_fan_hwmon_chip_info,
						    NULL);
	return PTR_ERR_OR_ZERO(hdev);
}
