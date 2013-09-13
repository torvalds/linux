/*
 * TI LP8788 MFD - backlight driver
 *
 * Copyright 2012 Texas Instruments
 *
 * Author: Milo(Woogyom) Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/mfd/lp8788.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
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

struct lp8788_bl_config {
	enum lp8788_bl_ctrl_mode bl_mode;
	enum lp8788_bl_dim_mode dim_mode;
	enum lp8788_bl_full_scale_current full_scale;
	enum lp8788_bl_ramp_step rise_time;
	enum lp8788_bl_ramp_step fall_time;
	enum pwm_polarity pwm_pol;
};

struct lp8788_bl {
	struct lp8788 *lp;
	struct backlight_device *bl_dev;
	struct lp8788_backlight_platform_data *pdata;
	enum lp8788_bl_ctrl_mode mode;
	struct pwm_device *pwm;
};

struct lp8788_bl_config default_bl_config = {
	.bl_mode    = LP8788_BL_REGISTER_ONLY,
	.dim_mode   = LP8788_DIM_EXPONENTIAL,
	.full_scale = LP8788_FULLSCALE_1900uA,
	.rise_time  = LP8788_RAMP_8192us,
	.fall_time  = LP8788_RAMP_8192us,
	.pwm_pol    = PWM_POLARITY_NORMAL,
};

static inline bool is_brightness_ctrl_by_pwm(enum lp8788_bl_ctrl_mode mode)
{
	return (mode == LP8788_BL_COMB_PWM_BASED);
}

static inline bool is_brightness_ctrl_by_register(enum lp8788_bl_ctrl_mode mode)
{
	return (mode == LP8788_BL_REGISTER_ONLY ||
		mode == LP8788_BL_COMB_REGISTER_BASED);
}

static int lp8788_backlight_configure(struct lp8788_bl *bl)
{
	struct lp8788_backlight_platform_data *pdata = bl->pdata;
	struct lp8788_bl_config *cfg = &default_bl_config;
	int ret;
	u8 val;

	/*
	 * Update chip configuration if platform data exists,
	 * otherwise use the default settings.
	 */
	if (pdata) {
		cfg->bl_mode    = pdata->bl_mode;
		cfg->dim_mode   = pdata->dim_mode;
		cfg->full_scale = pdata->full_scale;
		cfg->rise_time  = pdata->rise_time;
		cfg->fall_time  = pdata->fall_time;
		cfg->pwm_pol    = pdata->pwm_pol;
	}

	/* Brightness ramp up/down */
	val = (cfg->rise_time << LP8788_BL_RAMP_RISE_SHIFT) | cfg->fall_time;
	ret = lp8788_write_byte(bl->lp, LP8788_BL_RAMP, val);
	if (ret)
		return ret;

	/* Fullscale current setting */
	val = (cfg->full_scale << LP8788_BL_FULLSCALE_SHIFT) |
		(cfg->dim_mode << LP8788_BL_DIM_MODE_SHIFT);

	/* Brightness control mode */
	switch (cfg->bl_mode) {
	case LP8788_BL_REGISTER_ONLY:
		val |= LP8788_BL_EN;
		break;
	case LP8788_BL_COMB_PWM_BASED:
	case LP8788_BL_COMB_REGISTER_BASED:
		val |= LP8788_BL_EN | LP8788_BL_PWM_INPUT_EN |
			(cfg->pwm_pol << LP8788_BL_PWM_POLARITY_SHIFT);
		break;
	default:
		dev_err(bl->lp->dev, "invalid mode: %d\n", cfg->bl_mode);
		return -EINVAL;
	}

	bl->mode = cfg->bl_mode;

	return lp8788_write_byte(bl->lp, LP8788_BL_CONFIG, val);
}

