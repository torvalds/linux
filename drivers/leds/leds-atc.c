// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2020, The Linux Foundation. All rights reserved.

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/of_address.h>
#include <linux/of_device.h>

#define ATC_LED_CFG_SHIFT	1
#define ATC_LED_CFG_MASK	0x06
#define ATC_LED_ON		0x03
#define ATC_LED_OFF		0x00

struct atc_led_data {
	struct led_classdev cdev;
	struct device *dev;
	struct regmap *regmap;
	u32 addr;
};

static int atc_led_reg_masked_write(struct atc_led_data *led, u16 addr,
		u8 mask, u8 val)
{
	u8 reg;
	int error;

	error = regmap_raw_read(led->regmap, addr, &reg, 1);
	if (error)
		return error;
	reg &= ~mask;
	reg |= val;

	error = regmap_raw_write(led->regmap, addr, &reg, 1);

	return error;
}

static void atc_led_set(struct led_classdev *cdev, enum led_brightness value)
{
	struct platform_device *pdev = to_platform_device(cdev->dev->parent);
	struct atc_led_data *led = dev_get_drvdata(&pdev->dev);
	u8 brightness;
	int error;

	if (value > ATC_LED_ON)
		value = ATC_LED_ON;

	brightness = value << ATC_LED_CFG_SHIFT;
	error = atc_led_reg_masked_write(led, led->addr, ATC_LED_CFG_MASK, brightness);
	if (error)
		dev_err(led->dev, "Failed to set brightnes, addr = %d\n", led->addr);
}

static enum led_brightness atc_led_get(struct led_classdev *cdev)
{
	return cdev->brightness;
}

static int atc_led_probe(struct platform_device *pdev)
{
	struct atc_led_data *led;
	struct regmap *regmap;
	const __be32 *prop_addr;
	u8 reg;
	int error;

	regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!regmap) {
		dev_err(&pdev->dev, "Unable to get regmap\n");
		return -EINVAL;
	}

	led = devm_kzalloc(&pdev->dev, sizeof(*led), GFP_KERNEL);
	if (!led)
		return -ENOMEM;

	led->regmap = regmap;
	led->dev = &pdev->dev;

	prop_addr = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!prop_addr) {
		dev_err(&pdev->dev, "invalid IO resource\n");
		return -EINVAL;
	}
	led->addr = be32_to_cpu(*prop_addr);

	error = of_property_read_string(pdev->dev.of_node, "label", &led->cdev.name);
	if (error)
		led->cdev.name = devm_kasprintf(led->dev, GFP_KERNEL, "%pATC",
				led->dev->of_node);

	error = regmap_raw_read(led->regmap, led->addr, &reg, 1);

	led->cdev.brightness_set = atc_led_set;
	led->cdev.brightness_get = atc_led_get;
	led->cdev.brightness = (reg & ATC_LED_CFG_MASK) >> ATC_LED_CFG_SHIFT;
	led->cdev.max_brightness = ATC_LED_ON;

	error = led_classdev_register(led->dev, &led->cdev);
	if (error) {
		dev_err(led->dev, "unable to register ATC led, error = %d\n",
				error);
		return error;
	}

	platform_set_drvdata(pdev, led);
	return 0;
}

static void atc_led_remove(struct platform_device *pdev)
{
	struct atc_led_data *led = dev_get_drvdata(&pdev->dev);
	if (led != NULL)
		led_classdev_unregister(&led->cdev);
}

static struct of_device_id atc_led_match_table[] = {
	{ .compatible = "qcom,leds-atc", },
	{ },
};

static struct platform_driver atc_leds_driver = {
	.probe = atc_led_probe,
	.remove = atc_led_remove,
	.driver = {
		.name = "qcom,leds-atc",
		.of_match_table = atc_led_match_table,
	},
};

module_platform_driver(atc_leds_driver);

MODULE_DESCRIPTION("Qualcomm ATC-LED driver");
MODULE_LICENSE("GPL v2");
