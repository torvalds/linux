/*
 * leds-ns2.c - Driver for the Network Space v2 (and parents) dual-GPIO LED
 *
 * Copyright (C) 2010 LaCie
 *
 * Author: Simon Guinot <sguinot@lacie.com>
 *
 * Based on leds-gpio.c by Raphael Assenat <raph@8d.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_data/leds-kirkwood-ns2.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

/*
 * The Network Space v2 dual-GPIO LED is wired to a CPLD and can blink in
 * relation with the SATA activity. This capability is exposed through the
 * "sata" sysfs attribute.
 *
 * The following array detail the different LED registers and the combination
 * of their possible values:
 *
 *  cmd_led   |  slow_led  | /SATA active | LED state
 *            |            |              |
 *     1      |     0      |      x       |  off
 *     -      |     1      |      x       |  on
 *     0      |     0      |      1       |  on
 *     0      |     0      |      0       |  blink (rate 300ms)
 */

enum ns2_led_modes {
	NS_V2_LED_OFF,
	NS_V2_LED_ON,
	NS_V2_LED_SATA,
};

struct ns2_led_mode_value {
	enum ns2_led_modes	mode;
	int			cmd_level;
	int			slow_level;
};

static struct ns2_led_mode_value ns2_led_modval[] = {
	{ NS_V2_LED_OFF	, 1, 0 },
	{ NS_V2_LED_ON	, 0, 1 },
	{ NS_V2_LED_ON	, 1, 1 },
	{ NS_V2_LED_SATA, 0, 0 },
};

struct ns2_led_data {
	struct led_classdev	cdev;
	unsigned		cmd;
	unsigned		slow;
	unsigned char		sata; /* True when SATA mode active. */
	rwlock_t		rw_lock; /* Lock GPIOs. */
};

static int ns2_led_get_mode(struct ns2_led_data *led_dat,
			    enum ns2_led_modes *mode)
{
	int i;
	int ret = -EINVAL;
	int cmd_level;
	int slow_level;

	read_lock_irq(&led_dat->rw_lock);

	cmd_level = gpio_get_value(led_dat->cmd);
	slow_level = gpio_get_value(led_dat->slow);

	for (i = 0; i < ARRAY_SIZE(ns2_led_modval); i++) {
		if (cmd_level == ns2_led_modval[i].cmd_level &&
		    slow_level == ns2_led_modval[i].slow_level) {
			*mode = ns2_led_modval[i].mode;
			ret = 0;
			break;
		}
	}

	read_unlock_irq(&led_dat->rw_lock);

	return ret;
}

static void ns2_led_set_mode(struct ns2_led_data *led_dat,
			     enum ns2_led_modes mode)
{
	int i;
	unsigned long flags;

	write_lock_irqsave(&led_dat->rw_lock, flags);

	for (i = 0; i < ARRAY_SIZE(ns2_led_modval); i++) {
		if (mode == ns2_led_modval[i].mode) {
			gpio_set_value(led_dat->cmd,
				       ns2_led_modval[i].cmd_level);
			gpio_set_value(led_dat->slow,
				       ns2_led_modval[i].slow_level);
		}
	}

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

static ssize_t ns2_led_sata_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buff, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct ns2_led_data *led_dat =
		container_of(led_cdev, struct ns2_led_data, cdev);
	int ret;
	unsigned long enable;
	enum ns2_led_modes mode;

	ret = kstrtoul(buff, 10, &enable);
	if (ret < 0)
		return ret;

	enable = !!enable;

	if (led_dat->sata == enable)
		return count;

	ret = ns2_led_get_mode(led_dat, &mode);
	if (ret < 0)
		return ret;

	if (enable && mode == NS_V2_LED_ON)
		ns2_led_set_mode(led_dat, NS_V2_LED_SATA);
	if (!enable && mode == NS_V2_LED_SATA)
		ns2_led_set_mode(led_dat, NS_V2_LED_ON);

	led_dat->sata = enable;

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

static int
create_ns2_led(struct platform_device *pdev, struct ns2_led_data *led_dat,
	       const struct ns2_led *template)
{
	int ret;
	enum ns2_led_modes mode;

	ret = devm_gpio_request_one(&pdev->dev, template->cmd,
			gpio_get_value(template->cmd) ?
			GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW,
			template->name);
	if (ret) {
		dev_err(&pdev->dev, "%s: failed to setup command GPIO\n",
			template->name);
		return ret;
	}

	ret = devm_gpio_request_one(&pdev->dev, template->slow,
			gpio_get_value(template->slow) ?
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
	led_dat->cdev.brightness_set = ns2_led_set;
	led_dat->cdev.flags |= LED_CORE_SUSPENDRESUME;
	led_dat->cmd = template->cmd;
	led_dat->slow = template->slow;

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

	ret = device_create_file(led_dat->cdev.dev, &dev_attr_sata);
	if (ret < 0)
		goto err_free_cdev;

	return 0;

err_free_cdev:
	led_classdev_unregister(&led_dat->cdev);
	return ret;
}

static void delete_ns2_led(struct ns2_led_data *led_dat)
{
	device_remove_file(led_dat->cdev.dev, &dev_attr_sata);
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
	struct ns2_led *leds;
	int num_leds = 0;
	int i = 0;

	num_leds = of_get_child_count(np);
	if (!num_leds)
		return -ENODEV;

	leds = devm_kzalloc(dev, num_leds * sizeof(struct ns2_led),
			    GFP_KERNEL);
	if (!leds)
		return -ENOMEM;

	for_each_child_of_node(np, child) {
		const char *string;
		int ret;

		ret = of_get_named_gpio(child, "cmd-gpio", 0);
		if (ret < 0)
			return ret;
		leds[i].cmd = ret;
		ret = of_get_named_gpio(child, "slow-gpio", 0);
		if (ret < 0)
			return ret;
		leds[i].slow = ret;
		ret = of_property_read_string(child, "label", &string);
		leds[i].name = (ret == 0) ? string : child->name;
		ret = of_property_read_string(child, "linux,default-trigger",
					      &string);
		if (ret == 0)
			leds[i].default_trigger = string;

		i++;
	}

	pdata->leds = leds;
	pdata->num_leds = num_leds;

	return 0;
}

static const struct of_device_id of_ns2_leds_match[] = {
	{ .compatible = "lacie,ns2-leds", },
	{},
};
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
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(of_ns2_leds_match),
	},
};

module_platform_driver(ns2_led_driver);

MODULE_AUTHOR("Simon Guinot <sguinot@lacie.com>");
MODULE_DESCRIPTION("Network Space v2 LED driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-ns2");
