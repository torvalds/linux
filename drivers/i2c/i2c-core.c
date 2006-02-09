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
#include <asm/uaccess.h>


static LIST_HEAD(adapters);
static LIST_HEAD(drivers);
static DECLARE_MUTEX(core_lists);
static DEFINE_IDR(i2c_adapter_idr);

/* match always succeeds, as we want the probe() to tell if we really accept this match */
static int i2c_device_match(struct device *dev, struct device_driver *drv)
{
	return 1;
}

static int i2c_bus_suspend(struct device * dev, pm_message_t state)
{
	int rc = 0;

	if (dev->driver && dev->driver->suspend)
		rc = dev->driver->suspend(dev, state);
	return rc;
}

static int i2c_bus_resume(struct device * dev)
{
	int rc = 0;
	
	if (dev->driver && dev->driver->resume)
		rc = dev->driver->resume(dev);
	return rc;
}

static int i2c_device_probe(struct device *dev)
{
	return -ENODEV;
}

static int i2c_device_remove(struct device *dev)
{
	return 0;
}

struct bus_type i2c_bus_type = {
	.name =		"i2c",
	.match =	i2c_device_match,
	.probe =	i2c_device_probe,
	.remove =	i2c_device_remove,
	.suspend =      i2c_bus_suspend,
	.resume =       i2c_bus_resume,
};

void i2c_adapter_dev_release(struct device *dev)
{
	struct i2c_adapter *adap = dev_to_i2c_adapter(dev);
	complete(&adap->dev_released);
}

struct device_driver i2c_adapter_driver = {
	.owner = THIS_MODULE,
	.name =	"i2c_adapter",
	.bus = &i2c_bus_type,
};

static void i2c_adapter_class_dev_release(struct class_device *dev)
{
	struct i2c_adapter *adap = class_dev_to_i2c_adapter(dev);
	complete(&adap->class_dev_released);
}

struct class i2c_adapter_class = {
	.owner =	THIS_MODULE,
	.name =		"i2c-adapter",
	.release =	&i2c_adapter_class_dev_release,
};

static ssize_t show_adapter_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_adapter *adap = dev_to_i2c_adapter(dev);
	return sprintf(buf, "%s\n", adap->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_adapter_name, NULL);


static void i2c_client_release(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	complete(&client->released);
}

static ssize_t show_client_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%s\n", client->name);
}

/* 
 * We can't use the DEVICE_ATTR() macro here as we want the same filename for a
 * different type of a device.  So beware if the DEVICE_ATTR() macro ever
 * changes, this definition will also have to change.
 */
static struct device_attribute dev_attr_client_name = {
	.attr	= {.name = "name", .mode = S_IRUGO, .owner = THIS_MODULE },
	.show	= &show_client_name,
};


/* ---------------------------------------------------
 * registering functions 
 * --------------------------------------------------- 
 */

/* -----
 * i2c_add_adapter is called from within the algorithm layer,
 * when a new hw adapter registers. A new device is register to be
 * available for clients.
 */
int i2c_add_adapter(struct i2c_adapter *adap)
{
	int id, res = 0;
	struct list_head   *item;
	struct i2c_driver  *driver;

	down(&core_lists);

	if (idr_pre_get(&i2c_adapter_idr, GFP_KERNEL) == 0) {
		res = -ENOMEM;
		goto out_unlock;
	}

	res = idr_get_new(&i2c_adapter_idr, adap, &id);
	if (res < 0) {
		if (res == -EAGAIN)
			res = -ENOMEM;
		goto out_unlock;
	}

	adap->nr =  id & MAX_ID_MASK;
	init_MUTEX(&adap->bus_lock);
	init_MUTEX(&adap->clist_lock);
	list_add_tail(&adap->list,&adapters);
	INIT_LIST_HEAD(&adap->clients);

	/* Add the adapter to the driver core.
	 * If the parent pointer is not set up,
	 * we add this adapter to the host bus.
	 */
	if (adap->dev.parent == NULL)
		adap->dev.parent = &platform_bus;
	sprintf(adap->dev.bus_id, "i2c-%d", adap->nr);
	adap->dev.driver = &i2c_adapter_driver;
	adap->dev.release = &i2c_adapter_dev_release;
	device_register(&adap->dev);
	device_create_file(&adap->dev, &dev_attr_name);

	/* Add this adapter to the i2c_adapter class */
	memset(&adap->class_dev, 0x00, sizeof(struct class_device));
	adap->class_dev.dev = &adap->dev;
	adap->class_dev.class = &i2c_adapter_class;
	strlcpy(adap->class_dev.class_id, adap->dev.bus_id, BUS_ID_SIZE);
	class_device_register(&adap->class_dev);

	dev_dbg(&adap->dev, "adapter [%s] registered\n", adap->name);

	/* inform drivers of new adapters */
	list_for_each(item,&drivers) {
		driver = list_entry(item, struct i2c_driver, list);
		if (driver->attach_adapter)
			/* We ignore the return code; if it fails, too bad */
			driver->attach_adapter(adap);
	}

out_unlock:
	up(&core_lists);
	return res;
}


