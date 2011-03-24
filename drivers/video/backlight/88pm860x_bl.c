/*
 * Backlight driver for Marvell Semiconductor 88PM8606
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/backlight.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm860x.h>

#define MAX_BRIGHTNESS		(0xFF)
#define MIN_BRIGHTNESS		(0)

#define CURRENT_BITMASK		(0x1F << 1)

struct pm860x_backlight_data {
	struct pm860x_chip *chip;
	struct i2c_client *i2c;
	int	current_brightness;
	int	port;
	int	pwm;
	int	iset;
};

static inline int wled_a(int port)
{
	int ret;

	ret = ((port - PM8606_BACKLIGHT1) << 1) + 2;
	return ret;
}

static inline int wled_b(int port)
{
	int ret;

	ret = ((port - PM8606_BACKLIGHT1) << 1) + 3;
	return ret;
}

/* WLED2 & WLED3 share the same IDC */
static inline int wled_idc(int port)
{
	int ret;

	switch (port) {
	case PM8606_BACKLIGHT1:
	case PM8606_BACKLIGHT2:
		ret = ((port - PM8606_BACKLIGHT1) << 1) + 3;
		break;
	case PM8606_BACKLIGHT3:
	default:
		ret = ((port - PM8606_BACKLIGHT2) << 1) + 3;
		break;
	}
	return ret;
}

static int pm860x_backlight_set(struct backlight_device *bl, int brightness)
{
	struct pm860x_backlight_data *data = bl_get_data(bl);
	struct pm860x_chip *chip = data->chip;
	unsigned char value;
	int ret;

	if (brightness > MAX_BRIGHTNESS)
		value = MAX_BRIGHTNESS;
	else
		value = brightness;

	ret = pm860x_reg_write(data->i2c, wled_a(data->port), value);
	if (ret < 0)
		goto out;

	if ((data->current_brightness == 0) && brightness) {
		if (data->iset) {
			ret = pm860x_set_bits(data->i2c, wled_idc(data->port),
					      CURRENT_BITMASK, data->iset);
			if (ret < 0)
				goto out;
		}
		if (data->pwm) {
			ret = pm860x_set_bits(data->i2c, PM8606_PWM,
					      PM8606_PWM_FREQ_MASK, data->pwm);
			if (ret < 0)
				goto out;
		}
		if (brightness == MAX_BRIGHTNESS) {
			/* set WLED_ON bit as 100% */
			ret = pm860x_set_bits(data->i2c, wled_b(data->port),
					      PM8606_WLED_ON, PM8606_WLED_ON);
		}
	} else {
		if (brightness == MAX_BRIGHTNESS) {
			/* set WLED_ON bit as 100% */
			ret = pm860x_set_bits(data->i2c, wled_b(data->port),
					      PM8606_WLED_ON, PM8606_WLED_ON);
		} else {
			/* clear WLED_ON bit since it's not 100% */
			ret = pm860x_set_bits(data->i2c, wled_b(data->port),
					      PM8606_WLED_ON, 0);
		}
	}
	if (ret < 0)
		goto out;

	dev_dbg(chip->dev, "set brightness %d\n", value);
	data->current_brightness = value;
	return 0;
out:
	dev_dbg(chip->dev, "set brightness %d failure with return "
		"value:%d\n", value, ret);
	return ret;
}

static int pm860x_backlight_update_status(struct backlight_device *bl)
{
	int brightness = bl->props.brightness;

	if (bl->props.power != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	if (bl->props.state & BL_CORE_SUSPENDED)
		brightness = 0;

	return pm860x_backlight_set(bl, brightness);
}

static int pm860x_backlight_get_brightness(struct backlight_device *bl)
{
	struct pm860x_backlight_data *data = bl_get_data(bl);
	struct pm860x_chip *chip = data->chip;
	int ret;

	ret = pm860x_reg_read(data->i2c, wled_a(data->port));
	if (ret < 0)
		goto out;
	data->current_brightness = ret;
	dev_dbg(chip->dev, "get brightness %d\n", data->current_brightness);
	return data->current_brightness;
out:
	return -EINVAL;
}

static const struct backlight_ops pm860x_backlight_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= pm860x_backlight_update_status,
	.get_brightness	= pm860x_backlight_get_brightness,
};

