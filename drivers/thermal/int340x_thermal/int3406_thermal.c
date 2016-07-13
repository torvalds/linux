/*
 * INT3406 thermal driver for display participant device
 *
 * Copyright (C) 2016, Intel Corporation
 * Authors: Aaron Lu <aaron.lu@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/thermal.h>
#include <acpi/video.h>

#define INT3406_BRIGHTNESS_LIMITS_CHANGED	0x80

struct int3406_thermal_data {
	int upper_limit;
	int upper_limit_index;
	int lower_limit;
	int lower_limit_index;
	acpi_handle handle;
	struct acpi_video_device_brightness *br;
	struct backlight_device *raw_bd;
	struct thermal_cooling_device *cooling_dev;
};

static int int3406_thermal_to_raw(int level, struct int3406_thermal_data *d)
{
	int max_level = d->br->levels[d->br->count - 1];
	int raw_max = d->raw_bd->props.max_brightness;

	return level * raw_max / max_level;
}

static int int3406_thermal_to_acpi(int level, struct int3406_thermal_data *d)
{
	int raw_max = d->raw_bd->props.max_brightness;
	int max_level = d->br->levels[d->br->count - 1];

	return level * max_level / raw_max;
}

static int
int3406_thermal_get_max_state(struct thermal_cooling_device *cooling_dev,
			      unsigned long *state)
{
	struct int3406_thermal_data *d = cooling_dev->devdata;
	int index = d->lower_limit_index ? d->lower_limit_index : 2;

	*state = d->br->count - 1 - index;
	return 0;
}

static int
int3406_thermal_set_cur_state(struct thermal_cooling_device *cooling_dev,
			      unsigned long state)
{
	struct int3406_thermal_data *d = cooling_dev->devdata;
	int level, raw_level;

	if (state > d->br->count - 3)
		return -EINVAL;

	state = d->br->count - 1 - state;
	level = d->br->levels[state];

	if ((d->upper_limit && level > d->upper_limit) ||
	    (d->lower_limit && level < d->lower_limit))
		return -EINVAL;

	raw_level = int3406_thermal_to_raw(level, d);
	return backlight_device_set_brightness(d->raw_bd, raw_level);
}

static int
int3406_thermal_get_cur_state(struct thermal_cooling_device *cooling_dev,
			      unsigned long *state)
{
	struct int3406_thermal_data *d = cooling_dev->devdata;
	int raw_level, level, i;
	int *levels = d->br->levels;

	raw_level = d->raw_bd->props.brightness;
	level = int3406_thermal_to_acpi(raw_level, d);

	/*
	 * There is no 1:1 mapping between the firmware interface level with the
	 * raw interface level, we will have to find one that is close enough.
	 */
	for (i = 2; i < d->br->count; i++) {
		if (level < levels[i]) {
			if (i == 2)
				break;
			if ((level - levels[i - 1]) < (levels[i] - level))
				i--;
			break;
		}
	}

	*state = d->br->count - 1 - i;
	return 0;
}

static const struct thermal_cooling_device_ops video_cooling_ops = {
	.get_max_state = int3406_thermal_get_max_state,
	.get_cur_state = int3406_thermal_get_cur_state,
	.set_cur_state = int3406_thermal_set_cur_state,
};

static int int3406_thermal_get_index(int *array, int nr, int value)
{
	int i;

	for (i = 0; i < nr; i++) {
		if (array[i] == value)
			break;
	}
	return i == nr ? -ENOENT : i;
}

static void int3406_thermal_get_limit(struct int3406_thermal_data *d)
{
	acpi_status status;
	unsigned long long lower_limit, upper_limit;
	int index;

	status = acpi_evaluate_integer(d->handle, "DDDL", NULL, &lower_limit);
	if (ACPI_SUCCESS(status)) {
		index = int3406_thermal_get_index(d->br->levels, d->br->count,
						  lower_limit);
		if (index > 0) {
			d->lower_limit = (int)lower_limit;
			d->lower_limit_index = index;
		}
	}

	status = acpi_evaluate_integer(d->handle, "DDPC", NULL, &upper_limit);
	if (ACPI_SUCCESS(status)) {
		index = int3406_thermal_get_index(d->br->levels, d->br->count,
						  upper_limit);
		if (index > 0) {
			d->upper_limit = (int)upper_limit;
			d->upper_limit_index = index;
		}
	}
}

static void int3406_notify(acpi_handle handle, u32 event, void *data)
{
	if (event == INT3406_BRIGHTNESS_LIMITS_CHANGED)
		int3406_thermal_get_limit(data);
}

static int int3406_thermal_probe(struct platform_device *pdev)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct int3406_thermal_data *d;
	struct backlight_device *bd;
	int ret;

	if (!ACPI_HANDLE(&pdev->dev))
		return -ENODEV;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->handle = ACPI_HANDLE(&pdev->dev);

	bd = backlight_device_get_by_type(BACKLIGHT_RAW);
	if (!bd)
		return -ENODEV;
	d->raw_bd = bd;

	ret = acpi_video_get_levels(ACPI_COMPANION(&pdev->dev), &d->br, NULL);
	if (ret)
		return ret;

	int3406_thermal_get_limit(d);

	d->cooling_dev = thermal_cooling_device_register(acpi_device_bid(adev),
							 d, &video_cooling_ops);
	if (IS_ERR(d->cooling_dev))
		goto err;

	ret = acpi_install_notify_handler(adev->handle, ACPI_DEVICE_NOTIFY,
					  int3406_notify, d);
	if (ret)
		goto err_cdev;

	platform_set_drvdata(pdev, d);

	return 0;

err_cdev:
	thermal_cooling_device_unregister(d->cooling_dev);
err:
	kfree(d->br);
	return -ENODEV;
}

static int int3406_thermal_remove(struct platform_device *pdev)
{
	struct int3406_thermal_data *d = platform_get_drvdata(pdev);

	thermal_cooling_device_unregister(d->cooling_dev);
	kfree(d->br);
	return 0;
}

static const struct acpi_device_id int3406_thermal_match[] = {
	{"INT3406", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, int3406_thermal_match);

static struct platform_driver int3406_thermal_driver = {
	.probe = int3406_thermal_probe,
	.remove = int3406_thermal_remove,
	.driver = {
		   .name = "int3406 thermal",
		   .owner = THIS_MODULE,
		   .acpi_match_table = int3406_thermal_match,
		   },
};

module_platform_driver(int3406_thermal_driver);

MODULE_DESCRIPTION("INT3406 Thermal driver");
MODULE_LICENSE("GPL v2");