int i2c_del_adapter(struct i2c_adapter *adap)
{
	struct list_head  *item, *_n;
	struct i2c_adapter *adap_from_list;
	struct i2c_driver *driver;
	struct i2c_client *client;
	int res = 0;

	down(&core_lists);

	/* First make sure that this adapter was ever added */
	list_for_each_entry(adap_from_list, &adapters, list) {
		if (adap_from_list == adap)
			break;
	}
	if (adap_from_list != adap) {
		pr_debug("i2c-core: attempting to delete unregistered "
			 "adapter [%s]\n", adap->name);
		res = -EINVAL;
		goto out_unlock;
	}

	list_for_each(item,&drivers) {
		driver = list_entry(item, struct i2c_driver, list);
		if (driver->detach_adapter)
			if ((res = driver->detach_adapter(adap))) {
				dev_err(&adap->dev, "detach_adapter failed "
					"for driver [%s]\n",
					driver->driver.name);
				goto out_unlock;
			}
	}

	/* detach any active clients. This must be done first, because
	 * it can fail; in which case we give up. */
	list_for_each_safe(item, _n, &adap->clients) {
		client = list_entry(item, struct i2c_client, list);

		if ((res=client->driver->detach_client(client))) {
			dev_err(&adap->dev, "detach_client failed for client "
				"[%s] at address 0x%02x\n", client->name,
				client->addr);
			goto out_unlock;
		}
	}

	/* clean up the sysfs representation */
	init_completion(&adap->dev_released);
	init_completion(&adap->class_dev_released);
	class_device_unregister(&adap->class_dev);
	device_remove_file(&adap->dev, &dev_attr_name);
	device_unregister(&adap->dev);
	list_del(&adap->list);

	/* wait for sysfs to drop all references */
	wait_for_completion(&adap->dev_released);
	wait_for_completion(&adap->class_dev_released);

	/* free dynamically allocated bus id */
	idr_remove(&i2c_adapter_idr, adap->nr);

	dev_dbg(&adap->dev, "adapter [%s] unregistered\n", adap->name);

 out_unlock:
	up(&core_lists);
	return res;
}


/* -----
 * What follows is the "upwards" interface: commands for talking to clients,
 * which implement the functions to access the physical information of the
 * chips.
 */

int i2c_register_driver(struct module *owner, struct i2c_driver *driver)
{
	struct list_head   *item;
	struct i2c_adapter *adapter;
	int res = 0;

	down(&core_lists);

	/* add the driver to the list of i2c drivers in the driver core */
	driver->driver.owner = owner;
	driver->driver.bus = &i2c_bus_type;

	res = driver_register(&driver->driver);
	if (res)
		goto out_unlock;
	
	list_add_tail(&driver->list,&drivers);
	pr_debug("i2c-core: driver [%s] registered\n", driver->driver.name);

	/* now look for instances of driver on our adapters */
	if (driver->attach_adapter) {
		list_for_each(item,&adapters) {
			adapter = list_entry(item, struct i2c_adapter, list);
			driver->attach_adapter(adapter);
		}
	}

 out_unlock:
	up(&core_lists);
	return res;
}
EXPORT_SYMBOL(i2c_register_driver);

