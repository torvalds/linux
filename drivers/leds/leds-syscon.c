// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic Syscon LEDs Driver
 *
 * Copyright (c) 2014, Linaro Limited
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */
#include <linux/io.h>
#include <linux/init.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/leds.h>

/**
 * struct syscon_led - state container for syscon based LEDs
 * @cdev: LED class device for this LED
 * @map: regmap to access the syscon device backing this LED
 * @offset: the offset into the syscon regmap for the LED register
 * @mask: the bit in the register corresponding to the LED
 * @state: current state of the LED
 */
struct syscon_led {
	struct led_classdev cdev;
	struct regmap *map;
	u32 offset;
	u32 mask;
	bool state;
};

static void syscon_led_set(struct led_classdev *led_cdev,
	enum led_brightness value)
{
	struct syscon_led *sled =
		container_of(led_cdev, struct syscon_led, cdev);
	u32 val;
	int ret;

	if (value == LED_OFF) {
		val = 0;
		sled->state = false;
	} else {
		val = sled->mask;
		sled->state = true;
	}

	ret = regmap_update_bits(sled->map, sled->offset, sled->mask, val);
	if (ret < 0)
		dev_err(sled->cdev.dev, "error updating LED status\n");
}

static int syscon_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device *parent;
	struct regmap *map;
	struct syscon_led *sled;
	const char *state;
	int ret;

	parent = dev->parent;
	if (!parent) {
		dev_err(dev, "no parent for syscon LED\n");
		return -ENODEV;
	}
	map = syscon_node_to_regmap(parent->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "no regmap for syscon LED parent\n");
		return PTR_ERR(map);
	}

	sled = devm_kzalloc(dev, sizeof(*sled), GFP_KERNEL);
	if (!sled)
		return -ENOMEM;

	sled->map = map;

	if (of_property_read_u32(np, "offset", &sled->offset))
		return -EINVAL;
	if (of_property_read_u32(np, "mask", &sled->mask))
		return -EINVAL;
	sled->cdev.name =
		of_get_property(np, "label", NULL) ? : np->name;
	sled->cdev.default_trigger =
		of_get_property(np, "linux,default-trigger", NULL);

	state = of_get_property(np, "default-state", NULL);
	if (state) {
		if (!strcmp(state, "keep")) {
			u32 val;

			ret = regmap_read(map, sled->offset, &val);
			if (ret < 0)
				return ret;
			sled->state = !!(val & sled->mask);
		} else if (!strcmp(state, "on")) {
			sled->state = true;
			ret = regmap_update_bits(map, sled->offset,
						 sled->mask,
						 sled->mask);
			if (ret < 0)
				return ret;
		} else {
			sled->state = false;
			ret = regmap_update_bits(map, sled->offset,
						 sled->mask, 0);
			if (ret < 0)
				return ret;
		}
	}
	sled->cdev.brightness_set = syscon_led_set;

	ret = led_classdev_register(dev, &sled->cdev);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, sled);
	dev_info(dev, "registered LED %s\n", sled->cdev.name);

	return 0;
}

static const struct of_device_id of_syscon_leds_match[] = {
	{ .compatible = "register-bit-led", },
	{},
};

static struct platform_driver syscon_led_driver = {
	.probe		= syscon_led_probe,
	.driver		= {
		.name	= "leds-syscon",
		.of_match_table = of_syscon_leds_match,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(syscon_led_driver);
