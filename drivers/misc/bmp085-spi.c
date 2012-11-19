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
#include <linux/spi/spi.h>
#include <linux/err.h>
#include "bmp085.h"

static int bmp085_spi_probe(struct spi_device *client)
{
	int err;
	struct regmap *regmap;

	client->bits_per_word = 8;
	err = spi_setup(client);
	if (err < 0) {
		dev_err(&client->dev, "spi_setup failed!\n");
		return err;
	}

	regmap = devm_regmap_init_spi(client, &bmp085_regmap_config);
	if (IS_ERR(regmap)) {
		err = PTR_ERR(regmap);
		dev_err(&client->dev, "Failed to init regmap: %d\n", err);
		return err;
	}

	return bmp085_probe(&client->dev, regmap);
}

static int bmp085_spi_remove(struct spi_device *client)
{
	return bmp085_remove(&client->dev);
}

static const struct of_device_id bmp085_of_match[] = {
	{ .compatible = "bosch,bmp085", },
	{ },
};
MODULE_DEVICE_TABLE(of, bmp085_of_match);

static const struct spi_device_id bmp085_id[] = {
	{ "bmp180", 0 },
	{ "bmp181", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, bmp085_id);

static struct spi_driver bmp085_spi_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= BMP085_NAME,
		.of_match_table = bmp085_of_match
	},
	.id_table	= bmp085_id,
	.probe		= bmp085_spi_probe,
	.remove		= bmp085_spi_remove
};

module_spi_driver(bmp085_spi_driver);

MODULE_AUTHOR("Eric Andersson <eric.andersson@unixphere.com>");
MODULE_DESCRIPTION("BMP085 SPI bus driver");
MODULE_LICENSE("GPL");
