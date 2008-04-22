/* i2c-core.c - a device driver for the iic-bus interface		     */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 1995-99 Simon G. Vogl

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
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.		     */
/* ------------------------------------------------------------------------- */

/* With some changes from Kyösti Mälkki <kmalkki@cc.hut.fi>.
   All SMBus-related things are written by Frodo Looijaard <frodol@dds.nl>
   SMBus 2.0 support by Mark Studebaker <mdsxyz123@yahoo.com> and
   Jean Delvare <khali@linux-fr.org> */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/idr.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/hardirq.h>
#include <linux/irqflags.h>
#include <linux/semaphore.h>
#include <asm/uaccess.h>

#include "i2c-core.h"


static DEFINE_MUTEX(core_lock);
static DEFINE_IDR(i2c_adapter_idr);

#define is_newstyle_driver(d) ((d)->probe || (d)->remove)

/* ------------------------------------------------------------------------- */

static int i2c_device_match(struct device *dev, struct device_driver *drv)
{
	struct i2c_client	*client = to_i2c_client(dev);
	struct i2c_driver	*driver = to_i2c_driver(drv);

	/* make legacy i2c drivers bypass driver model probing entirely;
	 * such drivers scan each i2c adapter/bus themselves.
	 */
	if (!is_newstyle_driver(driver))
		return 0;

	/* new style drivers use the same kind of driver matching policy
	 * as platform devices or SPI:  compare device and driver IDs.
	 */
	return strcmp(client->driver_name, drv->name) == 0;
}

#ifdef	CONFIG_HOTPLUG

/* uevent helps with hotplug: modprobe -q $(MODALIAS) */
static int i2c_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct i2c_client	*client = to_i2c_client(dev);

	/* by definition, legacy drivers can't hotplug */
	if (dev->driver || !client->driver_name)
		return 0;

	if (add_uevent_var(env, "MODALIAS=%s", client->driver_name))
		return -ENOMEM;
	dev_dbg(dev, "uevent\n");
	return 0;
}

#else
#define i2c_device_uevent	NULL
#endif	/* CONFIG_HOTPLUG */

static int i2c_device_probe(struct device *dev)
{
	struct i2c_client	*client = to_i2c_client(dev);
	struct i2c_driver	*driver = to_i2c_driver(dev->driver);
	int status;

	if (!driver->probe)
		return -ENODEV;
	client->driver = driver;
	dev_dbg(dev, "probe\n");
	status = driver->probe(client);
	if (status)
		client->driver = NULL;
	return status;
}

static int i2c_device_remove(struct device *dev)
{
	struct i2c_client	*client = to_i2c_client(dev);
	struct i2c_driver	*driver;
	int			status;

	if (!dev->driver)
		return 0;

	driver = to_i2c_driver(dev->driver);
	if (driver->remove) {
		dev_dbg(dev, "remove\n");
		status = driver->remove(client);
	} else {
		dev->driver = NULL;
		status = 0;
	}
	if (status == 0)
		client->driver = NULL;
	return status;
}

static void i2c_device_shutdown(struct device *dev)
{
	struct i2c_driver *driver;

	if (!dev->driver)
		return;
	driver = to_i2c_driver(dev->driver);
	if (driver->shutdown)
		driver->shutdown(to_i2c_client(dev));
}

static int i2c_device_suspend(struct device * dev, pm_message_t mesg)
{
	struct i2c_driver *driver;

	if (!dev->driver)
		return 0;
	driver = to_i2c_driver(dev->driver);
	if (!driver->suspend)
		return 0;
	return driver->suspend(to_i2c_client(dev), mesg);
}

static int i2c_device_resume(struct device * dev)
{
	struct i2c_driver *driver;

	if (!dev->driver)
		return 0;
	driver = to_i2c_driver(dev->driver);
	if (!driver->resume)
		return 0;
	return driver->resume(to_i2c_client(dev));
}

static void i2c_client_release(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	complete(&client->released);
}

static void i2c_client_dev_release(struct device *dev)
{
	kfree(to_i2c_client(dev));
}

static ssize_t show_client_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%s\n", client->name);
}

static ssize_t show_modalias(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return client->driver_name
		? sprintf(buf, "%s\n", client->driver_name)
		: 0;
}

static struct device_attribute i2c_dev_attrs[] = {
	__ATTR(name, S_IRUGO, show_client_name, NULL),
	/* modalias helps coldplug:  modprobe $(cat .../modalias) */
	__ATTR(modalias, S_IRUGO, show_modalias, NULL),
	{ },
};

static struct bus_type i2c_bus_type = {
	.name		= "i2c",
	.dev_attrs	= i2c_dev_attrs,
	.match		= i2c_device_match,
	.uevent		= i2c_device_uevent,
	.probe		= i2c_device_probe,
	.remove		= i2c_device_remove,
	.shutdown	= i2c_device_shutdown,
	.suspend	= i2c_device_suspend,
	.resume		= i2c_device_resume,
};


/**
 * i2c_verify_client - return parameter as i2c_client, or NULL
 * @dev: device, probably from some driver model iterator
 *
 * When traversing the driver model tree, perhaps using driver model
 * iterators like @device_for_each_child(), you can't assume very much
 * about the nodes you find.  Use this function to avoid oopses caused
 * by wrongly treating some non-I2C device as an i2c_client.
 */
struct i2c_client *i2c_verify_client(struct device *dev)
{
	return (dev->bus == &i2c_bus_type)
			? to_i2c_client(dev)
			: NULL;
}
EXPORT_SYMBOL(i2c_verify_client);


/**
 * i2c_new_device - instantiate an i2c device for use with a new style driver
 * @adap: the adapter managing the device
 * @info: describes one I2C device; bus_num is ignored
 * Context: can sleep
 *
 * Create a device to work with a new style i2c driver, where binding is
 * handled through driver model probe()/remove() methods.  This call is not
 * appropriate for use by mainboad initialization logic, which usually runs
 * during an arch_initcall() long before any i2c_adapter could exist.
 *
 * This returns the new i2c client, which may be saved for later use with
 * i2c_unregister_device(); or NULL to indicate an error.
 */
