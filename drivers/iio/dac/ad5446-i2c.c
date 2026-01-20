// SPDX-License-Identifier: GPL-2.0
/*
 * AD5446 SPI I2C driver
 *
 * Copyright 2025 Analog Devices Inc.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/i2c.h>

#include <asm/byteorder.h>

#include "ad5446.h"

static int ad5622_write(struct ad5446_state *st, unsigned int val)
{
	struct i2c_client *client = to_i2c_client(st->dev);
	int ret;

	st->d16 = cpu_to_be16(val);

	ret = i2c_master_send_dmasafe(client, (char *)&st->d16, sizeof(st->d16));
	if (ret < 0)
		return ret;
	if (ret != sizeof(st->d16))
		return -EIO;

	return 0;
}

static int ad5446_i2c_probe(struct i2c_client *i2c)
{
	const struct i2c_device_id *id = i2c_client_get_device_id(i2c);
	const struct ad5446_chip_info *chip_info;

	chip_info = i2c_get_match_data(i2c);
	if (!chip_info)
		return -ENODEV;

	return ad5446_probe(&i2c->dev, id->name, chip_info);
}

/*
 * ad5446_supported_i2c_device_ids:
 * The AD5620/40/60 parts are available in different fixed internal reference
 * voltage options. The actual part numbers may look differently
 * (and a bit cryptic), however this style is used to make clear which
 * parts are supported here.
 */

static const struct ad5446_chip_info ad5602_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(8, 16, 4),
	.write = ad5622_write,
};

static const struct ad5446_chip_info ad5612_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(10, 16, 2),
	.write = ad5622_write,
};

static const struct ad5446_chip_info ad5622_chip_info = {
	.channel = AD5446_CHANNEL_POWERDOWN(12, 16, 0),
	.write = ad5622_write,
};

static const struct i2c_device_id ad5446_i2c_ids[] = {
	{"ad5301", (kernel_ulong_t)&ad5602_chip_info},
	{"ad5311", (kernel_ulong_t)&ad5612_chip_info},
	{"ad5321", (kernel_ulong_t)&ad5622_chip_info},
	{"ad5602", (kernel_ulong_t)&ad5602_chip_info},
	{"ad5612", (kernel_ulong_t)&ad5612_chip_info},
	{"ad5622", (kernel_ulong_t)&ad5622_chip_info},
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad5446_i2c_ids);

static const struct of_device_id ad5446_i2c_of_ids[] = {
	{ .compatible = "adi,ad5301", .data = &ad5602_chip_info },
	{ .compatible = "adi,ad5311", .data = &ad5612_chip_info },
	{ .compatible = "adi,ad5321", .data = &ad5622_chip_info },
	{ .compatible = "adi,ad5602", .data = &ad5602_chip_info },
	{ .compatible = "adi,ad5612", .data = &ad5612_chip_info },
	{ .compatible = "adi,ad5622", .data = &ad5622_chip_info },
	{ }
};
MODULE_DEVICE_TABLE(OF, ad5446_i2c_of_ids);

static struct i2c_driver ad5446_i2c_driver = {
	.driver = {
		.name	= "ad5446",
		.of_match_table = ad5446_i2c_of_ids,
	},
	.probe = ad5446_i2c_probe,
	.id_table = ad5446_i2c_ids,
};
module_i2c_driver(ad5446_i2c_driver);

MODULE_AUTHOR("Nuno Sá <nuno.sa@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD5622 and similar I2C DACs");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_AD5446");
