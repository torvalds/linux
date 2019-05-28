// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver for SMSC USB3503 USB 2.0 hub controller driver
 *
 * Copyright (c) 2012-2013 Dongjin Kim (tobetter@gmail.com)
 */

#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/platform_data/usb3503.h>
#include <linux/regmap.h>

#define USB3503_VIDL		0x00
#define USB3503_VIDM		0x01
#define USB3503_PIDL		0x02
#define USB3503_PIDM		0x03
#define USB3503_DIDL		0x04
#define USB3503_DIDM		0x05

#define USB3503_CFG1		0x06
#define USB3503_SELF_BUS_PWR	(1 << 7)

#define USB3503_CFG2		0x07
#define USB3503_CFG3		0x08
#define USB3503_NRD		0x09

#define USB3503_PDS		0x0a

#define USB3503_SP_ILOCK	0xe7
#define USB3503_SPILOCK_CONNECT	(1 << 1)
#define USB3503_SPILOCK_CONFIG	(1 << 0)

#define USB3503_CFGP		0xee
#define USB3503_CLKSUSP		(1 << 7)

#define USB3503_RESET		0xff

struct usb3503 {
	enum usb3503_mode	mode;
	struct regmap		*regmap;
	struct device		*dev;
	struct clk		*clk;
	u8	port_off_mask;
	int	gpio_intn;
	int	gpio_reset;
	int	gpio_connect;
	bool	secondary_ref_clk;
};

static int usb3503_reset(struct usb3503 *hub, int state)
{
	if (!state && gpio_is_valid(hub->gpio_connect))
		gpio_set_value_cansleep(hub->gpio_connect, 0);

	if (gpio_is_valid(hub->gpio_reset))
		gpio_set_value_cansleep(hub->gpio_reset, state);

	/* Wait T_HUBINIT == 4ms for hub logic to stabilize */
	if (state)
		usleep_range(4000, 10000);

	return 0;
}

static int usb3503_connect(struct usb3503 *hub)
{
	struct device *dev = hub->dev;
	int err;

	usb3503_reset(hub, 1);

	if (hub->regmap) {
		/* SP_ILOCK: set connect_n, config_n for config */
		err = regmap_write(hub->regmap, USB3503_SP_ILOCK,
			   (USB3503_SPILOCK_CONNECT
				 | USB3503_SPILOCK_CONFIG));
		if (err < 0) {
			dev_err(dev, "SP_ILOCK failed (%d)\n", err);
			return err;
		}

		/* PDS : Set the ports which are disabled in self-powered mode. */
		if (hub->port_off_mask) {
			err = regmap_update_bits(hub->regmap, USB3503_PDS,
					hub->port_off_mask,
					hub->port_off_mask);
			if (err < 0) {
				dev_err(dev, "PDS failed (%d)\n", err);
				return err;
			}
		}

		/* CFG1 : Set SELF_BUS_PWR, this enables self-powered operation. */
		err = regmap_update_bits(hub->regmap, USB3503_CFG1,
					 USB3503_SELF_BUS_PWR,
					 USB3503_SELF_BUS_PWR);
		if (err < 0) {
			dev_err(dev, "CFG1 failed (%d)\n", err);
			return err;
		}

		/* SP_LOCK: clear connect_n, config_n for hub connect */
		err = regmap_update_bits(hub->regmap, USB3503_SP_ILOCK,
					 (USB3503_SPILOCK_CONNECT
					  | USB3503_SPILOCK_CONFIG), 0);
		if (err < 0) {
			dev_err(dev, "SP_ILOCK failed (%d)\n", err);
			return err;
		}
	}

	if (gpio_is_valid(hub->gpio_connect))
		gpio_set_value_cansleep(hub->gpio_connect, 1);

	hub->mode = USB3503_MODE_HUB;
	dev_info(dev, "switched to HUB mode\n");

	return 0;
}

static int usb3503_switch_mode(struct usb3503 *hub, enum usb3503_mode mode)
{
	struct device *dev = hub->dev;
	int err = 0;

	switch (mode) {
	case USB3503_MODE_HUB:
		err = usb3503_connect(hub);
		break;

	case USB3503_MODE_STANDBY:
		usb3503_reset(hub, 0);
		dev_info(dev, "switched to STANDBY mode\n");
		break;

	default:
		dev_err(dev, "unknown mode is requested\n");
		err = -EINVAL;
		break;
	}

	return err;
}

static const struct regmap_config usb3503_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = USB3503_RESET,
};