struct i2c_client *
i2c_new_device(struct i2c_adapter *adap, struct i2c_board_info const *info)
{
	struct i2c_client	*client;
	int			status;

	client = kzalloc(sizeof *client, GFP_KERNEL);
	if (!client)
		return NULL;

	client->adapter = adap;

	client->dev.platform_data = info->platform_data;
	device_init_wakeup(&client->dev, info->flags & I2C_CLIENT_WAKE);

	client->flags = info->flags & ~I2C_CLIENT_WAKE;
	client->addr = info->addr;
	client->irq = info->irq;

	strlcpy(client->driver_name, info->driver_name,
		sizeof(client->driver_name));
	strlcpy(client->name, info->type, sizeof(client->name));

	/* a new style driver may be bound to this device when we
	 * return from this function, or any later moment (e.g. maybe
	 * hotplugging will load the driver module).  and the device
	 * refcount model is the standard driver model one.
	 */
	status = i2c_attach_client(client);
	if (status < 0) {
		kfree(client);
		client = NULL;
	}
	return client;
}
EXPORT_SYMBOL_GPL(i2c_new_device);


/**
 * i2c_unregister_device - reverse effect of i2c_new_device()
 * @client: value returned from i2c_new_device()
 * Context: can sleep
 */
void i2c_unregister_device(struct i2c_client *client)
{
	struct i2c_adapter	*adapter = client->adapter;
	struct i2c_driver	*driver = client->driver;

	if (driver && !is_newstyle_driver(driver)) {
		dev_err(&client->dev, "can't unregister devices "
			"with legacy drivers\n");
		WARN_ON(1);
		return;
	}

	mutex_lock(&adapter->clist_lock);
	list_del(&client->list);
	mutex_unlock(&adapter->clist_lock);

	device_unregister(&client->dev);
}
EXPORT_SYMBOL_GPL(i2c_unregister_device);


static int dummy_nop(struct i2c_client *client)
{
	return 0;
}

static struct i2c_driver dummy_driver = {
	.driver.name	= "dummy",
	.probe		= dummy_nop,
	.remove		= dummy_nop,
};

/**
 * i2c_new_dummy - return a new i2c device bound to a dummy driver
 * @adapter: the adapter managing the device
 * @address: seven bit address to be used
 * @type: optional label used for i2c_client.name
 * Context: can sleep
 *
 * This returns an I2C client bound to the "dummy" driver, intended for use
 * with devices that consume multiple addresses.  Examples of such chips
 * include various EEPROMS (like 24c04 and 24c08 models).
 *
 * These dummy devices have two main uses.  First, most I2C and SMBus calls
 * except i2c_transfer() need a client handle; the dummy will be that handle.
 * And second, this prevents the specified address from being bound to a
 * different driver.
 *
 * This returns the new i2c client, which should be saved for later use with
 * i2c_unregister_device(); or NULL to indicate an error.
 */
struct i2c_client *
i2c_new_dummy(struct i2c_adapter *adapter, u16 address, const char *type)
{
	struct i2c_board_info info = {
		.driver_name	= "dummy",
		.addr		= address,
	};

	if (type)
		strlcpy(info.type, type, sizeof info.type);
	return i2c_new_device(adapter, &info);
}
EXPORT_SYMBOL_GPL(i2c_new_dummy);

/* ------------------------------------------------------------------------- */

/* I2C bus adapters -- one roots each I2C or SMBUS segment */

static void i2c_adapter_dev_release(struct device *dev)
{
	struct i2c_adapter *adap = to_i2c_adapter(dev);
	complete(&adap->dev_released);
}

static ssize_t
show_adapter_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_adapter *adap = to_i2c_adapter(dev);
	return sprintf(buf, "%s\n", adap->name);
}

static struct device_attribute i2c_adapter_attrs[] = {
	__ATTR(name, S_IRUGO, show_adapter_name, NULL),
	{ },
};

static struct class i2c_adapter_class = {
	.owner			= THIS_MODULE,
	.name			= "i2c-adapter",
	.dev_attrs		= i2c_adapter_attrs,
};

static void i2c_scan_static_board_info(struct i2c_adapter *adapter)
{
	struct i2c_devinfo	*devinfo;

	mutex_lock(&__i2c_board_lock);
	list_for_each_entry(devinfo, &__i2c_board_list, list) {
		if (devinfo->busnum == adapter->nr
				&& !i2c_new_device(adapter,
						&devinfo->board_info))
			printk(KERN_ERR "i2c-core: can't create i2c%d-%04x\n",
				i2c_adapter_id(adapter),
				devinfo->board_info.addr);
	}
	mutex_unlock(&__i2c_board_lock);
}

static int i2c_do_add_adapter(struct device_driver *d, void *data)
{
	struct i2c_driver *driver = to_i2c_driver(d);
	struct i2c_adapter *adap = data;

	if (driver->attach_adapter) {
		/* We ignore the return code; if it fails, too bad */
		driver->attach_adapter(adap);
	}
	return 0;
}

static int i2c_register_adapter(struct i2c_adapter *adap)
{
	int res = 0, dummy;

	mutex_init(&adap->bus_lock);
	mutex_init(&adap->clist_lock);
	INIT_LIST_HEAD(&adap->clients);

	mutex_lock(&core_lock);

	/* Add the adapter to the driver core.
	 * If the parent pointer is not set up,
	 * we add this adapter to the host bus.
	 */
	if (adap->dev.parent == NULL) {
		adap->dev.parent = &platform_bus;
		pr_debug("I2C adapter driver [%s] forgot to specify "
			 "physical device\n", adap->name);
	}
	sprintf(adap->dev.bus_id, "i2c-%d", adap->nr);
	adap->dev.release = &i2c_adapter_dev_release;
	adap->dev.class = &i2c_adapter_class;
	res = device_register(&adap->dev);
	if (res)
		goto out_list;

	dev_dbg(&adap->dev, "adapter [%s] registered\n", adap->name);

	/* create pre-declared device nodes for new-style drivers */
	if (adap->nr < __i2c_first_dynamic_bus_num)
		i2c_scan_static_board_info(adap);

	/* let legacy drivers scan this bus for matching devices */
	dummy = bus_for_each_drv(&i2c_bus_type, NULL, adap,
				 i2c_do_add_adapter);

out_unlock:
	mutex_unlock(&core_lock);
	return res;

out_list:
	idr_remove(&i2c_adapter_idr, adap->nr);
	goto out_unlock;
}

