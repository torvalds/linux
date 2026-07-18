// SPDX-License-Identifier: GPL-2.0
/*
 * Flash and Torch LED Driver for Samsung S2M series PMICs.
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd
 * Copyright (c) 2026 Kaustabh Chakraborty <kauschluss@disroot.org>
 */

#include <linux/container_of.h>
#include <linux/led-class-flash.h>
#include <linux/mfd/samsung/core.h>
#include <linux/mfd/samsung/s2mu005.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <media/v4l2-flash-led-class.h>

#define MAX_CHANNELS	2

struct s2m_led {
	struct regmap *regmap;
	struct led_classdev_flash fled;
	struct v4l2_flash *v4l2_flash;
	/*
	 * The mutex object prevents the concurrent access of flash control
	 * registers by the LED and V4L2 subsystems.
	 */
	struct mutex lock;
	unsigned int reg_enable;
	u8 channel;
	u8 flash_brightness;
	u8 flash_timeout;
};

static struct s2m_led *to_s2m_led(struct led_classdev_flash *fled)
{
	return container_of(fled, struct s2m_led, fled);
}

static struct led_classdev_flash *to_s2m_fled(struct led_classdev *cdev)
{
	return container_of(cdev, struct led_classdev_flash, led_cdev);
}

static int s2m_fled_flash_brightness_set(struct led_classdev_flash *fled, u32 brightness)
{
	struct s2m_led *led = to_s2m_led(fled);
	struct led_flash_setting *setting = &fled->brightness;

	mutex_lock(&led->lock);
	led->flash_brightness = (brightness - setting->min) / setting->step;
	mutex_unlock(&led->lock);

	return 0;
}

static int s2m_fled_flash_timeout_set(struct led_classdev_flash *fled, u32 timeout)
{
	struct s2m_led *led = to_s2m_led(fled);
	struct led_flash_setting *setting = &fled->timeout;

	mutex_lock(&led->lock);
	led->flash_timeout = (timeout - setting->min) / setting->step;
	mutex_unlock(&led->lock);

	return 0;
}

#if IS_ENABLED(CONFIG_V4L2_FLASH_LED_CLASS)
static int s2m_fled_flash_external_strobe_set(struct v4l2_flash *v4l2_flash, bool enable)
{
	struct s2m_led *led = to_s2m_led(v4l2_flash->fled_cdev);

	return led->fled.ops->strobe_set(&led->fled, enable);
}

static const struct v4l2_flash_ops s2m_fled_v4l2_flash_ops = {
	.external_strobe_set = s2m_fled_flash_external_strobe_set,
};
#else
static const struct v4l2_flash_ops s2m_fled_v4l2_flash_ops;
#endif

static void s2m_fled_v4l2_flash_release(void *v4l2_flash)
{
	v4l2_flash_release(v4l2_flash);
}

static int s2mu005_fled_torch_brightness_set(struct led_classdev *cdev, enum led_brightness value)
{
	struct s2m_led *led = to_s2m_led(to_s2m_fled(cdev));
	int ret;

	mutex_lock(&led->lock);

	if (!value) {
		ret = regmap_clear_bits(led->regmap, led->reg_enable,
					S2MU005_FLED_TORCH_EN(led->channel));
		if (ret)
			dev_err(cdev->dev, "failed to disable torch LED\n");
		goto unlock;
	}

	ret = regmap_update_bits(led->regmap, S2MU005_REG_FLED_CH_CTRL1(led->channel),
				 S2MU005_FLED_TORCH_IOUT,
				 FIELD_PREP(S2MU005_FLED_TORCH_IOUT, value - 1));
	if (ret) {
		dev_err(cdev->dev, "failed to set torch current\n");
		goto unlock;
	}

	ret = regmap_set_bits(led->regmap, led->reg_enable, S2MU005_FLED_TORCH_EN(led->channel));
	if (ret) {
		dev_err(cdev->dev, "failed to enable torch LED\n");
		goto unlock;
	}

unlock:
	mutex_unlock(&led->lock);

	return ret;
}

