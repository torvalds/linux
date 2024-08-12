// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LP8788 MFD - backlight driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/mfd/lp8788.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* Register address */
#define LP8788_BL_CONFIG		0x96
#define LP8788_BL_EN			BIT(0)
#define LP8788_BL_PWM_INPUT_EN		BIT(5)
#define LP8788_BL_FULLSCALE_SHIFT	2
#define LP8788_BL_DIM_MODE_SHIFT	1
#define LP8788_BL_PWM_POLARITY_SHIFT	6

#define LP8788_BL_BRIGHTNESS		0x97

#define LP8788_BL_RAMP			0x98
#define LP8788_BL_RAMP_RISE_SHIFT	4

#define MAX_BRIGHTNESS			127
#define DEFAULT_BL_NAME			"lcd-backlight"

struct lp8788_bl {
	struct lp8788 *lp;
	struct backlight_device *bl_dev;
};

static int lp8788_backlight_configure(struct lp8788_bl *bl)
{
	int ret;
	u8 val;

	/* Brightness ramp up/down */
	val = (LP8788_RAMP_8192us << LP8788_BL_RAMP_RISE_SHIFT) | LP8788_RAMP_8192us;
	ret = lp8788_write_byte(bl->lp, LP8788_BL_RAMP, val);
	if (ret)
		return ret;

	/* Fullscale current setting */
	val = (LP8788_FULLSCALE_1900uA << LP8788_BL_FULLSCALE_SHIFT) |
		(LP8788_DIM_EXPONENTIAL << LP8788_BL_DIM_MODE_SHIFT);

	/* Brightness control mode */
	val |= LP8788_BL_EN;

	return lp8788_write_byte(bl->lp, LP8788_BL_CONFIG, val);
}

static int lp8788_bl_update_status(struct backlight_device *bl_dev)
{
	struct lp8788_bl *bl = bl_get_data(bl_dev);

	if (bl_dev->props.state & BL_CORE_SUSPENDED)
		bl_dev->props.brightness = 0;

	lp8788_write_byte(bl->lp, LP8788_BL_BRIGHTNESS, bl_dev->props.brightness);

	return 0;
}

static const struct backlight_ops lp8788_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = lp8788_bl_update_status,
};

static int lp8788_backlight_register(struct lp8788_bl *bl)
{
	struct backlight_device *bl_dev;
	struct backlight_properties props;

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = MAX_BRIGHTNESS;

	/* Initial brightness */
	props.brightness = 0;

	/* Backlight device name */
	bl_dev = backlight_device_register(DEFAULT_BL_NAME, bl->lp->dev, bl,
				       &lp8788_bl_ops, &props);
	if (IS_ERR(bl_dev))
		return PTR_ERR(bl_dev);

	bl->bl_dev = bl_dev;

	return 0;
}

static void lp8788_backlight_unregister(struct lp8788_bl *bl)
{
	struct backlight_device *bl_dev = bl->bl_dev;

	backlight_device_unregister(bl_dev);
}

static ssize_t lp8788_get_bl_ctl_mode(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	const char *strmode = "Register based";

	return scnprintf(buf, PAGE_SIZE, "%s\n", strmode);
}

static DEVICE_ATTR(bl_ctl_mode, S_IRUGO, lp8788_get_bl_ctl_mode, NULL);

static struct attribute *lp8788_attributes[] = {
	&dev_attr_bl_ctl_mode.attr,
	NULL,
};

static const struct attribute_group lp8788_attr_group = {
	.attrs = lp8788_attributes,
};

static int lp8788_backlight_probe(struct platform_device *pdev)
{
	struct lp8788 *lp = dev_get_drvdata(pdev->dev.parent);
	struct lp8788_bl *bl;
	int ret;

	bl = devm_kzalloc(lp->dev, sizeof(struct lp8788_bl), GFP_KERNEL);
	if (!bl)
		return -ENOMEM;

	bl->lp = lp;

	platform_set_drvdata(pdev, bl);

	ret = lp8788_backlight_configure(bl);
	if (ret) {
		dev_err(lp->dev, "backlight config err: %d\n", ret);
		goto err_dev;
	}

	ret = lp8788_backlight_register(bl);
	if (ret) {
		dev_err(lp->dev, "register backlight err: %d\n", ret);
		goto err_dev;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &lp8788_attr_group);
	if (ret) {
		dev_err(lp->dev, "register sysfs err: %d\n", ret);
		goto err_sysfs;
	}

	backlight_update_status(bl->bl_dev);

	return 0;

err_sysfs:
	lp8788_backlight_unregister(bl);
err_dev:
	return ret;
}

static void lp8788_backlight_remove(struct platform_device *pdev)
{
	struct lp8788_bl *bl = platform_get_drvdata(pdev);
	struct backlight_device *bl_dev = bl->bl_dev;

	bl_dev->props.brightness = 0;
	backlight_update_status(bl_dev);
	sysfs_remove_group(&pdev->dev.kobj, &lp8788_attr_group);
	lp8788_backlight_unregister(bl);
}

static struct platform_driver lp8788_bl_driver = {
	.probe = lp8788_backlight_probe,
	.remove_new = lp8788_backlight_remove,
	.driver = {
		.name = LP8788_DEV_BACKLIGHT,
	},
};
module_platform_driver(lp8788_bl_driver);

MODULE_DESCRIPTION("Texas Instruments LP8788 Backlight Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lp8788-backlight");
