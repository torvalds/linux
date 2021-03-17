// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MAXIM MAX77693/MAX77843 Haptic device driver
 *
 * Copyright (C) 2014,2015 Samsung Electronics
 * Jaewon Kim <jaewon02.kim@samsung.com>
 * Krzysztof Kozlowski <krzk@kernel.org>
 *
 * This program is not provided / owned by Maxim Integrated Products.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77693-private.h>
#include <linux/mfd/max77843-private.h>

#define MAX_MAGNITUDE_SHIFT	16

enum max77693_haptic_motor_type {
	MAX77693_HAPTIC_ERM = 0,
	MAX77693_HAPTIC_LRA,
};

enum max77693_haptic_pulse_mode {
	MAX77693_HAPTIC_EXTERNAL_MODE = 0,
	MAX77693_HAPTIC_INTERNAL_MODE,
};

enum max77693_haptic_pwm_divisor {
	MAX77693_HAPTIC_PWM_DIVISOR_32 = 0,
	MAX77693_HAPTIC_PWM_DIVISOR_64,
	MAX77693_HAPTIC_PWM_DIVISOR_128,
	MAX77693_HAPTIC_PWM_DIVISOR_256,
};

struct max77693_haptic {
	enum max77693_types dev_type;

	struct regmap *regmap_pmic;
	struct regmap *regmap_haptic;
	struct device *dev;
	struct input_dev *input_dev;
	struct pwm_device *pwm_dev;
	struct regulator *motor_reg;

	bool enabled;
	bool suspend_state;
	unsigned int magnitude;
	unsigned int pwm_duty;
	enum max77693_haptic_motor_type type;
	enum max77693_haptic_pulse_mode mode;

	struct work_struct work;
};

static int max77693_haptic_set_duty_cycle(struct max77693_haptic *haptic)
{
	struct pwm_args pargs;
	int delta;
	int error;

	pwm_get_args(haptic->pwm_dev, &pargs);
	delta = (pargs.period + haptic->pwm_duty) / 2;
	error = pwm_config(haptic->pwm_dev, delta, pargs.period);
	if (error) {
		dev_err(haptic->dev, "failed to configure pwm: %d\n", error);
		return error;
	}

	return 0;
}

static int max77843_haptic_bias(struct max77693_haptic *haptic, bool on)
{
	int error;

	if (haptic->dev_type != TYPE_MAX77843)
		return 0;

	error = regmap_update_bits(haptic->regmap_haptic,
				   MAX77843_SYS_REG_MAINCTRL1,
				   MAX77843_MAINCTRL1_BIASEN_MASK,
				   on << MAINCTRL1_BIASEN_SHIFT);
	if (error) {
		dev_err(haptic->dev, "failed to %s bias: %d\n",
			on ? "enable" : "disable", error);
		return error;
	}

	return 0;
}

static int max77693_haptic_configure(struct max77693_haptic *haptic,
				     bool enable)
{
	unsigned int value, config_reg;
	int error;

	switch (haptic->dev_type) {
	case TYPE_MAX77693:
		value = ((haptic->type << MAX77693_CONFIG2_MODE) |
			(enable << MAX77693_CONFIG2_MEN) |
			(haptic->mode << MAX77693_CONFIG2_HTYP) |
			MAX77693_HAPTIC_PWM_DIVISOR_128);
		config_reg = MAX77693_HAPTIC_REG_CONFIG2;
		break;
	case TYPE_MAX77843:
		value = (haptic->type << MCONFIG_MODE_SHIFT) |
			(enable << MCONFIG_MEN_SHIFT) |
			MAX77693_HAPTIC_PWM_DIVISOR_128;
		config_reg = MAX77843_HAP_REG_MCONFIG;
		break;
	default:
		return -EINVAL;
	}

	error = regmap_write(haptic->regmap_haptic,
			     config_reg, value);
	if (error) {
		dev_err(haptic->dev,
			"failed to update haptic config: %d\n", error);
		return error;
	}

	return 0;
}