/**
 * i2c_add_adapter - declare i2c adapter, use dynamic bus number
 * @adapter: the adapter to add
 * Context: can sleep
 *
 * This routine is used to declare an I2C adapter when its bus number
 * doesn't matter.  Examples: for I2C adapters dynamically added by
 * USB links or PCI plugin cards.
 *
 * When this returns zero, a new bus number was allocated and stored
 * in adap->nr, and the specified adapter became available for clients.
 * Otherwise, a negative errno value is returned.
 */
int i2c_add_adapter(struct i2c_adapter *adapter)
{
	int	id, res = 0;

retry:
	if (idr_pre_get(&i2c_adapter_idr, GFP_KERNEL) == 0)
		return -ENOMEM;

	mutex_lock(&core_lock);
	/* "above" here means "above or equal to", sigh */
	res = idr_get_new_above(&i2c_adapter_idr, adapter,
				__i2c_first_dynamic_bus_num, &id);
	mutex_unlock(&core_lock);

	if (res < 0) {
		if (res == -EAGAIN)
			goto retry;
		return res;
	}

	adapter->nr = id;
	return i2c_register_adapter(adapter);
}
EXPORT_SYMBOL(i2c_add_adapter);

/**
 * i2c_add_numbered_adapter - declare i2c adapter, use static bus number
 * @adap: the adapter to register (with adap->nr initialized)
 * Context: can sleep
 *
 * This routine is used to declare an I2C adapter when its bus number
 * matters.  For example, use it for I2C adapters from system-on-chip CPUs,
 * or otherwise built in to the system's mainboard, and where i2c_board_info
 * is used to properly configure I2C devices.
 *
 * If no devices have pre-been declared for this bus, then be sure to
 * register the adapter before any dynamically allocated ones.  Otherwise
 * the required bus ID may not be available.
 *
 * When this returns zero, the specified adapter became available for
 * clients using the bus number provided in adap->nr.  Also, the table
 * of I2C devices pre-declared using i2c_register_board_info() is scanned,
 * and the appropriate driver model device nodes are created.  Otherwise, a
 * negative errno value is returned.
 */
int i2c_add_numbered_adapter(struct i2c_adapter *adap)
{
	int	id;
	int	status;

	if (adap->nr & ~MAX_ID_MASK)
		return -EINVAL;

retry:
	if (idr_pre_get(&i2c_adapter_idr, GFP_KERNEL) == 0)
		return -ENOMEM;

	mutex_lock(&core_lock);
	/* "above" here means "above or equal to", sigh;
	 * we need the "equal to" result to force the result
	 */
	status = idr_get_new_above(&i2c_adapter_idr, adap, adap->nr, &id);
	if (status == 0 && id != adap->nr) {
		status = -EBUSY;
		idr_remove(&i2c_adapter_idr, id);
	}
	mutex_unlock(&core_lock);
	if (status == -EAGAIN)
		goto retry;

	if (status == 0)
		status = i2c_register_adapter(adap);
	return status;
}
EXPORT_SYMBOL_GPL(i2c_add_numbered_adapter);

static int i2c_do_del_adapter(struct device_driver *d, void *data)
{
	struct i2c_driver *driver = to_i2c_driver(d);
	struct i2c_adapter *adapter = data;
	int res;

	if (!driver->detach_adapter)
		return 0;
	res = driver->detach_adapter(adapter);
	if (res)
		dev_err(&adapter->dev, "detach_adapter failed (%d) "
			"for driver [%s]\n", res, driver->driver.name);
	return res;
}

/**
 * i2c_del_adapter - unregister I2C adapter
 * @adap: the adapter being unregistered
 * Context: can sleep
 *
 * This unregisters an I2C adapter which was previously registered
 * by @i2c_add_adapter or @i2c_add_numbered_adapter.
 */
int i2c_del_adapter(struct i2c_adapter *adap)
{
	struct list_head  *item, *_n;
	struct i2c_client *client;
	int res = 0;

	mutex_lock(&core_lock);

	/* First make sure that this adapter was ever added */
	if (idr_find(&i2c_adapter_idr, adap->nr) != adap) {
		pr_debug("i2c-core: attempting to delete unregistered "
			 "adapter [%s]\n", adap->name);
		res = -EINVAL;
		goto out_unlock;
	}

	/* Tell drivers about this removal */
	res = bus_for_each_drv(&i2c_bus_type, NULL, adap,
			       i2c_do_del_adapter);
	if (res)
		goto out_unlock;

	/* detach any active clients. This must be done first, because
	 * it can fail; in which case we give up. */
	list_for_each_safe(item, _n, &adap->clients) {
		struct i2c_driver	*driver;

		client = list_entry(item, struct i2c_client, list);
		driver = client->driver;

		/* new style, follow standard driver model */
		if (!driver || is_newstyle_driver(driver)) {
			i2c_unregister_device(client);
			continue;
		}

		/* legacy drivers create and remove clients themselves */
		if ((res = driver->detach_client(client))) {
			dev_err(&adap->dev, "detach_client failed for client "
				"[%s] at address 0x%02x\n", client->name,
				client->addr);
			goto out_unlock;
		}
	}

	/* clean up the sysfs representation */
	init_completion(&adap->dev_released);
	device_unregister(&adap->dev);

	/* wait for sysfs to drop all references */
	wait_for_completion(&adap->dev_released);

	/* free bus id */
	idr_remove(&i2c_adapter_idr, adap->nr);

	dev_dbg(&adap->dev, "adapter [%s] unregistered\n", adap->name);

 out_unlock:
	mutex_unlock(&core_lock);
	return res;
}
EXPORT_SYMBOL(i2c_del_adapter);


/* ------------------------------------------------------------------------- */

