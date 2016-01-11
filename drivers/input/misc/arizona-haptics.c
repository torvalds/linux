/*
 * Arizona haptics driver
 *
 * Copyright 2012 Wolfson Microelectronics plc
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/slab.h>

#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <linux/mfd/arizona/core.h>
#include <linux/mfd/arizona/pdata.h>
#include <linux/mfd/arizona/registers.h>

struct arizona_haptics {
	struct arizona *arizona;
	struct input_dev *input_dev;
	struct work_struct work;

	struct mutex mutex;
	u8 intensity;
};

static void arizona_haptics_work(struct work_struct *work)
{
	struct arizona_haptics *haptics = container_of(work,
						       struct arizona_haptics,
						       work);
	struct arizona *arizona = haptics->arizona;
	int ret;

	if (!haptics->arizona->dapm) {
		dev_err(arizona->dev, "No DAPM context\n");
		return;
	}

	if (haptics->intensity) {
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_HAPTICS_PHASE_2_INTENSITY,
					 ARIZONA_PHASE2_INTENSITY_MASK,
					 haptics->intensity);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to set intensity: %d\n",
				ret);
			return;
		}

		/* This enable sequence will be a noop if already enabled */
		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_HAPTICS_CONTROL_1,
					 ARIZONA_HAP_CTRL_MASK,
					 1 << ARIZONA_HAP_CTRL_SHIFT);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to start haptics: %d\n",
				ret);
			return;
		}

		ret = snd_soc_dapm_enable_pin(arizona->dapm, "HAPTICS");
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to start HAPTICS: %d\n",
				ret);
			return;
		}

		ret = snd_soc_dapm_sync(arizona->dapm);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to sync DAPM: %d\n",
				ret);
			return;
		}
	} else {
		/* This disable sequence will be a noop if already enabled */
		ret = snd_soc_dapm_disable_pin(arizona->dapm, "HAPTICS");
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to disable HAPTICS: %d\n",
				ret);
			return;
		}

		ret = snd_soc_dapm_sync(arizona->dapm);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to sync DAPM: %d\n",
				ret);
			return;
		}

		ret = regmap_update_bits(arizona->regmap,
					 ARIZONA_HAPTICS_CONTROL_1,
					 ARIZONA_HAP_CTRL_MASK, 0);
		if (ret != 0) {
			dev_err(arizona->dev, "Failed to stop haptics: %d\n",
				ret);
			return;
		}
	}
}

static int arizona_haptics_play(struct input_dev *input, void *data,
				struct ff_effect *effect)
{
	struct arizona_haptics *haptics = input_get_drvdata(input);
	struct arizona *arizona = haptics->arizona;

	if (!arizona->dapm) {
		dev_err(arizona->dev, "No DAPM context\n");
		return -EBUSY;
	}

	if (effect->u.rumble.strong_magnitude) {
		/* Scale the magnitude into the range the device supports */
		if (arizona->pdata.hap_act) {
			haptics->intensity =
				effect->u.rumble.strong_magnitude >> 9;
			if (effect->direction < 0x8000)
				haptics->intensity += 0x7f;
		} else {
			haptics->intensity =
				effect->u.rumble.strong_magnitude >> 8;
		}
	} else {
		haptics->intensity = 0;
	}

	schedule_work(&haptics->work);

	return 0;
}

static void arizona_haptics_close(struct input_dev *input)
{
	struct arizona_haptics *haptics = input_get_drvdata(input);

	cancel_work_sync(&haptics->work);

	if (haptics->arizona->dapm)
		snd_soc_dapm_disable_pin(haptics->arizona->dapm, "HAPTICS");
}

static int arizona_haptics_probe(struct platform_device *pdev)
{
	struct arizona *arizona = dev_get_drvdata(pdev->dev.parent);
	struct arizona_haptics *haptics;
	int ret;

	haptics = devm_kzalloc(&pdev->dev, sizeof(*haptics), GFP_KERNEL);
	if (!haptics)
		return -ENOMEM;

	haptics->arizona = arizona;

	ret = regmap_update_bits(arizona->regmap, ARIZONA_HAPTICS_CONTROL_1,
				 ARIZONA_HAP_ACT, arizona->pdata.hap_act);
	if (ret != 0) {
		dev_err(arizona->dev, "Failed to set haptics actuator: %d\n",
			ret);
		return ret;
	}

	INIT_WORK(&haptics->work, arizona_haptics_work);

	haptics->input_dev = devm_input_allocate_device(&pdev->dev);
	if (!haptics->input_dev) {
		dev_err(arizona->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	input_set_drvdata(haptics->input_dev, haptics);

	haptics->input_dev->name = "arizona:haptics";
	haptics->input_dev->dev.parent = pdev->dev.parent;
	haptics->input_dev->close = arizona_haptics_close;
	__set_bit(FF_RUMBLE, haptics->input_dev->ffbit);

	ret = input_ff_create_memless(haptics->input_dev, NULL,
				      arizona_haptics_play);
	if (ret < 0) {
		dev_err(arizona->dev, "input_ff_create_memless() failed: %d\n",
			ret);
		return ret;
	}

	ret = input_register_device(haptics->input_dev);
	if (ret < 0) {
		dev_err(arizona->dev, "couldn't register input device: %d\n",
			ret);
		return ret;
	}

	platform_set_drvdata(pdev, haptics);

	return 0;
}

static struct platform_driver arizona_haptics_driver = {
	.probe		= arizona_haptics_probe,
	.driver		= {
		.name	= "arizona-haptics",
	},
};
module_platform_driver(arizona_haptics_driver);

MODULE_ALIAS("platform:arizona-haptics");
MODULE_DESCRIPTION("Arizona haptics driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mark Brown <broonie@opensource.wolfsonmicro.com>");
