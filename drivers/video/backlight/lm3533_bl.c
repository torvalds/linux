// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm3533-bl.c -- LM3533 Backlight driver
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/backlight.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <linux/mfd/lm3533.h>


#define LM3533_HVCTRLBANK_COUNT		2
#define LM3533_BL_MAX_BRIGHTNESS	255

#define LM3533_REG_CTRLBANK_AB_BCONF	0x1a
#define   CTRLBANK_AB_BCONF_ALS(n)	BIT(2 * (n))
#define   CTRLBANK_AB_BCONF_MODE(n)	BIT(2 * (n) + 1)


struct lm3533_bl {
	struct regmap *regmap;
	struct lm3533_ctrlbank cb;
	struct backlight_device *bd;
	int id;

	u32 max_current;
	u32 pwm;

	bool have_als;
	bool linear;
};


static inline int lm3533_bl_get_ctrlbank_id(struct lm3533_bl *bl)
{
	return bl->id;
}

static int lm3533_bl_update_status(struct backlight_device *bd)
{
	struct lm3533_bl *bl = bl_get_data(bd);

	return lm3533_ctrlbank_set_brightness(&bl->cb, backlight_get_brightness(bd));
}

static int lm3533_bl_get_brightness(struct backlight_device *bd)
{
	struct lm3533_bl *bl = bl_get_data(bd);
	u32 val;
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

static ssize_t show_als_en(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	int ctrlbank = lm3533_bl_get_ctrlbank_id(bl);
	int ret;

	ret = regmap_test_bits(bl->regmap, LM3533_REG_CTRLBANK_AB_BCONF,
			       CTRLBANK_AB_BCONF_ALS(ctrlbank));
	if (ret < 0)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}

static ssize_t store_als_en(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	int ctrlbank = lm3533_bl_get_ctrlbank_id(bl);
	int enable;
	int ret;

	if (kstrtoint(buf, 0, &enable))
		return -EINVAL;

	ret = regmap_assign_bits(bl->regmap, LM3533_REG_CTRLBANK_AB_BCONF,
				 CTRLBANK_AB_BCONF_ALS(ctrlbank), enable);
	if (ret)
		return ret;

	return len;
}

static ssize_t show_linear(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	int ctrlbank = lm3533_bl_get_ctrlbank_id(bl);
	int ret;

	ret = regmap_test_bits(bl->regmap, LM3533_REG_CTRLBANK_AB_BCONF,
			       CTRLBANK_AB_BCONF_MODE(ctrlbank));
	if (ret < 0)
		return ret;

	return scnprintf(buf, PAGE_SIZE, "%x\n", ret);
}

static ssize_t store_linear(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t len)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	int ctrlbank = lm3533_bl_get_ctrlbank_id(bl);
	struct backlight_device *bd = bl->bd;
	unsigned long linear;
	int ret;

	if (kstrtoul(buf, 0, &linear))
		return -EINVAL;

	ret = regmap_assign_bits(bl->regmap, LM3533_REG_CTRLBANK_AB_BCONF,
				 CTRLBANK_AB_BCONF_MODE(ctrlbank), linear);
	if (ret)
		return ret;

	bd->props.scale = linear ? BACKLIGHT_SCALE_LINEAR :
				   BACKLIGHT_SCALE_NON_LINEAR;

	return len;
}

static ssize_t show_pwm(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	u32 val;
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
	u32 val;
	int ret;

	if (kstrtou32(buf, 0, &val))
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
	struct device *dev = kobj_to_dev(kobj);
	struct lm3533_bl *bl = dev_get_drvdata(dev);
	umode_t mode = attr->mode;

	if (attr == &dev_attr_als_channel.attr ||
					attr == &dev_attr_als_en.attr) {
		if (!bl->have_als)
			mode = 0;
	}

	return mode;
};

static struct attribute_group lm3533_bl_attribute_group = {
	.is_visible	= lm3533_bl_attr_is_visible,
	.attrs		= lm3533_bl_attributes
};

static const struct attribute_group *lm3533_bl_attribute_groups[] = {
	&lm3533_bl_attribute_group,
	NULL,
};

