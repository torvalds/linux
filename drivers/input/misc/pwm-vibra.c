/*
 *  PWM vibrator driver
 *
 *  Copyright (C) 2017 Collabora Ltd.
 *
 *  Based on previous work from:
 *  Copyright (C) 2012 Dmitry Torokhov <dmitry.torokhov@gmail.com>
 *
 *  Based on PWM beeper driver:
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 */

#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

struct pwm_vibrator {
	struct input_dev *input;
	struct pwm_device *pwm;
	struct pwm_device *pwm_dir;
	struct regulator *vcc;

	struct work_struct play_work;
	u16 level;
	u32 direction_duty_cycle;
	bool vcc_on;
};

static int pwm_vibrator_start(struct pwm_vibrator *vibrator)
{
	struct device *pdev = vibrator->input->dev.parent;
	struct pwm_state state;
	int err;

	if (!vibrator->vcc_on) {
		err = regulator_enable(vibrator->vcc);
		if (err) {
			dev_err(pdev, "failed to enable regulator: %d", err);
			return err;
		}
		vibrator->vcc_on = true;
	}

	pwm_get_state(vibrator->pwm, &state);
	pwm_set_relative_duty_cycle(&state, vibrator->level, 0xffff);
	state.enabled = true;

	err = pwm_apply_state(vibrator->pwm, &state);
	if (err) {
		dev_err(pdev, "failed to apply pwm state: %d", err);
		return err;
	}

	if (vibrator->pwm_dir) {
		pwm_get_state(vibrator->pwm_dir, &state);
		state.duty_cycle = vibrator->direction_duty_cycle;
		state.enabled = true;

		err = pwm_apply_state(vibrator->pwm_dir, &state);
		if (err) {
			dev_err(pdev, "failed to apply dir-pwm state: %d", err);
			pwm_disable(vibrator->pwm);
			return err;
		}
	}

	return 0;
}

static void pwm_vibrator_stop(struct pwm_vibrator *vibrator)
{
	if (vibrator->pwm_dir)
		pwm_disable(vibrator->pwm_dir);
	pwm_disable(vibrator->pwm);

	if (vibrator->vcc_on) {
		regulator_disable(vibrator->vcc);
		vibrator->vcc_on = false;
	}
}

static void pwm_vibrator_play_work(struct work_struct *work)
{
	struct pwm_vibrator *vibrator = container_of(work,
					struct pwm_vibrator, play_work);

	if (vibrator->level)
		pwm_vibrator_start(vibrator);
	else
		pwm_vibrator_stop(vibrator);
}

static int pwm_vibrator_play_effect(struct input_dev *dev, void *data,
				    struct ff_effect *effect)
{
	struct pwm_vibrator *vibrator = input_get_drvdata(dev);

	vibrator->level = effect->u.rumble.strong_magnitude;
	if (!vibrator->level)
		vibrator->level = effect->u.rumble.weak_magnitude;

	schedule_work(&vibrator->play_work);

	return 0;
}

static void pwm_vibrator_close(struct input_dev *input)
{
	struct pwm_vibrator *vibrator = input_get_drvdata(input);

	cancel_work_sync(&vibrator->play_work);
	pwm_vibrator_stop(vibrator);
}

static int pwm_vibrator_probe(struct platform_device *pdev)
{
	struct pwm_vibrator *vibrator;
	struct pwm_state state;
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
			dev_err(&pdev->dev, "Failed to request regulator: %d",
				err);
		return err;
	}

	vibrator->pwm = devm_pwm_get(&pdev->dev, "enable");
	err = PTR_ERR_OR_ZERO(vibrator->pwm);
	if (err) {
		if (err != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Failed to request main pwm: %d",
				err);
		return err;
	}

	INIT_WORK(&vibrator->play_work, pwm_vibrator_play_work);

	/* Sync up PWM state and ensure it is off. */
	pwm_init_state(vibrator->pwm, &state);
	state.enabled = false;
	err = pwm_apply_state(vibrator->pwm, &state);
	if (err) {
		dev_err(&pdev->dev, "failed to apply initial PWM state: %d",
			err);
		return err;
	}

	vibrator->pwm_dir = devm_pwm_get(&pdev->dev, "direction");
	err = PTR_ERR_OR_ZERO(vibrator->pwm_dir);
	switch (err) {
	case 0:
		/* Sync up PWM state and ensure it is off. */
		pwm_init_state(vibrator->pwm_dir, &state);
		state.enabled = false;
		err = pwm_apply_state(vibrator->pwm_dir, &state);
		if (err) {
			dev_err(&pdev->dev, "failed to apply initial PWM state: %d",
				err);
			return err;
		}

		vibrator->direction_duty_cycle =
			pwm_get_period(vibrator->pwm_dir) / 2;
		device_property_read_u32(&pdev->dev, "direction-duty-cycle-ns",
					 &vibrator->direction_duty_cycle);
		break;

	case -ENODATA:
		/* Direction PWM is optional */
		vibrator->pwm_dir = NULL;
		break;

	default:
		dev_err(&pdev->dev, "Failed to request direction pwm: %d", err);
		/* Fall through */

	case -EPROBE_DEFER:
		return err;
	}

	vibrator->input->name = "pwm-vibrator";
	vibrator->input->id.bustype = BUS_HOST;
	vibrator->input->dev.parent = &pdev->dev;
	vibrator->input->close = pwm_vibrator_close;

	input_set_drvdata(vibrator->input, vibrator);
	input_set_capability(vibrator->input, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(vibrator->input, NULL,
				      pwm_vibrator_play_effect);
	if (err) {
		dev_err(&pdev->dev, "Couldn't create FF dev: %d", err);
		return err;
	}

	err = input_register_device(vibrator->input);
	if (err) {
		dev_err(&pdev->dev, "Couldn't register input dev: %d", err);
		return err;
	}

	platform_set_drvdata(pdev, vibrator);

	return 0;
}

static int __maybe_unused pwm_vibrator_suspend(struct device *dev)
{
	struct pwm_vibrator *vibrator = dev_get_drvdata(dev);

	cancel_work_sync(&vibrator->play_work);
	if (vibrator->level)
		pwm_vibrator_stop(vibrator);

	return 0;
}

static int __maybe_unused pwm_vibrator_resume(struct device *dev)
{
	struct pwm_vibrator *vibrator = dev_get_drvdata(dev);

	if (vibrator->level)
		pwm_vibrator_start(vibrator);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pwm_vibrator_pm_ops,
			 pwm_vibrator_suspend, pwm_vibrator_resume);

#ifdef CONFIG_OF
static const struct of_device_id pwm_vibra_dt_match_table[] = {
	{ .compatible = "pwm-vibrator" },
	{},
};
MODULE_DEVICE_TABLE(of, pwm_vibra_dt_match_table);
#endif

static struct platform_driver pwm_vibrator_driver = {
	.probe	= pwm_vibrator_probe,
	.driver	= {
		.name	= "pwm-vibrator",
		.pm	= &pwm_vibrator_pm_ops,
		.of_match_table = of_match_ptr(pwm_vibra_dt_match_table),
	},
};
module_platform_driver(pwm_vibrator_driver);

MODULE_AUTHOR("Sebastian Reichel <sre@kernel.org>");
MODULE_DESCRIPTION("PWM vibrator driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pwm-vibrator");
