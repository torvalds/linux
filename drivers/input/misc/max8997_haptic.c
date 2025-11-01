// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MAX8997-haptic controller driver
 *
 * Copyright (C) 2012 Samsung Electronics
 * Donggeun Kim <dg77.kim@samsung.com>
 *
 * This program is not provided / owned by Maxim Integrated Products.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/pwm.h>
#include <linux/input.h>
#include <linux/mfd/max8997-private.h>
#include <linux/mfd/max8997.h>
#include <linux/regulator/consumer.h>

/* Haptic configuration 2 register */
#define MAX8997_MOTOR_TYPE_SHIFT	7
#define MAX8997_ENABLE_SHIFT		6
#define MAX8997_MODE_SHIFT		5

/* Haptic driver configuration register */
#define MAX8997_CYCLE_SHIFT		6
#define MAX8997_SIG_PERIOD_SHIFT	4
#define MAX8997_SIG_DUTY_SHIFT		2
#define MAX8997_PWM_DUTY_SHIFT		0

struct max8997_haptic {
	struct device *dev;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct regulator *regulator;

	struct work_struct work;
	struct mutex mutex;

	bool enabled;
	unsigned int level;

	struct pwm_device *pwm;
	unsigned int pwm_period;
	enum max8997_haptic_pwm_divisor pwm_divisor;

	enum max8997_haptic_motor_type type;
	enum max8997_haptic_pulse_mode mode;

	unsigned int internal_mode_pattern;
	unsigned int pattern_cycle;
	unsigned int pattern_signal_period;
};

static void max8997_haptic_set_internal_duty_cycle(struct max8997_haptic *chip)
{
	u8 duty_index = DIV_ROUND_UP(chip->level * 64, 100);

	switch (chip->internal_mode_pattern) {
	case 0:
		max8997_write_reg(chip->client,
				  MAX8997_HAPTIC_REG_SIGPWMDC1, duty_index);
		break;
	case 1:
		max8997_write_reg(chip->client,
				  MAX8997_HAPTIC_REG_SIGPWMDC2, duty_index);
		break;
	case 2:
		max8997_write_reg(chip->client,
				  MAX8997_HAPTIC_REG_SIGPWMDC3, duty_index);
		break;
	case 3:
		max8997_write_reg(chip->client,
				  MAX8997_HAPTIC_REG_SIGPWMDC4, duty_index);
		break;
	default:
		break;
	}
}

static void max8997_haptic_configure(struct max8997_haptic *chip)
{
	u8 value;

	value = chip->type << MAX8997_MOTOR_TYPE_SHIFT |
		chip->enabled << MAX8997_ENABLE_SHIFT |
		chip->mode << MAX8997_MODE_SHIFT | chip->pwm_divisor;
	max8997_write_reg(chip->client, MAX8997_HAPTIC_REG_CONF2, value);

	if (chip->mode == MAX8997_INTERNAL_MODE && chip->enabled) {
		value = chip->internal_mode_pattern << MAX8997_CYCLE_SHIFT |
			chip->internal_mode_pattern << MAX8997_SIG_PERIOD_SHIFT |
			chip->internal_mode_pattern << MAX8997_SIG_DUTY_SHIFT |
			chip->internal_mode_pattern << MAX8997_PWM_DUTY_SHIFT;
		max8997_write_reg(chip->client,
			MAX8997_HAPTIC_REG_DRVCONF, value);

		switch (chip->internal_mode_pattern) {
		case 0:
			value = chip->pattern_cycle << 4;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_CYCLECONF1, value);
			value = chip->pattern_signal_period;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_SIGCONF1, value);
			break;

		case 1:
			value = chip->pattern_cycle;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_CYCLECONF1, value);
			value = chip->pattern_signal_period;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_SIGCONF2, value);
			break;

		case 2:
			value = chip->pattern_cycle << 4;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_CYCLECONF2, value);
			value = chip->pattern_signal_period;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_SIGCONF3, value);
			break;

		case 3:
			value = chip->pattern_cycle;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_CYCLECONF2, value);
			value = chip->pattern_signal_period;
			max8997_write_reg(chip->client,
				MAX8997_HAPTIC_REG_SIGCONF4, value);
			break;

		default:
			break;
		}
	}
}