int i2c_del_driver(struct i2c_driver *driver)
{
	struct list_head   *item1, *item2, *_n;
	struct i2c_client  *client;
	struct i2c_adapter *adap;
	
	int res = 0;

	down(&core_lists);

	/* Have a look at each adapter, if clients of this driver are still
	 * attached. If so, detach them to be able to kill the driver 
	 * afterwards.
	 */
	list_for_each(item1,&adapters) {
		adap = list_entry(item1, struct i2c_adapter, list);
		if (driver->detach_adapter) {
			if ((res = driver->detach_adapter(adap))) {
				dev_err(&adap->dev, "detach_adapter failed "
					"for driver [%s]\n",
					driver->driver.name);
				goto out_unlock;
			}
		} else {
			list_for_each_safe(item2, _n, &adap->clients) {
				client = list_entry(item2, struct i2c_client, list);
				if (client->driver != driver)
					continue;
				dev_dbg(&adap->dev, "detaching client [%s] "
					"at 0x%02x\n", client->name,
					client->addr);
				if ((res = driver->detach_client(client))) {
					dev_err(&adap->dev, "detach_client "
						"failed for client [%s] at "
						"0x%02x\n", client->name,
						client->addr);
					goto out_unlock;
				}
			}
		}
	}

	driver_unregister(&driver->driver);
	list_del(&driver->list);
	pr_debug("i2c-core: driver [%s] unregistered\n", driver->driver.name);

 out_unlock:
	up(&core_lists);
	return 0;
}

static int __i2c_check_addr(struct i2c_adapter *adapter, unsigned int addr)
{
	struct list_head   *item;
	struct i2c_client  *client;

	list_for_each(item,&adapter->clients) {
		client = list_entry(item, struct i2c_client, list);
		if (client->addr == addr)
			return -EBUSY;
	}
	return 0;
}

int i2c_check_addr(struct i2c_adapter *adapter, int addr)
{
	int rval;

	down(&adapter->clist_lock);
	rval = __i2c_check_addr(adapter, addr);
	up(&adapter->clist_lock);

	return rval;
}

int i2c_attach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;

	down(&adapter->clist_lock);
	if (__i2c_check_addr(client->adapter, client->addr)) {
		up(&adapter->clist_lock);
		return -EBUSY;
	}
	list_add_tail(&client->list,&adapter->clients);
	up(&adapter->clist_lock);
	
	if (adapter->client_register)  {
		if (adapter->client_register(client))  {
			dev_dbg(&adapter->dev, "client_register "
				"failed for client [%s] at 0x%02x\n",
				client->name, client->addr);
		}
	}

	client->usage_count = 0;

	client->dev.parent = &client->adapter->dev;
	client->dev.driver = &client->driver->driver;
	client->dev.bus = &i2c_bus_type;
	client->dev.release = &i2c_client_release;
	
	snprintf(&client->dev.bus_id[0], sizeof(client->dev.bus_id),
		"%d-%04x", i2c_adapter_id(adapter), client->addr);
	dev_dbg(&adapter->dev, "client [%s] registered with bus id %s\n",
		client->name, client->dev.bus_id);
	device_register(&client->dev);
	device_create_file(&client->dev, &dev_attr_client_name);
	
	return 0;
}


int i2c_detach_client(struct i2c_client *client)
{
	struct i2c_adapter *adapter = client->adapter;
	int res = 0;
	
	if (client->usage_count > 0) {
		dev_warn(&client->dev, "Client [%s] still busy, "
			 "can't detach\n", client->name);
		return -EBUSY;
	}

	if (adapter->client_unregister)  {
		res = adapter->client_unregister(client);
		if (res) {
			dev_err(&client->dev,
				"client_unregister [%s] failed, "
				"client not detached\n", client->name);
			goto out;
		}
	}

	down(&adapter->clist_lock);
	list_del(&client->list);
	init_completion(&client->released);
	device_remove_file(&client->dev, &dev_attr_client_name);
	device_unregister(&client->dev);
	up(&adapter->clist_lock);
	wait_for_completion(&client->released);

 out:
	return res;
}

static int i2c_inc_use_client(struct i2c_client *client)
{

	if (!try_module_get(client->driver->driver.owner))
		return -ENODEV;
	if (!try_module_get(client->adapter->owner)) {
		module_put(client->driver->driver.owner);
		return -ENODEV;
	}

	return 0;
}

static void i2c_dec_use_client(struct i2c_client *client)
{
	module_put(client->driver->driver.owner);
	module_put(client->adapter->owner);
}

int i2c_use_client(struct i2c_client *client)
{
	int ret;

	ret = i2c_inc_use_client(client);
	if (ret)
		return ret;

	client->usage_count++;

	return 0;
}

int i2c_release_client(struct i2c_client *client)
{
	if (!client->usage_count) {
		pr_debug("i2c-core: %s used one too many times\n",
			 __FUNCTION__);
		return -EPERM;
	}
	
	client->usage_count--;
	i2c_dec_use_client(client);
	
	return 0;
}