static int max77693_haptic_lowsys(struct max77693_haptic *haptic, bool enable)
{
	int error;

	if (haptic->dev_type != TYPE_MAX77693)
		return 0;

	error = regmap_update_bits(haptic->regmap_pmic,
				   MAX77693_PMIC_REG_LSCNFG,
				   MAX77693_PMIC_LOW_SYS_MASK,
				   enable << MAX77693_PMIC_LOW_SYS_SHIFT);
	if (error) {
		dev_err(haptic->dev, "cannot update pmic regmap: %d\n", error);
		return error;
	}

	return 0;
}

static void max77693_haptic_enable(struct max77693_haptic *haptic)
{
	int error;

	if (haptic->enabled)
		return;

	error = pwm_enable(haptic->pwm_dev);
	if (error) {
		dev_err(haptic->dev,
			"failed to enable haptic pwm device: %d\n", error);
		return;
	}

	error = max77693_haptic_lowsys(haptic, true);
	if (error)
		goto err_enable_lowsys;

	error = max77693_haptic_configure(haptic, true);
	if (error)
		goto err_enable_config;

	haptic->enabled = true;

	return;

err_enable_config:
	max77693_haptic_lowsys(haptic, false);
err_enable_lowsys:
	pwm_disable(haptic->pwm_dev);
}

static void max77693_haptic_disable(struct max77693_haptic *haptic)
{
	int error;

	if (!haptic->enabled)
		return;

	error = max77693_haptic_configure(haptic, false);
	if (error)
		return;

	error = max77693_haptic_lowsys(haptic, false);
	if (error)
		goto err_disable_lowsys;

	pwm_disable(haptic->pwm_dev);
	haptic->enabled = false;

	return;

err_disable_lowsys:
	max77693_haptic_configure(haptic, true);
}

static void max77693_haptic_play_work(struct work_struct *work)
{
	struct max77693_haptic *haptic =
			container_of(work, struct max77693_haptic, work);
	int error;

	error = max77693_haptic_set_duty_cycle(haptic);
	if (error) {
		dev_err(haptic->dev, "failed to set duty cycle: %d\n", error);
		return;
	}

	if (haptic->magnitude)
		max77693_haptic_enable(haptic);
	else
		max77693_haptic_disable(haptic);
}

static int max77693_haptic_play_effect(struct input_dev *dev, void *data,
				       struct ff_effect *effect)
{
	struct max77693_haptic *haptic = input_get_drvdata(dev);
	struct pwm_args pargs;
	u64 period_mag_multi;

	haptic->magnitude = effect->u.rumble.strong_magnitude;
	if (!haptic->magnitude)
		haptic->magnitude = effect->u.rumble.weak_magnitude;

	/*
	 * The magnitude comes from force-feedback interface.
	 * The formula to convert magnitude to pwm_duty as follows:
	 * - pwm_duty = (magnitude * pwm_period) / MAX_MAGNITUDE(0xFFFF)
	 */
	pwm_get_args(haptic->pwm_dev, &pargs);
	period_mag_multi = (u64)pargs.period * haptic->magnitude;
	haptic->pwm_duty = (unsigned int)(period_mag_multi >>
						MAX_MAGNITUDE_SHIFT);

	schedule_work(&haptic->work);

	return 0;
}

static int max77693_haptic_open(struct input_dev *dev)
{
	struct max77693_haptic *haptic = input_get_drvdata(dev);
	int error;

	error = max77843_haptic_bias(haptic, true);
	if (error)
		return error;

	error = regulator_enable(haptic->motor_reg);
	if (error) {
		dev_err(haptic->dev,
			"failed to enable regulator: %d\n", error);
		return error;
	}

	return 0;
}

static void max77693_haptic_close(struct input_dev *dev)
{
	struct max77693_haptic *haptic = input_get_drvdata(dev);
	int error;

	cancel_work_sync(&haptic->work);
	max77693_haptic_disable(haptic);

	error = regulator_disable(haptic->motor_reg);
	if (error)
		dev_err(haptic->dev,
			"failed to disable regulator: %d\n", error);

	max77843_haptic_bias(haptic, false);
}

