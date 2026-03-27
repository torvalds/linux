// SPDX-License-Identifier: GPL-2.0-only
/**
 * yoga_fan.c - Lenovo Yoga/Legion Fan Hardware Monitoring Driver
 *
 * Provides fan speed monitoring for Lenovo Yoga, Legion, and IdeaPad
 * laptops by interfacing with the Embedded Controller (EC) via ACPI.
 *
 * The driver implements a passive discrete-time first-order lag filter
 * with slew-rate limiting (RLLag). This addresses low-resolution
 * tachometer sampling in the EC by smoothing RPM readings based on
 * the time delta (dt) between userspace requests, ensuring physical
 * consistency without background task overhead or race conditions.
 * The filter implements multirate filtering with autoreset in case
 * of large sampling time.
 *
 * Copyright (C) 2021-2026 Sergio Melas <sergiomelas@gmail.com>
 */
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/math64.h>

/* Driver Configuration Constants */
#define DRVNAME			"yogafan"
#define MAX_FANS		8

/* Filter Configuration Constants */
#define TAU_MS			1000	/* Time constant for the first-order lag (ms) */
#define MAX_SLEW_RPM_S		1500	/* Maximum allowed change in RPM per second */
#define MAX_SAMPLING		5000	/* Maximum allowed Ts for reset (ms) */
#define MIN_SAMPLING		100	/* Minimum interval between filter updates (ms) */

/* RPM Sanitation Constants */
#define RPM_FLOOR_LIMIT		50	/* Snap filtered value to 0 if raw is 0 */

struct yogafan_config {
	int multiplier;
	int fan_count;
	const char *paths[2];
};

struct yoga_fan_data {
	acpi_handle active_handles[MAX_FANS];
	long filtered_val[MAX_FANS];
	ktime_t last_sample[MAX_FANS];
	int multiplier;
	int fan_count;
};

/* Specific configurations mapped via DMI */
static const struct yogafan_config yoga_8bit_fans_cfg = {
	.multiplier = 100,
	.fan_count = 1,
	.paths = { "\\_SB.PCI0.LPC0.EC0.FANS", NULL }
};

static const struct yogafan_config ideapad_8bit_fan0_cfg = {
	.multiplier = 100,
	.fan_count = 1,
	.paths = { "\\_SB.PCI0.LPC0.EC0.FAN0", NULL }
};

static const struct yogafan_config legion_16bit_dual_cfg = {
	.multiplier = 1,
	.fan_count = 2,
	.paths = { "\\_SB.PCI0.LPC0.EC0.FANS", "\\_SB.PCI0.LPC0.EC0.FA2S" }
};

static void apply_rllag_filter(struct yoga_fan_data *data, int idx, long raw_rpm)
{
	ktime_t now = ktime_get_boottime();
	s64 dt_ms = ktime_to_ms(ktime_sub(now, data->last_sample[idx]));
	long delta, step, limit, alpha;
	s64 temp_num;

	if (raw_rpm < RPM_FLOOR_LIMIT) {
		data->filtered_val[idx] = 0;
		data->last_sample[idx] = now;
		return;
	}

	if (data->last_sample[idx] == 0 || dt_ms > MAX_SAMPLING) {
		data->filtered_val[idx] = raw_rpm;
		data->last_sample[idx] = now;
		return;
	}

	if (dt_ms < MIN_SAMPLING)
		return;

	delta = raw_rpm - data->filtered_val[idx];
	if (delta == 0) {
		data->last_sample[idx] = now;
		return;
	}

	temp_num = dt_ms << 12;
	alpha = (long)div64_s64(temp_num, (s64)(TAU_MS + dt_ms));
	step = (delta * alpha) >> 12;

	if (step == 0 && delta != 0)
		step = (delta > 0) ? 1 : -1;

	limit = (MAX_SLEW_RPM_S * (long)dt_ms) / 1000;
	if (limit < 1)
		limit = 1;

	if (step > limit)
		step = limit;
	else if (step < -limit)
		step = -limit;

	data->filtered_val[idx] += step;
	data->last_sample[idx] = now;
}

