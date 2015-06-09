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
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/backlight.h>
#include <linux/mfd/88pm860x.h>
#include <linux/module.h>

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
	int	reg_duty_cycle;
	int	reg_always_on;
	int	reg_current;
};

static int backlight_power_set(struct pm860x_chip *chip, int port,
		int on)
{
	int ret = -EINVAL;

	switch (port) {
	case 0:
		ret = on ? pm8606_osc_enable(chip, WLED1_DUTY) :
			pm8606_osc_disable(chip, WLED1_DUTY);
		break;
	case 1:
		ret = on ? pm8606_osc_enable(chip, WLED2_DUTY) :
			pm8606_osc_disable(chip, WLED2_DUTY);
		break;
	case 2:
		ret = on ? pm8606_osc_enable(chip, WLED3_DUTY) :
			pm8606_osc_disable(chip, WLED3_DUTY);
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

	if (brightness)
		backlight_power_set(chip, data->port, 1);

	ret = pm860x_reg_write(data->i2c, data->reg_duty_cycle, value);
	if (ret < 0)
		goto out;

	if ((data->current_brightness == 0) && brightness) {
		if (data->iset) {
			ret = pm860x_set_bits(data->i2c, data->reg_current,
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
			ret = pm860x_set_bits(data->i2c, data->reg_always_on,
					      PM8606_WLED_ON, PM8606_WLED_ON);
		}
	} else {
		if (brightness == MAX_BRIGHTNESS) {
			/* set WLED_ON bit as 100% */
			ret = pm860x_set_bits(data->i2c, data->reg_always_on,
					      PM8606_WLED_ON, PM8606_WLED_ON);
		} else {
			/* clear WLED_ON bit since it's not 100% */
			ret = pm860x_set_bits(data->i2c, data->reg_always_on,
					      PM8606_WLED_ON, 0);
		}
	}
	if (ret < 0)
		goto out;

	if (brightness == 0)
		backlight_power_set(chip, data->port, 0);

	dev_dbg(chip->dev, "set brightness %d\n", value);
	data->current_brightness = value;
	return 0;
out:
	dev_dbg(chip->dev, "set brightness %d failure with return value: %d\n",
		value, ret);
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

	ret = pm860x_reg_read(data->i2c, data->reg_duty_cycle);
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

#ifdef CONFIG_OF
static int pm860x_backlight_dt_init(struct platform_device *pdev,
				    struct pm860x_backlight_data *data,
				    char *name)
{
	struct device_node *nproot, *np;
	int iset = 0;

	nproot = of_get_child_by_name(pdev->dev.parent->of_node, "backlights");
	if (!nproot) {
		dev_err(&pdev->dev, "failed to find backlights node\n");
		return -ENODEV;
	}
	for_each_child_of_node(nproot, np) {
		if (!of_node_cmp(np->name, name)) {
			of_property_read_u32(np, "marvell,88pm860x-iset",
					     &iset);
			data->iset = PM8606_WLED_CURRENT(iset);
			of_property_read_u32(np, "marvell,88pm860x-pwm",
					     &data->pwm);
			break;
		}
	}
	of_node_put(nproot);
	return 0;
}
#else
#define pm860x_backlight_dt_init(x, y, z)	(-1)
#endif

static int pm860x_backlight_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm860x_backlight_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct pm860x_backlight_data *data;
	struct backlight_device *bl;
	struct resource *res;
	struct backlight_properties props;
	char name[MFD_NAME_SIZE];
	int ret = 0;

	data = devm_kzalloc(&pdev->dev, sizeof(struct pm860x_backlight_data),
			    GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	res = platform_get_resource_byname(pdev, IORESOURCE_REG, "duty cycle");
	if (!res) {
		dev_err(&pdev->dev, "No REG resource for duty cycle\n");
		return -ENXIO;
	}
	data->reg_duty_cycle = res->start;
	res = platform_get_resource_byname(pdev, IORESOURCE_REG, "always on");
	if (!res) {
		dev_err(&pdev->dev, "No REG resource for always on\n");
		return -ENXIO;
	}
	data->reg_always_on = res->start;
	res = platform_get_resource_byname(pdev, IORESOURCE_REG, "current");
	if (!res) {
		dev_err(&pdev->dev, "No REG resource for current\n");
		return -ENXIO;
	}
	data->reg_current = res->start;

	memset(name, 0, MFD_NAME_SIZE);
	sprintf(name, "backlight-%d", pdev->id);
	data->port = pdev->id;
	data->chip = chip;
	data->i2c = (chip->id == CHIP_PM8606) ? chip->client : chip->companion;
	data->current_brightness = MAX_BRIGHTNESS;
	if (pm860x_backlight_dt_init(pdev, data, name)) {
		if (pdata) {
			data->pwm = pdata->pwm;
			data->iset = pdata->iset;
		}
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = MAX_BRIGHTNESS;
	bl = devm_backlight_device_register(&pdev->dev, name, &pdev->dev, data,
					&pm860x_backlight_ops, &props);
	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}
	bl->props.brightness = MAX_BRIGHTNESS;

	platform_set_drvdata(pdev, bl);

	/* read current backlight */
	ret = pm860x_backlight_get_brightness(bl);
	if (ret < 0)
		return ret;

	backlight_update_status(bl);
	return 0;
}

static struct platform_driver pm860x_backlight_driver = {
	.driver		= {
		.name	= "88pm860x-backlight",
	},
	.probe		= pm860x_backlight_probe,
};

module_platform_driver(pm860x_backlight_driver);

MODULE_DESCRIPTION("Backlight Driver for Marvell Semiconductor 88PM8606");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:88pm860x-backlight");