static int s2mu005_fled_flash_strobe_set(struct led_classdev_flash *fled, bool state)
{
	struct s2m_led *led = to_s2m_led(fled);
	int ret;

	mutex_lock(&led->lock);

	ret = regmap_clear_bits(led->regmap, led->reg_enable, S2MU005_FLED_FLASH_EN(led->channel));
	if (ret) {
		dev_err(fled->led_cdev.dev, "failed to disable flash LED\n");
		goto unlock;
	}

	if (!state)
		goto unlock;

	ret = regmap_update_bits(led->regmap, S2MU005_REG_FLED_CH_CTRL0(led->channel),
				 S2MU005_FLED_FLASH_IOUT,
				 FIELD_PREP(S2MU005_FLED_FLASH_IOUT, led->flash_brightness));
	if (ret) {
		dev_err(fled->led_cdev.dev, "failed to set flash brightness\n");
		goto unlock;
	}

	ret = regmap_update_bits(led->regmap, S2MU005_REG_FLED_CH_CTRL3(led->channel),
				 S2MU005_FLED_FLASH_TIMEOUT,
				 FIELD_PREP(S2MU005_FLED_FLASH_TIMEOUT, led->flash_timeout));
	if (ret) {
		dev_err(fled->led_cdev.dev, "failed to set flash timeout\n");
		goto unlock;
	}

	ret = regmap_set_bits(led->regmap, led->reg_enable, S2MU005_FLED_FLASH_EN(led->channel));
	if (ret) {
		dev_err(fled->led_cdev.dev, "failed to enable flash LED\n");
		goto unlock;
	}

unlock:
	mutex_unlock(&led->lock);

	return ret;
}

static int s2mu005_fled_flash_strobe_get(struct led_classdev_flash *fled, bool *state)
{
	struct s2m_led *led = to_s2m_led(fled);
	u32 val;
	int ret;

	mutex_lock(&led->lock);

	ret = regmap_read(led->regmap, S2MU005_REG_FLED_STATUS, &val);
	if (ret) {
		dev_err(fled->led_cdev.dev, "failed to fetch LED status\n");
		goto unlock;
	}

	*state = !!(val & S2MU005_FLED_FLASH_STATUS(led->channel));

unlock:
	mutex_unlock(&led->lock);

	return ret;
}

static const struct led_flash_ops s2mu005_fled_flash_ops = {
	.flash_brightness_set = s2m_fled_flash_brightness_set,
	.timeout_set = s2m_fled_flash_timeout_set,
	.strobe_set = s2mu005_fled_flash_strobe_set,
	.strobe_get = s2mu005_fled_flash_strobe_get,
};

static int s2mu005_fled_init(struct s2m_led *led, struct device *dev, struct regmap *regmap,
			     unsigned int nr_channels)
{
	unsigned int val;
	int ret;

	ret = regmap_read(regmap, S2MU005_REG_ID, &val);
	if (ret)
		return dev_err_probe(dev, ret, "failed to read revision\n");

	for (int i = 0; i < nr_channels; i++) {
		/*
		 * Read the revision register. Revision EVT0 has the register
		 * at CTRL4, while EVT1 and higher have it at CTRL6.
		 */
		if (FIELD_GET(S2MU005_ID_MASK, val) == 0)
			led[i].reg_enable = S2MU005_REG_FLED_CTRL4;
		else
			led[i].reg_enable = S2MU005_REG_FLED_CTRL6;
	}

	/* Enable the LED channels. */
	ret = regmap_set_bits(regmap, S2MU005_REG_FLED_CTRL1, S2MU005_FLED_CH_EN);
	if (ret)
		return dev_err_probe(dev, ret, "failed to enable LED channels\n");

	return 0;
}

static int s2mu005_fled_init_channel(struct s2m_led *led, struct device *dev,
				     struct fwnode_handle *fwnp)
{
	struct led_classdev *cdev = &led->fled.led_cdev;
	struct led_init_data init_data = {};
	struct v4l2_flash_config v4l2_cfg = {};
	int ret;