static int pm860x_backlight_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm860x_backlight_pdata *pdata = NULL;
	struct pm860x_backlight_data *data;
	struct backlight_device *bl;
	struct mfd_cell *cell;
	struct resource *res;
	struct backlight_properties props;
	unsigned char value;
	char name[MFD_NAME_SIZE];
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No I/O resource!\n");
		return -EINVAL;
	}

	cell = pdev->dev.platform_data;
	if (cell == NULL)
		return -ENODEV;
	pdata = cell->mfd_data;
	if (pdata == NULL) {
		dev_err(&pdev->dev, "platform data isn't assigned to "
			"backlight\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof(struct pm860x_backlight_data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	strncpy(name, res->name, MFD_NAME_SIZE);
	data->chip = chip;
	data->i2c = (chip->id == CHIP_PM8606) ? chip->client	\
			: chip->companion;
	data->current_brightness = MAX_BRIGHTNESS;
	data->pwm = pdata->pwm;
	data->iset = pdata->iset;
	data->port = pdata->flags;
	if (data->port < 0) {
		dev_err(&pdev->dev, "wrong platform data is assigned");
		kfree(data);
		return -EINVAL;
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = MAX_BRIGHTNESS;
	bl = backlight_device_register(name, &pdev->dev, data,
					&pm860x_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		kfree(data);
		return PTR_ERR(bl);
	}
	bl->props.brightness = MAX_BRIGHTNESS;

	platform_set_drvdata(pdev, bl);

	/* Enable reference VSYS */
	ret = pm860x_reg_read(data->i2c, PM8606_VSYS);
	if (ret < 0)
		goto out;
	if ((ret & PM8606_VSYS_EN) == 0) {
		value = ret | PM8606_VSYS_EN;
		ret = pm860x_reg_write(data->i2c, PM8606_VSYS, value);
		if (ret < 0)
			goto out;
	}
	/* Enable reference OSC */
	ret = pm860x_reg_read(data->i2c, PM8606_MISC);
	if (ret < 0)
		goto out;
	if ((ret & PM8606_MISC_OSC_EN) == 0) {
		value = ret | PM8606_MISC_OSC_EN;
		ret = pm860x_reg_write(data->i2c, PM8606_MISC, value);
		if (ret < 0)
			goto out;
	}
	/* read current backlight */
	ret = pm860x_backlight_get_brightness(bl);
	if (ret < 0)
		goto out;

	backlight_update_status(bl);
	return 0;
out:
	backlight_device_unregister(bl);
	kfree(data);
	return ret;
}

static int pm860x_backlight_remove(struct platform_device *pdev)
{
	struct backlight_device *bl = platform_get_drvdata(pdev);
	struct pm860x_backlight_data *data = bl_get_data(bl);

	backlight_device_unregister(bl);
	kfree(data);
	return 0;
}

static struct platform_driver pm860x_backlight_driver = {
	.driver		= {
		.name	= "88pm860x-backlight",
		.owner	= THIS_MODULE,
	},
	.probe		= pm860x_backlight_probe,
	.remove		= pm860x_backlight_remove,
};

static int __init pm860x_backlight_init(void)
{
	return platform_driver_register(&pm860x_backlight_driver);
}
module_init(pm860x_backlight_init);

static void __exit pm860x_backlight_exit(void)
{
	platform_driver_unregister(&pm860x_backlight_driver);
}
module_exit(pm860x_backlight_exit);

MODULE_DESCRIPTION("Backlight Driver for Marvell Semiconductor 88PM8606");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:88pm860x-backlight");