static int yoga_fan_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	struct yoga_fan_data *data = dev_get_drvdata(dev);
	unsigned long long raw_acpi;
	acpi_status status;

	if (type != hwmon_fan || attr != hwmon_fan_input)
		return -EOPNOTSUPP;

	status = acpi_evaluate_integer(data->active_handles[channel], NULL, NULL, &raw_acpi);
	if (ACPI_FAILURE(status))
		return -EIO;

	apply_rllag_filter(data, channel, (long)raw_acpi * data->multiplier);
	*val = data->filtered_val[channel];

	return 0;
}

static umode_t yoga_fan_is_visible(const void *data, enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	const struct yoga_fan_data *fan_data = data;

	if (type == hwmon_fan && channel < fan_data->fan_count)
		return 0444;

	return 0;
}

static const struct hwmon_ops yoga_fan_hwmon_ops = {
	.is_visible = yoga_fan_is_visible,
	.read = yoga_fan_read,
};

static const struct hwmon_channel_info *yoga_fan_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT, HWMON_F_INPUT,
			   HWMON_F_INPUT, HWMON_F_INPUT,
			   HWMON_F_INPUT, HWMON_F_INPUT,
			   HWMON_F_INPUT, HWMON_F_INPUT),
	NULL
};

static const struct hwmon_chip_info yoga_fan_chip_info = {
	.ops = &yoga_fan_hwmon_ops,
	.info = yoga_fan_info,
};

static const struct dmi_system_id yogafan_quirks[] = {
	{
		.ident = "Lenovo Yoga",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Yoga"),
		},
		.driver_data = (void *)&yoga_8bit_fans_cfg,
	},
	{
		.ident = "Lenovo Legion",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "Legion"),
		},
		.driver_data = (void *)&legion_16bit_dual_cfg,
	},
	{
		.ident = "Lenovo IdeaPad",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "IdeaPad"),
		},
		.driver_data = (void *)&ideapad_8bit_fan0_cfg,
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, yogafan_quirks);

static int yoga_fan_probe(struct platform_device *pdev)
{
	const struct dmi_system_id *dmi_id;
	const struct yogafan_config *cfg;
	struct yoga_fan_data *data;
	struct device *hwmon_dev;
	int i;

	dmi_id = dmi_first_match(yogafan_quirks);
	if (!dmi_id)
		return -ENODEV;

	cfg = dmi_id->driver_data;
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->multiplier = cfg->multiplier;

	for (i = 0; i < cfg->fan_count; i++) {
		acpi_status status;

		status = acpi_get_handle(NULL, (char *)cfg->paths[i],
					 &data->active_handles[data->fan_count]);
		if (ACPI_SUCCESS(status))
			data->fan_count++;
	}

	if (data->fan_count == 0)
		return -ENODEV;

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, DRVNAME,
							 data, &yoga_fan_chip_info, NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct platform_driver yoga_fan_driver = {
	.driver = { .name = DRVNAME },
	.probe = yoga_fan_probe,
};

static struct platform_device *yoga_fan_device;

static int __init yoga_fan_init(void)
{
	int ret;

	if (!dmi_check_system(yogafan_quirks))
		return -ENODEV;

	ret = platform_driver_register(&yoga_fan_driver);
	if (ret)
		return ret;

	yoga_fan_device = platform_device_register_simple(DRVNAME, -1, NULL, 0);
	if (IS_ERR(yoga_fan_device)) {
		platform_driver_unregister(&yoga_fan_driver);
		return PTR_ERR(yoga_fan_device);
	}
	return 0;
}

static void __exit yoga_fan_exit(void)
{
	platform_device_unregister(yoga_fan_device);
	platform_driver_unregister(&yoga_fan_driver);
}

module_init(yoga_fan_init);
module_exit(yoga_fan_exit);

MODULE_AUTHOR("Sergio Melas <sergiomelas@gmail.com>");
MODULE_DESCRIPTION("Lenovo Yoga/Legion Fan Monitor Driver");
MODULE_LICENSE("GPL");
