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
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/leds-kirkwood-ns2.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include "leds.h"

/*
 * The Network Space v2 dual-GPIO LED is wired to a CPLD. Three different LED
 * modes are available: off, on and SATA activity blinking. The LED modes are
 * controlled through two GPIOs (command and slow): each combination of values
 * for the command/slow GPIOs corresponds to a LED mode.
 */

struct ns2_led_data {
	struct led_classdev	cdev;
	unsigned int		cmd;
	unsigned int		slow;
	bool			can_sleep;
	unsigned char		sata; /* True when SATA mode active. */
	rwlock_t		rw_lock; /* Lock GPIOs. */
	int			num_modes;
	struct ns2_led_modval	*modval;
};

static int ns2_led_get_mode(struct ns2_led_data *led_dat,
			    enum ns2_led_modes *mode)
{
	int i;
	int ret = -EINVAL;
	int cmd_level;
	int slow_level;

	cmd_level = gpio_get_value_cansleep(led_dat->cmd);
	slow_level = gpio_get_value_cansleep(led_dat->slow);

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

static void ns2_led_set_mode(struct ns2_led_data *led_dat,
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
		gpio_set_value(led_dat->cmd,
			       led_dat->modval[i].cmd_level);
		gpio_set_value(led_dat->slow,
			       led_dat->modval[i].slow_level);
		goto exit_unlock;
	}

	gpio_set_value_cansleep(led_dat->cmd, led_dat->modval[i].cmd_level);
	gpio_set_value_cansleep(led_dat->slow, led_dat->modval[i].slow_level);

exit_unlock:
	write_unlock_irqrestore(&led_dat->rw_lock, flags);
}

static void ns2_led_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct ns2_led_data *led_dat =
		container_of(led_cdev, struct ns2_led_data, cdev);
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
	struct ns2_led_data *led_dat =
		container_of(led_cdev, struct ns2_led_data, cdev);
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
	struct ns2_led_data *led_dat =
		container_of(led_cdev, struct ns2_led_data, cdev);

	return sprintf(buf, "%d\n", led_dat->sata);
}

static DEVICE_ATTR(sata, 0644, ns2_led_sata_show, ns2_led_sata_store);

static struct attribute *ns2_led_attrs[] = {
	&dev_attr_sata.attr,
	NULL
};
ATTRIBUTE_GROUPS(ns2_led);

static int
create_ns2_led(struct platform_device *pdev, struct ns2_led_data *led_dat,
	       const struct ns2_led *template)
{
	int ret;
	enum ns2_led_modes mode;

	ret = devm_gpio_request_one(&pdev->dev, template->cmd,
			gpio_get_value_cansleep(template->cmd) ?
			GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
			template->name);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to setup command GPIO\n",
			template->name);
		return ret;
	}

	ret = devm_gpio_request_one(&pdev->dev, template->slow,
			gpio_get_value_cansleep(template->slow) ?
			GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
			template->name);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to setup slow GPIO\n",
			template->name);
		return ret;
	}

	rwlock_init(&led_dat->rw_lock);

	led_dat->cdev.name = template->name;
	led_dat->cdev.default_trigger = template->default_trigger;
	led_dat->cdev.blink_set = NULL;
	led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led_dat->cdev.groups = ns2_led_groups;
	led_dat->cmd = template->cmd;
	led_dat->slow = template->slow;
	led_dat->can_sleep = gpio_cansleep(led_dat->cmd) |
				gpio_cansleep(led_dat->slow);
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

	ret = led_classdev_register(&pdev->dev, &led_dat->cdev);
	if (ret < 0)
		return ret;

	return 0;
}

static void delete_ns2_led(struct ns2_led_data *led_dat)
{
	led_classdev_unregister(&led_dat->cdev);
}

#ifdef CONFIG_OF_GPIO
/*
 * Translate OpenFirmware node properties into platform_data.
 */