/*
 * An i2c_driver is used with one or more i2c_client (device) nodes to access
 * i2c slave chips, on a bus instance associated with some i2c_adapter.  There
 * are two models for binding the driver to its device:  "new style" drivers
 * follow the standard Linux driver model and just respond to probe() calls
 * issued if the driver core sees they match(); "legacy" drivers create device
 * nodes themselves.
 */

int i2c_register_driver(struct module *owner, struct i2c_driver *driver)
{
	int res;

	/* new style driver methods can't mix with legacy ones */
	if (is_newstyle_driver(driver)) {
		if (driver->attach_adapter || driver->detach_adapter
				|| driver->detach_client) {
			printk(KERN_WARNING
					"i2c-core: driver [%s] is confused\n",
					driver->driver.name);
			return -EINVAL;
		}
	}

	/* add the driver to the list of i2c drivers in the driver core */
	driver->driver.owner = owner;
	driver->driver.bus = &i2c_bus_type;

	/* for new style drivers, when registration returns the driver core
	 * will have called probe() for all matching-but-unbound devices.
	 */
	res = driver_register(&driver->driver);
	if (res)
		return res;

	mutex_lock(&core_lock);

	pr_debug("i2c-core: driver [%s] registered\n", driver->driver.name);

	/* legacy drivers scan i2c busses directly */
	if (driver->attach_adapter) {
		struct i2c_adapter *adapter;

		down(&i2c_adapter_class.sem);
		list_for_each_entry(adapter, &i2c_adapter_class.devices,
				    dev.node) {
			driver->attach_adapter(adapter);
		}
		up(&i2c_adapter_class.sem);
	}

	mutex_unlock(&core_lock);
	return 0;
}
EXPORT_SYMBOL(i2c_register_driver);

/**
 * i2c_del_driver - unregister I2C driver
 * @driver: the driver being unregistered
 * Context: can sleep
 */
void i2c_del_driver(struct i2c_driver *driver)
{
	struct list_head   *item2, *_n;
	struct i2c_client  *client;
	struct i2c_adapter *adap;

	mutex_lock(&core_lock);

	/* new-style driver? */
	if (is_newstyle_driver(driver))
		goto unregister;

	/* Have a look at each adapter, if clients of this driver are still
	 * attached. If so, detach them to be able to kill the driver
	 * afterwards.
	 */
	down(&i2c_adapter_class.sem);
	list_for_each_entry(adap, &i2c_adapter_class.devices, dev.node) {
		if (driver->detach_adapter) {
			if (driver->detach_adapter(adap)) {
				dev_err(&adap->dev, "detach_adapter failed "
					"for driver [%s]\n",
					driver->driver.name);
			}
		} else {
			list_for_each_safe(item2, _n, &adap->clients) {
				client = list_entry(item2, struct i2c_client, list);
				if (client->driver != driver)
					continue;
				dev_dbg(&adap->dev, "detaching client [%s] "
					"at 0x%02x\n", client->name,
					client->addr);
				if (driver->detach_client(client)) {
					dev_err(&adap->dev, "detach_client "
						"failed for client [%s] at "
						"0x%02x\n", client->name,
						client->addr);
				}
			}
		}
	}
	up(&i2c_adapter_class.sem);

 unregister:
	driver_unregister(&driver->driver);
	pr_debug("i2c-core: driver [%s] unregistered\n", driver->driver.name);

	mutex_unlock(&core_lock);
}
EXPORT_SYMBOL(i2c_del_driver);

/* ------------------------------------------------------------------------- */

static int __i2c_check_addr(struct device *dev, void *addrp)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	int			addr = *(int *)addrp;

	if (client && client->addr == addr)
		return -EBUSY;
	return 0;
}

static int i2c_check_addr(struct i2c_adapter *adapter, int addr)
{
	return device_for_each_child(&adapter->dev, &addr, __i2c_check_addr);
}

int i2c_attach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int res = 0;

	client->dev.parent = &client->adapter->dev;
	client->dev.bus = &i2c_bus_type;

	if (client->driver)
		client->dev.driver = &client->driver->driver;

	if (client->driver && !is_newstyle_driver(client->driver)) {
		client->dev.release = i2c_client_release;
		client->dev.uevent_suppress = 1;
	} else
		client->dev.release = i2c_client_dev_release;

	snprintf(&client->dev.bus_id[0], sizeof(client->dev.bus_id),
		"%d-%04x", i2c_adapter_id(adapter), client->addr);
	res = device_register(&client->dev);
	if (res)
		goto out_err;

	mutex_lock(&adapter->clist_lock);
	list_add_tail(&client->list, &adapter->clients);
	mutex_unlock(&adapter->clist_lock);

	dev_dbg(&adapter->dev, "client [%s] registered with bus id %s\n",
		client->name, client->dev.bus_id);

	if (adapter->client_register)  {
		if (adapter->client_register(client)) {
			dev_dbg(&adapter->dev, "client_register "
				"failed for client [%s] at 0x%02x\n",
				client->name, client->addr);
		}
	}

	return 0;

out_err:
	dev_err(&adapter->dev, "Failed to attach i2c client %s at 0x%02x "
		"(%d)\n", client->name, client->addr, res);
	return res;
}
EXPORT_SYMBOL(i2c_attach_client);

int i2c_detach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int res = 0;

	if (adapter->client_unregister)  {
		res = adapter->client_unregister(client);
		if (res) {
			dev_err(&client->dev,
				"client_unregister [%s] failed, "
				"client not detached\n", client->name);
			goto out;
		}
	}

	mutex_lock(&adapter->clist_lock);
	list_del(&client->list);
	mutex_unlock(&adapter->clist_lock);

	init_completion(&client->released);
	device_unregister(&client->dev);
	wait_for_completion(&client->released);

 out:
	return res;
}
EXPORT_SYMBOL(i2c_detach_client);

/**
 * i2c_use_client - increments the reference count of the i2c client structure
 * @client: the client being referenced
 *
 * Each live reference to a client should be refcounted. The driver model does
 * that automatically as part of driver binding, so that most drivers don't
 * need to do this explicitly: they hold a reference until they're unbound
 * from the device.
 *
 * A pointer to the client with the incremented reference counter is returned.
 */
