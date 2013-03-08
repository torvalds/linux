/*
 * NXP ISP1301 USB transceiver driver
 *
 * Copyright (C) 2012 Roland Stigge
 *
 * Author: Roland Stigge <stigge@antcom.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/i2c.h>
#include <linux/usb/phy.h>

#define DRV_NAME		"isp1301"

struct isp1301 {
	struct usb_phy		phy;
	struct mutex		mutex;

	struct i2c_client	*client;
};

static const struct i2c_device_id isp1301_id[] = {
	{ "isp1301", 0 },
	{ }
};

static struct i2c_client *isp1301_i2c_client;

static int isp1301_probe(struct i2c_client *client,
			 const struct i2c_device_id *i2c_id)
{
	struct isp1301 *isp;
	struct usb_phy *phy;

	isp = devm_kzalloc(&client->dev, sizeof(*isp), GFP_KERNEL);
	if (!isp)
		return -ENOMEM;

	isp->client = client;
	mutex_init(&isp->mutex);

	phy = &isp->phy;
	phy->label = DRV_NAME;
	phy->type = USB_PHY_TYPE_USB2;

	i2c_set_clientdata(client, isp);
	usb_add_phy_dev(phy);

	isp1301_i2c_client = client;

	return 0;
}

static int isp1301_remove(struct i2c_client *client)
{
	struct isp1301 *isp = i2c_get_clientdata(client);

	usb_remove_phy(&isp->phy);
	isp1301_i2c_client = NULL;

	return 0;
}

static struct i2c_driver isp1301_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = isp1301_probe,
	.remove = isp1301_remove,
	.id_table = isp1301_id,
};

module_i2c_driver(isp1301_driver);

static int match(struct device *dev, void *data)
{
	struct device_node *node = (struct device_node *)data;
	return (dev->of_node == node) &&
		(dev->driver == &isp1301_driver.driver);
}

struct i2c_client *isp1301_get_client(struct device_node *node)
{
	if (node) { /* reference of ISP1301 I2C node via DT */
		struct device *dev = bus_find_device(&i2c_bus_type, NULL,
						     node, match);
		if (!dev)
			return NULL;
		return to_i2c_client(dev);
	} else { /* non-DT: only one ISP1301 chip supported */
		return isp1301_i2c_client;
	}
}
EXPORT_SYMBOL_GPL(isp1301_get_client);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("NXP ISP1301 USB transceiver driver");
MODULE_LICENSE("GPL");