static int usb3503_probe(struct usb3503 *hub)
{
	struct device *dev = hub->dev;
	struct usb3503_platform_data *pdata = dev_get_platdata(dev);
	struct device_node *np = dev->of_node;
	int err;
	u32 mode = USB3503_MODE_HUB;
	const u32 *property;
	int len;

	if (pdata) {
		hub->port_off_mask	= pdata->port_off_mask;
		hub->gpio_intn		= pdata->gpio_intn;
		hub->gpio_connect	= pdata->gpio_connect;
		hub->gpio_reset		= pdata->gpio_reset;
		hub->mode		= pdata->initial_mode;
	} else if (np) {
		u32 rate = 0;
		hub->port_off_mask = 0;

		if (!of_property_read_u32(np, "refclk-frequency", &rate)) {
			switch (rate) {
			case 38400000:
			case 26000000:
			case 19200000:
			case 12000000:
				hub->secondary_ref_clk = 0;
				break;
			case 24000000:
			case 27000000:
			case 25000000:
			case 50000000:
				hub->secondary_ref_clk = 1;
				break;
			default:
				dev_err(dev,
					"unsupported reference clock rate (%d)\n",
					(int) rate);
				return -EINVAL;
			}
		}

		hub->clk = devm_clk_get_optional(dev, "refclk");
		if (IS_ERR(hub->clk)) {
			dev_err(dev, "unable to request refclk (%ld)\n",
					PTR_ERR(hub->clk));
			return PTR_ERR(hub->clk);
		}

		if (rate != 0) {
			err = clk_set_rate(hub->clk, rate);
			if (err) {
				dev_err(dev,
					"unable to set reference clock rate to %d\n",
					(int)rate);
				return err;
			}
		}

		err = clk_prepare_enable(hub->clk);
		if (err) {
			dev_err(dev, "unable to enable reference clock\n");
			return err;
		}

		property = of_get_property(np, "disabled-ports", &len);
		if (property && (len / sizeof(u32)) > 0) {
			int i;
			for (i = 0; i < len / sizeof(u32); i++) {
				u32 port = be32_to_cpu(property[i]);
				if ((1 <= port) && (port <= 3))
					hub->port_off_mask |= (1 << port);
			}
		}

		hub->gpio_intn	= of_get_named_gpio(np, "intn-gpios", 0);
		if (hub->gpio_intn == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		hub->gpio_connect = of_get_named_gpio(np, "connect-gpios", 0);
		if (hub->gpio_connect == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		hub->gpio_reset = of_get_named_gpio(np, "reset-gpios", 0);
		if (hub->gpio_reset == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		of_property_read_u32(np, "initial-mode", &mode);
		hub->mode = mode;
	}

	if (hub->port_off_mask && !hub->regmap)
		dev_err(dev, "Ports disabled with no control interface\n");

	if (gpio_is_valid(hub->gpio_intn)) {
		int val = hub->secondary_ref_clk ? GPIOF_OUT_INIT_LOW :
						   GPIOF_OUT_INIT_HIGH;
		err = devm_gpio_request_one(dev, hub->gpio_intn, val,
					    "usb3503 intn");
		if (err) {
			dev_err(dev,
				"unable to request GPIO %d as interrupt pin (%d)\n",
				hub->gpio_intn, err);
			return err;
		}
	}

	if (gpio_is_valid(hub->gpio_connect)) {
		err = devm_gpio_request_one(dev, hub->gpio_connect,
				GPIOF_OUT_INIT_LOW, "usb3503 connect");
		if (err) {
			dev_err(dev,
				"unable to request GPIO %d as connect pin (%d)\n",
				hub->gpio_connect, err);
			return err;
		}
	}

	if (gpio_is_valid(hub->gpio_reset)) {
		err = devm_gpio_request_one(dev, hub->gpio_reset,
				GPIOF_OUT_INIT_LOW, "usb3503 reset");
		/* Datasheet defines a hardware reset to be at least 100us */
		usleep_range(100, 10000);
		if (err) {
			dev_err(dev,
				"unable to request GPIO %d as reset pin (%d)\n",
				hub->gpio_reset, err);
			return err;
		}
	}

	usb3503_switch_mode(hub, hub->mode);

	dev_info(dev, "%s: probed in %s mode\n", __func__,
			(hub->mode == USB3503_MODE_HUB) ? "hub" : "standby");

	return 0;
}

static int usb3503_i2c_probe(struct i2c_client *i2c,
			     const struct i2c_device_id *id)
{
	struct usb3503 *hub;
	int err;

	hub = devm_kzalloc(&i2c->dev, sizeof(struct usb3503), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;

	i2c_set_clientdata(i2c, hub);
	hub->regmap = devm_regmap_init_i2c(i2c, &usb3503_regmap_config);
	if (IS_ERR(hub->regmap)) {
		err = PTR_ERR(hub->regmap);
		dev_err(&i2c->dev, "Failed to initialise regmap: %d\n", err);
		return err;
	}
	hub->dev = &i2c->dev;

	return usb3503_probe(hub);
}

static int usb3503_i2c_remove(struct i2c_client *i2c)
{
	struct usb3503 *hub;

	hub = i2c_get_clientdata(i2c);
	clk_disable_unprepare(hub->clk);

	return 0;
}

static int usb3503_platform_probe(struct platform_device *pdev)
{
	struct usb3503 *hub;

	hub = devm_kzalloc(&pdev->dev, sizeof(struct usb3503), GFP_KERNEL);
	if (!hub)
		return -ENOMEM;
	hub->dev = &pdev->dev;
	platform_set_drvdata(pdev, hub);

	return usb3503_probe(hub);
}

static int usb3503_platform_remove(struct platform_device *pdev)
{
	struct usb3503 *hub;

	hub = platform_get_drvdata(pdev);
	clk_disable_unprepare(hub->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int usb3503_suspend(struct usb3503 *hub)
{
	usb3503_switch_mode(hub, USB3503_MODE_STANDBY);
	clk_disable_unprepare(hub->clk);

	return 0;
}

static int usb3503_resume(struct usb3503 *hub)
{
	clk_prepare_enable(hub->clk);
	usb3503_switch_mode(hub, hub->mode);

	return 0;
}

static int usb3503_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	return usb3503_suspend(i2c_get_clientdata(client));
}

static int usb3503_i2c_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);

	return usb3503_resume(i2c_get_clientdata(client));
}

static int usb3503_platform_suspend(struct device *dev)
{
	return usb3503_suspend(dev_get_drvdata(dev));
}

static int usb3503_platform_resume(struct device *dev)
{
	return usb3503_resume(dev_get_drvdata(dev));
}
#endif

static SIMPLE_DEV_PM_OPS(usb3503_i2c_pm_ops, usb3503_i2c_suspend,
		usb3503_i2c_resume);

static SIMPLE_DEV_PM_OPS(usb3503_platform_pm_ops, usb3503_platform_suspend,
		usb3503_platform_resume);

static const struct i2c_device_id usb3503_id[] = {
	{ USB3503_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, usb3503_id);

#ifdef CONFIG_OF
static const struct of_device_id usb3503_of_match[] = {
	{ .compatible = "smsc,usb3503", },
	{ .compatible = "smsc,usb3503a", },
	{},
};
MODULE_DEVICE_TABLE(of, usb3503_of_match);
#endif

static struct i2c_driver usb3503_i2c_driver = {
	.driver = {
		.name = USB3503_I2C_NAME,
		.pm = &usb3503_i2c_pm_ops,
		.of_match_table = of_match_ptr(usb3503_of_match),
	},
	.probe		= usb3503_i2c_probe,
	.remove		= usb3503_i2c_remove,
	.id_table	= usb3503_id,
};

static struct platform_driver usb3503_platform_driver = {
	.driver = {
		.name = USB3503_I2C_NAME,
		.of_match_table = of_match_ptr(usb3503_of_match),
		.pm = &usb3503_platform_pm_ops,
	},
	.probe		= usb3503_platform_probe,
	.remove		= usb3503_platform_remove,
};

static int __init usb3503_init(void)
{
	int err;

	err = i2c_add_driver(&usb3503_i2c_driver);
	if (err != 0)
		pr_err("usb3503: Failed to register I2C driver: %d\n", err);

	err = platform_driver_register(&usb3503_platform_driver);
	if (err != 0)
		pr_err("usb3503: Failed to register platform driver: %d\n",
		       err);

	return 0;
}
module_init(usb3503_init);

static void __exit usb3503_exit(void)
{
	platform_driver_unregister(&usb3503_platform_driver);
	i2c_del_driver(&usb3503_i2c_driver);
}
module_exit(usb3503_exit);

MODULE_AUTHOR("Dongjin Kim <tobetter@gmail.com>");
MODULE_DESCRIPTION("USB3503 USB HUB driver");
MODULE_LICENSE("GPL");
