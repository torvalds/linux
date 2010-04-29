/*
 * twl4030-vibra.c - TWL4030 Vibrator driver
 *
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Written by Henrik Saari <henrik.saari@nokia.com>
 * Updates by Felipe Balbi <felipe.balbi@nokia.com>
 * Input by Jari Vanhala <ext-jari.vanhala@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/twl4030-codec.h>
#include <linux/input.h>
#include <linux/slab.h>

/* MODULE ID2 */
#define LEDEN		0x00

/* ForceFeedback */
#define EFFECT_DIR_180_DEG	0x8000 /* range is 0 - 0xFFFF */

struct vibra_info {
	struct device		*dev;
	struct input_dev	*input_dev;

	struct workqueue_struct *workqueue;
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

	twl4030_codec_enable_resource(TWL4030_CODEC_RES_POWER);

	/* turn H-Bridge on */
	twl_i2c_read_u8(TWL4030_MODULE_AUDIO_VOICE,
			&reg, TWL4030_REG_VIBRA_CTL);
	twl_i2c_write_u8(TWL4030_MODULE_AUDIO_VOICE,
			 (reg | TWL4030_VIBRA_EN), TWL4030_REG_VIBRA_CTL);

	twl4030_codec_enable_resource(TWL4030_CODEC_RES_APLL);

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

	twl4030_codec_disable_resource(TWL4030_CODEC_RES_POWER);
	twl4030_codec_disable_resource(TWL4030_CODEC_RES_APLL);

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
	queue_work(info->workqueue, &info->play_work);
	return 0;
}

static int twl4030_vibra_open(struct input_dev *input)
{
	struct vibra_info *info = input_get_drvdata(input);

	info->workqueue = create_singlethread_workqueue("vibra");
	if (info->workqueue == NULL) {
		dev_err(&input->dev, "couldn't create workqueue\n");
		return -ENOMEM;
	}
	return 0;
}

static void twl4030_vibra_close(struct input_dev *input)
{
	struct vibra_info *info = input_get_drvdata(input);

	cancel_work_sync(&info->play_work);
	INIT_WORK(&info->play_work, vibra_play_work); /* cleanup */
	destroy_workqueue(info->workqueue);
	info->workqueue = NULL;

	if (info->enabled)
		vibra_disable(info);
}

/*** Module ***/
#if CONFIG_PM
static int twl4030_vibra_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct vibra_info *info = platform_get_drvdata(pdev);

	if (info->enabled)
		vibra_disable(info);

	return 0;
}

static int twl4030_vibra_resume(struct device *dev)
{
	vibra_disable_leds();
	return 0;
}

static SIMPLE_DEV_PM_OPS(twl4030_vibra_pm_ops,
			 twl4030_vibra_suspend, twl4030_vibra_resume);
#endif

static int __devinit twl4030_vibra_probe(struct platform_device *pdev)
{
	struct twl4030_codec_vibra_data *pdata = pdev->dev.platform_data;
	struct vibra_info *info;
	int ret;

	if (!pdata) {
		dev_dbg(&pdev->dev, "platform_data not available\n");
		return -EINVAL;
	}

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->coexist = pdata->coexist;
	INIT_WORK(&info->play_work, vibra_play_work);

	info->input_dev = input_allocate_device();
	if (info->input_dev == NULL) {
		dev_err(&pdev->dev, "couldn't allocate input device\n");
		ret = -ENOMEM;
		goto err_kzalloc;
	}

	input_set_drvdata(info->input_dev, info);

	info->input_dev->name = "twl4030:vibrator";
	info->input_dev->id.version = 1;
	info->input_dev->dev.parent = pdev->dev.parent;
	info->input_dev->open = twl4030_vibra_open;
	info->input_dev->close = twl4030_vibra_close;
	__set_bit(FF_RUMBLE, info->input_dev->ffbit);

	ret = input_ff_create_memless(info->input_dev, NULL, vibra_play);
	if (ret < 0) {
		dev_dbg(&pdev->dev, "couldn't register vibrator to FF\n");
		goto err_ialloc;
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
err_ialloc:
	input_free_device(info->input_dev);
err_kzalloc:
	kfree(info);
	return ret;
}

static int __devexit twl4030_vibra_remove(struct platform_device *pdev)
{
	struct vibra_info *info = platform_get_drvdata(pdev);

	/* this also free ff-memless and calls close if needed */
	input_unregister_device(info->input_dev);
	kfree(info);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver twl4030_vibra_driver = {
	.probe		= twl4030_vibra_probe,
	.remove		= __devexit_p(twl4030_vibra_remove),
	.driver		= {
		.name	= "twl4030_codec_vibra",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &twl4030_vibra_pm_ops,
#endif
	},
};

static int __init twl4030_vibra_init(void)
{
	return platform_driver_register(&twl4030_vibra_driver);
}
module_init(twl4030_vibra_init);

static void __exit twl4030_vibra_exit(void)
{
	platform_driver_unregister(&twl4030_vibra_driver);
}
module_exit(twl4030_vibra_exit);

MODULE_ALIAS("platform:twl4030_codec_vibra");

MODULE_DESCRIPTION("TWL4030 Vibra driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nokia Corporation");
