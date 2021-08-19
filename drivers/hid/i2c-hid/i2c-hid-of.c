/*
 * HID over I2C Open Firmware Subclass
 *
 * Copyright (c) 2012 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright (c) 2012 Ecole Nationale de l'Aviation Civile, France
 * Copyright (c) 2012 Red Hat, Inc
 *
 * This code was forked out of the core code, which was partly based on
 * "USB HID support for Linux":
 *
 *  Copyright (c) 1999 Andreas Gal
 *  Copyright (c) 2000-2005 Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2005 Michael Haboustak <mike-@cinci.rr.com> for Concept2, Inc
 *  Copyright (c) 2007-2008 Oliver Neukum
 *  Copyright (c) 2006-2010 Jiri Kosina
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/pm.h>
#include <linux/regulator/consumer.h>

#include "i2c-hid.h"

struct i2c_hid_of {
	struct i2chid_ops ops;

	struct i2c_client *client;
	struct regulator_bulk_data supplies[2];
	int post_power_delay_ms;
};

static int i2c_hid_of_power_up(struct i2chid_ops *ops)
{
	struct i2c_hid_of *ihid_of = container_of(ops, struct i2c_hid_of, ops);
	struct device *dev = &ihid_of->client->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ihid_of->supplies),
				    ihid_of->supplies);
	if (ret) {
		dev_warn(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	if (ihid_of->post_power_delay_ms)
		msleep(ihid_of->post_power_delay_ms);

	return 0;
}

static void i2c_hid_of_power_down(struct i2chid_ops *ops)
{
	struct i2c_hid_of *ihid_of = container_of(ops, struct i2c_hid_of, ops);

	regulator_bulk_disable(ARRAY_SIZE(ihid_of->supplies),
			       ihid_of->supplies);
}

static int i2c_hid_of_probe(struct i2c_client *client,
			    const struct i2c_device_id *dev_id)
{
	struct device *dev = &client->dev;
	struct i2c_hid_of *ihid_of;
	u16 hid_descriptor_address;
	int ret;
	u32 val;

	ihid_of = devm_kzalloc(&client->dev, sizeof(*ihid_of), GFP_KERNEL);
	if (!ihid_of)
		return -ENOMEM;

	ihid_of->ops.power_up = i2c_hid_of_power_up;
	ihid_of->ops.power_down = i2c_hid_of_power_down;

	ret = of_property_read_u32(dev->of_node, "hid-descr-addr", &val);
	if (ret) {
		dev_err(&client->dev, "HID register address not provided\n");
		return -ENODEV;
	}
	if (val >> 16) {
		dev_err(&client->dev, "Bad HID register address: 0x%08x\n",
			val);
		return -EINVAL;
	}
	hid_descriptor_address = val;

	if (!device_property_read_u32(&client->dev, "post-power-on-delay-ms",
				      &val))
		ihid_of->post_power_delay_ms = val;

	ihid_of->supplies[0].supply = "vdd";
	ihid_of->supplies[1].supply = "vddl";
	ret = devm_regulator_bulk_get(&client->dev,
				      ARRAY_SIZE(ihid_of->supplies),
				      ihid_of->supplies);
	if (ret)
		return ret;

	return i2c_hid_core_probe(client, &ihid_of->ops,
				  hid_descriptor_address);
}

static const struct of_device_id i2c_hid_of_match[] = {
	{ .compatible = "hid-over-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_hid_of_match);

static const struct i2c_device_id i2c_hid_of_id_table[] = {
	{ "hid", 0 },
	{ "hid-over-i2c", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, i2c_hid_of_id_table);

static struct i2c_driver i2c_hid_of_driver = {
	.driver = {
		.name	= "i2c_hid_of",
		.pm	= &i2c_hid_core_pm,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(i2c_hid_of_match),
	},

	.probe		= i2c_hid_of_probe,
	.remove		= i2c_hid_core_remove,
	.shutdown	= i2c_hid_core_shutdown,
	.id_table	= i2c_hid_of_id_table,
};

module_i2c_driver(i2c_hid_of_driver);

MODULE_DESCRIPTION("HID over I2C OF driver");
MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_LICENSE("GPL");
