// SPDX-License-Identifier: GPL-2.0-only
/*
 * Regulator haptic driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Jaewon Kim <jaewon02.kim@samsung.com>
 * Author: Hyunhee Kim <hyunhee.kim@samsung.com>
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_data/regulator-haptic.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#define MAX_MAGNITUDE_SHIFT	16

struct regulator_haptic {
	struct device *dev;
	struct input_dev *input_dev;
	struct regulator *regulator;

	struct work_struct work;
	struct mutex mutex;

	bool active;
	bool suspended;

	unsigned int max_volt;
	unsigned int min_volt;
	unsigned int magnitude;
};

static int regulator_haptic_toggle(struct regulator_haptic *haptic, bool on)
{
	int error;

	if (haptic->active != on) {

		error = on ? regulator_enable(haptic->regulator) :
			     regulator_disable(haptic->regulator);
		if (error) {
			dev_err(haptic->dev,
				"failed to switch regulator %s: %d\n",
				on ? "on" : "off", error);
			return error;
		}

		haptic->active = on;
	}

	return 0;
}

static int regulator_haptic_set_voltage(struct regulator_haptic *haptic,
					 unsigned int magnitude)
{
	u64 volt_mag_multi;
	unsigned int intensity;
	int error;

	volt_mag_multi = (u64)(haptic->max_volt - haptic->min_volt) * magnitude;
	intensity = (unsigned int)(volt_mag_multi >> MAX_MAGNITUDE_SHIFT);

	error = regulator_set_voltage(haptic->regulator,
				      intensity + haptic->min_volt,
				      haptic->max_volt);
	if (error) {
		dev_err(haptic->dev, "cannot set regulator voltage to %d: %d\n",
			intensity + haptic->min_volt, error);
		return error;
	}

	regulator_haptic_toggle(haptic, !!magnitude);

	return 0;
}

static void regulator_haptic_work(struct work_struct *work)
{
	struct regulator_haptic *haptic = container_of(work,
					struct regulator_haptic, work);

	mutex_lock(&haptic->mutex);

	if (!haptic->suspended)
		regulator_haptic_set_voltage(haptic, haptic->magnitude);

	mutex_unlock(&haptic->mutex);
}

static int regulator_haptic_play_effect(struct input_dev *input, void *data,
					struct ff_effect *effect)
{
	struct regulator_haptic *haptic = input_get_drvdata(input);

	haptic->magnitude = effect->u.rumble.strong_magnitude;
	if (!haptic->magnitude)
		haptic->magnitude = effect->u.rumble.weak_magnitude;

	schedule_work(&haptic->work);

	return 0;
}

static void regulator_haptic_close(struct input_dev *input)
{
	struct regulator_haptic *haptic = input_get_drvdata(input);

	cancel_work_sync(&haptic->work);
	regulator_haptic_set_voltage(haptic, 0);
}

static int __maybe_unused
regulator_haptic_parse_dt(struct device *dev, struct regulator_haptic *haptic)
{
	struct device_node *node;
	int error;

	node = dev->of_node;
	if(!node) {
		dev_err(dev, "Missing device tree data\n");
		return -EINVAL;
	}

	error = of_property_read_u32(node, "max-microvolt", &haptic->max_volt);
	if (error) {
		dev_err(dev, "cannot parse max-microvolt\n");
		return error;
	}

	error = of_property_read_u32(node, "min-microvolt", &haptic->min_volt);
	if (error) {
		dev_err(dev, "cannot parse min-microvolt\n");
		return error;
	}

	return 0;
}

static int regulator_haptic_probe(struct platform_device *pdev)
{
	const struct regulator_haptic_data *pdata = dev_get_platdata(&pdev->dev);
	struct regulator_haptic *haptic;
	struct input_dev *input_dev;
	int error;

	haptic = devm_kzalloc(&pdev->dev, sizeof(*haptic), GFP_KERNEL);
	if (!haptic)
		return -ENOMEM;

	platform_set_drvdata(pdev, haptic);
	haptic->dev = &pdev->dev;
	mutex_init(&haptic->mutex);
	INIT_WORK(&haptic->work, regulator_haptic_work);

	if (pdata) {
		haptic->max_volt = pdata->max_volt;
		haptic->min_volt = pdata->min_volt;
	} else if (IS_ENABLED(CONFIG_OF)) {
		error = regulator_haptic_parse_dt(&pdev->dev, haptic);
		if (error)
			return error;
	} else {
		dev_err(&pdev->dev, "Missing platform data\n");
		return -EINVAL;
	}

	haptic->regulator = devm_regulator_get_exclusive(&pdev->dev, "haptic");
	if (IS_ERR(haptic->regulator)) {
		dev_err(&pdev->dev, "failed to get regulator\n");
		return PTR_ERR(haptic->regulator);
	}

	input_dev = devm_input_allocate_device(&pdev->dev);
	if (!input_dev)
		return	-ENOMEM;

	haptic->input_dev = input_dev;
	haptic->input_dev->name = "regulator-haptic";
	haptic->input_dev->dev.parent = &pdev->dev;
	haptic->input_dev->close = regulator_haptic_close;
	input_set_drvdata(haptic->input_dev, haptic);
	input_set_capability(haptic->input_dev, EV_FF, FF_RUMBLE);

	error = input_ff_create_memless(input_dev, NULL,
					regulator_haptic_play_effect);
	if (error) {
		dev_err(&pdev->dev, "failed to create force-feedback\n");
		return error;
	}

	error = input_register_device(haptic->input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return error;
	}

	return 0;
}

static int __maybe_unused regulator_haptic_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct regulator_haptic *haptic = platform_get_drvdata(pdev);
	int error;

	error = mutex_lock_interruptible(&haptic->mutex);
	if (error)
		return error;

	regulator_haptic_set_voltage(haptic, 0);

	haptic->suspended = true;

	mutex_unlock(&haptic->mutex);

	return 0;
}

static int __maybe_unused regulator_haptic_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct regulator_haptic *haptic = platform_get_drvdata(pdev);
	unsigned int magnitude;

	mutex_lock(&haptic->mutex);

	haptic->suspended = false;

	magnitude = READ_ONCE(haptic->magnitude);
	if (magnitude)
		regulator_haptic_set_voltage(haptic, magnitude);

	mutex_unlock(&haptic->mutex);

	return 0;
}

static SIMPLE_DEV_PM_OPS(regulator_haptic_pm_ops,
		regulator_haptic_suspend, regulator_haptic_resume);

static const struct of_device_id regulator_haptic_dt_match[] = {
	{ .compatible = "regulator-haptic" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, regulator_haptic_dt_match);

static struct platform_driver regulator_haptic_driver = {
	.probe		= regulator_haptic_probe,
	.driver		= {
		.name		= "regulator-haptic",
		.of_match_table = regulator_haptic_dt_match,
		.pm		= &regulator_haptic_pm_ops,
	},
};
module_platform_driver(regulator_haptic_driver);

MODULE_AUTHOR("Jaewon Kim <jaewon02.kim@samsung.com>");
MODULE_AUTHOR("Hyunhee Kim <hyunhee.kim@samsung.com>");
MODULE_DESCRIPTION("Regulator haptic driver");
MODULE_LICENSE("GPL");