struct i2c_client *i2c_use_client(struct i2c_client *client)
{
	get_device(&client->dev);
	return client;
}
EXPORT_SYMBOL(i2c_use_client);

/**
 * i2c_release_client - release a use of the i2c client structure
 * @client: the client being no longer referenced
 *
 * Must be called when a user of a client is finished with it.
 */
void i2c_release_client(struct i2c_client *client)
{
	put_device(&client->dev);
}
EXPORT_SYMBOL(i2c_release_client);

struct i2c_cmd_arg {
	unsigned	cmd;
	void		*arg;
};

static int i2c_cmd(struct device *dev, void *_arg)
{
	struct i2c_client	*client = i2c_verify_client(dev);
	struct i2c_cmd_arg	*arg = _arg;

	if (client && client->driver && client->driver->command)
		client->driver->command(client, arg->cmd, arg->arg);
	return 0;
}

void i2c_clients_command(struct i2c_adapter *adap, unsigned int cmd, void *arg)
{
	struct i2c_cmd_arg	cmd_arg;

	cmd_arg.cmd = cmd;
	cmd_arg.arg = arg;
	device_for_each_child(&adap->dev, &cmd_arg, i2c_cmd);
}
EXPORT_SYMBOL(i2c_clients_command);

static int __init i2c_init(void)
{
	int retval;

	retval = bus_register(&i2c_bus_type);
	if (retval)
		return retval;
	retval = class_register(&i2c_adapter_class);
	if (retval)
		goto bus_err;
	retval = i2c_add_driver(&dummy_driver);
	if (retval)
		goto class_err;
	return 0;

class_err:
	class_unregister(&i2c_adapter_class);
bus_err:
	bus_unregister(&i2c_bus_type);
	return retval;
}

static void __exit i2c_exit(void)
{
	i2c_del_driver(&dummy_driver);
	class_unregister(&i2c_adapter_class);
	bus_unregister(&i2c_bus_type);
}

subsys_initcall(i2c_init);
module_exit(i2c_exit);

/* ----------------------------------------------------
 * the functional interface to the i2c busses.
 * ----------------------------------------------------
 */

int i2c_transfer(struct i2c_adapter * adap, struct i2c_msg *msgs, int num)
{
	int ret;

	if (adap->algo->master_xfer) {
#ifdef DEBUG
		for (ret = 0; ret < num; ret++) {
			dev_dbg(&adap->dev, "master_xfer[%d] %c, addr=0x%02x, "
				"len=%d%s\n", ret, (msgs[ret].flags & I2C_M_RD)
				? 'R' : 'W', msgs[ret].addr, msgs[ret].len,
				(msgs[ret].flags & I2C_M_RECV_LEN) ? "+" : "");
		}
#endif

		if (in_atomic() || irqs_disabled()) {
			ret = mutex_trylock(&adap->bus_lock);
			if (!ret)
				/* I2C activity is ongoing. */
				return -EAGAIN;
		} else {
			mutex_lock_nested(&adap->bus_lock, adap->level);
		}

		ret = adap->algo->master_xfer(adap,msgs,num);
		mutex_unlock(&adap->bus_lock);

		return ret;
	} else {
		dev_dbg(&adap->dev, "I2C level transfers not supported\n");
		return -ENOSYS;
	}
}
EXPORT_SYMBOL(i2c_transfer);

int i2c_master_send(struct i2c_client *client,const char *buf ,int count)
{
	int ret;
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.len = count;
	msg.buf = (char *)buf;

	ret = i2c_transfer(adap, &msg, 1);

	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	   transmitted, else error code. */
	return (ret == 1) ? count : ret;
}
EXPORT_SYMBOL(i2c_master_send);

int i2c_master_recv(struct i2c_client *client, char *buf ,int count)
{
	struct i2c_adapter *adap=client->adapter;
	struct i2c_msg msg;
	int ret;

	msg.addr = client->addr;
	msg.flags = client->flags & I2C_M_TEN;
	msg.flags |= I2C_M_RD;
	msg.len = count;
	msg.buf = buf;

	ret = i2c_transfer(adap, &msg, 1);

	/* If everything went ok (i.e. 1 msg transmitted), return #bytes
	   transmitted, else error code. */
	return (ret == 1) ? count : ret;
}
EXPORT_SYMBOL(i2c_master_recv);

/* ----------------------------------------------------
 * the i2c address scanning function
 * Will not work for 10-bit addresses!
 * ----------------------------------------------------
 */
static int i2c_probe_address(struct i2c_adapter *adapter, int addr, int kind,
			     int (*found_proc) (struct i2c_adapter *, int, int))
{
	int err;

	/* Make sure the address is valid */
	if (addr < 0x03 || addr > 0x77) {
		dev_warn(&adapter->dev, "Invalid probe address 0x%02x\n",
			 addr);
		return -EINVAL;
	}

	/* Skip if already in use */
	if (i2c_check_addr(adapter, addr))
		return 0;

	/* Make sure there is something at this address, unless forced */
	if (kind < 0) {
		if (i2c_smbus_xfer(adapter, addr, 0, 0, 0,
				   I2C_SMBUS_QUICK, NULL) < 0)
			return 0;

		/* prevent 24RF08 corruption */
		if ((addr & ~0x0f) == 0x50)
			i2c_smbus_xfer(adapter, addr, 0, 0, 0,
				       I2C_SMBUS_QUICK, NULL);
	}

	/* Finally call the custom detection function */
	err = found_proc(adapter, addr, kind);
	/* -ENODEV can be returned if there is a chip at the given address
	   but it isn't supported by this chip driver. We catch it here as
	   this isn't an error. */
	if (err == -ENODEV)
		err = 0;

	if (err)
		dev_warn(&adapter->dev, "Client creation failed at 0x%x (%d)\n",
			 addr, err);
	return err;
}

