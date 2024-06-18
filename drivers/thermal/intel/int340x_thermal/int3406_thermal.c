// SPDX-License-Identifier: GPL-2.0-only
/*
 * INT3406 thermal driver for display participant device
 *
 * Copyright (C) 2016, Intel Corporation
 * Authors: Aaron Lu <aaron.lu@intel.com>
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
	int lower_limit;
	acpi_handle handle;
	struct acpi_video_device_brightness *br;
	struct backlight_device *raw_bd;
	struct thermal_cooling_device *cooling_dev;
};

/*
 * According to the ACPI spec,
 * "Each brightness level is represented by a number between 0 and 100,
 * and can be thought of as a percentage. For example, 50 can be 50%
 * power consumption or 50% brightness, as defined by the OEM."
 *
 * As int3406 device uses this value to communicate with the native
 * graphics driver, we make the assumption that it represents
 * the percentage of brightness only
 */
#define ACPI_TO_RAW(v, d) (d->raw_bd->props.max_brightness * v / 100)
#define RAW_TO_ACPI(v, d) (v * 100 / d->raw_bd->props.max_brightness)

static int
int3406_thermal_get_max_state(struct thermal_cooling_device *cooling_dev,
			      unsigned long *state)
{
	struct int3406_thermal_data *d = cooling_dev->devdata;

	*state = d->upper_limit - d->lower_limit;
	return 0;
}

static int
int3406_thermal_set_cur_state(struct thermal_cooling_device *cooling_dev,
			      unsigned long state)
{
	struct int3406_thermal_data *d = cooling_dev->devdata;
	int acpi_level, raw_level;

	if (state > d->upper_limit - d->lower_limit)
		return -EINVAL;

	acpi_level = d->br->levels[d->upper_limit - state];

	raw_level = ACPI_TO_RAW(acpi_level, d);

	return backlight_device_set_brightness(d->raw_bd, raw_level);
}

static int
int3406_thermal_get_cur_state(struct thermal_cooling_device *cooling_dev,
			      unsigned long *state)
{
	struct int3406_thermal_data *d = cooling_dev->devdata;
	int acpi_level;
	int index;

	acpi_level = RAW_TO_ACPI(d->raw_bd->props.brightness, d);

	/*
	 * There is no 1:1 mapping between the firmware interface level
	 * with the raw interface level, we will have to find one that is
	 * right above it.
	 */
	for (index = d->lower_limit; index < d->upper_limit; index++) {
		if (acpi_level <= d->br->levels[index])
			break;
	}

	*state = d->upper_limit - index;
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

	for (i = 2; i < nr; i++) {
		if (array[i] == value)
			break;
	}
	return i == nr ? -ENOENT : i;
}

static void int3406_thermal_get_limit(struct int3406_thermal_data *d)
{
	acpi_status status;
	unsigned long long lower_limit, upper_limit;

	status = acpi_evaluate_integer(d->handle, "DDDL", NULL, &lower_limit);
	if (ACPI_SUCCESS(status))
		d->lower_limit = int3406_thermal_get_index(d->br->levels,
					d->br->count, lower_limit);

	status = acpi_evaluate_integer(d->handle, "DDPC", NULL, &upper_limit);
	if (ACPI_SUCCESS(status))
		d->upper_limit = int3406_thermal_get_index(d->br->levels,
					d->br->count, upper_limit);

	/* lower_limit and upper_limit should be always set */
	d->lower_limit = d->lower_limit > 0 ? d->lower_limit : 2;
	d->upper_limit = d->upper_limit > 0 ? d->upper_limit : d->br->count - 1;
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

static void int3406_thermal_remove(struct platform_device *pdev)
{
	struct int3406_thermal_data *d = platform_get_drvdata(pdev);

	thermal_cooling_device_unregister(d->cooling_dev);
	kfree(d->br);
}

static const struct acpi_device_id int3406_thermal_match[] = {
	{"INT3406", 0},
	{}
};

MODULE_DEVICE_TABLE(acpi, int3406_thermal_match);

static struct platform_driver int3406_thermal_driver = {
	.probe = int3406_thermal_probe,
	.remove_new = int3406_thermal_remove,
	.driver = {
		   .name = "int3406 thermal",
		   .acpi_match_table = int3406_thermal_match,
		   },
};

module_platform_driver(int3406_thermal_driver);

MODULE_DESCRIPTION("INT3406 Thermal driver");
MODULE_LICENSE("GPL v2");
