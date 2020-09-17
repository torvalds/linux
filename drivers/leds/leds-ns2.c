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

struct ns2_led_modval {
	enum ns2_led_modes	mode;
	int			cmd_level;
	int			slow_level;
};

struct ns2_led_of_one {
	const char	*name;
	const char	*default_trigger;
	struct gpio_desc *cmd;
	struct gpio_desc *slow;
	int		num_modes;
	struct ns2_led_modval *modval;
};

struct ns2_led_of {
	int			num_leds;
	struct ns2_led_of_one	*leds;
};

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

static int ns2_led_get_mode(struct ns2_led *led_dat,
			    enum ns2_led_modes *mode)
{
	int i;
	int ret = -EINVAL;
	int cmd_level;
	int slow_level;

	cmd_level = gpiod_get_value_cansleep(led_dat->cmd);
	slow_level = gpiod_get_value_cansleep(led_dat->slow);

	for (i = 0; i < led_dat->num_modes; i++) {
		if (cmd_level == led_dat->modval[i].cmd_level &&
		    slow_level == led_dat->modval[i].slow_level) {
			*mode = led_dat->modval[i].mode;
			ret = 0;
			break;
		}
	}

	return ret;
}

static void ns2_led_set_mode(struct ns2_led *led_dat,
			     enum ns2_led_modes mode)
{
	int i;
	bool found = false;
	unsigned long flags;

	for (i = 0; i < led_dat->num_modes; i++)
		if (mode == led_dat->modval[i].mode) {
			found = true;
			break;
		}

	if (!found)
		return;

	write_lock_irqsave(&led_dat->rw_lock, flags);

	if (!led_dat->can_sleep) {
		gpiod_set_value(led_dat->cmd,
				led_dat->modval[i].cmd_level);
		gpiod_set_value(led_dat->slow,
				led_dat->modval[i].slow_level);
		goto exit_unlock;
	}

	gpiod_set_value_cansleep(led_dat->cmd, led_dat->modval[i].cmd_level);
	gpiod_set_value_cansleep(led_dat->slow, led_dat->modval[i].slow_level);

exit_unlock:
	write_unlock_irqrestore(&led_dat->rw_lock, flags);
}

static void ns2_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct ns2_led *led_dat =
		container_of(led_cdev, struct ns2_led, cdev);
	enum ns2_led_modes mode;

	if (value == LED_OFF)
		mode = NS_V2_LED_OFF;
	else if (led_dat->sata)
		mode = NS_V2_LED_SATA;
	else
		mode = NS_V2_LED_ON;

	ns2_led_set_mode(led_dat, mode);
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
	struct ns2_led *led_dat =
		container_of(led_cdev, struct ns2_led, cdev);
	int ret;
	unsigned long enable;

	ret = kstrtoul(buff, 10, &enable);
	if (ret < 0)
		return ret;

	enable = !!enable;

	if (led_dat->sata == enable)
		goto exit;

	led_dat->sata = enable;

	if (!led_get_brightness(led_cdev))
		goto exit;

	if (enable)
		ns2_led_set_mode(led_dat, NS_V2_LED_SATA);
	else
		ns2_led_set_mode(led_dat, NS_V2_LED_ON);

exit:
	return count;
}

static ssize_t ns2_led_sata_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct ns2_led *led_dat =
		container_of(led_cdev, struct ns2_led, cdev);

	return sprintf(buf, "%d\n", led_dat->sata);
}

static DEVICE_ATTR(sata, 0644, ns2_led_sata_show, ns2_led_sata_store);

static struct attribute *ns2_led_attrs[] = {
	&dev_attr_sata.attr,
	NULL
};
ATTRIBUTE_GROUPS(ns2_led);

static int
create_ns2_led(struct platform_device *pdev, struct ns2_led *led_dat,
	       const struct ns2_led_of_one *template)
{
	int ret;
	enum ns2_led_modes mode;

	rwlock_init(&led_dat->rw_lock);

	led_dat->cdev.name = template->name;
	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->cdev.blink_set = NULL;
	led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led_dat->cdev.groups = ns2_led_groups;
	led_dat->cmd = template->cmd;
	led_dat->slow = template->slow;
	led_dat->can_sleep = gpiod_cansleep(led_dat->cmd) |
				gpiod_cansleep(led_dat->slow);
	if (led_dat->can_sleep)
		led_dat->cdev.brightness_set_blocking = ns2_led_set_blocking;
	else
		led_dat->cdev.brightness_set = ns2_led_set;
	led_dat->modval = template->modval;
	led_dat->num_modes = template->num_modes;

	ret = ns2_led_get_mode(led_dat, &mode);
	if (ret < 0)
		return ret;

	/* Set LED initial state. */
	led_dat->sata = (mode == NS_V2_LED_SATA) ? 1 : 0;
	led_dat->cdev.brightness =
		(mode == NS_V2_LED_OFF) ? LED_OFF : LED_FULL;

	return devm_led_classdev_register(&pdev->dev, &led_dat->cdev);
}