int i2c_probe(struct i2c_adapter *adapter,
	      const struct i2c_client_address_data *address_data,
	      int (*found_proc) (struct i2c_adapter *, int, int))
{
	int i, err;
	int adap_id = i2c_adapter_id(adapter);

	/* Force entries are done first, and are not affected by ignore
	   entries */
	if (address_data->forces) {
		const unsigned short * const *forces = address_data->forces;
		int kind;

		for (kind = 0; forces[kind]; kind++) {
			for (i = 0; forces[kind][i] != I2C_CLIENT_END;
			     i += 2) {
				if (forces[kind][i] == adap_id
				 || forces[kind][i] == ANY_I2C_BUS) {
					dev_dbg(&adapter->dev, "found force "
						"parameter for adapter %d, "
						"addr 0x%02x, kind %d\n",
						adap_id, forces[kind][i + 1],
						kind);
					err = i2c_probe_address(adapter,
						forces[kind][i + 1],
						kind, found_proc);
					if (err)
						return err;
				}
			}
		}
	}

	/* Stop here if we can't use SMBUS_QUICK */
	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_QUICK)) {
		if (address_data->probe[0] == I2C_CLIENT_END
		 && address_data->normal_i2c[0] == I2C_CLIENT_END)
			return 0;

		dev_warn(&adapter->dev, "SMBus Quick command not supported, "
			 "can't probe for chips\n");
		return -1;
	}

	/* Probe entries are done second, and are not affected by ignore
	   entries either */
	for (i = 0; address_data->probe[i] != I2C_CLIENT_END; i += 2) {
		if (address_data->probe[i] == adap_id
		 || address_data->probe[i] == ANY_I2C_BUS) {
			dev_dbg(&adapter->dev, "found probe parameter for "
				"adapter %d, addr 0x%02x\n", adap_id,
				address_data->probe[i + 1]);
			err = i2c_probe_address(adapter,
						address_data->probe[i + 1],
						-1, found_proc);
			if (err)
				return err;
		}
	}

	/* Normal entries are done last, unless shadowed by an ignore entry */
	for (i = 0; address_data->normal_i2c[i] != I2C_CLIENT_END; i += 1) {
		int j, ignore;

		ignore = 0;
		for (j = 0; address_data->ignore[j] != I2C_CLIENT_END;
		     j += 2) {
			if ((address_data->ignore[j] == adap_id ||
			     address_data->ignore[j] == ANY_I2C_BUS)
			 && address_data->ignore[j + 1]
			    == address_data->normal_i2c[i]) {
				dev_dbg(&adapter->dev, "found ignore "
					"parameter for adapter %d, "
					"addr 0x%02x\n", adap_id,
					address_data->ignore[j + 1]);
				ignore = 1;
				break;
			}
		}
		if (ignore)
			continue;

		dev_dbg(&adapter->dev, "found normal entry for adapter %d, "
			"addr 0x%02x\n", adap_id,
			address_data->normal_i2c[i]);
		err = i2c_probe_address(adapter, address_data->normal_i2c[i],
					-1, found_proc);
		if (err)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL(i2c_probe);

struct i2c_client *
i2c_new_probed_device(struct i2c_adapter *adap,
		      struct i2c_board_info *info,
		      unsigned short const *addr_list)
{
	int i;

	/* Stop here if the bus doesn't support probing */
	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_READ_BYTE)) {
		dev_err(&adap->dev, "Probing not supported\n");
		return NULL;
	}

	for (i = 0; addr_list[i] != I2C_CLIENT_END; i++) {
		/* Check address validity */
		if (addr_list[i] < 0x03 || addr_list[i] > 0x77) {
			dev_warn(&adap->dev, "Invalid 7-bit address "
				 "0x%02x\n", addr_list[i]);
			continue;
		}

		/* Check address availability */
		if (i2c_check_addr(adap, addr_list[i])) {
			dev_dbg(&adap->dev, "Address 0x%02x already in "
				"use, not probing\n", addr_list[i]);
			continue;
		}

		/* Test address responsiveness
		   The default probe method is a quick write, but it is known
		   to corrupt the 24RF08 EEPROMs due to a state machine bug,
		   and could also irreversibly write-protect some EEPROMs, so
		   for address ranges 0x30-0x37 and 0x50-0x5f, we use a byte
		   read instead. Also, some bus drivers don't implement
		   quick write, so we fallback to a byte read it that case
		   too. */
		if ((addr_list[i] & ~0x07) == 0x30
		 || (addr_list[i] & ~0x0f) == 0x50
		 || !i2c_check_functionality(adap, I2C_FUNC_SMBUS_QUICK)) {
			if (i2c_smbus_xfer(adap, addr_list[i], 0,
					   I2C_SMBUS_READ, 0,
					   I2C_SMBUS_BYTE, NULL) >= 0)
				break;
		} else {
			if (i2c_smbus_xfer(adap, addr_list[i], 0,
					   I2C_SMBUS_WRITE, 0,
					   I2C_SMBUS_QUICK, NULL) >= 0)
				break;
		}
	}

	if (addr_list[i] == I2C_CLIENT_END) {
		dev_dbg(&adap->dev, "Probing failed, no device found\n");
		return NULL;
	}

	info->addr = addr_list[i];
	return i2c_new_device(adap, info);
}
EXPORT_SYMBOL_GPL(i2c_new_probed_device);

struct i2c_adapter* i2c_get_adapter(int id)
{
	struct i2c_adapter *adapter;

	mutex_lock(&core_lock);
	adapter = (struct i2c_adapter *)idr_find(&i2c_adapter_idr, id);
	if (adapter && !try_module_get(adapter->owner))
		adapter = NULL;

	mutex_unlock(&core_lock);
	return adapter;
}
EXPORT_SYMBOL(i2c_get_adapter);

void i2c_put_adapter(struct i2c_adapter *adap)
{
	module_put(adap->owner);
}
EXPORT_SYMBOL(i2c_put_adapter);

/* The SMBus parts */

#define POLY    (0x1070U << 3)
static u8
crc8(u16 data)
{
	int i;

	for(i = 0; i < 8; i++) {
		if (data & 0x8000)
			data = data ^ POLY;
		data = data << 1;
	}
	return (u8)(data >> 8);
}

