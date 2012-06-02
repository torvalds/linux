/*
 * Copyright (c) 2011 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/rmi.h>
#include <linux/types.h>
#ifdef CONFIG_RMI4_DEBUG
#include <linux/debugfs.h>
#endif
#include "rmi_driver.h"
DEFINE_MUTEX(rmi_bus_mutex);

static struct rmi_function_list {
	struct list_head list;
	struct rmi_function_handler *fh;
} rmi_supported_functions;

static struct rmi_character_driver_list {
	struct list_head list;
	struct rmi_char_driver *cd;
} rmi_character_drivers;

static atomic_t physical_device_count;

#ifdef CONFIG_RMI4_DEBUG
static struct dentry *rmi_debugfs_root;
#endif

static int rmi_bus_match(struct device *dev, struct device_driver *driver)
{
	struct rmi_driver *rmi_driver;
	struct rmi_device *rmi_dev;
	struct rmi_device_platform_data *pdata;

	rmi_driver = to_rmi_driver(driver);
	rmi_dev = to_rmi_device(dev);
	pdata = to_rmi_platform_data(rmi_dev);
	dev_dbg(dev, "%s: Matching %s.\n", __func__, pdata->sensor_name);

	if (!strcmp(pdata->driver_name, rmi_driver->driver.name)) {
		rmi_dev->driver = rmi_driver;
		dev_dbg(dev, "%s: Match %s to %s succeeded.\n", __func__,
			pdata->driver_name, rmi_driver->driver.name);
		return 1;
	}

	dev_vdbg(dev, "%s: Match %s to %s failed.\n", __func__,
		pdata->driver_name, rmi_driver->driver.name);
	return 0;
}

#ifdef CONFIG_PM
static int rmi_bus_suspend(struct device *dev)
{
#ifdef GENERIC_SUBSYS_PM_OPS
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm && pm->suspend)
		return pm->suspend(dev);
#endif

	return 0;
}

static int rmi_bus_resume(struct device *dev)
{
#ifdef GENERIC_SUBSYS_PM_OPS
	const struct dev_pm_ops *pm = dev->driver ? dev->driver->pm : NULL;

	if (pm && pm->resume)
		return pm->resume(dev);
	else if (dev->driver && dev->driver->resume)
		return dev->driver->resume(dev);
#else
	if (dev->driver && dev->driver->resume)
		return dev->driver->resume(dev);
#endif

	return 0;
}
#endif

static int rmi_bus_probe(struct device *dev)
{
	struct rmi_driver *driver;
	struct rmi_device *rmi_dev = to_rmi_device(dev);

	driver = rmi_dev->driver;
	if (driver && driver->probe)
		return driver->probe(rmi_dev);

	return 0;
}

static int rmi_bus_remove(struct device *dev)
{
	struct rmi_driver *driver;
	struct rmi_device *rmi_dev = to_rmi_device(dev);

	driver = rmi_dev->driver;
	if (driver && driver->remove)
		return driver->remove(rmi_dev);

	return 0;
}

static void rmi_bus_shutdown(struct device *dev)
{
	struct rmi_driver *driver;
	struct rmi_device *rmi_dev = to_rmi_device(dev);

	driver = rmi_dev->driver;
	if (driver && driver->shutdown)
		driver->shutdown(rmi_dev);
}

static SIMPLE_DEV_PM_OPS(rmi_bus_pm_ops,
			 rmi_bus_suspend, rmi_bus_resume);

struct bus_type rmi_bus_type = {
	.name		= "rmi",
	.match		= rmi_bus_match,
	.probe		= rmi_bus_probe,
	.remove		= rmi_bus_remove,
	.shutdown	= rmi_bus_shutdown,
	.pm		= &rmi_bus_pm_ops
};

static void release_rmidev_device(struct device *dev) {
	device_unregister(dev);
}

int rmi_register_phys_device(struct rmi_phys_device *phys)
{
	struct rmi_device_platform_data *pdata = phys->dev->platform_data;
	struct rmi_device *rmi_dev;

	if (!pdata) {
		dev_err(phys->dev, "no platform data!\n");
		return -EINVAL;
	}

	rmi_dev = kzalloc(sizeof(struct rmi_device), GFP_KERNEL);
	if (!rmi_dev)
		return -ENOMEM;

	rmi_dev->phys = phys;
	rmi_dev->dev.bus = &rmi_bus_type;

	rmi_dev->number = atomic_inc_return(&physical_device_count) - 1;
	rmi_dev->dev.release = release_rmidev_device;

	dev_set_name(&rmi_dev->dev, "sensor%02d", rmi_dev->number);
	dev_dbg(phys->dev, "%s: Registered %s as %s.\n", __func__,
		pdata->sensor_name, dev_name(&rmi_dev->dev));

#ifdef CONFIG_RMI4_DEBUG
	if (rmi_debugfs_root) {
		rmi_dev->debugfs_root = debugfs_create_dir(
			dev_name(&rmi_dev->dev), rmi_debugfs_root);
		if (!rmi_dev->debugfs_root)
			dev_err(&rmi_dev->dev, "Failed to create debugfs root.\n");
	}
#endif
	phys->rmi_dev = rmi_dev;
	return device_register(&rmi_dev->dev);
}
EXPORT_SYMBOL(rmi_register_phys_device);

void rmi_unregister_phys_device(struct rmi_phys_device *phys)
{
	struct rmi_device *rmi_dev = phys->rmi_dev;

#ifdef CONFIG_RMI4_DEBUG
	if (rmi_dev->debugfs_root)
		debugfs_remove(rmi_dev->debugfs_root);
#endif

	kfree(rmi_dev);
}
EXPORT_SYMBOL(rmi_unregister_phys_device);

int rmi_register_driver(struct rmi_driver *driver)
{
	driver->driver.bus = &rmi_bus_type;
	return driver_register(&driver->driver);
}
EXPORT_SYMBOL(rmi_register_driver);

static int __rmi_driver_remove(struct device *dev, void *data)
{
	struct rmi_driver *driver = data;
	struct rmi_device *rmi_dev = to_rmi_device(dev);

	if (rmi_dev->driver == driver)
		rmi_dev->driver = NULL;

	return 0;
}

void rmi_unregister_driver(struct rmi_driver *driver)
{
	bus_for_each_dev(&rmi_bus_type, NULL, driver, __rmi_driver_remove);
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL(rmi_unregister_driver);

static int __rmi_bus_fh_add(struct device *dev, void *data)
{
	struct rmi_driver *driver;
	struct rmi_device *rmi_dev = to_rmi_device(dev);

	driver = rmi_dev->driver;
	if (driver && driver->fh_add)
		driver->fh_add(rmi_dev, data);

	return 0;
}

int rmi_register_function_driver(struct rmi_function_handler *fh)
{
	struct rmi_function_list *entry;
	struct rmi_function_handler *fh_dup;

	fh_dup = rmi_get_function_handler(fh->func);
	if (fh_dup) {
		pr_err("%s: function f%.2x already registered!\n", __func__,
			fh->func);
		return -EINVAL;
	}

	entry = kzalloc(sizeof(struct rmi_function_list), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->fh = fh;
	INIT_LIST_HEAD(&entry->list);
	list_add_tail(&entry->list, &rmi_supported_functions.list);

	/* notify devices of the new function handler */
	bus_for_each_dev(&rmi_bus_type, NULL, fh, __rmi_bus_fh_add);

	return 0;
}
EXPORT_SYMBOL(rmi_register_function_driver);

