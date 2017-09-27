/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012-2013, NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/host1x.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/of_device.h>

#include "bus.h"
#include "dev.h"

static DEFINE_MUTEX(clients_lock);
static LIST_HEAD(clients);

static DEFINE_MUTEX(drivers_lock);
static LIST_HEAD(drivers);

static DEFINE_MUTEX(devices_lock);
static LIST_HEAD(devices);

struct host1x_subdev {
	struct host1x_client *client;
	struct device_node *np;
	struct list_head list;
};

/**
 * host1x_subdev_add() - add a new subdevice with an associated device node
 * @device: host1x device to add the subdevice to
 * @np: device node
 */
static int host1x_subdev_add(struct host1x_device *device,
			     struct host1x_driver *driver,
			     struct device_node *np)
{
	struct host1x_subdev *subdev;
	struct device_node *child;
	int err;

	subdev = kzalloc(sizeof(*subdev), GFP_KERNEL);
	if (!subdev)
		return -ENOMEM;

	INIT_LIST_HEAD(&subdev->list);
	subdev->np = of_node_get(np);

	mutex_lock(&device->subdevs_lock);
	list_add_tail(&subdev->list, &device->subdevs);
	mutex_unlock(&device->subdevs_lock);

	/* recursively add children */
	for_each_child_of_node(np, child) {
		if (of_match_node(driver->subdevs, child) &&
		    of_device_is_available(child)) {
			err = host1x_subdev_add(device, driver, child);
			if (err < 0) {
				/* XXX cleanup? */
				of_node_put(child);
				return err;
			}
		}
	}

	return 0;
}

/**
 * host1x_subdev_del() - remove subdevice
 * @subdev: subdevice to remove
 */
static void host1x_subdev_del(struct host1x_subdev *subdev)
{
	list_del(&subdev->list);
	of_node_put(subdev->np);
	kfree(subdev);
}

/**
 * host1x_device_parse_dt() - scan device tree and add matching subdevices
 * @device: host1x logical device
 * @driver: host1x driver
 */
static int host1x_device_parse_dt(struct host1x_device *device,
				  struct host1x_driver *driver)
{
	struct device_node *np;
	int err;

	for_each_child_of_node(device->dev.parent->of_node, np) {
		if (of_match_node(driver->subdevs, np) &&
		    of_device_is_available(np)) {
			err = host1x_subdev_add(device, driver, np);
			if (err < 0) {
				of_node_put(np);
				return err;
			}
		}
	}

	return 0;
}

static void host1x_subdev_register(struct host1x_device *device,
				   struct host1x_subdev *subdev,
				   struct host1x_client *client)
{
	int err;

	/*
	 * Move the subdevice to the list of active (registered) subdevices
	 * and associate it with a client. At the same time, associate the
	 * client with its parent device.
	 */
	mutex_lock(&device->subdevs_lock);
	mutex_lock(&device->clients_lock);
	list_move_tail(&client->list, &device->clients);
	list_move_tail(&subdev->list, &device->active);
	client->parent = &device->dev;
	subdev->client = client;
	mutex_unlock(&device->clients_lock);
	mutex_unlock(&device->subdevs_lock);

	if (list_empty(&device->subdevs)) {
		err = device_add(&device->dev);
		if (err < 0)
			dev_err(&device->dev, "failed to add: %d\n", err);
		else
			device->registered = true;
	}
}

static void __host1x_subdev_unregister(struct host1x_device *device,
				       struct host1x_subdev *subdev)
{
	struct host1x_client *client = subdev->client;

	/*
	 * If all subdevices have been activated, we're about to remove the
	 * first active subdevice, so unload the driver first.
	 */
	if (list_empty(&device->subdevs)) {
		if (device->registered) {
			device->registered = false;
			device_del(&device->dev);
		}
	}

	/*
	 * Move the subdevice back to the list of idle subdevices and remove
	 * it from list of clients.
	 */
	mutex_lock(&device->clients_lock);
	subdev->client = NULL;
	client->parent = NULL;
	list_move_tail(&subdev->list, &device->subdevs);
	/*
	 * XXX: Perhaps don't do this here, but rather explicitly remove it
	 * when the device is about to be deleted.
	 *
	 * This is somewhat complicated by the fact that this function is
	 * used to remove the subdevice when a client is unregistered but
	 * also when the composite device is about to be removed.
	 */
	list_del_init(&client->list);
	mutex_unlock(&device->clients_lock);
}

static void host1x_subdev_unregister(struct host1x_device *device,
				     struct host1x_subdev *subdev)
{
	mutex_lock(&device->subdevs_lock);
	__host1x_subdev_unregister(device, subdev);
	mutex_unlock(&device->subdevs_lock);
}

