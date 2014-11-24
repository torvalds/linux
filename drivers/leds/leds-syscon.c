/*
 * Generic Syscon LEDs Driver
 *
 * Copyright (c) 2014, Linaro Limited
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 * This driver provides system reboot functionality for APM X-Gene SoC.
 * For system shutdown, this is board specify. If a board designer
 * implements GPIO shutdown, use the gpio-poweroff.c driver.
 */
#include <linux/io.h>
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

static const struct of_device_id syscon_match[] = {
	{ .compatible = "syscon", },
	{},
};

static int __init syscon_leds_init(void)
{
	const struct of_device_id *devid;
	struct device_node *np;
	struct device_node *child;
	struct regmap *map;
	struct platform_device *pdev;
	struct device *dev;
	int ret;

	np = of_find_matching_node_and_match(NULL, syscon_match,
					     &devid);
	if (!np)
		return -ENODEV;

	map = syscon_node_to_regmap(np);
	if (IS_ERR(map))
		return PTR_ERR(map);

	/*
	 * If the map is there, the device should be there, we allocate
	 * memory on the syscon device's behalf here.
	 */
	pdev = of_find_device_by_node(np);
	if (!pdev)
		return -ENODEV;
	dev = &pdev->dev;

	for_each_available_child_of_node(np, child) {
		struct syscon_led *sled;
		const char *state;

		/* Only check for register-bit-leds */
		if (of_property_match_string(child, "compatible",
					     "register-bit-led") < 0)
			continue;

		sled = devm_kzalloc(dev, sizeof(*sled), GFP_KERNEL);
		if (!sled)
			return -ENOMEM;

		sled->map = map;

		if (of_property_read_u32(child, "offset", &sled->offset))
			return -EINVAL;
		if (of_property_read_u32(child, "mask", &sled->mask))
			return -EINVAL;
		sled->cdev.name =
			of_get_property(child, "label", NULL) ? : child->name;
		sled->cdev.default_trigger =
			of_get_property(child, "linux,default-trigger", NULL);

		state = of_get_property(child, "default-state", NULL);
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

		dev_info(dev, "registered LED %s\n", sled->cdev.name);
	}

       return 0;
}
device_initcall(syscon_leds_init);