/* Incremental CRC8 over count bytes in the array pointed to by p */
static u8 i2c_smbus_pec(u8 crc, u8 *p, size_t count)
{
	int i;

	for(i = 0; i < count; i++)
		crc = crc8((crc ^ p[i]) << 8);
	return crc;
}

/* Assume a 7-bit address, which is reasonable for SMBus */
static u8 i2c_smbus_msg_pec(u8 pec, struct i2c_msg *msg)
{
	/* The address will be sent first */
	u8 addr = (msg->addr << 1) | !!(msg->flags & I2C_M_RD);
	pec = i2c_smbus_pec(pec, &addr, 1);

	/* The data buffer follows */
	return i2c_smbus_pec(pec, msg->buf, msg->len);
}

/* Used for write only transactions */
static inline void i2c_smbus_add_pec(struct i2c_msg *msg)
{
	msg->buf[msg->len] = i2c_smbus_msg_pec(0, msg);
	msg->len++;
}

/* Return <0 on CRC error
   If there was a write before this read (most cases) we need to take the
   partial CRC from the write part into account.
   Note that this function does modify the message (we need to decrease the
   message length to hide the CRC byte from the caller). */
static int i2c_smbus_check_pec(u8 cpec, struct i2c_msg *msg)
{
	u8 rpec = msg->buf[--msg->len];
	cpec = i2c_smbus_msg_pec(cpec, msg);

	if (rpec != cpec) {
		pr_debug("i2c-core: Bad PEC 0x%02x vs. 0x%02x\n",
			rpec, cpec);
		return -1;
	}
	return 0;
}

s32 i2c_smbus_write_quick(struct i2c_client *client, u8 value)
{
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      value,0,I2C_SMBUS_QUICK,NULL);
}
EXPORT_SYMBOL(i2c_smbus_write_quick);

s32 i2c_smbus_read_byte(struct i2c_client *client)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,0,I2C_SMBUS_BYTE, &data))
		return -1;
	else
		return data.byte;
}
EXPORT_SYMBOL(i2c_smbus_read_byte);

s32 i2c_smbus_write_byte(struct i2c_client *client, u8 value)
{
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}
EXPORT_SYMBOL(i2c_smbus_write_byte);

s32 i2c_smbus_read_byte_data(struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,command, I2C_SMBUS_BYTE_DATA,&data))
		return -1;
	else
		return data.byte;
}
EXPORT_SYMBOL(i2c_smbus_read_byte_data);

s32 i2c_smbus_write_byte_data(struct i2c_client *client, u8 command, u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_BYTE_DATA,&data);
}
EXPORT_SYMBOL(i2c_smbus_write_byte_data);

s32 i2c_smbus_read_word_data(struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,command, I2C_SMBUS_WORD_DATA, &data))
		return -1;
	else
		return data.word;
}
EXPORT_SYMBOL(i2c_smbus_read_word_data);

s32 i2c_smbus_write_word_data(struct i2c_client *client, u8 command, u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_WORD_DATA,&data);
}
EXPORT_SYMBOL(i2c_smbus_write_word_data);

/**
 * i2c_smbus_read_block_data - SMBus block read request
 * @client: Handle to slave device
 * @command: Command byte issued to let the slave know what data should
 *	be returned
 * @values: Byte array into which data will be read; big enough to hold
 *	the data returned by the slave.  SMBus allows at most 32 bytes.
 *
 * Returns the number of bytes read in the slave's response, else a
 * negative number to indicate some kind of error.
 *
 * Note that using this function requires that the client's adapter support
 * the I2C_FUNC_SMBUS_READ_BLOCK_DATA functionality.  Not all adapter drivers
 * support this; its emulation through I2C messaging relies on a specific
 * mechanism (I2C_M_RECV_LEN) which may not be implemented.
 */
s32 i2c_smbus_read_block_data(struct i2c_client *client, u8 command,
			      u8 *values)
{
	union i2c_smbus_data data;

	if (i2c_smbus_xfer(client->adapter, client->addr, client->flags,
	                   I2C_SMBUS_READ, command,
	                   I2C_SMBUS_BLOCK_DATA, &data))
		return -1;

	memcpy(values, &data.block[1], data.block[0]);
	return data.block[0];
}
EXPORT_SYMBOL(i2c_smbus_read_block_data);

s32 i2c_smbus_write_block_data(struct i2c_client *client, u8 command,
			       u8 length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(&data.block[1], values, length);
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
			      I2C_SMBUS_WRITE,command,
			      I2C_SMBUS_BLOCK_DATA,&data);
}
EXPORT_SYMBOL(i2c_smbus_write_block_data);

/* Returns the number of read bytes */
s32 i2c_smbus_read_i2c_block_data(struct i2c_client *client, u8 command,
				  u8 length, u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_READ,command,
	                      I2C_SMBUS_I2C_BLOCK_DATA,&data))
		return -1;

	memcpy(values, &data.block[1], data.block[0]);
	return data.block[0];
}
EXPORT_SYMBOL(i2c_smbus_read_i2c_block_data);

s32 i2c_smbus_write_i2c_block_data(struct i2c_client *client, u8 command,
				   u8 length, const u8 *values)
{
	union i2c_smbus_data data;

	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	data.block[0] = length;
	memcpy(data.block + 1, values, length);
	return i2c_smbus_xfer(client->adapter, client->addr, client->flags,
			      I2C_SMBUS_WRITE, command,
			      I2C_SMBUS_I2C_BLOCK_DATA, &data);
}
EXPORT_SYMBOL(i2c_smbus_write_i2c_block_data);

/* Simulate a SMBus command using the i2c protocol
   No checking of parameters is done!  */
static s32 i2c_smbus_xfer_emulated(struct i2c_adapter * adapter, u16 addr,
                                   unsigned short flags,
                                   char read_write, u8 command, int size,
                                   union i2c_smbus_data * data)
{
	/* So we need to generate a series of msgs. In the case of writing, we
	  need to use only one message; when reading, we need two. We initialize
	  most things with sane defaults, to keep the code below somewhat
	  simpler. */
	unsigned char msgbuf0[I2C_SMBUS_BLOCK_MAX+3];
	unsigned char msgbuf1[I2C_SMBUS_BLOCK_MAX+2];
	int num = read_write == I2C_SMBUS_READ?2:1;
	struct i2c_msg msg[2] = { { addr, flags, 1, msgbuf0 },
	                          { addr, flags | I2C_M_RD, 0, msgbuf1 }
	                        };
	int i;
	u8 partial_pec = 0;

