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

#define CY_I2C_NAME		"cyttsp-i2c"

#define CY_I2C_DATA_SIZE	128

static int cyttsp_i2c_read_block_data(struct device *dev, u8 *xfer_buf,
				      u16 addr, u8 length, void *values)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 client_addr = client->addr | ((addr >> 8) & 0x1);
	u8 addr_lo = addr & 0xFF;
	struct i2c_msg msgs[] = {
		{
			.addr = client_addr,
			.flags = 0,
			.len = 1,
			.buf = &addr_lo,
		},
		{
			.addr = client_addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = values,
		},
	};
	int retval;

	retval = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (retval < 0)
		return retval;

	return retval != ARRAY_SIZE(msgs) ? -EIO : 0;
}

static int cyttsp_i2c_write_block_data(struct device *dev, u8 *xfer_buf,
				       u16 addr, u8 length, const void *values)
{
	struct i2c_client *client = to_i2c_client(dev);
	u8 client_addr = client->addr | ((addr >> 8) & 0x1);
	u8 addr_lo = addr & 0xFF;
	struct i2c_msg msgs[] = {
		{
			.addr = client_addr,
			.flags = 0,
			.len = length + 1,
			.buf = xfer_buf,
		},
	};
	int retval;

	xfer_buf[0] = addr_lo;
	memcpy(&xfer_buf[1], values, length);

	retval = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (retval < 0)
		return retval;

	return retval != ARRAY_SIZE(msgs) ? -EIO : 0;
}

static const struct cyttsp_bus_ops cyttsp_i2c_bus_ops = {
	.bustype	= BUS_I2C,
	.write		= cyttsp_i2c_write_block_data,
	.read           = cyttsp_i2c_read_block_data,
};

static int cyttsp_i2c_probe(struct i2c_client *client)
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
	{ CY_I2C_NAME },
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
		.pm	= pm_sleep_ptr(&cyttsp_pm_ops),
		.of_match_table = cyttsp_of_i2c_match,
	},
	.probe		= cyttsp_i2c_probe,
	.id_table	= cyttsp_i2c_id,
};

module_i2c_driver(cyttsp_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cypress TrueTouch(R) Standard Product (TTSP) I2C driver");
MODULE_AUTHOR("Cypress");