/**
 * host1x_device_init() - initialize a host1x logical device
 * @device: host1x logical device
 *
 * The driver for the host1x logical device can call this during execution of
 * its &host1x_driver.probe implementation to initialize each of its clients.
 * The client drivers access the subsystem specific driver data using the
 * &host1x_client.parent field and driver data associated with it (usually by
 * calling dev_get_drvdata()).
 */
int host1x_device_init(struct host1x_device *device)
{
	struct host1x_client *client;
	int err;

	mutex_lock(&device->clients_lock);

	list_for_each_entry(client, &device->clients, list) {
		if (client->ops && client->ops->init) {
			err = client->ops->init(client);
			if (err < 0) {
				dev_err(&device->dev,
					"failed to initialize %s: %d\n",
					dev_name(client->dev), err);
				mutex_unlock(&device->clients_lock);
				return err;
			}
		}
	}

	mutex_unlock(&device->clients_lock);

	return 0;
}
EXPORT_SYMBOL(host1x_device_init);

/**
 * host1x_device_exit() - uninitialize host1x logical device
 * @device: host1x logical device
 *
 * When the driver for a host1x logical device is unloaded, it can call this
 * function to tear down each of its clients. Typically this is done after a
 * subsystem-specific data structure is removed and the functionality can no
 * longer be used.
 */
int host1x_device_exit(struct host1x_device *device)
{
	struct host1x_client *client;
	int err;

	mutex_lock(&device->clients_lock);

	list_for_each_entry_reverse(client, &device->clients, list) {
		if (client->ops && client->ops->exit) {
			err = client->ops->exit(client);
			if (err < 0) {
				dev_err(&device->dev,
					"failed to cleanup %s: %d\n",
					dev_name(client->dev), err);
				mutex_unlock(&device->clients_lock);
				return err;
			}
		}
	}

	mutex_unlock(&device->clients_lock);

	return 0;
}
EXPORT_SYMBOL(host1x_device_exit);

static int host1x_add_client(struct host1x *host1x,
			     struct host1x_client *client)
{
	struct host1x_device *device;
	struct host1x_subdev *subdev;

	mutex_lock(&host1x->devices_lock);

	list_for_each_entry(device, &host1x->devices, list) {
		list_for_each_entry(subdev, &device->subdevs, list) {
			if (subdev->np == client->dev->of_node) {
				host1x_subdev_register(device, subdev, client);
				mutex_unlock(&host1x->devices_lock);
				return 0;
			}
		}
	}

	mutex_unlock(&host1x->devices_lock);
	return -ENODEV;
}

static int host1x_del_client(struct host1x *host1x,
			     struct host1x_client *client)
{
	struct host1x_device *device, *dt;
	struct host1x_subdev *subdev;

	mutex_lock(&host1x->devices_lock);

	list_for_each_entry_safe(device, dt, &host1x->devices, list) {
		list_for_each_entry(subdev, &device->active, list) {
			if (subdev->client == client) {
				host1x_subdev_unregister(device, subdev);
				mutex_unlock(&host1x->devices_lock);
				return 0;
			}
		}
	}

	mutex_unlock(&host1x->devices_lock);
	return -ENODEV;
}

static int host1x_device_match(struct device *dev, struct device_driver *drv)
{
	return strcmp(dev_name(dev), drv->name) == 0;
}

static const struct dev_pm_ops host1x_device_pm_ops = {
	.suspend = pm_generic_suspend,
	.resume = pm_generic_resume,
	.freeze = pm_generic_freeze,
	.thaw = pm_generic_thaw,
	.poweroff = pm_generic_poweroff,
	.restore = pm_generic_restore,
};

struct bus_type host1x_bus_type = {
	.name = "host1x",
	.match = host1x_device_match,
	.pm = &host1x_device_pm_ops,
};

static void __host1x_device_del(struct host1x_device *device)
{
	struct host1x_subdev *subdev, *sd;
	struct host1x_client *client, *cl;

	mutex_lock(&device->subdevs_lock);

	/* unregister subdevices */
	list_for_each_entry_safe(subdev, sd, &device->active, list) {
		/*
		 * host1x_subdev_unregister() will remove the client from
		 * any lists, so we'll need to manually add it back to the
		 * list of idle clients.
		 *
		 * XXX: Alternatively, perhaps don't remove the client from
		 * any lists in host1x_subdev_unregister() and instead do
		 * that explicitly from host1x_unregister_client()?
		 */
		client = subdev->client;

		__host1x_subdev_unregister(device, subdev);

		/* add the client to the list of idle clients */
		mutex_lock(&clients_lock);
		list_add_tail(&client->list, &clients);
		mutex_unlock(&clients_lock);
	}

	/* remove subdevices */
	list_for_each_entry_safe(subdev, sd, &device->subdevs, list)
		host1x_subdev_del(subdev);

	mutex_unlock(&device->subdevs_lock);

	/* move clients to idle list */
	mutex_lock(&clients_lock);
	mutex_lock(&device->clients_lock);

	list_for_each_entry_safe(client, cl, &device->clients, list)
		list_move_tail(&client->list, &clients);

	mutex_unlock(&device->clients_lock);
	mutex_unlock(&clients_lock);

	/* finally remove the device */
	list_del_init(&device->list);
}