static int lm3533_bl_setup(struct lm3533_bl *bl)
{
	int ctrlbank = lm3533_bl_get_ctrlbank_id(bl);
	int ret;

	ret = regmap_assign_bits(bl->regmap, LM3533_REG_CTRLBANK_AB_BCONF,
				 CTRLBANK_AB_BCONF_MODE(ctrlbank), bl->linear);
	if (ret)
		return ret;

	ret = lm3533_ctrlbank_set_max_current(&bl->cb, bl->max_current);
	if (ret)
		return ret;

	return lm3533_ctrlbank_set_pwm(&bl->cb, bl->pwm);
}

static int lm3533_bl_probe(struct platform_device *pdev)
{
	struct lm3533 *lm3533;
	struct lm3533_bl *bl;
	struct backlight_device *bd;
	struct backlight_properties props;
	char *name = NULL;
	u32 default_brightness = LM3533_BL_MAX_BRIGHTNESS;
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	lm3533 = dev_get_drvdata(pdev->dev.parent);
	if (!lm3533)
		return -EINVAL;

	if (pdev->id < 0 || pdev->id >= LM3533_HVCTRLBANK_COUNT) {
		dev_err(&pdev->dev, "illegal backlight id %d\n", pdev->id);
		return -EINVAL;
	}

	bl = devm_kzalloc(&pdev->dev, sizeof(*bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	bl->regmap = lm3533->regmap;
	bl->have_als = lm3533->have_als;
	bl->id = pdev->id;

	bl->cb.regmap = lm3533->regmap;
	bl->cb.id = lm3533_bl_get_ctrlbank_id(bl);
	bl->cb.dev = NULL;			/* until registered */

	name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s-%d",
			      pdev->name, pdev->id);
	if (!name)
		return -ENOMEM;

	device_property_read_u32(&pdev->dev, "default-brightness",
				 &default_brightness);

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = LM3533_BL_MAX_BRIGHTNESS;
	props.brightness = default_brightness;

	bl->linear = device_property_read_bool(&pdev->dev,
					       "ti,linear-mapping-mode");
	props.scale = bl->linear ? BACKLIGHT_SCALE_LINEAR :
				   BACKLIGHT_SCALE_NON_LINEAR;

	bd = devm_backlight_device_register(&pdev->dev, name, &pdev->dev,
					    bl, &lm3533_bl_ops, &props);
	if (IS_ERR(bd)) {
		dev_err(&pdev->dev, "failed to register backlight device\n");
		return PTR_ERR(bd);
	}

	bl->bd = bd;
	bl->cb.dev = &bl->bd->dev;

	platform_set_drvdata(pdev, bl);

	device_property_read_u32(&pdev->dev, "led-max-microamp",
				 &bl->max_current);
	bl->max_current = clamp(bl->max_current, LM3533_MAX_CURRENT_MIN,
				LM3533_MAX_CURRENT_MAX);

	device_property_read_u32(&pdev->dev, "ti,pwm-config-mask", &bl->pwm);

	ret = lm3533_bl_setup(bl);
	if (ret)
		return ret;

	backlight_update_status(bd);

	ret = lm3533_ctrlbank_enable(&bl->cb);
	if (ret)
		return ret;

	return 0;
}

static void lm3533_bl_remove(struct platform_device *pdev)
{
	struct lm3533_bl *bl = platform_get_drvdata(pdev);
	struct backlight_device *bd = bl->bd;

	dev_dbg(&bd->dev, "%s\n", __func__);

	bd->props.power = BACKLIGHT_POWER_OFF;
	bd->props.brightness = 0;

	lm3533_ctrlbank_disable(&bl->cb);
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

static const struct of_device_id lm3533_bl_match_table[] = {
	{ .compatible = "ti,lm3533-backlight" },
	{ }
};
MODULE_DEVICE_TABLE(of, lm3533_bl_match_table);

static struct platform_driver lm3533_bl_driver = {
	.driver = {
		.name	= "lm3533-backlight",
		.pm	= &lm3533_bl_pm_ops,
		.dev_groups = lm3533_bl_attribute_groups,
		.of_match_table = lm3533_bl_match_table,
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
