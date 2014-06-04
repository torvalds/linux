/*
 * lm3533-bl.c -- LM3533 Backlight driver
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/slab.h>

#include <linux/mfd/lm3533.h>


#define LM3533_HVCTRLBANK_COUNT		2
#define LM3533_BL_MAX_BRIGHTNESS	255

#define LM3533_REG_CTRLBANK_AB_BCONF	0x1a


struct lm3533_bl {
	struct lm3533 *lm3533;
	struct lm3533_ctrlbank cb;
	struct backlight_device *bd;
	int id;
};


static inline int lm3533_bl_get_ctrlbank_id(struct lm3533_bl *bl)
{
	return bl->id;
}

static int lm3533_bl_update_status(struct backlight_device *bd)
{
	struct lm3533_bl *bl = bl_get_data(bd);
	int brightness = bd->props.brightness;

	if (bd->props.power != FB_BLANK_UNBLANK)
		brightness = 0;
	if (bd->props.fb_blank != FB_BLANK_UNBLANK)
		brightness = 0;

	return lm3533_ctrlbank_set_brightness(&bl->cb, (u8)brightness);
}

static int lm3533_bl_get_brightness(struct backlight_device *bd)
{
	struct lm3533_bl *bl = bl_get_data(bd);
	u8 val;
	int ret;

	ret = lm3533_ctrlbank_get_brightness(&bl->cb, &val);
	if (ret)
		return ret;

	return val;
}

static const struct backlight_ops lm3533_bl_ops = {
	.get_brightness	= lm3533_bl_get_brightness,
	.update_status	= lm3533_bl_update_status,
};

static ssize_t show_id(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", bl->id);
}

static ssize_t show_als_channel(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	unsigned channel = lm3533_bl_get_ctrlbank_id(bl);

	return scnprintf(buf, PAGE_SIZE, "%u\n", channel);
}

static ssize_t show_als_en(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	int ctrlbank = lm3533_bl_get_ctrlbank_id(bl);
	u8 val;
	u8 mask;
	bool enable;
	int ret;

	ret = lm3533_read(bl->lm3533, LM3533_REG_CTRLBANK_AB_BCONF, &val);
	if (ret)
		return ret;

	mask = 1 << (2 * ctrlbank);
	enable = val & mask;

	return scnprintf(buf, PAGE_SIZE, "%d\n", enable);
}

static ssize_t store_als_en(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	int ctrlbank = lm3533_bl_get_ctrlbank_id(bl);
	int enable;
	u8 val;
	u8 mask;
	int ret;

	if (kstrtoint(buf, 0, &enable))
		return -EINVAL;

	mask = 1 << (2 * ctrlbank);

	if (enable)
		val = mask;
	else
		val = 0;

	ret = lm3533_update(bl->lm3533, LM3533_REG_CTRLBANK_AB_BCONF, val,
									mask);
	if (ret)
		return ret;

	return len;
}

static ssize_t show_linear(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	u8 val;
	u8 mask;
	int linear;
	int ret;

	ret = lm3533_read(bl->lm3533, LM3533_REG_CTRLBANK_AB_BCONF, &val);
	if (ret)
		return ret;

	mask = 1 << (2 * lm3533_bl_get_ctrlbank_id(bl) + 1);

	if (val & mask)
		linear = 1;
	else
		linear = 0;

	return scnprintf(buf, PAGE_SIZE, "%x\n", linear);
}

static ssize_t store_linear(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	unsigned long linear;
	u8 mask;
	u8 val;
	int ret;

	if (kstrtoul(buf, 0, &linear))
		return -EINVAL;

	mask = 1 << (2 * lm3533_bl_get_ctrlbank_id(bl) + 1);

	if (linear)
		val = mask;
	else
		val = 0;

	ret = lm3533_update(bl->lm3533, LM3533_REG_CTRLBANK_AB_BCONF, val,
									mask);
	if (ret)
		return ret;

	return len;
}

static ssize_t show_pwm(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	u8 val;
	int ret;

	ret = lm3533_ctrlbank_get_pwm(&bl->cb, &val);
	if (ret)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t store_pwm(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t len)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	u8 val;
	int ret;

	if (kstrtou8(buf, 0, &val))
		return -EINVAL;

	ret = lm3533_ctrlbank_set_pwm(&bl->cb, val);
	if (ret)
		return ret;

	return len;
}

static LM3533_ATTR_RO(als_channel);
static LM3533_ATTR_RW(als_en);
static LM3533_ATTR_RO(id);
static LM3533_ATTR_RW(linear);
static LM3533_ATTR_RW(pwm);

static struct attribute *lm3533_bl_attributes[] = {
	&dev_attr_als_channel.attr,
	&dev_attr_als_en.attr,
	&dev_attr_id.attr,
	&dev_attr_linear.attr,
	&dev_attr_pwm.attr,
	NULL,
};

static umode_t lm3533_bl_attr_is_visible(struct kobject *kobj,
					     struct attribute *attr, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_als_channel.attr ||
					attr == &dev_attr_als_en.attr) {
		if (!bl->lm3533->have_als)
			mode = 0;
	}

	return mode;
};

static struct attribute_group lm3533_bl_attribute_group = {
	.is_visible	= lm3533_bl_attr_is_visible,
	.attrs		= lm3533_bl_attributes
};

static int lm3533_bl_setup(struct lm3533_bl *bl,
					struct lm3533_bl_platform_data *pdata)
{
	int ret;

	ret = lm3533_ctrlbank_set_max_current(&bl->cb, pdata->max_current);
	if (ret)
		return ret;

	return lm3533_ctrlbank_set_pwm(&bl->cb, pdata->pwm);
}

static int lm3533_bl_probe(struct platform_device *pdev)
{
	struct lm3533 *lm3533;
	struct lm3533_bl_platform_data *pdata;
	struct lm3533_bl *bl;
	struct backlight_device *bd;
	struct backlight_properties props;
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	lm3533 = dev_get_drvdata(pdev->dev.parent);
	if (!lm3533)
		return -EINVAL;

	pdata = dev_get_platdata(&pdev->dev);
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}

	if (pdev->id < 0 || pdev->id >= LM3533_HVCTRLBANK_COUNT) {
		dev_err(&pdev->dev, "illegal backlight id %d\n", pdev->id);
		return -EINVAL;
	}

	bl = devm_kzalloc(&pdev->dev, sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	bl->lm3533 = lm3533;
	bl->id = pdev->id;

	bl->cb.lm3533 = lm3533;
	bl->cb.id = lm3533_bl_get_ctrlbank_id(bl);
	bl->cb.dev = NULL;			/* until registered */

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = LM3533_BL_MAX_BRIGHTNESS;
	props.brightness = pdata->default_brightness;
	bd = devm_backlight_device_register(&pdev->dev, pdata->name,
					pdev->dev.parent, bl, &lm3533_bl_ops,
					&props);
	if (IS_ERR(bd)) {
		dev_err(&pdev->dev, "failed to register backlight device\n");
		return PTR_ERR(bd);
	}

	bl->bd = bd;
	bl->cb.dev = &bl->bd->dev;

	platform_set_drvdata(pdev, bl);

	ret = sysfs_create_group(&bd->dev.kobj, &lm3533_bl_attribute_group);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to create sysfs attributes\n");
		return ret;
	}

	backlight_update_status(bd);

	ret = lm3533_bl_setup(bl, pdata);
	if (ret)
		goto err_sysfs_remove;

	ret = lm3533_ctrlbank_enable(&bl->cb);
	if (ret)
		goto err_sysfs_remove;

	return 0;