static int max77693_haptic_probe(struct platform_device *pdev)
{
	struct max77693_dev *max77693 = dev_get_drvdata(pdev->dev.parent);
	struct max77693_haptic *haptic;
	int error;

	haptic = devm_kzalloc(&pdev->dev, sizeof(*haptic), GFP_KERNEL);
	if (!haptic)
		return -ENOMEM;

	haptic->regmap_pmic = max77693->regmap;
	haptic->dev = &pdev->dev;
	haptic->type = MAX77693_HAPTIC_LRA;
	haptic->mode = MAX77693_HAPTIC_EXTERNAL_MODE;
	haptic->suspend_state = false;

	/* Variant-specific init */
	haptic->dev_type = platform_get_device_id(pdev)->driver_data;
	switch (haptic->dev_type) {
	case TYPE_MAX77693:
		haptic->regmap_haptic = max77693->regmap_haptic;
		break;
	case TYPE_MAX77843:
		haptic->regmap_haptic = max77693->regmap;
		break;
	default:
		dev_err(&pdev->dev, "unsupported device type: %u\n",
			haptic->dev_type);
		return -EINVAL;
	}

	INIT_WORK(&haptic->work, max77693_haptic_play_work);

	/* Get pwm and regulatot for haptic device */
	haptic->pwm_dev = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(haptic->pwm_dev)) {
		dev_err(&pdev->dev, "failed to get pwm device\n");
		return PTR_ERR(haptic->pwm_dev);
	}

	/*
	 * FIXME: pwm_apply_args() should be removed when switching to the
	 * atomic PWM API.
	 */
	pwm_apply_args(haptic->pwm_dev);

	haptic->motor_reg = devm_regulator_get(&pdev->dev, "haptic");
	if (IS_ERR(haptic->motor_reg)) {
		dev_err(&pdev->dev, "failed to get regulator\n");
		return PTR_ERR(haptic->motor_reg);
	}

	/* Initialize input device for haptic device */
	haptic->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!haptic->input_dev) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		return -ENOMEM;
	}

	haptic->input_dev->name = "max77693-haptic";
	haptic->input_dev->id.version = 1;
	haptic->input_dev->dev.parent = &pdev->dev;
	haptic->input_dev->open = max77693_haptic_open;
	haptic->input_dev->close = max77693_haptic_close;
	input_set_drvdata(haptic->input_dev, haptic);
	input_set_capability(haptic->input_dev, EV_FF, FF_RUMBLE);

	error = input_ff_create_memless(haptic->input_dev, NULL,
				max77693_haptic_play_effect);
	if (error) {
		dev_err(&pdev->dev, "failed to create force-feedback\n");
		return error;
	}

	error = input_register_device(haptic->input_dev);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		return error;
	}

	platform_set_drvdata(pdev, haptic);

	return 0;
}

static int __maybe_unused max77693_haptic_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77693_haptic *haptic = platform_get_drvdata(pdev);

	if (haptic->enabled) {
		max77693_haptic_disable(haptic);
		haptic->suspend_state = true;
	}

	return 0;
}

static int __maybe_unused max77693_haptic_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max77693_haptic *haptic = platform_get_drvdata(pdev);

	if (haptic->suspend_state) {
		max77693_haptic_enable(haptic);
		haptic->suspend_state = false;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(max77693_haptic_pm_ops,
			 max77693_haptic_suspend, max77693_haptic_resume);

static const struct platform_device_id max77693_haptic_id[] = {
	{ "max77693-haptic", TYPE_MAX77693 },
	{ "max77843-haptic", TYPE_MAX77843 },
	{},
};
MODULE_DEVICE_TABLE(platform, max77693_haptic_id);

static struct platform_driver max77693_haptic_driver = {
	.driver		= {
		.name	= "max77693-haptic",
		.pm	= &max77693_haptic_pm_ops,
	},
	.probe		= max77693_haptic_probe,
	.id_table	= max77693_haptic_id,
};
module_platform_driver(max77693_haptic_driver);

MODULE_AUTHOR("Jaewon Kim <jaewon02.kim@samsung.com>");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_DESCRIPTION("MAXIM 77693/77843 Haptic driver");
MODULE_ALIAS("platform:max77693-haptic");
MODULE_LICENSE("GPL");
