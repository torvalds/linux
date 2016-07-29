/*
 * TSC2005 touchscreen driver
 *
 * Copyright (C) 2006-2010 Nokia Corporation
 * Copyright (C) 2015 QWERTY Embedded Design
 * Copyright (C) 2015 EMAC Inc.
 *
 * Based on original tsc2005.c by Lauri Leukkunen <lauri.leukkunen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/input.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include "tsc200x-core.h"

static const struct input_id tsc2005_input_id = {
	.bustype = BUS_SPI,
	.product = 2005,
};

static int tsc2005_cmd(struct device *dev, u8 cmd)
{
	u8 tx = TSC200X_CMD | TSC200X_CMD_12BIT | cmd;
	struct spi_transfer xfer = {
		.tx_buf         = &tx,
		.len            = 1,
		.bits_per_word  = 8,
	};
	struct spi_message msg;
	struct spi_device *spi = to_spi_device(dev);
	int error;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	error = spi_sync(spi, &msg);
	if (error) {
		dev_err(dev, "%s: failed, command: %x, spi error: %d\n",
			__func__, cmd, error);
		return error;
	}

	return 0;
}

static int tsc2005_probe(struct spi_device *spi)
{
	int error;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	if (!spi->max_speed_hz)
		spi->max_speed_hz = TSC2005_SPI_MAX_SPEED_HZ;

	error = spi_setup(spi);
	if (error)
		return error;

	return tsc200x_probe(&spi->dev, spi->irq, &tsc2005_input_id,
			     devm_regmap_init_spi(spi, &tsc200x_regmap_config),
			     tsc2005_cmd);
}

static int tsc2005_remove(struct spi_device *spi)
{
	return tsc200x_remove(&spi->dev);
}

static struct spi_driver tsc2005_driver = {
	.driver	= {
		.name	= "tsc2005",
		.pm	= &tsc200x_pm_ops,
	},
	.probe	= tsc2005_probe,
	.remove	= tsc2005_remove,
};
module_spi_driver(tsc2005_driver);

MODULE_AUTHOR("Michael Welling <mwelling@ieee.org>");
MODULE_DESCRIPTION("TSC2005 Touchscreen Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:tsc2005");