static int
ns2_leds_get_of_pdata(struct device *dev, struct ns2_led_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	struct device_node *child;
	struct ns2_led *led, *leds;
	int ret, num_leds = 0;

	num_leds = of_get_child_count(np);
	if (!num_leds)
		return -ENODEV;

	leds = devm_kcalloc(dev, num_leds, sizeof(struct ns2_led),
			    GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	led = leds;
	for_each_child_of_node(np, child) {
		const char *string;
		int i, num_modes;
		struct ns2_led_modval *modval;

		ret = of_get_named_gpio(child, "cmd-gpio", 0);
		if (ret < 0)
			goto err_node_put;
		led->cmd = ret;
		ret = of_get_named_gpio(child, "slow-gpio", 0);
		if (ret < 0)
			goto err_node_put;
		led->slow = ret;
		ret = of_property_read_string(child, "label", &string);
		led->name = (ret == 0) ? string : child->name;
		ret = of_property_read_string(child, "linux,default-trigger",
					      &string);
		if (ret == 0)
			led->default_trigger = string;

		ret = of_property_count_u32_elems(child, "modes-map");
		if (ret < 0 || ret % 3) {
			dev_err(dev,
				"Missing or malformed modes-map property\n");
			ret = -EINVAL;
			goto err_node_put;
		}

		num_modes = ret / 3;
		modval = devm_kcalloc(dev,
				      num_modes,
				      sizeof(struct ns2_led_modval),
				      GFP_KERNEL);
		if (!modval) {
			ret = -ENOMEM;
			goto err_node_put;
		}

		for (i = 0; i < num_modes; i++) {
			of_property_read_u32_index(child,
						"modes-map", 3 * i,
						(u32 *) &modval[i].mode);
			of_property_read_u32_index(child,
						"modes-map", 3 * i + 1,
						(u32 *) &modval[i].cmd_level);
			of_property_read_u32_index(child,
						"modes-map", 3 * i + 2,
						(u32 *) &modval[i].slow_level);
		}

		led->num_modes = num_modes;
		led->modval = modval;

		led++;
	}

	pdata->leds = leds;
	pdata->num_leds = num_leds;

	return 0;

err_node_put:
	of_node_put(child);
	return ret;
}

static const struct of_device_id of_ns2_leds_match[] = {
	{ .compatible = "lacie,ns2-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_ns2_leds_match);
#endif /* CONFIG_OF_GPIO */

struct ns2_led_priv {
	int num_leds;
	struct ns2_led_data leds_data[];
};

static inline int sizeof_ns2_led_priv(int num_leds)
{
	return sizeof(struct ns2_led_priv) +
		      (sizeof(struct ns2_led_data) * num_leds);
}

static int ns2_led_probe(struct platform_device *pdev)
{
	struct ns2_led_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct ns2_led_priv *priv;
	int i;
	int ret;

#ifdef CONFIG_OF_GPIO
	if (!pdata) {
		pdata = devm_kzalloc(&pdev->dev,
				     sizeof(struct ns2_led_platform_data),
				     GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;

		ret = ns2_leds_get_of_pdata(&pdev->dev, pdata);
		if (ret)
			return ret;
	}
#else
	if (!pdata)
		return -EINVAL;
#endif /* CONFIG_OF_GPIO */

	priv = devm_kzalloc(&pdev->dev,
			    sizeof_ns2_led_priv(pdata->num_leds), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->num_leds = pdata->num_leds;

	for (i = 0; i < priv->num_leds; i++) {
		ret = create_ns2_led(pdev, &priv->leds_data[i],
				     &pdata->leds[i]);
		if (ret < 0) {
			for (i = i - 1; i >= 0; i--)
				delete_ns2_led(&priv->leds_data[i]);
			return ret;
		}
	}

	platform_set_drvdata(pdev, priv);

	return 0;
}

static int ns2_led_remove(struct platform_device *pdev)
{
	int i;
	struct ns2_led_priv *priv;

	priv = platform_get_drvdata(pdev);

	for (i = 0; i < priv->num_leds; i++)
		delete_ns2_led(&priv->leds_data[i]);

	return 0;
}

static struct platform_driver ns2_led_driver = {
	.probe		= ns2_led_probe,
	.remove		= ns2_led_remove,
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
