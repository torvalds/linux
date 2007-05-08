/*
    i2c-isa.c - an i2c-core-like thing for ISA hardware monitoring chips
    Copyright (C) 2005  Jean Delvare <khali@linux-fr.org>

    Based on the i2c-isa pseudo-adapter from the lm_sensors project
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> 

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* This implements an i2c-core-like thing for ISA hardware monitoring
   chips. Such chips are linked to the i2c subsystem for historical
   reasons (because the early ISA hardware monitoring chips such as the
   LM78 had both an I2C and an ISA interface). They used to be
   registered with the main i2c-core, but as a first step in the
   direction of a clean separation between I2C and ISA chip drivers,
   we now have this separate core for ISA ones. It is significantly
   more simple than the real one, of course, because we don't have to
   handle multiple busses: there is only one (fake) ISA adapter.
   It is worth noting that we still rely on i2c-core for some things
   at the moment - but hopefully this won't last. */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/platform_device.h>
#include <linux/completion.h>

/* Exported by i2c-core for i2c-isa only */
extern void i2c_adapter_dev_release(struct device *dev);
extern struct class i2c_adapter_class;

static u32 isa_func(struct i2c_adapter *adapter);

/* This is the actual algorithm we define */
static const struct i2c_algorithm isa_algorithm = {
	.functionality	= isa_func,
};

/* There can only be one... */
static struct i2c_adapter isa_adapter = {
	.owner		= THIS_MODULE,
	.id		= I2C_HW_ISA,
	.class          = I2C_CLASS_HWMON,
	.algo		= &isa_algorithm,
	.name		= "ISA main adapter",
};

/* We can't do a thing... */
static u32 isa_func(struct i2c_adapter *adapter)
{
	return 0;
}


/* We implement an interface which resembles i2c_{add,del}_driver,
   but for i2c-isa drivers. We don't have to remember and handle lists
   of drivers and adapters so this is much more simple, of course. */

int i2c_isa_add_driver(struct i2c_driver *driver)
{
	int res;

	/* Add the driver to the list of i2c drivers in the driver core */
	driver->driver.bus = &i2c_bus_type;
	res = driver_register(&driver->driver);
	if (res)
		return res;
	dev_dbg(&isa_adapter.dev, "Driver %s registered\n", driver->driver.name);

	/* Now look for clients */
	res = driver->attach_adapter(&isa_adapter);
	if (res) {
		dev_dbg(&isa_adapter.dev,
			"Driver %s failed to attach adapter, unregistering\n",
			driver->driver.name);
		driver_unregister(&driver->driver);
	}
	return res;
}

int i2c_isa_del_driver(struct i2c_driver *driver)
{
	struct list_head *item, *_n;
	struct i2c_client *client;
	int res;

	/* Detach all clients belonging to this one driver */
	list_for_each_safe(item, _n, &isa_adapter.clients) {
		client = list_entry(item, struct i2c_client, list);
		if (client->driver != driver)
			continue;
		dev_dbg(&isa_adapter.dev, "Detaching client %s at 0x%x\n",
			client->name, client->addr);
		if ((res = driver->detach_client(client))) {
			dev_err(&isa_adapter.dev, "Failed, driver "
				"%s not unregistered!\n",
				driver->driver.name);
			return res;
		}
	}

	/* Get the driver off the core list */
	driver_unregister(&driver->driver);
	dev_dbg(&isa_adapter.dev, "Driver %s unregistered\n", driver->driver.name);

	return 0;
}


static int __init i2c_isa_init(void)
{
	int err;

	mutex_init(&isa_adapter.clist_lock);
	INIT_LIST_HEAD(&isa_adapter.clients);

	isa_adapter.nr = ANY_I2C_ISA_BUS;
	isa_adapter.dev.parent = &platform_bus;
	sprintf(isa_adapter.dev.bus_id, "i2c-%d", isa_adapter.nr);
	isa_adapter.dev.release = &i2c_adapter_dev_release;
	isa_adapter.dev.class = &i2c_adapter_class;
	err = device_register(&isa_adapter.dev);
	if (err) {
		printk(KERN_ERR "i2c-isa: Failed to register device\n");
		goto exit;
	}

	dev_dbg(&isa_adapter.dev, "%s registered\n", isa_adapter.name);

	return 0;

exit:
	return err;
}

static void __exit i2c_isa_exit(void)
{
#ifdef DEBUG
	struct list_head  *item, *_n;
	struct i2c_client *client = NULL;
#endif

	/* There should be no more active client */
#ifdef DEBUG
	dev_dbg(&isa_adapter.dev, "Looking for clients\n");
	list_for_each_safe(item, _n, &isa_adapter.clients) {
		client = list_entry(item, struct i2c_client, list);
		dev_err(&isa_adapter.dev, "Driver %s still has an active "
			"ISA client at 0x%x\n", client->driver->driver.name,
			client->addr);
	}
	if (client != NULL)
		return;
#endif

	/* Clean up the sysfs representation */
	dev_dbg(&isa_adapter.dev, "Unregistering from sysfs\n");
	init_completion(&isa_adapter.dev_released);
	device_unregister(&isa_adapter.dev);

	/* Wait for sysfs to drop all references */
	dev_dbg(&isa_adapter.dev, "Waiting for sysfs completion\n");
	wait_for_completion(&isa_adapter.dev_released);

	dev_dbg(&isa_adapter.dev, "%s unregistered\n", isa_adapter.name);
}

EXPORT_SYMBOL(i2c_isa_add_driver);
EXPORT_SYMBOL(i2c_isa_del_driver);

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("ISA bus access through i2c");
MODULE_LICENSE("GPL");

module_init(i2c_isa_init);
module_exit(i2c_isa_exit);