static void host1x_device_release(struct device *dev)
{
	struct host1x_device *device = to_host1x_device(dev);

	__host1x_device_del(device);
	kfree(device);
}

static int host1x_device_add(struct host1x *host1x,
			     struct host1x_driver *driver)
{
	struct host1x_client *client, *tmp;
	struct host1x_subdev *subdev;
	struct host1x_device *device;
	int err;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;

	device_initialize(&device->dev);

	mutex_init(&device->subdevs_lock);
	INIT_LIST_HEAD(&device->subdevs);
	INIT_LIST_HEAD(&device->active);
	mutex_init(&device->clients_lock);
	INIT_LIST_HEAD(&device->clients);
	INIT_LIST_HEAD(&device->list);
	device->driver = driver;

	device->dev.coherent_dma_mask = host1x->dev->coherent_dma_mask;
	device->dev.dma_mask = &device->dev.coherent_dma_mask;
	dev_set_name(&device->dev, "%s", driver->driver.name);
	of_dma_configure(&device->dev, host1x->dev->of_node);
	device->dev.release = host1x_device_release;
	device->dev.of_node = host1x->dev->of_node;
	device->dev.bus = &host1x_bus_type;
	device->dev.parent = host1x->dev;

	err = host1x_device_parse_dt(device, driver);
	if (err < 0) {
		kfree(device);
		return err;
	}

	list_add_tail(&device->list, &host1x->devices);

	mutex_lock(&clients_lock);

	list_for_each_entry_safe(client, tmp, &clients, list) {
		list_for_each_entry(subdev, &device->subdevs, list) {
			if (subdev->np == client->dev->of_node) {
				host1x_subdev_register(device, subdev, client);
				break;
			}
		}
	}

	mutex_unlock(&clients_lock);

	return 0;
}

/*
 * Removes a device by first unregistering any subdevices and then removing
 * itself from the list of devices.
 *
 * This function must be called with the host1x->devices_lock held.
 */
static void host1x_device_del(struct host1x *host1x,
			      struct host1x_device *device)
{
	if (device->registered) {
		device->registered = false;
		device_del(&device->dev);
	}

	put_device(&device->dev);
}

static void host1x_attach_driver(struct host1x *host1x,
				 struct host1x_driver *driver)
{
	struct host1x_device *device;
	int err;

	mutex_lock(&host1x->devices_lock);

	list_for_each_entry(device, &host1x->devices, list) {
		if (device->driver == driver) {
			mutex_unlock(&host1x->devices_lock);
			return;
		}
	}

	err = host1x_device_add(host1x, driver);
	if (err < 0)
		dev_err(host1x->dev, "failed to allocate device: %d\n", err);

	mutex_unlock(&host1x->devices_lock);
}

static void host1x_detach_driver(struct host1x *host1x,
				 struct host1x_driver *driver)
{
	struct host1x_device *device, *tmp;

	mutex_lock(&host1x->devices_lock);

	list_for_each_entry_safe(device, tmp, &host1x->devices, list)
		if (device->driver == driver)
			host1x_device_del(host1x, device);

	mutex_unlock(&host1x->devices_lock);
}

/**
 * host1x_register() - register a host1x controller
 * @host1x: host1x controller
 *
 * The host1x controller driver uses this to register a host1x controller with
 * the infrastructure. Note that all Tegra SoC generations have only ever come
 * with a single host1x instance, so this function is somewhat academic.
 */
int host1x_register(struct host1x *host1x)
{
	struct host1x_driver *driver;

	mutex_lock(&devices_lock);
	list_add_tail(&host1x->list, &devices);
	mutex_unlock(&devices_lock);

	mutex_lock(&drivers_lock);

	list_for_each_entry(driver, &drivers, list)
		host1x_attach_driver(host1x, driver);

	mutex_unlock(&drivers_lock);

	return 0;
}

/**
 * host1x_unregister() - unregister a host1x controller
 * @host1x: host1x controller
 *
 * The host1x controller driver uses this to remove a host1x controller from
 * the infrastructure.
 */
