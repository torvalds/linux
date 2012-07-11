/*
 * Copyright (c) 2012  Bosch Sensortec GmbH
 * Copyright (c) 2012  Unixphere AB
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include "bmp085.h"

#define BMP085_I2C_ADDRESS	0x77

static const unsigned short normal_i2c[] = { BMP085_I2C_ADDRESS,
							I2C_CLIENT_END };

static int bmp085_i2c_detect(struct i2c_client *client,
			     struct i2c_board_info *info)
{
	if (client->addr != BMP085_I2C_ADDRESS)
		return -ENODEV;

	return bmp085_detect(&client->dev);
}

static int __devinit bmp085_i2c_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	int err;
	struct regmap *regmap = devm_regmap_init_i2c(client,
						     &bmp085_regmap_config);

	if (IS_ERR(regmap)) {
		err = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to init regmap: %d\n", err);
		return err;
	}

	return bmp085_probe(&client->dev, regmap);
}

static int bmp085_i2c_remove(struct i2c_client *client)
{
	return bmp085_remove(&client->dev);
}

static const struct of_device_id bmp085_of_match[] = {
	{ .compatible = "bosch,bmp085", },
	{ },
};
MODULE_DEVICE_TABLE(of, bmp085_of_match);

static const struct i2c_device_id bmp085_id[] = {
	{ BMP085_NAME, 0 },
	{ "bmp180", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bmp085_id);

static struct i2c_driver bmp085_i2c_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= BMP085_NAME,
		.of_match_table = bmp085_of_match
	},
	.id_table	= bmp085_id,
	.probe		= bmp085_i2c_probe,
	.remove		= __devexit_p(bmp085_i2c_remove),

	.detect		= bmp085_i2c_detect,
	.address_list	= normal_i2c
};

module_i2c_driver(bmp085_i2c_driver);

MODULE_AUTHOR("Eric Andersson <eric.andersson@unixphere.com>");
MODULE_DESCRIPTION("BMP085 I2C bus driver");
MODULE_LICENSE("GPL");
