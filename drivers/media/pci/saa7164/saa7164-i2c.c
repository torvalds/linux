/*
 *  Driver for the NXP SAA7164 PCIe bridge
 *
 *  Copyright (c) 2010-2015 Steven Toth <stoth@kernellabs.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "saa7164.h"

static int i2c_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs, int num)
{
	struct saa7164_i2c *bus = i2c_adap->algo_data;
	struct saa7164_dev *dev = bus->dev;
	int i, retval = 0;

	dprintk(DBGLVL_I2C, "%s(num = %d)\n", __func__, num);

	for (i = 0 ; i < num; i++) {
		dprintk(DBGLVL_I2C, "%s(num = %d) addr = 0x%02x  len = 0x%x\n",
			__func__, num, msgs[i].addr, msgs[i].len);
		if (msgs[i].flags & I2C_M_RD) {
			retval = saa7164_api_i2c_read(bus,
				msgs[i].addr,
				0 /* reglen */,
				NULL /* reg */, msgs[i].len, msgs[i].buf);
		} else if (i + 1 < num && (msgs[i + 1].flags & I2C_M_RD) &&
			   msgs[i].addr == msgs[i + 1].addr) {
			/* write then read from same address */

			retval = saa7164_api_i2c_read(bus, msgs[i].addr,
				msgs[i].len, msgs[i].buf,
				msgs[i+1].len, msgs[i+1].buf
				);

			i++;

			if (retval < 0)
				goto err;
		} else {
			/* write */
			retval = saa7164_api_i2c_write(bus, msgs[i].addr,
				msgs[i].len, msgs[i].buf);
		}
		if (retval < 0)
			goto err;
	}
	return num;

err:
	return retval;
}

static u32 saa7164_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm saa7164_i2c_algo_template = {
	.master_xfer	= i2c_xfer,
	.functionality	= saa7164_functionality,
};

/* ----------------------------------------------------------------------- */

static struct i2c_adapter saa7164_i2c_adap_template = {
	.name              = "saa7164",
	.owner             = THIS_MODULE,
	.algo              = &saa7164_i2c_algo_template,
};

static struct i2c_client saa7164_i2c_client_template = {
	.name	= "saa7164 internal",
};

int saa7164_i2c_register(struct saa7164_i2c *bus)
{
	struct saa7164_dev *dev = bus->dev;

	dprintk(DBGLVL_I2C, "%s(bus = %d)\n", __func__, bus->nr);

	bus->i2c_adap = saa7164_i2c_adap_template;
	bus->i2c_client = saa7164_i2c_client_template;

	bus->i2c_adap.dev.parent = &dev->pci->dev;

	strlcpy(bus->i2c_adap.name, bus->dev->name,
		sizeof(bus->i2c_adap.name));

	bus->i2c_adap.algo_data = bus;
	i2c_set_adapdata(&bus->i2c_adap, bus);
	i2c_add_adapter(&bus->i2c_adap);

	bus->i2c_client.adapter = &bus->i2c_adap;

	if (0 != bus->i2c_rc)
		printk(KERN_ERR "%s: i2c bus %d register FAILED\n",
			dev->name, bus->nr);

	return bus->i2c_rc;
}

int saa7164_i2c_unregister(struct saa7164_i2c *bus)
{
	i2c_del_adapter(&bus->i2c_adap);
	return 0;
}