static int __rmi_bus_fh_remove(struct device *dev, void *data)
{
	struct rmi_driver *driver;
	struct rmi_device *rmi_dev = to_rmi_device(dev);

	driver = rmi_dev->driver;
	if (driver && driver->fh_remove)
		driver->fh_remove(rmi_dev, data);

	return 0;
}

void rmi_unregister_function_driver(struct rmi_function_handler *fh)
{
	struct rmi_function_list *entry, *n;

	/* notify devices of the removal of the function handler */
	bus_for_each_dev(&rmi_bus_type, NULL, fh, __rmi_bus_fh_remove);

	if (list_empty(&rmi_supported_functions.list))
		return;

	list_for_each_entry_safe(entry, n, &rmi_supported_functions.list,
									list) {
		if (entry->fh->func == fh->func) {
			list_del(&entry->list);
			kfree(entry);
		}
	}

}
EXPORT_SYMBOL(rmi_unregister_function_driver);

struct rmi_function_handler *rmi_get_function_handler(int id)
{
	struct rmi_function_list *entry;

	if (list_empty(&rmi_supported_functions.list))
		return NULL;

	list_for_each_entry(entry, &rmi_supported_functions.list, list)
		if (entry->fh->func == id)
			return entry->fh;

	return NULL;
}
EXPORT_SYMBOL(rmi_get_function_handler);

static void rmi_release_character_device(struct device *dev)
{
	dev_dbg(dev, "%s: Called.\n", __func__);
	return;
}

