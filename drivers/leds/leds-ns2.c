// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * leds-ns2.c - Driver for the Network Space v2 (and parents) dual-GPIO LED
 *
 * Copyright (C) 2010 LaCie
 *
 * Author: Simon Guinot <sguinot@lacie.com>
 *
 * Based on leds-gpio.c by Raphael Assenat <raph@8d.com>
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio/consumer.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>
#include "leds.h"

enum ns2_led_modes {
	NS_V2_LED_OFF,
	NS_V2_LED_ON,
	NS_V2_LED_SATA,
};

/*
 * If the size of this structure or types of its members is changed,
 * the filling of array modval in function ns2_led_register must be changed
 * accordingly.
 */
struct ns2_led_modval {
	u32			mode;
	u32			cmd_level;
	u32			slow_level;
} __packed;

/*
 * The Network Space v2 dual-GPIO LED is wired to a CPLD. Three different LED
 * modes are available: off, on and SATA activity blinking. The LED modes are
 * controlled through two GPIOs (command and slow): each combination of values
 * for the command/slow GPIOs corresponds to a LED mode.
 */

struct ns2_led {
	struct led_classdev	cdev;
	struct gpio_desc	*cmd;
	struct gpio_desc	*slow;
	bool			can_sleep;
	unsigned char		sata; /* True when SATA mode active. */
	rwlock_t		rw_lock; /* Lock GPIOs. */
	int			num_modes;
	struct ns2_led_modval	*modval;
};

static int ns2_led_get_mode(struct ns2_led *led, enum ns2_led_modes *mode)
{
	int i;
	int cmd_level;
	int slow_level;

	cmd_level = gpiod_get_value_cansleep(led->cmd);
	slow_level = gpiod_get_value_cansleep(led->slow);

	for (i = 0; i < led->num_modes; i++) {
		if (cmd_level == led->modval[i].cmd_level &&
		    slow_level == led->modval[i].slow_level) {
			*mode = led->modval[i].mode;
			return 0;
		}
	}

	return -EINVAL;
}

static void ns2_led_set_mode(struct ns2_led *led, enum ns2_led_modes mode)
{
	int i;
	unsigned long flags;

	for (i = 0; i < led->num_modes; i++)
		if (mode == led->modval[i].mode)
			break;

	if (i == led->num_modes)
		return;

	write_lock_irqsave(&led->rw_lock, flags);

	if (!led->can_sleep) {
		gpiod_set_value(led->cmd, led->modval[i].cmd_level);
		gpiod_set_value(led->slow, led->modval[i].slow_level);
		goto exit_unlock;
	}

	gpiod_set_value_cansleep(led->cmd, led->modval[i].cmd_level);
	gpiod_set_value_cansleep(led->slow, led->modval[i].slow_level);

exit_unlock:
	write_unlock_irqrestore(&led->rw_lock, flags);
}

static void ns2_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct ns2_led *led = container_of(led_cdev, struct ns2_led, cdev);
	enum ns2_led_modes mode;

	if (value == LED_OFF)
		mode = NS_V2_LED_OFF;
	else if (led->sata)
		mode = NS_V2_LED_SATA;
	else
		mode = NS_V2_LED_ON;

	ns2_led_set_mode(led, mode);
}

static int ns2_led_set_blocking(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	ns2_led_set(led_cdev, value);
	return 0;
}

static ssize_t ns2_led_sata_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buff, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct ns2_led *led = container_of(led_cdev, struct ns2_led, cdev);
	int ret;
	unsigned long enable;

	ret = kstrtoul(buff, 10, &enable);
	if (ret < 0)
		return ret;

	enable = !!enable;

	if (led->sata == enable)
		goto exit;

	led->sata = enable;

	if (!led_get_brightness(led_cdev))
		goto exit;

	if (enable)
		ns2_led_set_mode(led, NS_V2_LED_SATA);
	else
		ns2_led_set_mode(led, NS_V2_LED_ON);

exit:
	return count;
}

static ssize_t ns2_led_sata_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct ns2_led *led = container_of(led_cdev, struct ns2_led, cdev);

	return sprintf(buf, "%d\n", led->sata);
}

static DEVICE_ATTR(sata, 0644, ns2_led_sata_show, ns2_led_sata_store);

static struct attribute *ns2_led_attrs[] = {
	&dev_attr_sata.attr,
	NULL
};
ATTRIBUTE_GROUPS(ns2_led);

static int ns2_led_register(struct device *dev, struct fwnode_handle *node,
			    struct ns2_led *led)
{
	struct led_init_data init_data = {};
	struct ns2_led_modval *modval;
	enum ns2_led_modes mode;
	int nmodes, ret;

	led->cmd = devm_fwnode_gpiod_get_index(dev, node, "cmd", 0, GPIOD_ASIS,
					       fwnode_get_name(node));
	if (IS_ERR(led->cmd))
		return PTR_ERR(led->cmd);

	led->slow = devm_fwnode_gpiod_get_index(dev, node, "slow", 0,
						GPIOD_ASIS,
						fwnode_get_name(node));
	if (IS_ERR(led->slow))
		return PTR_ERR(led->slow);

	ret = fwnode_property_count_u32(node, "modes-map");
	if (ret < 0 || ret % 3) {
		dev_err(dev, "Missing or malformed modes-map for %pfw\n", node);
		return -EINVAL;
	}

	nmodes = ret / 3;
	modval = devm_kcalloc(dev, nmodes, sizeof(*modval), GFP_KERNEL);
	if (!modval)
		return -ENOMEM;

	fwnode_property_read_u32_array(node, "modes-map", (void *)modval,
				       nmodes * 3);

	rwlock_init(&led->rw_lock);

	led->cdev.blink_set = NULL;
	led->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led->cdev.groups = ns2_led_groups;
	led->can_sleep = gpiod_cansleep(led->cmd) || gpiod_cansleep(led->slow);
	if (led->can_sleep)
		led->cdev.brightness_set_blocking = ns2_led_set_blocking;
	else
		led->cdev.brightness_set = ns2_led_set;
	led->num_modes = nmodes;
	led->modval = modval;

	ret = ns2_led_get_mode(led, &mode);
	if (ret < 0)
		return ret;

	/* Set LED initial state. */
	led->sata = (mode == NS_V2_LED_SATA) ? 1 : 0;
	led->cdev.brightness = (mode == NS_V2_LED_OFF) ? LED_OFF : LED_FULL;

	init_data.fwnode = node;

	ret = devm_led_classdev_register_ext(dev, &led->cdev, &init_data);
	if (ret)
		dev_err(dev, "Failed to register LED for node %pfw\n", node);

	return ret;
}

static int ns2_led_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ns2_led *leds;
	int count;
	int ret;

	count = device_get_child_node_count(dev);
	if (!count)
		return -ENODEV;

	leds = devm_kcalloc(dev, count, sizeof(*leds), GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	device_for_each_child_node_scoped(dev, child) {
		ret = ns2_led_register(dev, child, leds++);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct of_device_id of_ns2_leds_match[] = {
	{ .compatible = "lacie,ns2-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_ns2_leds_match);

static struct platform_driver ns2_led_driver = {
	.probe		= ns2_led_probe,
	.driver		= {
		.name		= "leds-ns2",
		.of_match_table	= of_ns2_leds_match,
	},
};

module_platform_driver(ns2_led_driver);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("Network Space v2 LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-ns2");
