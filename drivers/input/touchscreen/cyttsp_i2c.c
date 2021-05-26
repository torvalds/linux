// SPDX-License-Identifier: GPL-2.0-only
/*
 * cyttsp_i2c.c
 * Cypress TrueTouch(TM) Standard Product (TTSP) I2C touchscreen driver.
 * For use with Cypress Txx3xx parts.
 * Supported parts include:
 * CY8CTST341
 * CY8CTMA340
 *
 * Copyright (C) 2009, 2010, 2011 Cypress Semiconductor, Inc.
 * Copyright (C) 2012 Javier Martinez Canillas <javier@dowhile0.org>
 *
 * Contact Cypress Semiconductor at www.cypress.com <ttdrivers@cypress.com>
 */

#include "cyttsp_core.h"

#include <linux/i2c.h>
#include <linux/input.h>

#define CY_I2C_DATA_SIZE	128

static const struct cyttsp_bus_ops cyttsp_i2c_bus_ops = {
	.bustype	= BUS_I2C,
	.write		= cyttsp_i2c_write_block_data,
	.read           = cyttsp_i2c_read_block_data,
};

static int cyttsp_i2c_probe(struct i2c_client *client,
				      const struct i2c_device_id *id)
{
	struct cyttsp *ts;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "I2C functionality not Supported\n");
		return -EIO;
	}

	ts = cyttsp_probe(&cyttsp_i2c_bus_ops, &client->dev, client->irq,
			  CY_I2C_DATA_SIZE);

	if (IS_ERR(ts))
		return PTR_ERR(ts);

	i2c_set_clientdata(client, ts);
	return 0;
}

static const struct i2c_device_id cyttsp_i2c_id[] = {
	{ CY_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, cyttsp_i2c_id);

static const struct of_device_id cyttsp_of_i2c_match[] = {
	{ .compatible = "cypress,cy8ctma340", },
	{ .compatible = "cypress,cy8ctst341", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, cyttsp_of_i2c_match);

static struct i2c_driver cyttsp_i2c_driver = {
	.driver = {
		.name	= CY_I2C_NAME,
		.pm	= &cyttsp_pm_ops,
		.of_match_table = cyttsp_of_i2c_match,
	},
	.probe		= cyttsp_i2c_probe,
	.id_table	= cyttsp_i2c_id,
};

module_i2c_driver(cyttsp_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard Product (TTSP) I2C driver");
MODULE_AUTHOR("Cypress");
