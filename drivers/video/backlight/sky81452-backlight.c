// SPDX-License-Identifier: GPL-2.0-only
/*
 * sky81452-backlight.c	SKY81452 backlight driver
 *
 * Copyright 2014 Skyworks Solutions Inc.
 * Author : Gyungoh Yoo <jack.yoo@skyworksinc.com>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/* registers */
#define SKY81452_REG0	0x00
#define SKY81452_REG1	0x01
#define SKY81452_REG2	0x02
#define SKY81452_REG4	0x04
#define SKY81452_REG5	0x05

/* bit mask */
#define SKY81452_CS	0xFF
#define SKY81452_EN	0x3F
#define SKY81452_IGPW	0x20
#define SKY81452_PWMMD	0x10
#define SKY81452_PHASE	0x08
#define SKY81452_ILIM	0x04
#define SKY81452_VSHRT	0x03
#define SKY81452_OCP	0x80
#define SKY81452_OTMP	0x40
#define SKY81452_SHRT	0x3F
#define SKY81452_OPN	0x3F

#define SKY81452_DEFAULT_NAME "lcd-backlight"
#define SKY81452_MAX_BRIGHTNESS	(SKY81452_CS + 1)

/**
 * struct sky81452_bl_platform_data - backlight platform data
 * @name:	backlight driver name.
 *		If it is not defined, default name is lcd-backlight.
 * @gpiod_enable:GPIO descriptor which control EN pin
 * @enable:	Enable mask for current sink channel 1, 2, 3, 4, 5 and 6.
 * @ignore_pwm:	true if DPWMI should be ignored.
 * @dpwm_mode:	true is DPWM dimming mode, otherwise Analog dimming mode.
 * @phase_shift:true is phase shift mode.
 * @short_detection_threshold:	It should be one of 4, 5, 6 and 7V.
 * @boost_current_limit:	It should be one of 2300, 2750mA.
 */
struct sky81452_bl_platform_data {
	const char *name;
	struct gpio_desc *gpiod_enable;
	unsigned int enable;
	bool ignore_pwm;
	bool dpwm_mode;
	bool phase_shift;
	unsigned int short_detection_threshold;
	unsigned int boost_current_limit;
};

#define CTZ(b) __builtin_ctz(b)

static int sky81452_bl_update_status(struct backlight_device *bd)
{
	const struct sky81452_bl_platform_data *pdata =
			dev_get_platdata(bd->dev.parent);
	const unsigned int brightness = (unsigned int)bd->props.brightness;
	struct regmap *regmap = bl_get_data(bd);
	int ret;

	if (brightness > 0) {
		ret = regmap_write(regmap, SKY81452_REG0, brightness - 1);
		if (ret < 0)
			return ret;

		return regmap_update_bits(regmap, SKY81452_REG1, SKY81452_EN,
					pdata->enable << CTZ(SKY81452_EN));
	}

	return regmap_update_bits(regmap, SKY81452_REG1, SKY81452_EN, 0);
}

static const struct backlight_ops sky81452_bl_ops = {
	.update_status = sky81452_bl_update_status,
};

static ssize_t sky81452_bl_store_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct regmap *regmap = bl_get_data(to_backlight_device(dev));
	unsigned long value;
	int ret;

	ret = kstrtoul(buf, 16, &value);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(regmap, SKY81452_REG1, SKY81452_EN,
					value << CTZ(SKY81452_EN));
	if (ret < 0)
		return ret;

	return count;
}

static ssize_t sky81452_bl_show_open_short(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct regmap *regmap = bl_get_data(to_backlight_device(dev));
	unsigned int reg, value = 0;
	char tmp[3];
	int i, ret;

	reg = !strcmp(attr->attr.name, "open") ? SKY81452_REG5 : SKY81452_REG4;
	ret = regmap_read(regmap, reg, &value);
	if (ret < 0)
		return ret;

	if (value & SKY81452_SHRT) {
		*buf = 0;
		for (i = 0; i < 6; i++) {
			if (value & 0x01) {
				sprintf(tmp, "%d ", i + 1);
				strcat(buf, tmp);
			}
			value >>= 1;
		}
		strcat(buf, "\n");
	} else {
		strcpy(buf, "none\n");
	}

	return strlen(buf);
}

static ssize_t sky81452_bl_show_fault(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct regmap *regmap = bl_get_data(to_backlight_device(dev));
	unsigned int value = 0;
	int ret;

	ret = regmap_read(regmap, SKY81452_REG4, &value);
	if (ret < 0)
		return ret;

	*buf = 0;

	if (value & SKY81452_OCP)
		strcat(buf, "over-current ");

	if (value & SKY81452_OTMP)
		strcat(buf, "over-temperature");

	strcat(buf, "\n");
	return strlen(buf);
}

static DEVICE_ATTR(enable, S_IWGRP | S_IWUSR, NULL, sky81452_bl_store_enable);
static DEVICE_ATTR(open, S_IRUGO, sky81452_bl_show_open_short, NULL);
static DEVICE_ATTR(short, S_IRUGO, sky81452_bl_show_open_short, NULL);
static DEVICE_ATTR(fault, S_IRUGO, sky81452_bl_show_fault, NULL);

static struct attribute *sky81452_bl_attribute[] = {
	&dev_attr_enable.attr,
	&dev_attr_open.attr,
	&dev_attr_short.attr,
	&dev_attr_fault.attr,
	NULL
};

static const struct attribute_group sky81452_bl_attr_group = {
	.attrs = sky81452_bl_attribute,
};

