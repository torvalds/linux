// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Avionic Design GmbH
 * Copyright (C) 2012-2013, NVIDIA Corporation
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/host1x.h>
#include <linux/of.h>
#include <linux/seq_file.h>
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
 * @driver: host1x driver containing the subdevices
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
	client->host = &device->dev;
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
	client->host = NULL;
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
		if (client->ops && client->ops->early_init) {
			err = client->ops->early_init(client);
			if (err < 0) {
				dev_err(&device->dev, "failed to early initialize %s: %d\n",
					dev_name(client->dev), err);
				goto teardown_late;
			}
		}
	}

	list_for_each_entry(client, &device->clients, list) {
		if (client->ops && client->ops->init) {
			err = client->ops->init(client);
			if (err < 0) {
				dev_err(&device->dev,
					"failed to initialize %s: %d\n",
					dev_name(client->dev), err);
				goto teardown;
			}
		}
	}

	mutex_unlock(&device->clients_lock);

	return 0;

teardown:
	list_for_each_entry_continue_reverse(client, &device->clients, list)
		if (client->ops->exit)
			client->ops->exit(client);

	/* reset client to end of list for late teardown */
	client = list_entry(&device->clients, struct host1x_client, list);

teardown_late:
	list_for_each_entry_continue_reverse(client, &device->clients, list)
		if (client->ops->late_exit)
			client->ops->late_exit(client);

	mutex_unlock(&device->clients_lock);
	return err;
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

	list_for_each_entry_reverse(client, &device->clients, list) {
		if (client->ops && client->ops->late_exit) {
			err = client->ops->late_exit(client);
			if (err < 0) {
				dev_err(&device->dev, "failed to late cleanup %s: %d\n",
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

/*
 * Note that this is really only needed for backwards compatibility
 * with libdrm, which parses this information from sysfs and will
 * fail if it can't find the OF_FULLNAME, specifically.
 */
static int host1x_device_uevent(const struct device *dev,
				struct kobj_uevent_env *env)
{
	of_device_uevent(dev->parent, env);

	return 0;
}

static int host1x_dma_configure(struct device *dev)
{
	return of_dma_configure(dev, dev->of_node, true);
}

static const struct dev_pm_ops host1x_device_pm_ops = {
	.suspend = pm_generic_suspend,
	.resume = pm_generic_resume,
	.freeze = pm_generic_freeze,
	.thaw = pm_generic_thaw,
	.poweroff = pm_generic_poweroff,
	.restore = pm_generic_restore,
};

const struct bus_type host1x_bus_type = {
	.name = "host1x",
	.match = host1x_device_match,
	.uevent = host1x_device_uevent,
	.dma_configure = host1x_dma_configure,
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
	device->dev.release = host1x_device_release;
	device->dev.bus = &host1x_bus_type;
	device->dev.parent = host1x->dev;

	of_dma_configure(&device->dev, host1x->dev->of_node, true);

	device->dev.dma_parms = &device->dma_parms;
	dma_set_max_seg_size(&device->dev, UINT_MAX);

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

static int host1x_devices_show(struct seq_file *s, void *data)
{
	struct host1x *host1x = s->private;
	struct host1x_device *device;

	mutex_lock(&host1x->devices_lock);

	list_for_each_entry(device, &host1x->devices, list) {
		struct host1x_subdev *subdev;

		seq_printf(s, "%s\n", dev_name(&device->dev));

		mutex_lock(&device->subdevs_lock);

		list_for_each_entry(subdev, &device->active, list)
			seq_printf(s, "  %pOFf: %s\n", subdev->np,
				   dev_name(subdev->client->dev));

		list_for_each_entry(subdev, &device->subdevs, list)
			seq_printf(s, "  %pOFf:\n", subdev->np);

		mutex_unlock(&device->subdevs_lock);
	}

	mutex_unlock(&host1x->devices_lock);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(host1x_devices);

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

	debugfs_create_file("devices", S_IRUGO, host1x->debugfs, host1x,
			    &host1x_devices_fops);

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
	struct host1x *host1x;

	driver_unregister(&driver->driver);

	mutex_lock(&devices_lock);

	list_for_each_entry(host1x, &devices, list)
		host1x_detach_driver(host1x, driver);

	mutex_unlock(&devices_lock);

	mutex_lock(&drivers_lock);
	list_del_init(&driver->list);
	mutex_unlock(&drivers_lock);
}
EXPORT_SYMBOL(host1x_driver_unregister);

/**
 * __host1x_client_init() - initialize a host1x client
 * @client: host1x client
 * @key: lock class key for the client-specific mutex
 */
void __host1x_client_init(struct host1x_client *client, struct lock_class_key *key)
{
	host1x_bo_cache_init(&client->cache);
	INIT_LIST_HEAD(&client->list);
	__mutex_init(&client->lock, "host1x client lock", key);
	client->usecount = 0;
}
EXPORT_SYMBOL(__host1x_client_init);

/**
 * host1x_client_exit() - uninitialize a host1x client
 * @client: host1x client
 */
void host1x_client_exit(struct host1x_client *client)
{
	mutex_destroy(&client->lock);
}
EXPORT_SYMBOL(host1x_client_exit);

/**
 * __host1x_client_register() - register a host1x client
 * @client: host1x client
 *
 * Registers a host1x client with each host1x controller instance. Note that
 * each client will only match their parent host1x controller and will only be
 * associated with that instance. Once all clients have been registered with
 * their parent host1x controller, the infrastructure will set up the logical
 * device and call host1x_device_init(), which will in turn call each client's
 * &host1x_client_ops.init implementation.
 */
int __host1x_client_register(struct host1x_client *client)
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
EXPORT_SYMBOL(__host1x_client_register);

/**
 * host1x_client_unregister() - unregister a host1x client
 * @client: host1x client
 *
 * Removes a host1x client from its host1x controller instance. If a logical
 * device has already been initialized, it will be torn down.
 */
void host1x_client_unregister(struct host1x_client *client)
{
	struct host1x_client *c;
	struct host1x *host1x;
	int err;

	mutex_lock(&devices_lock);

	list_for_each_entry(host1x, &devices, list) {
		err = host1x_del_client(host1x, client);
		if (!err) {
			mutex_unlock(&devices_lock);
			return;
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

	host1x_bo_cache_destroy(&client->cache);
}
EXPORT_SYMBOL(host1x_client_unregister);

int host1x_client_suspend(struct host1x_client *client)
{
	int err = 0;

	mutex_lock(&client->lock);

	if (client->usecount == 1) {
		if (client->ops && client->ops->suspend) {
			err = client->ops->suspend(client);
			if (err < 0)
				goto unlock;
		}
	}

	client->usecount--;
	dev_dbg(client->dev, "use count: %u\n", client->usecount);

	if (client->parent) {
		err = host1x_client_suspend(client->parent);
		if (err < 0)
			goto resume;
	}

	goto unlock;

resume:
	if (client->usecount == 0)
		if (client->ops && client->ops->resume)
			client->ops->resume(client);

	client->usecount++;
unlock:
	mutex_unlock(&client->lock);
	return err;
}
EXPORT_SYMBOL(host1x_client_suspend);

int host1x_client_resume(struct host1x_client *client)
{
	int err = 0;

	mutex_lock(&client->lock);

	if (client->parent) {
		err = host1x_client_resume(client->parent);
		if (err < 0)
			goto unlock;
	}

	if (client->usecount == 0) {
		if (client->ops && client->ops->resume) {
			err = client->ops->resume(client);
			if (err < 0)
				goto suspend;
		}
	}

	client->usecount++;
	dev_dbg(client->dev, "use count: %u\n", client->usecount);

	goto unlock;

suspend:
	if (client->parent)
		host1x_client_suspend(client->parent);
unlock:
	mutex_unlock(&client->lock);
	return err;
}
EXPORT_SYMBOL(host1x_client_resume);

struct host1x_bo_mapping *host1x_bo_pin(struct device *dev, struct host1x_bo *bo,
					enum dma_data_direction dir,
					struct host1x_bo_cache *cache)
{
	struct host1x_bo_mapping *mapping;

	if (cache) {
		mutex_lock(&cache->lock);

		list_for_each_entry(mapping, &cache->mappings, entry) {
			if (mapping->bo == bo && mapping->direction == dir) {
				kref_get(&mapping->ref);
				goto unlock;
			}
		}
	}

	mapping = bo->ops->pin(dev, bo, dir);
	if (IS_ERR(mapping))
		goto unlock;

	spin_lock(&mapping->bo->lock);
	list_add_tail(&mapping->list, &bo->mappings);
	spin_unlock(&mapping->bo->lock);

	if (cache) {
		INIT_LIST_HEAD(&mapping->entry);
		mapping->cache = cache;

		list_add_tail(&mapping->entry, &cache->mappings);

		/* bump reference count to track the copy in the cache */
		kref_get(&mapping->ref);
	}

unlock:
	if (cache)
		mutex_unlock(&cache->lock);

	return mapping;
}
EXPORT_SYMBOL(host1x_bo_pin);

static void __host1x_bo_unpin(struct kref *ref)
{
	struct host1x_bo_mapping *mapping = to_host1x_bo_mapping(ref);

	/*
	 * When the last reference of the mapping goes away, make sure to remove the mapping from
	 * the cache.
	 */
	if (mapping->cache)
		list_del(&mapping->entry);

	spin_lock(&mapping->bo->lock);
	list_del(&mapping->list);
	spin_unlock(&mapping->bo->lock);

	mapping->bo->ops->unpin(mapping);
}

void host1x_bo_unpin(struct host1x_bo_mapping *mapping)
{
	struct host1x_bo_cache *cache = mapping->cache;

	if (cache)
		mutex_lock(&cache->lock);

	kref_put(&mapping->ref, __host1x_bo_unpin);

	if (cache)
		mutex_unlock(&cache->lock);
}
EXPORT_SYMBOL(host1x_bo_unpin);