static void max8997_haptic_enable(struct max8997_haptic *chip)
{
	int error;

	guard(mutex)(&chip->mutex);

	if (chip->mode != MAX8997_EXTERNAL_MODE)
		max8997_haptic_set_internal_duty_cycle(chip);

	if (!chip->enabled) {
		error = regulator_enable(chip->regulator);
		if (error) {
			dev_err(chip->dev, "Failed to enable regulator\n");
			return;
		}
		max8997_haptic_configure(chip);
	}

	/*
	 * It would be more straight forward to configure the external PWM
	 * earlier i.e. when the internal duty_cycle is setup in internal mode.
	 * But historically this is done only after the regulator was enabled
	 * and max8997_haptic_configure() set the enable bit in
	 * MAX8997_HAPTIC_REG_CONF2. So better keep it this way.
	 */
	if (chip->mode == MAX8997_EXTERNAL_MODE) {
		struct pwm_state state;

		pwm_init_state(chip->pwm, &state);
		state.period = chip->pwm_period;
		state.duty_cycle = chip->pwm_period * chip->level / 100;
		state.enabled = true;

		error = pwm_apply_might_sleep(chip->pwm, &state);
		if (error) {
			dev_err(chip->dev, "Failed to enable PWM\n");
			regulator_disable(chip->regulator);
			return;
		}
	}

	chip->enabled = true;
}

static void max8997_haptic_disable(struct max8997_haptic *chip)
{
	guard(mutex)(&chip->mutex);

	if (chip->enabled) {
		chip->enabled = false;
		max8997_haptic_configure(chip);
		if (chip->mode == MAX8997_EXTERNAL_MODE)
			pwm_disable(chip->pwm);
		regulator_disable(chip->regulator);
	}
}

static void max8997_haptic_play_effect_work(struct work_struct *work)
{
	struct max8997_haptic *chip =
			container_of(work, struct max8997_haptic, work);

	if (chip->level)
		max8997_haptic_enable(chip);
	else
		max8997_haptic_disable(chip);
}

static int max8997_haptic_play_effect(struct input_dev *dev, void *data,
				  struct ff_effect *effect)
{
	struct max8997_haptic *chip = input_get_drvdata(dev);

	chip->level = effect->u.rumble.strong_magnitude;
	if (!chip->level)
		chip->level = effect->u.rumble.weak_magnitude;

	schedule_work(&chip->work);

	return 0;
}

static void max8997_haptic_close(struct input_dev *dev)
{
	struct max8997_haptic *chip = input_get_drvdata(dev);

	cancel_work_sync(&chip->work);
	max8997_haptic_disable(chip);
}