err_sysfs_remove:
	sysfs_remove_group(&bd->dev.kobj, &lm3533_bl_attribute_group);

	return ret;
}

static int lm3533_bl_remove(struct platform_device *pdev)
{
	struct lm3533_bl *bl = platform_get_drvdata(pdev);
	struct backlight_device *bd = bl->bd;

	dev_dbg(&bd->dev, "%s\n", __func__);

	bd->props.power = FB_BLANK_POWERDOWN;
	bd->props.brightness = 0;

	lm3533_ctrlbank_disable(&bl->cb);
	sysfs_remove_group(&bd->dev.kobj, &lm3533_bl_attribute_group);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int lm3533_bl_suspend(struct device *dev)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	return lm3533_ctrlbank_disable(&bl->cb);
}

static int lm3533_bl_resume(struct device *dev)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);

	dev_dbg(dev, "%s\n", __func__);

	return lm3533_ctrlbank_enable(&bl->cb);
}
#endif

static SIMPLE_DEV_PM_OPS(lm3533_bl_pm_ops, lm3533_bl_suspend, lm3533_bl_resume);

static void lm3533_bl_shutdown(struct platform_device *pdev)
{
	struct lm3533_bl *bl = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "%s\n", __func__);

	lm3533_ctrlbank_disable(&bl->cb);
}

static struct platform_driver lm3533_bl_driver = {
	.driver = {
		.name	= "lm3533-backlight",
		.owner	= THIS_MODULE,
		.pm	= &lm3533_bl_pm_ops,
	},
	.probe		= lm3533_bl_probe,
	.remove		= lm3533_bl_remove,
	.shutdown	= lm3533_bl_shutdown,
};
module_platform_driver(lm3533_bl_driver);

MODULE_AUTHOR("Johan Hovold <jhovold@gmail.com>");
MODULE_DESCRIPTION("LM3533 Backlight driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lm3533-backlight");
