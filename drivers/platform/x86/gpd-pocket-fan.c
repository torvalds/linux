// SPDX-License-Identifier: GPL-2.0+
/*
 * GPD Pocket fan controller driver
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 */

#include <linux/acpi.h>
#include <linux/devm-helpers.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/thermal.h>
#include <linux/workqueue.h>

#define MAX_SPEED 3

#define TEMP_LIMIT0_DEFAULT	55000
#define TEMP_LIMIT1_DEFAULT	60000
#define TEMP_LIMIT2_DEFAULT	65000

#define HYSTERESIS_DEFAULT	3000

#define SPEED_ON_AC_DEFAULT	2

static int temp_limits[3] = {
	TEMP_LIMIT0_DEFAULT, TEMP_LIMIT1_DEFAULT, TEMP_LIMIT2_DEFAULT,
};
module_param_array(temp_limits, int, NULL, 0444);
MODULE_PARM_DESC(temp_limits,
		 "Millicelsius values above which the fan speed increases");

static int hysteresis = HYSTERESIS_DEFAULT;
module_param(hysteresis, int, 0444);
MODULE_PARM_DESC(hysteresis,
		 "Hysteresis in millicelsius before lowering the fan speed");

static int speed_on_ac = SPEED_ON_AC_DEFAULT;
module_param(speed_on_ac, int, 0444);
MODULE_PARM_DESC(speed_on_ac,
		 "minimum fan speed to allow when system is powered by AC");

struct gpd_pocket_fan_data {
	struct device *dev;
	struct thermal_zone_device *dts0;
	struct thermal_zone_device *dts1;
	struct gpio_desc *gpio0;
	struct gpio_desc *gpio1;
	struct delayed_work work;
	int last_speed;
};

static void gpd_pocket_fan_set_speed(struct gpd_pocket_fan_data *fan, int speed)
{
	if (speed == fan->last_speed)
		return;

	gpiod_direction_output(fan->gpio0, !!(speed & 1));
	gpiod_direction_output(fan->gpio1, !!(speed & 2));

	fan->last_speed = speed;
}

static int gpd_pocket_fan_min_speed(void)
{
	if (power_supply_is_system_supplied())
		return speed_on_ac;
	else
		return 0;
}

static void gpd_pocket_fan_worker(struct work_struct *work)
{
	struct gpd_pocket_fan_data *fan =
		container_of(work, struct gpd_pocket_fan_data, work.work);
	int t0, t1, temp, speed, min_speed, i;

	if (thermal_zone_get_temp(fan->dts0, &t0) ||
	    thermal_zone_get_temp(fan->dts1, &t1)) {
		dev_warn(fan->dev, "Error getting temperature\n");
		speed = MAX_SPEED;
		goto set_speed;
	}

	temp = max(t0, t1);

	speed = fan->last_speed;
	min_speed = gpd_pocket_fan_min_speed();

	/* Determine minimum speed */
	for (i = min_speed; i < ARRAY_SIZE(temp_limits); i++) {
		if (temp < temp_limits[i])
			break;
	}
	if (speed < i)
		speed = i;

	/* Use hysteresis before lowering speed again */
	for (i = min_speed; i < ARRAY_SIZE(temp_limits); i++) {
		if (temp <= (temp_limits[i] - hysteresis))
			break;
	}
	if (speed > i)
		speed = i;

	if (fan->last_speed <= 0 && speed)
		speed = MAX_SPEED; /* kick start motor */

set_speed:
	gpd_pocket_fan_set_speed(fan, speed);

	/* When mostly idle (low temp/speed), slow down the poll interval. */
	queue_delayed_work(system_wq, &fan->work,
			   msecs_to_jiffies(4000 / (speed + 1)));
}

static void gpd_pocket_fan_force_update(struct gpd_pocket_fan_data *fan)
{
	fan->last_speed = -1;
	mod_delayed_work(system_wq, &fan->work, 0);
}