static void lp8788_pwm_ctrl(struct lp8788_bl *bl, int br, int max_br)
{
	unsigned int period;
	unsigned int duty;
	struct device *dev;
	struct pwm_device *pwm;

	if (!bl->pdata)
		return;

	period = bl->pdata->period_ns;
	duty = br * period / max_br;
	dev = bl->lp->dev;

	/* request PWM device with the consumer name */
	if (!bl->pwm) {
		pwm = devm_pwm_get(dev, LP8788_DEV_BACKLIGHT);
		if (IS_ERR(pwm)) {
			dev_err(dev, "can not get PWM device\n");
			return;
		}

		bl->pwm = pwm;
	}

	pwm_config(bl->pwm, duty, period);
	if (duty)
		pwm_enable(bl->pwm);
	else
		pwm_disable(bl->pwm);
}

static int lp8788_bl_update_status(struct backlight_device *bl_dev)
{
	struct lp8788_bl *bl = bl_get_data(bl_dev);
	enum lp8788_bl_ctrl_mode mode = bl->mode;

	if (bl_dev->props.state & BL_CORE_SUSPENDED)
		bl_dev->props.brightness = 0;

	if (is_brightness_ctrl_by_pwm(mode)) {
		int brt = bl_dev->props.brightness;
		int max = bl_dev->props.max_brightness;

		lp8788_pwm_ctrl(bl, brt, max);
	} else if (is_brightness_ctrl_by_register(mode)) {
		u8 brt = bl_dev->props.brightness;

		lp8788_write_byte(bl->lp, LP8788_BL_BRIGHTNESS, brt);
	}

	return 0;
}

static int lp8788_bl_get_brightness(struct backlight_device *bl_dev)
{
	return bl_dev->props.brightness;
}

static const struct backlight_ops lp8788_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = lp8788_bl_update_status,
	.get_brightness = lp8788_bl_get_brightness,
};

static int lp8788_backlight_register(struct lp8788_bl *bl)
{
	struct backlight_device *bl_dev;
	struct backlight_properties props;
	struct lp8788_backlight_platform_data *pdata = bl->pdata;
	int init_brt;
	char *name;

	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = MAX_BRIGHTNESS;

	/* Initial brightness */
	if (pdata)
		init_brt = min_t(int, pdata->initial_brightness,
				props.max_brightness);
	else
		init_brt = 0;

	props.brightness = init_brt;

	/* Backlight device name */
	if (!pdata || !pdata->name)
		name = DEFAULT_BL_NAME;
	else
		name = pdata->name;

	bl_dev = backlight_device_register(name, bl->lp->dev, bl,
				       &lp8788_bl_ops, &props);
	if (IS_ERR(bl_dev))
		return PTR_ERR(bl_dev);

	bl->bl_dev = bl_dev;

	return 0;
}

static void lp8788_backlight_unregister(struct lp8788_bl *bl)
{
	struct backlight_device *bl_dev = bl->bl_dev;

	if (bl_dev)
		backlight_device_unregister(bl_dev);
}

static ssize_t lp8788_get_bl_ctl_mode(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct lp8788_bl *bl = dev_get_drvdata(dev);
	enum lp8788_bl_ctrl_mode mode = bl->mode;
	char *strmode;

	if (is_brightness_ctrl_by_pwm(mode))
		strmode = "PWM based";
	else if (is_brightness_ctrl_by_register(mode))
		strmode = "Register based";
	else
		strmode = "Invalid mode";

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
	if (lp->pdata)
		bl->pdata = lp->pdata->bl_pdata;

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

static int lp8788_backlight_remove(struct platform_device *pdev)
{
	struct lp8788_bl *bl = platform_get_drvdata(pdev);
	struct backlight_device *bl_dev = bl->bl_dev;

	bl_dev->props.brightness = 0;
	backlight_update_status(bl_dev);
	sysfs_remove_group(&pdev->dev.kobj, &lp8788_attr_group);
	lp8788_backlight_unregister(bl);

	return 0;
}

static struct platform_driver lp8788_bl_driver = {
	.probe = lp8788_backlight_probe,
	.remove = lp8788_backlight_remove,
	.driver = {
		.name = LP8788_DEV_BACKLIGHT,
		.owner = THIS_MODULE,
	},
};
module_platform_driver(lp8788_bl_driver);

MODULE_DESCRIPTION("Texas Instruments LP8788 Backlight Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:lp8788-backlight");
