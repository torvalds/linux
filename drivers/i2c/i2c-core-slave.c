// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux I2C core slave support code
 *
 * Copyright (C) 2014 by Wolfram Sang <wsa@sang-engineering.com>
 */

#include <dt-bindings/i2c/i2c.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>

#include "i2c-core.h"

#define CREATE_TRACE_POINTS
#include <trace/events/i2c_slave.h>

int i2c_slave_register(struct i2c_client *client, i2c_slave_cb_t slave_cb)
{
	int ret;

	if (WARN(IS_ERR_OR_NULL(client) || !slave_cb, "insufficient data\n"))
		return -EINVAL;

	if (!(client->flags & I2C_CLIENT_SLAVE))
		dev_warn(&client->dev, "%s: client slave flag not set. You might see address collisions\n",
			 __func__);

	if (!(client->flags & I2C_CLIENT_TEN)) {
		/* Enforce stricter address checking */
		ret = i2c_check_7bit_addr_validity_strict(client->addr);
		if (ret) {
			dev_err(&client->dev, "%s: invalid address\n", __func__);
			return ret;
		}
	}

	if (!client->adapter->algo->reg_slave) {
		dev_err(&client->dev, "%s: not supported by adapter\n", __func__);
		return -EOPNOTSUPP;
	}

	client->slave_cb = slave_cb;

	i2c_lock_bus(client->adapter, I2C_LOCK_ROOT_ADAPTER);
	ret = client->adapter->algo->reg_slave(client);
	i2c_unlock_bus(client->adapter, I2C_LOCK_ROOT_ADAPTER);

	if (ret) {
		client->slave_cb = NULL;
		dev_err(&client->dev, "%s: adapter returned error %d\n", __func__, ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_slave_register);

int i2c_slave_unregister(struct i2c_client *client)
{
	int ret;

	if (IS_ERR_OR_NULL(client))
		return -EINVAL;

	if (!client->adapter->algo->unreg_slave) {
		dev_err(&client->dev, "%s: not supported by adapter\n", __func__);
		return -EOPNOTSUPP;
	}

	i2c_lock_bus(client->adapter, I2C_LOCK_ROOT_ADAPTER);
	ret = client->adapter->algo->unreg_slave(client);
	i2c_unlock_bus(client->adapter, I2C_LOCK_ROOT_ADAPTER);

	if (ret == 0)
		client->slave_cb = NULL;
	else
		dev_err(&client->dev, "%s: adapter returned error %d\n", __func__, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_slave_unregister);

int i2c_slave_event(struct i2c_client *client,
		    enum i2c_slave_event event, u8 *val)
{
	int ret = client->slave_cb(client, event, val);

	if (trace_i2c_slave_enabled())
		trace_i2c_slave(client, event, val, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(i2c_slave_event);

/**
 * i2c_detect_slave_mode - detect operation mode
 * @dev: The device owning the bus
 *
 * This checks the device nodes for an I2C slave by checking the address
 * used in the reg property. If the address match the I2C_OWN_SLAVE_ADDRESS
 * flag this means the device is configured to act as a I2C slave and it will
 * be listening at that address.
 *
 * Returns true if an I2C own slave address is detected, otherwise returns
 * false.
 */
bool i2c_detect_slave_mode(struct device *dev)
{
	if (IS_BUILTIN(CONFIG_OF) && dev->of_node) {
		u32 reg;

		for_each_child_of_node_scoped(dev->of_node, child) {
			of_property_read_u32(child, "reg", &reg);
			if (reg & I2C_OWN_SLAVE_ADDRESS)
				return true;
		}
	} else if (IS_BUILTIN(CONFIG_ACPI) && ACPI_HANDLE(dev)) {
		dev_dbg(dev, "ACPI slave is not supported yet\n");
	}
	return false;
}
EXPORT_SYMBOL_GPL(i2c_detect_slave_mode);