	msgbuf0[0] = command;
	switch(size) {
	case I2C_SMBUS_QUICK:
		msg[0].len = 0;
		/* Special case: The read/write field is used as data */
		msg[0].flags = flags | (read_write==I2C_SMBUS_READ)?I2C_M_RD:0;
		num = 1;
		break;
	case I2C_SMBUS_BYTE:
		if (read_write == I2C_SMBUS_READ) {
			/* Special case: only a read! */
			msg[0].flags = I2C_M_RD | flags;
			num = 1;
		}
		break;
	case I2C_SMBUS_BYTE_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 1;
		else {
			msg[0].len = 2;
			msgbuf0[1] = data->byte;
		}
		break;
	case I2C_SMBUS_WORD_DATA:
		if (read_write == I2C_SMBUS_READ)
			msg[1].len = 2;
		else {
			msg[0].len=3;
			msgbuf0[1] = data->word & 0xff;
			msgbuf0[2] = data->word >> 8;
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		num = 2; /* Special case */
		read_write = I2C_SMBUS_READ;
		msg[0].len = 3;
		msg[1].len = 2;
		msgbuf0[1] = data->word & 0xff;
		msgbuf0[2] = data->word >> 8;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			msg[1].flags |= I2C_M_RECV_LEN;
			msg[1].len = 1; /* block length will be added by
					   the underlying bus driver */
		} else {
			msg[0].len = data->block[0] + 2;
			if (msg[0].len > I2C_SMBUS_BLOCK_MAX + 2) {
				dev_err(&adapter->dev, "smbus_access called with "
				       "invalid block write size (%d)\n",
				       data->block[0]);
				return -1;
			}
			for (i = 1; i < msg[0].len; i++)
				msgbuf0[i] = data->block[i-1];
		}
		break;
	case I2C_SMBUS_BLOCK_PROC_CALL:
		num = 2; /* Another special case */
		read_write = I2C_SMBUS_READ;
		if (data->block[0] > I2C_SMBUS_BLOCK_MAX) {
			dev_err(&adapter->dev, "%s called with invalid "
				"block proc call size (%d)\n", __func__,
				data->block[0]);
			return -1;
		}
		msg[0].len = data->block[0] + 2;
		for (i = 1; i < msg[0].len; i++)
			msgbuf0[i] = data->block[i-1];
		msg[1].flags |= I2C_M_RECV_LEN;
		msg[1].len = 1; /* block length will be added by
				   the underlying bus driver */
		break;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			msg[1].len = data->block[0];
		} else {
			msg[0].len = data->block[0] + 1;
			if (msg[0].len > I2C_SMBUS_BLOCK_MAX + 1) {
				dev_err(&adapter->dev, "i2c_smbus_xfer_emulated called with "
				       "invalid block write size (%d)\n",
				       data->block[0]);
				return -1;
			}
			for (i = 1; i <= data->block[0]; i++)
				msgbuf0[i] = data->block[i];
		}
		break;
	default:
		dev_err(&adapter->dev, "smbus_access called with invalid size (%d)\n",
		       size);
		return -1;
	}

	i = ((flags & I2C_CLIENT_PEC) && size != I2C_SMBUS_QUICK
				      && size != I2C_SMBUS_I2C_BLOCK_DATA);
	if (i) {
		/* Compute PEC if first message is a write */
		if (!(msg[0].flags & I2C_M_RD)) {
			if (num == 1) /* Write only */
				i2c_smbus_add_pec(&msg[0]);
			else /* Write followed by read */
				partial_pec = i2c_smbus_msg_pec(0, &msg[0]);
		}
		/* Ask for PEC if last message is a read */
		if (msg[num-1].flags & I2C_M_RD)
			msg[num-1].len++;
	}

	if (i2c_transfer(adapter, msg, num) < 0)
		return -1;

	/* Check PEC if last message is a read */
	if (i && (msg[num-1].flags & I2C_M_RD)) {
		if (i2c_smbus_check_pec(partial_pec, &msg[num-1]) < 0)
			return -1;
	}

	if (read_write == I2C_SMBUS_READ)
		switch(size) {
			case I2C_SMBUS_BYTE:
				data->byte = msgbuf0[0];
				break;
			case I2C_SMBUS_BYTE_DATA:
				data->byte = msgbuf1[0];
				break;
			case I2C_SMBUS_WORD_DATA:
			case I2C_SMBUS_PROC_CALL:
				data->word = msgbuf1[0] | (msgbuf1[1] << 8);
				break;
			case I2C_SMBUS_I2C_BLOCK_DATA:
				for (i = 0; i < data->block[0]; i++)
					data->block[i+1] = msgbuf1[i];
				break;
			case I2C_SMBUS_BLOCK_DATA:
			case I2C_SMBUS_BLOCK_PROC_CALL:
				for (i = 0; i < msgbuf1[0] + 1; i++)
					data->block[i] = msgbuf1[i];
				break;
		}
	return 0;
}


s32 i2c_smbus_xfer(struct i2c_adapter * adapter, u16 addr, unsigned short flags,
                   char read_write, u8 command, int size,
                   union i2c_smbus_data * data)
{
	s32 res;

	flags &= I2C_M_TEN | I2C_CLIENT_PEC;

	if (adapter->algo->smbus_xfer) {
		mutex_lock(&adapter->bus_lock);
		res = adapter->algo->smbus_xfer(adapter,addr,flags,read_write,
		                                command,size,data);
		mutex_unlock(&adapter->bus_lock);
	} else
		res = i2c_smbus_xfer_emulated(adapter,addr,flags,read_write,
	                                      command,size,data);

	return res;
}
EXPORT_SYMBOL(i2c_smbus_xfer);

MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus main module");
MODULE_LICENSE("GPL");