static int ns2_leds_parse_one(struct device *dev, struct device_node *np,
			      struct ns2_led_of_one *led)
{
	struct ns2_led_modval *modval;
	int nmodes, ret, i;

	ret = of_property_read_string(np, "label", &led->name);
	if (ret)
		led->name = np->name;

	led->cmd = devm_gpiod_get_from_of_node(dev, np, "cmd-gpio", 0,
					       GPIOD_ASIS, led->name);
	if (IS_ERR(led->cmd))
		return PTR_ERR(led->cmd);

	led->slow = devm_gpiod_get_from_of_node(dev, np, "slow-gpio", 0,
						GPIOD_ASIS, led->name);
	if (IS_ERR(led->slow))
		return PTR_ERR(led->slow);

	of_property_read_string(np, "linux,default-trigger",
				&led->default_trigger);

	ret = of_property_count_u32_elems(np, "modes-map");
	if (ret < 0 || ret % 3) {
		dev_err(dev, "Missing or malformed modes-map for %pOF\n", np);
		return -EINVAL;
	}

	nmodes = ret / 3;
	modval = devm_kcalloc(dev, nmodes, sizeof(*modval), GFP_KERNEL);
	if (!modval)
		return -ENOMEM;

	for (i = 0; i < nmodes; i++) {
		u32 val;

		of_property_read_u32_index(np, "modes-map", 3 * i, &val);
		modval[i].mode = val;
		of_property_read_u32_index(np, "modes-map", 3 * i + 1, &val);
		modval[i].cmd_level = val;
		of_property_read_u32_index(np, "modes-map", 3 * i + 2, &val);
		modval[i].slow_level = val;
	}

	led->num_modes = nmodes;
	led->modval = modval;

	return 0;
}

/*
 * Translate OpenFirmware node properties into platform_data.
 */
static int
ns2_leds_parse_of(struct device *dev, struct ns2_led_of *ofdata)
{
	struct device_node *np = dev_of_node(dev);
	struct device_node *child;
	struct ns2_led_of_one *led, *leds;
	int ret, num_leds = 0;

	num_leds = of_get_available_child_count(np);
	if (!num_leds)
		return -ENODEV;

	leds = devm_kcalloc(dev, num_leds, sizeof(struct ns2_led),
			    GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	led = leds;
	for_each_available_child_of_node(np, child) {
		ret = ns2_leds_parse_one(dev, child, led++);
		if (ret < 0) {
			of_node_put(child);
			return ret;
		}
	}

	ofdata->leds = leds;
	ofdata->num_leds = num_leds;

	return 0;
}

static const struct of_device_id of_ns2_leds_match[] = {
	{ .compatible = "lacie,ns2-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_ns2_leds_match);

static int ns2_led_probe(struct platform_device *pdev)
{
	struct ns2_led_of *ofdata;
	struct ns2_led *leds;
	int i;
	int ret;

	ofdata = devm_kzalloc(&pdev->dev, sizeof(struct ns2_led_of),
			      GFP_KERNEL);
	if (!ofdata)
		return -ENOMEM;

	ret = ns2_leds_parse_of(&pdev->dev, ofdata);
	if (ret)
		return ret;

	leds = devm_kzalloc(&pdev->dev, array_size(sizeof(*leds),
						   ofdata->num_leds),
			    GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	for (i = 0; i < ofdata->num_leds; i++) {
		ret = create_ns2_led(pdev, &leds[i], &ofdata->leds[i]);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static struct platform_driver ns2_led_driver = {
	.probe		= ns2_led_probe,
	.driver		= {
		.name		= "leds-ns2",
		.of_match_table	= of_match_ptr(of_ns2_leds_match),
	},
};

module_platform_driver(ns2_led_driver);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("Network Space v2 LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-ns2");
