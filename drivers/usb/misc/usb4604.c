// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for SMSC USB4604 USB HSIC 4-port 2.0 hub controller driver
 * Based on usb3503 driver
 *
 * Copyright (c) 2012-2013 Dongjin Kim (tobetter@gmail.com)
 * Copyright (c) 2016 Linaro Ltd.
 */

#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>

enum usb4604_mode {
	USB4604_MODE_UNKNOWN,
	USB4604_MODE_HUB,
	USB4604_MODE_STANDBY,
};

struct usb4604 {
	enum usb4604_mode	mode;
	struct device		*dev;
	struct gpio_desc	*gpio_reset;
};

static void usb4604_reset(struct usb4604 *hub, int state)
{
	gpiod_set_value_cansleep(hub->gpio_reset, state);

	/* Wait for i2c logic to come up */
	if (state)
		msleep(250);
}

static int usb4604_connect(struct usb4604 *hub)
{
	struct device *dev = hub->dev;
	struct i2c_client *client = to_i2c_client(dev);
	int err;
	u8 connect_cmd[] = { 0xaa, 0x55, 0x00 };

	usb4604_reset(hub, 1);

	err = i2c_master_send(client, connect_cmd, ARRAY_SIZE(connect_cmd));
	if (err < 0) {
		usb4604_reset(hub, 0);
		return err;
	}

	hub->mode = USB4604_MODE_HUB;
	dev_dbg(dev, "switched to HUB mode\n");

	return 0;
}

static int usb4604_switch_mode(struct usb4604 *hub, enum usb4604_mode mode)
{
	struct device *dev = hub->dev;
	int err = 0;

	switch (mode) {
	case USB4604_MODE_HUB:
		err = usb4604_connect(hub);
		break;

	case USB4604_MODE_STANDBY:
		usb4604_reset(hub, 0);
		dev_dbg(dev, "switched to STANDBY mode\n");
		break;

	default:
		dev_err(dev, "unknown mode is requested\n");
		err = -EINVAL;
		break;
	}

	return err;
}

static int usb4604_probe(struct usb4604 *hub)
{
	struct device *dev = hub->dev;
	struct device_node *np = dev->of_node;
	struct gpio_desc *gpio;
	u32 mode = USB4604_MODE_HUB;

	gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(gpio))
		return PTR_ERR(gpio);
	hub->gpio_reset = gpio;

	if (of_property_read_u32(np, "initial-mode", &hub->mode))
		hub->mode = mode;

	return usb4604_switch_mode(hub, hub->mode);
}

static int usb4604_i2c_probe(struct i2c_client *i2c)
{
	struct usb4604 *hub;

	hub = devm_kzalloc(&i2c->dev, sizeof(*hub), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	i2c_set_clientdata(i2c, hub);
	hub->dev = &i2c->dev;

	return usb4604_probe(hub);
}

static int __maybe_unused usb4604_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct usb4604 *hub = i2c_get_clientdata(client);

	usb4604_switch_mode(hub, USB4604_MODE_STANDBY);

	return 0;
}

static int __maybe_unused usb4604_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct usb4604 *hub = i2c_get_clientdata(client);

	usb4604_switch_mode(hub, hub->mode);

	return 0;
}

static SIMPLE_DEV_PM_OPS(usb4604_i2c_pm_ops, usb4604_i2c_suspend,
		usb4604_i2c_resume);

static const struct i2c_device_id usb4604_id[] = {
	{ "usb4604", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, usb4604_id);

#ifdef CONFIG_OF
static const struct of_device_id usb4604_of_match[] = {
	{ .compatible = "smsc,usb4604" },
	{}
};
MODULE_DEVICE_TABLE(of, usb4604_of_match);
#endif

static struct i2c_driver usb4604_i2c_driver = {
	.driver = {
		.name = "usb4604",
		.pm = pm_ptr(&usb4604_i2c_pm_ops),
		.of_match_table = of_match_ptr(usb4604_of_match),
	},
	.probe_new	= usb4604_i2c_probe,
	.id_table	= usb4604_id,
};
module_i2c_driver(usb4604_i2c_driver);

MODULE_DESCRIPTION("USB4604 USB HUB driver");
MODULE_LICENSE("GPL v2");
