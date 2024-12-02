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
#include <linux/gpio/consumer.h>
#include <linux/hid.h>
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
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[2];
	int post_power_delay_ms;
	int post_reset_delay_ms;
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

	gpiod_set_value_cansleep(ihid_of->reset_gpio, 0);
	if (ihid_of->post_reset_delay_ms)
		msleep(ihid_of->post_reset_delay_ms);

	return 0;
}

static void i2c_hid_of_power_down(struct i2chid_ops *ops)
{
	struct i2c_hid_of *ihid_of = container_of(ops, struct i2c_hid_of, ops);

	gpiod_set_value_cansleep(ihid_of->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ihid_of->supplies),
			       ihid_of->supplies);
}

static int i2c_hid_of_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct i2c_hid_of *ihid_of;
	u16 hid_descriptor_address;
	u32 quirks = 0;
	int ret;
	u32 val;

	ihid_of = devm_kzalloc(dev, sizeof(*ihid_of), GFP_KERNEL);
	if (!ihid_of)
		return -ENOMEM;

	ihid_of->client = client;
	ihid_of->ops.power_up = i2c_hid_of_power_up;
	ihid_of->ops.power_down = i2c_hid_of_power_down;

	ret = device_property_read_u32(dev, "hid-descr-addr", &val);
	if (ret) {
		dev_err(dev, "HID register address not provided\n");
		return -ENODEV;
	}
	if (val >> 16) {
		dev_err(dev, "Bad HID register address: 0x%08x\n", val);
		return -EINVAL;
	}
	hid_descriptor_address = val;

	if (!device_property_read_u32(dev, "post-power-on-delay-ms", &val))
		ihid_of->post_power_delay_ms = val;

	/*
	 * Note this is a kernel internal device-property set by x86 platform code,
	 * this MUST not be used in devicetree files without first adding it to
	 * the DT bindings.
	 */
	if (!device_property_read_u32(dev, "post-reset-deassert-delay-ms", &val))
		ihid_of->post_reset_delay_ms = val;

	/* Start out with reset asserted */
	ihid_of->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ihid_of->reset_gpio))
		return PTR_ERR(ihid_of->reset_gpio);

	ihid_of->supplies[0].supply = "vdd";
	ihid_of->supplies[1].supply = "vddl";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ihid_of->supplies),
				      ihid_of->supplies);
	if (ret)
		return ret;

	if (device_property_read_bool(dev, "touchscreen-inverted-x"))
		quirks |= HID_QUIRK_X_INVERT;

	if (device_property_read_bool(dev, "touchscreen-inverted-y"))
		quirks |= HID_QUIRK_Y_INVERT;

	return i2c_hid_core_probe(client, &ihid_of->ops,
				  hid_descriptor_address, quirks);
}

#ifdef CONFIG_OF
static const struct of_device_id i2c_hid_of_match[] = {
	{ .compatible = "hid-over-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_hid_of_match);
#endif

static const struct i2c_device_id i2c_hid_of_id_table[] = {
	{ "hid" },
	{ "hid-over-i2c" },
	{ }
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
