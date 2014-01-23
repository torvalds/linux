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
 */
static int host1x_subdev_add(struct host1x_device *device,
			     struct device_node *np)
{
	struct host1x_subdev *subdev;

	subdev = kzalloc(sizeof(*subdev), GFP_KERNEL);
	if (!subdev)
		return -ENOMEM;

	INIT_LIST_HEAD(&subdev->list);
	subdev->np = of_node_get(np);

	mutex_lock(&device->subdevs_lock);
	list_add_tail(&subdev->list, &device->subdevs);
	mutex_unlock(&device->subdevs_lock);

	return 0;
}

/**
 * host1x_subdev_del() - remove subdevice
 */
static void host1x_subdev_del(struct host1x_subdev *subdev)
{
	list_del(&subdev->list);
	of_node_put(subdev->np);
	kfree(subdev);
}

/**
 * host1x_device_parse_dt() - scan device tree and add matching subdevices
 */
static int host1x_device_parse_dt(struct host1x_device *device)
{
	struct device_node *np;
	int err;

	for_each_child_of_node(device->dev.parent->of_node, np) {
		if (of_match_node(device->driver->subdevs, np) &&
		    of_device_is_available(np)) {
			err = host1x_subdev_add(device, np);
			if (err < 0)
				return err;
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

	/*
	 * When all subdevices have been registered, the composite device is
	 * ready to be probed.
	 */
	if (list_empty(&device->subdevs)) {
		err = device->driver->probe(device);
		if (err < 0)
			dev_err(&device->dev, "probe failed: %d\n", err);
	}
}

static void __host1x_subdev_unregister(struct host1x_device *device,
				       struct host1x_subdev *subdev)
{
	struct host1x_client *client = subdev->client;
	int err;

	/*
	 * If all subdevices have been activated, we're about to remove the
	 * first active subdevice, so unload the driver first.
	 */
	if (list_empty(&device->subdevs)) {
		err = device->driver->remove(device);
		if (err < 0)
			dev_err(&device->dev, "remove failed: %d\n", err);
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

static int host1x_register_client(struct host1x *host1x,
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

static int host1x_unregister_client(struct host1x *host1x,
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

static struct bus_type host1x_bus_type = {
	.name = "host1x",
};

int host1x_bus_init(void)
{
	return bus_register(&host1x_bus_type);
}

void host1x_bus_exit(void)
{
	bus_unregister(&host1x_bus_type);
}

static void host1x_device_release(struct device *dev)
{
	struct host1x_device *device = to_host1x_device(dev);

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

	mutex_init(&device->subdevs_lock);
	INIT_LIST_HEAD(&device->subdevs);
	INIT_LIST_HEAD(&device->active);
	mutex_init(&device->clients_lock);
	INIT_LIST_HEAD(&device->clients);
	INIT_LIST_HEAD(&device->list);
	device->driver = driver;

	device->dev.coherent_dma_mask = host1x->dev->coherent_dma_mask;
	device->dev.dma_mask = &device->dev.coherent_dma_mask;
	device->dev.release = host1x_device_release;
	dev_set_name(&device->dev, "%s", driver->name);
	device->dev.bus = &host1x_bus_type;
	device->dev.parent = host1x->dev;

	err = device_register(&device->dev);
	if (err < 0)
		return err;

	err = host1x_device_parse_dt(device);
	if (err < 0) {
		device_unregister(&device->dev);
		return err;
	}

	mutex_lock(&host1x->devices_lock);
	list_add_tail(&device->list, &host1x->devices);
	mutex_unlock(&host1x->devices_lock);

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
	device_unregister(&device->dev);
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

	mutex_unlock(&host1x->devices_lock);

	err = host1x_device_add(host1x, driver);
	if (err < 0)
		dev_err(host1x->dev, "failed to allocate device: %d\n", err);
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

int host1x_driver_register(struct host1x_driver *driver)
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

	return 0;
}
EXPORT_SYMBOL(host1x_driver_register);

void host1x_driver_unregister(struct host1x_driver *driver)
{
	mutex_lock(&drivers_lock);
	list_del_init(&driver->list);
	mutex_unlock(&drivers_lock);
}
EXPORT_SYMBOL(host1x_driver_unregister);

int host1x_client_register(struct host1x_client *client)
{
	struct host1x *host1x;
	int err;

	mutex_lock(&devices_lock);

	list_for_each_entry(host1x, &devices, list) {
		err = host1x_register_client(host1x, client);
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

int host1x_client_unregister(struct host1x_client *client)
{
	struct host1x_client *c;
	struct host1x *host1x;
	int err;

	mutex_lock(&devices_lock);

	list_for_each_entry(host1x, &devices, list) {
		err = host1x_unregister_client(host1x, client);
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
