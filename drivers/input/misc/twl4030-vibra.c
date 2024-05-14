// SPDX-License-Identifier: GPL-2.0-only
/*
 * twl4030-vibra.c - TWL4030 Vibrator driver
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Written by Henrik Saari <henrik.saari@nokia.com>
 * Updates by Felipe Balbi <felipe.balbi@nokia.com>
 * Input by Jari Vanhala <ext-jari.vanhala@nokia.com>
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/workqueue.h>
#include <linux/mfd/twl.h>
#include <linux/mfd/twl4030-audio.h>
#include <linux/input.h>
#include <linux/slab.h>

/* MODULE ID2 */
#define LEDEN		0x00

/* ForceFeedback */
#define EFFECT_DIR_180_DEG	0x8000 /* range is 0 - 0xFFFF */

struct vibra_info {
	struct device		*dev;
	struct input_dev	*input_dev;

	struct work_struct	play_work;

	bool			enabled;
	int			speed;
	int			direction;

	bool			coexist;
};

static void vibra_disable_leds(void)
{
	u8 reg;

	/* Disable LEDA & LEDB, cannot be used with vibra (PWM) */
	twl_i2c_read_u8(TWL4030_MODULE_LED, &reg, LEDEN);
	reg &= ~0x03;
	twl_i2c_write_u8(TWL4030_MODULE_LED, LEDEN, reg);
}

/* Powers H-Bridge and enables audio clk */
static void vibra_enable(struct vibra_info *info)
{
	u8 reg;

	twl4030_audio_enable_resource(TWL4030_AUDIO_RES_POWER);

	/* turn H-Bridge on */
	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			&reg, TWL4030_REG_VIBRA_CTL);
	twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			 (reg | TWL4030_VIBRA_EN), TWL4030_REG_VIBRA_CTL);

	twl4030_audio_enable_resource(TWL4030_AUDIO_RES_APLL);

	info->enabled = true;
}

static void vibra_disable(struct vibra_info *info)
{
	u8 reg;

	/* Power down H-Bridge */
	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			&reg, TWL4030_REG_VIBRA_CTL);
	twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			 (reg & ~TWL4030_VIBRA_EN), TWL4030_REG_VIBRA_CTL);

	twl4030_audio_disable_resource(TWL4030_AUDIO_RES_APLL);
	twl4030_audio_disable_resource(TWL4030_AUDIO_RES_POWER);

	info->enabled = false;
}

static void vibra_play_work(struct work_struct *work)
{
	struct vibra_info *info = container_of(work,
			struct vibra_info, play_work);
	int dir;
	int pwm;
	u8 reg;

	dir = info->direction;
	pwm = info->speed;

	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			&reg, TWL4030_REG_VIBRA_CTL);
	if (pwm && (!info->coexist || !(reg & TWL4030_VIBRA_SEL))) {

		if (!info->enabled)
			vibra_enable(info);

		/* set vibra rotation direction */
		twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
				&reg, TWL4030_REG_VIBRA_CTL);
		reg = (dir) ? (reg | TWL4030_VIBRA_DIR) :
			(reg & ~TWL4030_VIBRA_DIR);
		twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
				 reg, TWL4030_REG_VIBRA_CTL);

		/* set PWM, 1 = max, 255 = min */
		twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
				 256 - pwm, TWL4030_REG_VIBRA_SET);
	} else {
		if (info->enabled)
			vibra_disable(info);
	}
}

/*** Input/ForceFeedback ***/

static int vibra_play(struct input_dev *input, void *data,
		      struct ff_effect *effect)
{
	struct vibra_info *info = input_get_drvdata(input);

	info->speed = effect->u.rumble.strong_magnitude >> 8;
	if (!info->speed)
		info->speed = effect->u.rumble.weak_magnitude >> 9;
	info->direction = effect->direction < EFFECT_DIR_180_DEG ? 0 : 1;
	schedule_work(&info->play_work);
	return 0;
}

static void twl4030_vibra_close(struct input_dev *input)
{
	struct vibra_info *info = input_get_drvdata(input);

	cancel_work_sync(&info->play_work);

	if (info->enabled)
		vibra_disable(info);
}

/*** Module ***/
static int __maybe_unused twl4030_vibra_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vibra_info *info = platform_get_drvdata(pdev);

	if (info->enabled)
		vibra_disable(info);

	return 0;
}

static int __maybe_unused twl4030_vibra_resume(struct device *dev)
{
	vibra_disable_leds();
	return 0;
}

static SIMPLE_DEV_PM_OPS(twl4030_vibra_pm_ops,
			 twl4030_vibra_suspend, twl4030_vibra_resume);

static bool twl4030_vibra_check_coexist(struct device_node *parent)
{
	struct device_node *node;

	node = of_get_child_by_name(parent, "codec");
	if (node) {
		of_node_put(node);
		return true;
	}

	return false;
}

static int twl4030_vibra_probe(struct platform_device *pdev)
{
	struct device_node *twl4030_core_node = pdev->dev.parent->of_node;
	struct vibra_info *info;
	int ret;

	if (!twl4030_core_node) {
		dev_dbg(&pdev->dev, "twl4030 OF node is missing\n");
		return -EINVAL;
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->coexist = twl4030_vibra_check_coexist(twl4030_core_node);
	INIT_WORK(&info->play_work, vibra_play_work);

	info->input_dev = devm_input_allocate_device(&pdev->dev);
	if (info->input_dev == NULL) {
		dev_err(&pdev->dev, "couldn't allocate input device\n");
		return -ENOMEM;
	}

	input_set_drvdata(info->input_dev, info);

	info->input_dev->name = "twl4030:vibrator";
	info->input_dev->id.version = 1;
	info->input_dev->close = twl4030_vibra_close;
	__set_bit(FF_RUMBLE, info->input_dev->ffbit);

	ret = input_ff_create_memless(info->input_dev, NULL, vibra_play);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "couldn't register vibrator to FF\n");
		return ret;
	}

	ret = input_register_device(info->input_dev);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "couldn't register input device\n");
		goto err_iff;
	}

	vibra_disable_leds();

	platform_set_drvdata(pdev, info);
	return 0;

err_iff:
	input_ff_destroy(info->input_dev);
	return ret;
}

static struct platform_driver twl4030_vibra_driver = {
	.probe		= twl4030_vibra_probe,
	.driver		= {
		.name	= "twl4030-vibra",
		.pm	= &twl4030_vibra_pm_ops,
	},
};
module_platform_driver(twl4030_vibra_driver);

MODULE_ALIAS("platform:twl4030-vibra");
MODULE_DESCRIPTION("TWL4030 Vibra driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nokia Corporation");
