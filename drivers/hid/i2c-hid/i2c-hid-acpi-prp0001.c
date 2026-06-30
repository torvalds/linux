// SPDX-License-Identifier: GPL-2.0-only
/*
 * HID over I2C driver for PRP0001 devices missing hid-descr-addr
 *
 * Some devices, for example the Lenovo KaiTian N60d and Inspur CP300L3, use
 * _HID "PRP0001" with _DSD compatible "hid-over-i2c" but lack "hid-descr-addr"
 * from the _DSD. The HID descriptor address is provided only through an ACPI
 * _DSM. The TPD0 node in the DSDT shows _DSM Function 1 returning 0x20.
 *
 * Copyright (C) 2026 谢致邦 (XIE Zhibang) <Yeking@Red54.com>
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

#include "i2c-hid.h"
#include "i2c-hid-acpi.h"

static int i2c_hid_acpi_prp0001_power_up(struct i2chid_ops *ops)
{
	/* give the device time to power up */
	msleep(750);
	return 0;
}

static struct i2chid_ops i2c_hid_acpi_prp0001_ops = {
	.power_up = i2c_hid_acpi_prp0001_power_up,
	/*
	 * No .restore_sequence needed: the _DSM on these devices returns a
	 * constant (0x20) with no side effects, unlike some PNP0C50 _DSM
	 * implementations that switch the hardware between PS/2 and I2C modes.
	 */
};

static int i2c_hid_acpi_prp0001_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct acpi_device *adev;
	u16 hid_descriptor_address;
	int ret;

	/* If hid-descr-addr is present, let i2c-hid-of handle it */
	if (device_property_present(dev, "hid-descr-addr"))
		return -ENODEV;

	adev = ACPI_COMPANION(dev);
	if (!adev)
		return -ENODEV;

	ret = i2c_hid_acpi_get_descriptor(adev);
	if (ret < 0)
		return ret;
	dev_warn(dev,
		 "hid-descr-addr device property NOT found, using ACPI _DSM fallback. Contact vendor for firmware update!\n");
	hid_descriptor_address = ret;

	/*
	 * No acpi_device_fix_up_power() needed: TPD0 has no _PS0, _PS3, _PSC
	 * or _PRx methods and follows I2C bus power.
	 */
	return i2c_hid_core_probe(client, &i2c_hid_acpi_prp0001_ops,
				  hid_descriptor_address, 0);
}

static const struct of_device_id i2c_hid_acpi_prp0001_of_match[] = {
	{ .compatible = "hid-over-i2c" },
	{},
};
MODULE_DEVICE_TABLE(of, i2c_hid_acpi_prp0001_of_match);

static const struct i2c_device_id i2c_hid_acpi_prp0001_id[] = {
	{ .name = "hid-over-i2c" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, i2c_hid_acpi_prp0001_id);

static struct i2c_driver i2c_hid_acpi_prp0001_driver = {
	.driver = {
		.name	= "i2c_hid_acpi_prp0001",
		.pm	= &i2c_hid_core_pm,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		/*
		 * of_match_ptr() makes this NULL when CONFIG_OF=n, but that's
		 * fine: the I2C id_table with "hid-over-i2c" handles matching
		 * via client->name (set by acpi_set_modalias() from the _DSD
		 * compatible property).
		 */
		.of_match_table = of_match_ptr(i2c_hid_acpi_prp0001_of_match),
	},

	.probe		= i2c_hid_acpi_prp0001_probe,
	.remove		= i2c_hid_core_remove,
	.shutdown	= i2c_hid_core_shutdown,
	.id_table	= i2c_hid_acpi_prp0001_id,
};

module_i2c_driver(i2c_hid_acpi_prp0001_driver);

MODULE_DESCRIPTION("HID over I2C driver for PRP0001 devices missing hid-descr-addr");
MODULE_AUTHOR("谢致邦 (XIE Zhibang) <Yeking@Red54.com>");
MODULE_LICENSE("GPL");
