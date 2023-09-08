// SPDX-License-Identifier: GPL-2.0+
/*
 *  GPIO vibrator driver
 *
 *  Copyright (C) 2019 Luca Weiss <luca@z3ntu.xyz>
 *
 *  Based on PWM vibrator driver:
 *  Copyright (C) 2017 Collabora Ltd.
 *
 *  Based on previous work from:
 *  Copyright (C) 2012 Dmitry Torokhov <dmitry.torokhov@gmail.com>
 *
 *  Based on PWM beeper driver:
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/gpio/consumer.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct gpio_vibrator {
	struct input_dev *input;
	struct gpio_desc *gpio;
	struct regulator *vcc;

	struct work_struct play_work;
	bool running;
	bool vcc_on;
};

static int gpio_vibrator_start(struct gpio_vibrator *vibrator)
{
	struct device *pdev = vibrator->input->dev.parent;
	int err;

	if (!vibrator->vcc_on) {
		err = regulator_enable(vibrator->vcc);
		if (err) {
			dev_err(pdev, "failed to enable regulator: %d\n", err);
			return err;
		}
		vibrator->vcc_on = true;
	}

	gpiod_set_value_cansleep(vibrator->gpio, 1);

	return 0;
}

static void gpio_vibrator_stop(struct gpio_vibrator *vibrator)
{
	gpiod_set_value_cansleep(vibrator->gpio, 0);

	if (vibrator->vcc_on) {
		regulator_disable(vibrator->vcc);
		vibrator->vcc_on = false;
	}
}

static void gpio_vibrator_play_work(struct work_struct *work)
{
	struct gpio_vibrator *vibrator =
		container_of(work, struct gpio_vibrator, play_work);

	if (vibrator->running)
		gpio_vibrator_start(vibrator);
	else
		gpio_vibrator_stop(vibrator);
}

static int gpio_vibrator_play_effect(struct input_dev *dev, void *data,
				     struct ff_effect *effect)
{
	struct gpio_vibrator *vibrator = input_get_drvdata(dev);
	int level;

	level = effect->u.rumble.strong_magnitude;
	if (!level)
		level = effect->u.rumble.weak_magnitude;

	vibrator->running = level;
	schedule_work(&vibrator->play_work);

	return 0;
}

static void gpio_vibrator_close(struct input_dev *input)
{
	struct gpio_vibrator *vibrator = input_get_drvdata(input);

	cancel_work_sync(&vibrator->play_work);
	gpio_vibrator_stop(vibrator);
	vibrator->running = false;
}

static int gpio_vibrator_probe(struct platform_device *pdev)
{
	struct gpio_vibrator *vibrator;
	int err;

	vibrator = devm_kzalloc(&pdev->dev, sizeof(*vibrator), GFP_KERNEL);
	if (!vibrator)
		return -ENOMEM;

	vibrator->input = devm_input_allocate_device(&pdev->dev);
	if (!vibrator->input)
		return -ENOMEM;

	vibrator->vcc = devm_regulator_get(&pdev->dev, "vcc");
	err = PTR_ERR_OR_ZERO(vibrator->vcc);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to request regulator: %d\n",
				err);
		return err;
	}

	vibrator->gpio = devm_gpiod_get(&pdev->dev, "enable", GPIOD_OUT_LOW);
	err = PTR_ERR_OR_ZERO(vibrator->gpio);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to request main gpio: %d\n",
				err);
		return err;
	}

	INIT_WORK(&vibrator->play_work, gpio_vibrator_play_work);

	vibrator->input->name = "gpio-vibrator";
	vibrator->input->id.bustype = BUS_HOST;
	vibrator->input->close = gpio_vibrator_close;

	input_set_drvdata(vibrator->input, vibrator);
	input_set_capability(vibrator->input, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(vibrator->input, NULL,
				      gpio_vibrator_play_effect);
	if (err) {
		dev_err(&pdev->dev, "Couldn't create FF dev: %d\n", err);
		return err;
	}

	err = input_register_device(vibrator->input);
	if (err) {
		dev_err(&pdev->dev, "Couldn't register input dev: %d\n", err);
		return err;
	}

	platform_set_drvdata(pdev, vibrator);

	return 0;
}

static int gpio_vibrator_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_vibrator *vibrator = platform_get_drvdata(pdev);

	cancel_work_sync(&vibrator->play_work);
	if (vibrator->running)
		gpio_vibrator_stop(vibrator);

	return 0;
}

static int gpio_vibrator_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_vibrator *vibrator = platform_get_drvdata(pdev);

	if (vibrator->running)
		gpio_vibrator_start(vibrator);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gpio_vibrator_pm_ops,
				gpio_vibrator_suspend, gpio_vibrator_resume);

#ifdef CONFIG_OF
static const struct of_device_id gpio_vibra_dt_match_table[] = {
	{ .compatible = "gpio-vibrator" },
	{}
};
MODULE_DEVICE_TABLE(of, gpio_vibra_dt_match_table);
#endif

static struct platform_driver gpio_vibrator_driver = {
	.probe	= gpio_vibrator_probe,
	.driver	= {
		.name	= "gpio-vibrator",
		.pm	= pm_sleep_ptr(&gpio_vibrator_pm_ops),
		.of_match_table = of_match_ptr(gpio_vibra_dt_match_table),
	},
};
module_platform_driver(gpio_vibrator_driver);

MODULE_AUTHOR("Luca Weiss <luca@z3ntu.xy>");
MODULE_DESCRIPTION("GPIO vibrator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:gpio-vibrator");