void i2c_clients_command(struct i2c_adapter *adap, unsigned int cmd, void *arg)
{
	struct list_head  *item;
	struct i2c_client *client;

	down(&adap->clist_lock);
	list_for_each(item,&adap->clients) {
		client = list_entry(item, struct i2c_client, list);
		if (!try_module_get(client->driver->driver.owner))
			continue;
		if (NULL != client->driver->command) {
			up(&adap->clist_lock);
			client->driver->command(client,cmd,arg);
			down(&adap->clist_lock);
		}
		module_put(client->driver->driver.owner);
       }
       up(&adap->clist_lock);
}

static int __init i2c_init(void)
{
	int retval;

	retval = bus_register(&i2c_bus_type);
	if (retval)
		return retval;
	retval = driver_register(&i2c_adapter_driver);
	if (retval)
		return retval;
	return class_register(&i2c_adapter_class);
}

static void __exit i2c_exit(void)
{
	class_unregister(&i2c_adapter_class);
	driver_unregister(&i2c_adapter_driver);
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
				"len=%d\n", ret, msgs[ret].flags & I2C_M_RD ?
				'R' : 'W', msgs[ret].addr, msgs[ret].len);
		}
#endif

		down(&adap->bus_lock);
		ret = adap->algo->master_xfer(adap,msgs,num);
		up(&adap->bus_lock);

		return ret;
	} else {
		dev_dbg(&adap->dev, "I2C level transfers not supported\n");
		return -ENOSYS;
	}
}

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


int i2c_control(struct i2c_client *client,
	unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct i2c_adapter *adap = client->adapter;

	dev_dbg(&client->adapter->dev, "i2c ioctl, cmd: 0x%x, arg: %#lx\n", cmd, arg);
	switch (cmd) {
		case I2C_RETRIES:
			adap->retries = arg;
			break;
		case I2C_TIMEOUT:
			adap->timeout = arg;
			break;
		default:
			if (adap->algo->algo_control!=NULL)
				ret = adap->algo->algo_control(adap,cmd,arg);
	}
	return ret;
}

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
	return (err == -ENODEV) ? 0 : err;
}

int i2c_probe(struct i2c_adapter *adapter,
	      struct i2c_client_address_data *address_data,
	      int (*found_proc) (struct i2c_adapter *, int, int))
{
	int i, err;
	int adap_id = i2c_adapter_id(adapter);

	/* Force entries are done first, and are not affected by ignore
	   entries */
	if (address_data->forces) {
		unsigned short **forces = address_data->forces;
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
			}
			ignore = 1;
			break;
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

struct i2c_adapter* i2c_get_adapter(int id)
{
	struct i2c_adapter *adapter;
	
	down(&core_lists);
	adapter = (struct i2c_adapter *)idr_find(&i2c_adapter_idr, id);
	if (adapter && !try_module_get(adapter->owner))
		adapter = NULL;

	up(&core_lists);
	return adapter;
}

void i2c_put_adapter(struct i2c_adapter *adap)
{
	module_put(adap->owner);
}

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

s32 i2c_smbus_read_byte(struct i2c_client *client)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,0,I2C_SMBUS_BYTE, &data))
		return -1;
	else
		return 0x0FF & data.byte;
}

s32 i2c_smbus_write_byte(struct i2c_client *client, u8 value)
{
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}

s32 i2c_smbus_read_byte_data(struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,command, I2C_SMBUS_BYTE_DATA,&data))
		return -1;
	else
		return 0x0FF & data.byte;
}

s32 i2c_smbus_write_byte_data(struct i2c_client *client, u8 command, u8 value)
{
	union i2c_smbus_data data;
	data.byte = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_BYTE_DATA,&data);
}

s32 i2c_smbus_read_word_data(struct i2c_client *client, u8 command)
{
	union i2c_smbus_data data;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                   I2C_SMBUS_READ,command, I2C_SMBUS_WORD_DATA, &data))
		return -1;
	else
		return 0x0FFFF & data.word;
}

s32 i2c_smbus_write_word_data(struct i2c_client *client, u8 command, u16 value)
{
	union i2c_smbus_data data;
	data.word = value;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_WRITE,command,
	                      I2C_SMBUS_WORD_DATA,&data);
}

s32 i2c_smbus_write_block_data(struct i2c_client *client, u8 command,
			       u8 length, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (length > I2C_SMBUS_BLOCK_MAX)
		length = I2C_SMBUS_BLOCK_MAX;
	for (i = 1; i <= length; i++)
		data.block[i] = values[i-1];
	data.block[0] = length;
	return i2c_smbus_xfer(client->adapter,client->addr,client->flags,
			      I2C_SMBUS_WRITE,command,
			      I2C_SMBUS_BLOCK_DATA,&data);
}