int host1x_unregister(struct host1x *host1x)
{
	struct host1x_driver *driver;

	mutex_lock(&drivers_lock);

	list_for_each_entry(driver, &drivers, list)
		host1x_detach_driver(host1x, driver);

	mutex_unlock(&drivers_lock);

	mutex_lock(&devices_lock);
	list_del_init(&host1x->list);
	mutex_unlock(&devices_lock);

	return 0;
}

static int host1x_device_probe(struct device *dev)
{
	struct host1x_driver *driver = to_host1x_driver(dev->driver);
	struct host1x_device *device = to_host1x_device(dev);

	if (driver->probe)
		return driver->probe(device);

	return 0;
}

static int host1x_device_remove(struct device *dev)
{
	struct host1x_driver *driver = to_host1x_driver(dev->driver);
	struct host1x_device *device = to_host1x_device(dev);

	if (driver->remove)
		return driver->remove(device);

	return 0;
}

static void host1x_device_shutdown(struct device *dev)
{
	struct host1x_driver *driver = to_host1x_driver(dev->driver);
	struct host1x_device *device = to_host1x_device(dev);

	if (driver->shutdown)
		driver->shutdown(device);
}

/**
 * host1x_driver_register_full() - register a host1x driver
 * @driver: host1x driver
 * @owner: owner module
 *
 * Drivers for host1x logical devices call this function to register a driver
 * with the infrastructure. Note that since these drive logical devices, the
 * registration of the driver actually triggers tho logical device creation.
 * A logical device will be created for each host1x instance.
 */
int host1x_driver_register_full(struct host1x_driver *driver,
				struct module *owner)
{
	struct host1x *host1x;

	INIT_LIST_HEAD(&driver->list);

	mutex_lock(&drivers_lock);
	list_add_tail(&driver->list, &drivers);
	mutex_unlock(&drivers_lock);

	mutex_lock(&devices_lock);

	list_for_each_entry(host1x, &devices, list)
		host1x_attach_driver(host1x, driver);

	mutex_unlock(&devices_lock);

	driver->driver.bus = &host1x_bus_type;
	driver->driver.owner = owner;
	driver->driver.probe = host1x_device_probe;
	driver->driver.remove = host1x_device_remove;
	driver->driver.shutdown = host1x_device_shutdown;

	return driver_register(&driver->driver);
}
EXPORT_SYMBOL(host1x_driver_register_full);

/**
 * host1x_driver_unregister() - unregister a host1x driver
 * @driver: host1x driver
 *
 * Unbinds the driver from each of the host1x logical devices that it is
 * bound to, effectively removing the subsystem devices that they represent.
 */
void host1x_driver_unregister(struct host1x_driver *driver)
{
	driver_unregister(&driver->driver);

	mutex_lock(&drivers_lock);
	list_del_init(&driver->list);
	mutex_unlock(&drivers_lock);
}
EXPORT_SYMBOL(host1x_driver_unregister);

/**
 * host1x_client_register() - register a host1x client
 * @client: host1x client
 *
 * Registers a host1x client with each host1x controller instance. Note that
 * each client will only match their parent host1x controller and will only be
 * associated with that instance. Once all clients have been registered with
 * their parent host1x controller, the infrastructure will set up the logical
 * device and call host1x_device_init(), which will in turn call each client's
 * &host1x_client_ops.init implementation.
 */
int host1x_client_register(struct host1x_client *client)
{
	struct host1x *host1x;
	int err;

	mutex_lock(&devices_lock);

	list_for_each_entry(host1x, &devices, list) {
		err = host1x_add_client(host1x, client);
		if (!err) {
			mutex_unlock(&devices_lock);
			return 0;
		}
	}

	mutex_unlock(&devices_lock);

	mutex_lock(&clients_lock);
	list_add_tail(&client->list, &clients);
	mutex_unlock(&clients_lock);

	return 0;
}
EXPORT_SYMBOL(host1x_client_register);

/**
 * host1x_client_unregister() - unregister a host1x client
 * @client: host1x client
 *
 * Removes a host1x client from its host1x controller instance. If a logical
 * device has already been initialized, it will be torn down.
 */
int host1x_client_unregister(struct host1x_client *client)
{
	struct host1x_client *c;
	struct host1x *host1x;
	int err;

	mutex_lock(&devices_lock);

	list_for_each_entry(host1x, &devices, list) {
		err = host1x_del_client(host1x, client);
		if (!err) {
			mutex_unlock(&devices_lock);
			return 0;
		}
	}

	mutex_unlock(&devices_lock);
	mutex_lock(&clients_lock);

	list_for_each_entry(c, &clients, list) {
		if (c == client) {
			list_del_init(&c->list);
			break;
		}
	}

	mutex_unlock(&clients_lock);

	return 0;
}
EXPORT_SYMBOL(host1x_client_unregister);
