/*
 * Hardware monitoring driver for Maxim MAX16064
 *
 * Copyright (c) 2011 Ericsson AB.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include "pmbus.h"

static struct pmbus_driver_info max16064_info = {
	.pages = 4,
	.direct[PSC_VOLTAGE_IN] = true,
	.direct[PSC_VOLTAGE_OUT] = true,
	.direct[PSC_TEMPERATURE] = true,
	.m[PSC_VOLTAGE_IN] = 19995,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = -1,
	.m[PSC_VOLTAGE_OUT] = 19995,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = -1,
	.m[PSC_TEMPERATURE] = -7612,
	.b[PSC_TEMPERATURE] = 335,
	.R[PSC_TEMPERATURE] = -3,
	.func[0] = PMBUS_HAVE_VOUT | PMBUS_HAVE_TEMP
		| PMBUS_HAVE_STATUS_VOUT | PMBUS_HAVE_STATUS_TEMP,
	.func[1] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
	.func[2] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
	.func[3] = PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT,
};

static int max16064_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	return pmbus_do_probe(client, id, &max16064_info);
}

static int max16064_remove(struct i2c_client *client)
{
	return pmbus_do_remove(client);
}

static const struct i2c_device_id max16064_id[] = {
	{"max16064", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, max16064_id);

/* This is the driver that will be inserted */
static struct i2c_driver max16064_driver = {
	.driver = {
		   .name = "max16064",
		   },
	.probe = max16064_probe,
	.remove = max16064_remove,
	.id_table = max16064_id,
};

static int __init max16064_init(void)
{
	return i2c_add_driver(&max16064_driver);
}

static void __exit max16064_exit(void)
{
	i2c_del_driver(&max16064_driver);
}

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for Maxim MAX16064");
MODULE_LICENSE("GPL");
module_init(max16064_init);
module_exit(max16064_exit);