/* Returns the number of read bytes */
s32 i2c_smbus_read_i2c_block_data(struct i2c_client *client, u8 command, u8 *values)
{
	union i2c_smbus_data data;
	int i;
	if (i2c_smbus_xfer(client->adapter,client->addr,client->flags,
	                      I2C_SMBUS_READ,command,
	                      I2C_SMBUS_I2C_BLOCK_DATA,&data))
		return -1;
	else {
		for (i = 1; i <= data.block[0]; i++)
			values[i-1] = data.block[i];
		return data.block[0];
	}
}

s32 i2c_smbus_write_i2c_block_data(struct i2c_client *client, u8 command,
				   u8 length, u8 *values)
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
			msgbuf0[2] = (data->word >> 8) & 0xff;
		}
		break;
	case I2C_SMBUS_PROC_CALL:
		num = 2; /* Special case */
		read_write = I2C_SMBUS_READ;
		msg[0].len = 3;
		msg[1].len = 2;
		msgbuf0[1] = data->word & 0xff;
		msgbuf0[2] = (data->word >> 8) & 0xff;
		break;
	case I2C_SMBUS_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			dev_err(&adapter->dev, "Block read not supported "
			       "under I2C emulation!\n");
			return -1;
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
		dev_dbg(&adapter->dev, "Block process call not supported "
		       "under I2C emulation!\n");
		return -1;
	case I2C_SMBUS_I2C_BLOCK_DATA:
		if (read_write == I2C_SMBUS_READ) {
			msg[1].len = I2C_SMBUS_BLOCK_MAX;
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
				/* fixed at 32 for now */
				data->block[0] = I2C_SMBUS_BLOCK_MAX;
				for (i = 0; i < I2C_SMBUS_BLOCK_MAX; i++)
					data->block[i+1] = msgbuf1[i];
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
		down(&adapter->bus_lock);
		res = adapter->algo->smbus_xfer(adapter,addr,flags,read_write,
		                                command,size,data);
		up(&adapter->bus_lock);
	} else
		res = i2c_smbus_xfer_emulated(adapter,addr,flags,read_write,
	                                      command,size,data);

	return res;
}


/* Next four are needed by i2c-isa */
EXPORT_SYMBOL_GPL(i2c_adapter_dev_release);
EXPORT_SYMBOL_GPL(i2c_adapter_driver);
EXPORT_SYMBOL_GPL(i2c_adapter_class);
EXPORT_SYMBOL_GPL(i2c_bus_type);

EXPORT_SYMBOL(i2c_add_adapter);
EXPORT_SYMBOL(i2c_del_adapter);
EXPORT_SYMBOL(i2c_del_driver);
EXPORT_SYMBOL(i2c_attach_client);
EXPORT_SYMBOL(i2c_detach_client);
EXPORT_SYMBOL(i2c_use_client);
EXPORT_SYMBOL(i2c_release_client);
EXPORT_SYMBOL(i2c_clients_command);
EXPORT_SYMBOL(i2c_check_addr);

EXPORT_SYMBOL(i2c_master_send);
EXPORT_SYMBOL(i2c_master_recv);
EXPORT_SYMBOL(i2c_control);
EXPORT_SYMBOL(i2c_transfer);
EXPORT_SYMBOL(i2c_get_adapter);
EXPORT_SYMBOL(i2c_put_adapter);
EXPORT_SYMBOL(i2c_probe);

EXPORT_SYMBOL(i2c_smbus_xfer);
EXPORT_SYMBOL(i2c_smbus_write_quick);
EXPORT_SYMBOL(i2c_smbus_read_byte);
EXPORT_SYMBOL(i2c_smbus_write_byte);
EXPORT_SYMBOL(i2c_smbus_read_byte_data);
EXPORT_SYMBOL(i2c_smbus_write_byte_data);
EXPORT_SYMBOL(i2c_smbus_read_word_data);
EXPORT_SYMBOL(i2c_smbus_write_word_data);
EXPORT_SYMBOL(i2c_smbus_write_block_data);
EXPORT_SYMBOL(i2c_smbus_read_i2c_block_data);
EXPORT_SYMBOL(i2c_smbus_write_i2c_block_data);

MODULE_AUTHOR("Simon G. Vogl <simon@tk.uni-linz.ac.at>");
MODULE_DESCRIPTION("I2C-Bus main module");
MODULE_LICENSE("GPL");