static int max8997_haptic_probe(struct platform_device *pdev)
{
	struct max8997_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	const struct max8997_platform_data *pdata =
					dev_get_platdata(iodev->dev);
	const struct max8997_haptic_platform_data *haptic_pdata = NULL;
	struct max8997_haptic *chip;
	struct input_dev *input_dev;
	int error;

	if (pdata)
		haptic_pdata = pdata->haptic_pdata;

	if (!haptic_pdata) {
		dev_err(&pdev->dev, "no haptic platform data\n");
		return -EINVAL;
	}

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!chip || !input_dev) {
		dev_err(&pdev->dev, "unable to allocate memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	INIT_WORK(&chip->work, max8997_haptic_play_effect_work);
	mutex_init(&chip->mutex);

	chip->client = iodev->haptic;
	chip->dev = &pdev->dev;
	chip->input_dev = input_dev;
	chip->pwm_period = haptic_pdata->pwm_period;
	chip->type = haptic_pdata->type;
	chip->mode = haptic_pdata->mode;
	chip->pwm_divisor = haptic_pdata->pwm_divisor;

	switch (chip->mode) {
	case MAX8997_INTERNAL_MODE:
		chip->internal_mode_pattern =
				haptic_pdata->internal_mode_pattern;
		chip->pattern_cycle = haptic_pdata->pattern_cycle;
		chip->pattern_signal_period =
				haptic_pdata->pattern_signal_period;
		break;

	case MAX8997_EXTERNAL_MODE:
		chip->pwm = pwm_get(&pdev->dev, NULL);
		if (IS_ERR(chip->pwm)) {
			error = PTR_ERR(chip->pwm);
			dev_err(&pdev->dev,
				"unable to request PWM for haptic, error: %d\n",
				error);
			goto err_free_mem;
		}

		break;

	default:
		dev_err(&pdev->dev,
			"Invalid chip mode specified (%d)\n", chip->mode);
		error = -EINVAL;
		goto err_free_mem;
	}

	chip->regulator = regulator_get(&pdev->dev, "inmotor");
	if (IS_ERR(chip->regulator)) {
		error = PTR_ERR(chip->regulator);
		dev_err(&pdev->dev,
			"unable to get regulator, error: %d\n",
			error);
		goto err_free_pwm;
	}

	input_dev->name = "max8997-haptic";
	input_dev->id.version = 1;
	input_dev->dev.parent = &pdev->dev;
	input_dev->close = max8997_haptic_close;
	input_set_drvdata(input_dev, chip);
	input_set_capability(input_dev, EV_FF, FF_RUMBLE);

	error = input_ff_create_memless(input_dev, NULL,
				max8997_haptic_play_effect);
	if (error) {
		dev_err(&pdev->dev,
			"unable to create FF device, error: %d\n",
			error);
		goto err_put_regulator;
	}

	error = input_register_device(input_dev);
	if (error) {
		dev_err(&pdev->dev,
			"unable to register input device, error: %d\n",
			error);
		goto err_destroy_ff;
	}

	platform_set_drvdata(pdev, chip);
	return 0;

err_destroy_ff:
	input_ff_destroy(input_dev);
err_put_regulator:
	regulator_put(chip->regulator);
err_free_pwm:
	if (chip->mode == MAX8997_EXTERNAL_MODE)
		pwm_put(chip->pwm);
err_free_mem:
	input_free_device(input_dev);
	kfree(chip);

	return error;
}

static void max8997_haptic_remove(struct platform_device *pdev)
{
	struct max8997_haptic *chip = platform_get_drvdata(pdev);

	input_unregister_device(chip->input_dev);
	regulator_put(chip->regulator);

	if (chip->mode == MAX8997_EXTERNAL_MODE)
		pwm_put(chip->pwm);

	kfree(chip);
}

static int max8997_haptic_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct max8997_haptic *chip = platform_get_drvdata(pdev);

	max8997_haptic_disable(chip);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(max8997_haptic_pm_ops,
				max8997_haptic_suspend, NULL);

static const struct platform_device_id max8997_haptic_id[] = {
	{ "max8997-haptic", 0 },
	{ },
};
MODULE_DEVICE_TABLE(platform, max8997_haptic_id);

static struct platform_driver max8997_haptic_driver = {
	.driver	= {
		.name	= "max8997-haptic",
		.pm	= pm_sleep_ptr(&max8997_haptic_pm_ops),
	},
	.probe		= max8997_haptic_probe,
	.remove		= max8997_haptic_remove,
	.id_table	= max8997_haptic_id,
};
module_platform_driver(max8997_haptic_driver);

MODULE_AUTHOR("Donggeun Kim <dg77.kim@samsung.com>");
MODULE_DESCRIPTION("max8997_haptic driver");
MODULE_LICENSE("GPL");