static int rmi_register_character_device(struct device *dev, void *data)
{
	struct rmi_device *rmi_dev;
	struct rmi_char_driver *char_driver = data;
	struct rmi_char_device *char_dev;
	int retval;

	dev_dbg(dev, "Attaching character device.\n");
	rmi_dev = to_rmi_device(dev);
	if (char_driver->match && !char_driver->match(rmi_dev))
		return 0;

	if (!char_driver->init) {
		dev_err(dev, "ERROR: No init() function in %s.\n", __func__);
		return -EINVAL;
	}

	char_dev = kzalloc(sizeof(struct rmi_char_device), GFP_KERNEL);
	if (!char_dev)
		return -ENOMEM;

	char_dev->rmi_dev = rmi_dev;
	char_dev->driver = char_driver;

	char_dev->dev.parent = dev;
	char_dev->dev.release = rmi_release_character_device;
	char_dev->dev.driver = &char_driver->driver;
	retval = device_register(&char_dev->dev);
	if (!retval) {
		dev_err(dev, "Failed to register character device.\n");
		goto error_exit;
	}

	retval = char_driver->init(char_dev);
	if (retval) {
		dev_err(dev, "Failed to initialize character device.\n");
		goto error_exit;
	}

	mutex_lock(&rmi_bus_mutex);
	list_add_tail(&char_dev->list, &char_driver->devices);
	mutex_unlock(&rmi_bus_mutex);
	dev_info(&char_dev->dev, "Registered a device.\n");
	return retval;

error_exit:
	kfree(char_dev);
	return retval;
}

int rmi_register_character_driver(struct rmi_char_driver *char_driver)
{
	struct rmi_character_driver_list *entry;
	int retval;

	pr_debug("%s: Registering character driver %s.\n", __func__,
		char_driver->driver.name);

	char_driver->driver.bus = &rmi_bus_type;
	INIT_LIST_HEAD(&char_driver->devices);
	retval = driver_register(&char_driver->driver);
	if (retval) {
		pr_err("%s: Failed to register %s, code: %d.\n", __func__,
		       char_driver->driver.name, retval);
		return retval;
	}

	entry = kzalloc(sizeof(struct rmi_character_driver_list), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;
	entry->cd = char_driver;

	mutex_lock(&rmi_bus_mutex);
	list_add_tail(&entry->list, &rmi_character_drivers.list);
	mutex_unlock(&rmi_bus_mutex);

	/* notify devices of the removal of the function handler */
	bus_for_each_dev(&rmi_bus_type, NULL, char_driver,
			 rmi_register_character_device);

	return 0;
}
EXPORT_SYMBOL(rmi_register_character_driver);


int rmi_unregister_character_driver(struct rmi_char_driver *char_driver)
{
	struct rmi_character_driver_list *entry, *n;
	struct rmi_char_device *char_dev, *m;
	pr_debug("%s: Unregistering character driver %s.\n", __func__,
		char_driver->driver.name);

	mutex_lock(&rmi_bus_mutex);
	list_for_each_entry_safe(char_dev, m, &char_driver->devices,
				 list) {
		list_del(&char_dev->list);
		char_dev->driver->remove(char_dev);
	}
	list_for_each_entry_safe(entry, n, &rmi_character_drivers.list,
				 list) {
		if (entry->cd == char_driver) {
			list_del(&entry->list);
			kfree(entry);
		}
	}
	mutex_unlock(&rmi_bus_mutex);

	driver_unregister(&char_driver->driver);

	return 0;
}
EXPORT_SYMBOL(rmi_unregister_character_driver);

static int __init rmi_bus_init(void)
{
	int error;

	mutex_init(&rmi_bus_mutex);
	INIT_LIST_HEAD(&rmi_supported_functions.list);
	INIT_LIST_HEAD(&rmi_character_drivers.list);

#ifdef CONFIG_RMI4_DEBUG
	rmi_debugfs_root = debugfs_create_dir(rmi_bus_type.name, NULL);
	if (!rmi_debugfs_root)
		pr_err("%s: Failed to create debugfs root.\n", __func__);
	else if (IS_ERR(rmi_debugfs_root)) {
		pr_err("%s: Kernel may not contain debugfs support, code=%ld\n",
		       __func__, PTR_ERR(rmi_debugfs_root));
		rmi_debugfs_root = NULL;
	}
#endif

	error = bus_register(&rmi_bus_type);
	if (error < 0) {
		pr_err("%s: error registering the RMI bus: %d\n", __func__,
		       error);
		return error;
	}
	pr_debug("%s: successfully registered RMI bus.\n", __func__);

	return 0;
}

static void __exit rmi_bus_exit(void)
{
	/* We should only ever get here if all drivers are unloaded, so
	 * all we have to do at this point is unregister ourselves.
	 */
#ifdef CONFIG_RMI4_DEBUG
	if (rmi_debugfs_root)
		debugfs_remove(rmi_debugfs_root);
#endif
	bus_unregister(&rmi_bus_type);
}

module_init(rmi_bus_init);
module_exit(rmi_bus_exit);

MODULE_AUTHOR("Christopher Heiny <cheiny@synaptics.com");
MODULE_DESCRIPTION("RMI bus");
MODULE_LICENSE("GPL");
MODULE_VERSION(RMI_DRIVER_VERSION);
