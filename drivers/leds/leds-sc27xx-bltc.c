// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Spreadtrum Communications Inc.

#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <uapi/linux/uleds.h>

/* PMIC global control register definition */
#define SC27XX_MODULE_EN0	0xc08
#define SC27XX_CLK_EN0		0xc18
#define SC27XX_RGB_CTRL		0xebc

#define SC27XX_BLTC_EN		BIT(9)
#define SC27XX_RTC_EN		BIT(7)
#define SC27XX_RGB_PD		BIT(0)

/* Breathing light controller register definition */
#define SC27XX_LEDS_CTRL	0x00
#define SC27XX_LEDS_PRESCALE	0x04
#define SC27XX_LEDS_DUTY	0x08
#define SC27XX_LEDS_CURVE0	0x0c
#define SC27XX_LEDS_CURVE1	0x10

#define SC27XX_CTRL_SHIFT	4
#define SC27XX_LED_RUN		BIT(0)
#define SC27XX_LED_TYPE		BIT(1)

#define SC27XX_DUTY_SHIFT	8
#define SC27XX_DUTY_MASK	GENMASK(15, 0)
#define SC27XX_MOD_MASK		GENMASK(7, 0)

#define SC27XX_LEDS_OFFSET	0x10
#define SC27XX_LEDS_MAX		3

struct sc27xx_led {
	char name[LED_MAX_NAME_SIZE];
	struct led_classdev ldev;
	struct sc27xx_led_priv *priv;
	u8 line;
	bool active;
};

struct sc27xx_led_priv {
	struct sc27xx_led leds[SC27XX_LEDS_MAX];
	struct regmap *regmap;
	struct mutex lock;
	u32 base;
};

#define to_sc27xx_led(ldev) \
	container_of(ldev, struct sc27xx_led, ldev)

static int sc27xx_led_init(struct regmap *regmap)
{
	int err;

	err = regmap_update_bits(regmap, SC27XX_MODULE_EN0, SC27XX_BLTC_EN,
				 SC27XX_BLTC_EN);
	if (err)
		return err;

	err = regmap_update_bits(regmap, SC27XX_CLK_EN0, SC27XX_RTC_EN,
				 SC27XX_RTC_EN);
	if (err)
		return err;

	return regmap_update_bits(regmap, SC27XX_RGB_CTRL, SC27XX_RGB_PD, 0);
}

static u32 sc27xx_led_get_offset(struct sc27xx_led *leds)
{
	return leds->priv->base + SC27XX_LEDS_OFFSET * leds->line;
}

static int sc27xx_led_enable(struct sc27xx_led *leds, enum led_brightness value)
{
	u32 base = sc27xx_led_get_offset(leds);
	u32 ctrl_base = leds->priv->base + SC27XX_LEDS_CTRL;
	u8 ctrl_shift = SC27XX_CTRL_SHIFT * leds->line;
	struct regmap *regmap = leds->priv->regmap;
	int err;

	err = regmap_update_bits(regmap, base + SC27XX_LEDS_DUTY,
				 SC27XX_DUTY_MASK,
				 (value << SC27XX_DUTY_SHIFT) |
				 SC27XX_MOD_MASK);
	if (err)
		return err;

	return regmap_update_bits(regmap, ctrl_base,
			(SC27XX_LED_RUN | SC27XX_LED_TYPE) << ctrl_shift,
			(SC27XX_LED_RUN | SC27XX_LED_TYPE) << ctrl_shift);
}

static int sc27xx_led_disable(struct sc27xx_led *leds)
{
	struct regmap *regmap = leds->priv->regmap;
	u32 ctrl_base = leds->priv->base + SC27XX_LEDS_CTRL;
	u8 ctrl_shift = SC27XX_CTRL_SHIFT * leds->line;

	return regmap_update_bits(regmap, ctrl_base,
			(SC27XX_LED_RUN | SC27XX_LED_TYPE) << ctrl_shift, 0);
}

static int sc27xx_led_set(struct led_classdev *ldev, enum led_brightness value)
{
	struct sc27xx_led *leds = to_sc27xx_led(ldev);
	int err;

	mutex_lock(&leds->priv->lock);

	if (value == LED_OFF)
		err = sc27xx_led_disable(leds);
	else
		err = sc27xx_led_enable(leds, value);

	mutex_unlock(&leds->priv->lock);

	return err;
}

static int sc27xx_led_register(struct device *dev, struct sc27xx_led_priv *priv)
{
	int i, err;

	err = sc27xx_led_init(priv->regmap);
	if (err)
		return err;

	for (i = 0; i < SC27XX_LEDS_MAX; i++) {
		struct sc27xx_led *led = &priv->leds[i];

		if (!led->active)
			continue;

		led->line = i;
		led->priv = priv;
		led->ldev.name = led->name;
		led->ldev.brightness_set_blocking = sc27xx_led_set;

		err = devm_led_classdev_register(dev, &led->ldev);
		if (err)
			return err;
	}

	return 0;
}

static int sc27xx_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node, *child;
	struct sc27xx_led_priv *priv;
	const char *str;
	u32 base, count, reg;
	int err;

	count = of_get_child_count(np);
	if (!count || count > SC27XX_LEDS_MAX)
		return -EINVAL;

	err = of_property_read_u32(np, "reg", &base);
	if (err) {
		dev_err(dev, "fail to get reg of property\n");
		return err;
	}

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);
	mutex_init(&priv->lock);
	priv->base = base;
	priv->regmap = dev_get_regmap(dev->parent, NULL);
	if (!priv->regmap) {
		err = -ENODEV;
		dev_err(dev, "failed to get regmap: %d\n", err);
		return err;
	}

	for_each_child_of_node(np, child) {
		err = of_property_read_u32(child, "reg", &reg);
		if (err) {
			of_node_put(child);
			mutex_destroy(&priv->lock);
			return err;
		}

		if (reg >= SC27XX_LEDS_MAX || priv->leds[reg].active) {
			of_node_put(child);
			mutex_destroy(&priv->lock);
			return -EINVAL;
		}

		priv->leds[reg].active = true;

		err = of_property_read_string(child, "label", &str);
		if (err)
			snprintf(priv->leds[reg].name, LED_MAX_NAME_SIZE,
				 "sc27xx::");
		else
			snprintf(priv->leds[reg].name, LED_MAX_NAME_SIZE,
				 "sc27xx:%s", str);
	}

	err = sc27xx_led_register(dev, priv);
	if (err)
		mutex_destroy(&priv->lock);

	return err;
}

static int sc27xx_led_remove(struct platform_device *pdev)
{
	struct sc27xx_led_priv *priv = platform_get_drvdata(pdev);

	mutex_destroy(&priv->lock);
	return 0;
}

static const struct of_device_id sc27xx_led_of_match[] = {
	{ .compatible = "sprd,sc2731-bltc", },
	{ }
};
MODULE_DEVICE_TABLE(of, sc27xx_led_of_match);

static struct platform_driver sc27xx_led_driver = {
	.driver = {
		.name = "sprd-bltc",
		.of_match_table = sc27xx_led_of_match,
	},
	.probe = sc27xx_led_probe,
	.remove = sc27xx_led_remove,
};

module_platform_driver(sc27xx_led_driver);

MODULE_DESCRIPTION("Spreadtrum SC27xx breathing light controller driver");
MODULE_AUTHOR("Xiaotong Lu <xiaotong.lu@spreadtrum.com>");
MODULE_LICENSE("GPL v2");