#ifdef CONFIG_OF
static struct sky81452_bl_platform_data *sky81452_bl_parse_dt(
							struct device *dev)
{
	struct device_node *np = dev->of_node;
	struct sky81452_bl_platform_data *pdata;
	int num_entry;
	unsigned int sources[6];
	int ret;

	if (!np) {
		dev_err(dev, "backlight node not found.\n");
		return ERR_PTR(-ENODATA);
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return ERR_PTR(-ENOMEM);

	of_property_read_string(np, "name", &pdata->name);
	pdata->ignore_pwm = of_property_read_bool(np, "skyworks,ignore-pwm");
	pdata->dpwm_mode = of_property_read_bool(np, "skyworks,dpwm-mode");
	pdata->phase_shift = of_property_read_bool(np, "skyworks,phase-shift");
	pdata->gpiod_enable = devm_gpiod_get_optional(dev, NULL, GPIOD_OUT_HIGH);

	ret = of_property_count_u32_elems(np, "led-sources");
	if (ret < 0) {
		pdata->enable = SKY81452_EN >> CTZ(SKY81452_EN);
	} else {
		num_entry = ret;
		if (num_entry > 6)
			num_entry = 6;

		ret = of_property_read_u32_array(np, "led-sources", sources,
					num_entry);
		if (ret < 0) {
			dev_err(dev, "led-sources node is invalid.\n");
			return ERR_PTR(-EINVAL);
		}

		pdata->enable = 0;
		while (--num_entry)
			pdata->enable |= (1 << sources[num_entry]);
	}

	ret = of_property_read_u32(np,
			"skyworks,short-detection-threshold-volt",
			&pdata->short_detection_threshold);
	if (ret < 0)
		pdata->short_detection_threshold = 7;

	ret = of_property_read_u32(np, "skyworks,current-limit-mA",
			&pdata->boost_current_limit);
	if (ret < 0)
		pdata->boost_current_limit = 2750;

	return pdata;
}
#else
static struct sky81452_bl_platform_data *sky81452_bl_parse_dt(
							struct device *dev)
{
	return ERR_PTR(-EINVAL);
}
#endif

static int sky81452_bl_init_device(struct regmap *regmap,
		struct sky81452_bl_platform_data *pdata)
{
	unsigned int value;

	value = pdata->ignore_pwm ? SKY81452_IGPW : 0;
	value |= pdata->dpwm_mode ? SKY81452_PWMMD : 0;
	value |= pdata->phase_shift ? 0 : SKY81452_PHASE;

	if (pdata->boost_current_limit == 2300)
		value |= SKY81452_ILIM;
	else if (pdata->boost_current_limit != 2750)
		return -EINVAL;

	if (pdata->short_detection_threshold < 4 ||
				pdata->short_detection_threshold > 7)
		return -EINVAL;
	value |= (7 - pdata->short_detection_threshold) << CTZ(SKY81452_VSHRT);

	return regmap_write(regmap, SKY81452_REG2, value);
}

static int sky81452_bl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap = dev_get_drvdata(dev->parent);
	struct sky81452_bl_platform_data *pdata;
	struct backlight_device *bd;
	struct backlight_properties props;
	const char *name;
	int ret;

	pdata = sky81452_bl_parse_dt(dev);
	if (IS_ERR(pdata))
		return PTR_ERR(pdata);

	ret = sky81452_bl_init_device(regmap, pdata);
	if (ret < 0) {
		dev_err(dev, "failed to initialize. err=%d\n", ret);
		return ret;
	}

	memset(&props, 0, sizeof(props));
	props.max_brightness = SKY81452_MAX_BRIGHTNESS;
	name = pdata->name ? pdata->name : SKY81452_DEFAULT_NAME;
	bd = devm_backlight_device_register(dev, name, dev, regmap,
						&sky81452_bl_ops, &props);
	if (IS_ERR(bd)) {
		dev_err(dev, "failed to register. err=%ld\n", PTR_ERR(bd));
		return PTR_ERR(bd);
	}

	platform_set_drvdata(pdev, bd);

	ret = sysfs_create_group(&bd->dev.kobj, &sky81452_bl_attr_group);
	if (ret < 0) {
		dev_err(dev, "failed to create attribute. err=%d\n", ret);
		return ret;
	}

	return ret;
}

static void sky81452_bl_remove(struct platform_device *pdev)
{
	const struct sky81452_bl_platform_data *pdata =
						dev_get_platdata(&pdev->dev);
	struct backlight_device *bd = platform_get_drvdata(pdev);

	sysfs_remove_group(&bd->dev.kobj, &sky81452_bl_attr_group);

	bd->props.power = BACKLIGHT_POWER_ON;
	bd->props.brightness = 0;
	backlight_update_status(bd);

	if (pdata->gpiod_enable)
		gpiod_set_value_cansleep(pdata->gpiod_enable, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id sky81452_bl_of_match[] = {
	{ .compatible = "skyworks,sky81452-backlight", },
	{ }
};
MODULE_DEVICE_TABLE(of, sky81452_bl_of_match);
#endif

static struct platform_driver sky81452_bl_driver = {
	.driver = {
		.name = "sky81452-backlight",
		.of_match_table = of_match_ptr(sky81452_bl_of_match),
	},
	.probe = sky81452_bl_probe,
	.remove = sky81452_bl_remove,
};

module_platform_driver(sky81452_bl_driver);

MODULE_DESCRIPTION("Skyworks SKY81452 backlight driver");
MODULE_AUTHOR("Gyungoh Yoo <jack.yoo@skyworksinc.com>");
MODULE_LICENSE("GPL v2");