static int gpd_pocket_fan_probe(struct platform_device *pdev)
{
	struct gpd_pocket_fan_data *fan;
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(temp_limits); i++) {
		if (temp_limits[i] < 20000 || temp_limits[i] > 90000) {
			dev_err(&pdev->dev, "Invalid temp-limit %d (must be between 20000 and 90000)\n",
				temp_limits[i]);
			temp_limits[0] = TEMP_LIMIT0_DEFAULT;
			temp_limits[1] = TEMP_LIMIT1_DEFAULT;
			temp_limits[2] = TEMP_LIMIT2_DEFAULT;
			break;
		}
	}
	if (hysteresis < 1000 || hysteresis > 10000) {
		dev_err(&pdev->dev, "Invalid hysteresis %d (must be between 1000 and 10000)\n",
			hysteresis);
		hysteresis = HYSTERESIS_DEFAULT;
	}
	if (speed_on_ac < 0 || speed_on_ac > MAX_SPEED) {
		dev_err(&pdev->dev, "Invalid speed_on_ac %d (must be between 0 and 3)\n",
			speed_on_ac);
		speed_on_ac = SPEED_ON_AC_DEFAULT;
	}

	fan = devm_kzalloc(&pdev->dev, sizeof(*fan), GFP_KERNEL);
	if (!fan)
		return -ENOMEM;

	fan->dev = &pdev->dev;
	ret = devm_delayed_work_autocancel(&pdev->dev, &fan->work,
					   gpd_pocket_fan_worker);
	if (ret)
		return ret;

	/* Note this returns a "weak" reference which we don't need to free */
	fan->dts0 = thermal_zone_get_zone_by_name("soc_dts0");
	if (IS_ERR(fan->dts0))
		return -EPROBE_DEFER;

	fan->dts1 = thermal_zone_get_zone_by_name("soc_dts1");
	if (IS_ERR(fan->dts1))
		return -EPROBE_DEFER;

	fan->gpio0 = devm_gpiod_get_index(fan->dev, NULL, 0, GPIOD_ASIS);
	if (IS_ERR(fan->gpio0))
		return PTR_ERR(fan->gpio0);

	fan->gpio1 = devm_gpiod_get_index(fan->dev, NULL, 1, GPIOD_ASIS);
	if (IS_ERR(fan->gpio1))
		return PTR_ERR(fan->gpio1);

	gpd_pocket_fan_force_update(fan);

	platform_set_drvdata(pdev, fan);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int gpd_pocket_fan_suspend(struct device *dev)
{
	struct gpd_pocket_fan_data *fan = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&fan->work);
	gpd_pocket_fan_set_speed(fan, gpd_pocket_fan_min_speed());
	return 0;
}

static int gpd_pocket_fan_resume(struct device *dev)
{
	struct gpd_pocket_fan_data *fan = dev_get_drvdata(dev);

	gpd_pocket_fan_force_update(fan);
	return 0;
}
#endif
static SIMPLE_DEV_PM_OPS(gpd_pocket_fan_pm_ops,
			 gpd_pocket_fan_suspend,
			 gpd_pocket_fan_resume);

static struct acpi_device_id gpd_pocket_fan_acpi_match[] = {
	{ "FAN02501" },
	{},
};
MODULE_DEVICE_TABLE(acpi, gpd_pocket_fan_acpi_match);

static struct platform_driver gpd_pocket_fan_driver = {
	.probe	= gpd_pocket_fan_probe,
	.driver	= {
		.name			= "gpd_pocket_fan",
		.acpi_match_table	= gpd_pocket_fan_acpi_match,
		.pm			= &gpd_pocket_fan_pm_ops,
	 },
};

module_platform_driver(gpd_pocket_fan_driver);
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com");
MODULE_DESCRIPTION("GPD pocket fan driver");
MODULE_LICENSE("GPL");