	cdev->max_brightness = 16;
	cdev->brightness_set_blocking = s2mu005_fled_torch_brightness_set;
	cdev->flags |= LED_DEV_CAP_FLASH;

	led->fled.timeout.min = 62000;
	led->fled.timeout.step = 62000;
	led->fled.timeout.max = 992000;
	led->fled.timeout.val = 992000;

	led->fled.brightness.min = 25000;
	led->fled.brightness.step = 25000;
	led->fled.brightness.max = 375000; /* 400000 causes flickering */
	led->fled.brightness.val = 375000;

	s2m_fled_flash_timeout_set(&led->fled, led->fled.timeout.val);
	s2m_fled_flash_brightness_set(&led->fled, led->fled.brightness.val);

	led->fled.ops = &s2mu005_fled_flash_ops;

	init_data.fwnode = fwnp;
	ret = devm_led_classdev_flash_register_ext(dev, &led->fled, &init_data);
	if (ret)
		return dev_err_probe(dev, ret, "failed to create LED flash device\n");

	v4l2_cfg.intensity.min = led->fled.brightness.min;
	v4l2_cfg.intensity.step = led->fled.brightness.step;
	v4l2_cfg.intensity.max = led->fled.brightness.max;
	v4l2_cfg.intensity.val = led->fled.brightness.val;

	v4l2_cfg.has_external_strobe = true;

	led->v4l2_flash = v4l2_flash_init(dev, fwnp, &led->fled, &s2m_fled_v4l2_flash_ops,
					  &v4l2_cfg);
	if (IS_ERR(led->v4l2_flash))
		return dev_err_probe(dev, PTR_ERR(led->v4l2_flash),
				     "failed to create V4L2 flash device\n");

	ret = devm_add_action_or_reset(dev, s2m_fled_v4l2_flash_release, led->v4l2_flash);
	if (ret)
		return dev_err_probe(dev, ret, "failed to add cleanup action\n");

	return 0;
}

static int s2m_fled_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sec_pmic_dev *ddata = dev_get_drvdata(dev->parent);
	struct s2m_led *led;
	int ret;

	led = devm_kzalloc(dev, sizeof(*led) * MAX_CHANNELS, GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	/* Initialize LED controller with variant-specific implementation. */
	ret = s2mu005_fled_init(led, dev, ddata->regmap_pmic, MAX_CHANNELS);
	if (ret)
		return ret;

	device_for_each_child_node_scoped(dev, child) {
		u32 reg;

		if (fwnode_property_read_u32(child, "reg", &reg))
			continue;

		if (reg >= MAX_CHANNELS) {
			dev_warn(dev, "channel %d is non-existent\n", reg);
			continue;
		}

		if (led[reg].regmap) {
			dev_warn(dev, "duplicate node for channel %d\n", reg);
			continue;
		}

		led[reg].regmap = ddata->regmap_pmic;
		led[reg].channel = (u8)reg;

		ret = devm_mutex_init(dev, &led[reg].lock);
		if (ret)
			return dev_err_probe(dev, ret, "failed to create mutex\n");

		/* Initialize LED channel with variant-specific implementation. */
		ret = s2mu005_fled_init_channel(led + reg, dev, child);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct platform_device_id s2m_fled_id_table[] = {
	{ "s2mu005-flash", S2MU005 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(platform, s2m_fled_id_table);

static const struct of_device_id s2m_fled_of_match_table[] = {
	{ .compatible = "samsung,s2mu005-flash", .data = (void *)S2MU005 },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, s2m_fled_of_match_table);

static struct platform_driver s2m_fled_driver = {
	.driver = {
		.name = "s2m-flash",
	},
	.probe = s2m_fled_probe,
	.id_table = s2m_fled_id_table,
};
module_platform_driver(s2m_fled_driver);

MODULE_DESCRIPTION("Flash/Torch LED Driver for Samsung S2M Series PMICs");
MODULE_AUTHOR("Kaustabh Chakraborty <kauschluss@disroot.org>");
MODULE_LICENSE("GPL");
