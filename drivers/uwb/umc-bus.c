/*
 * Bus for UWB Multi-interface Controller capabilities.
 *
 * Copyright (C) 2007 Cambridge Silicon Radio Ltd.
 *
 * This file is released under the GNU GPL v2.
 */
#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/uwb/umc.h>
#include <linux/pci.h>

static int umc_bus_unbind_helper(struct device *dev, void *data)
{
	struct device *parent = data;

	if (dev->parent == parent && dev->driver)
		device_release_driver(dev);
	return 0;
}

/**
 * umc_controller_reset - reset the whole UMC controller
 * @umc: the UMC device for the radio controller.
 *
 * Drivers will be unbound from all UMC devices belonging to the
 * controller and then the radio controller will be rebound.  The
 * radio controller is expected to do a full hardware reset when it is
 * probed.
 *
 * If this is called while a probe() or remove() is in progress it
 * will return -EAGAIN and not perform the reset.
 */
int umc_controller_reset(struct umc_dev *umc)
{
	struct device *parent = umc->dev.parent;
	int ret;

	if (down_trylock(&parent->sem))
		return -EAGAIN;
	bus_for_each_dev(&umc_bus_type, NULL, parent, umc_bus_unbind_helper);
	ret = device_attach(&umc->dev);
	if (ret == 1)
		ret = 0;
	up(&parent->sem);

	return ret;
}
EXPORT_SYMBOL_GPL(umc_controller_reset);

/**
 * umc_match_pci_id - match a UMC driver to a UMC device's parent PCI device.
 * @umc_drv: umc driver with match_data pointing to a zero-terminated
 * table of pci_device_id's.
 * @umc: umc device whose parent is to be matched.
 */
int umc_match_pci_id(struct umc_driver *umc_drv, struct umc_dev *umc)
{
	const struct pci_device_id *id_table = umc_drv->match_data;
	struct pci_dev *pci;

	if (umc->dev.parent->bus != &pci_bus_type)
		return 0;

	pci = to_pci_dev(umc->dev.parent);
	return pci_match_id(id_table, pci) != NULL;
}
EXPORT_SYMBOL_GPL(umc_match_pci_id);

static int umc_bus_rescan_helper(struct device *dev, void *data)
{
	int ret = 0;

	if (!dev->driver)
		ret = device_attach(dev);

	return ret < 0 ? ret : 0;
}

static void umc_bus_rescan(void)
{
	int err;

	/*
	 * We can't use bus_rescan_devices() here as it deadlocks when
	 * it tries to retake the dev->parent semaphore.
	 */
	err = bus_for_each_dev(&umc_bus_type, NULL, NULL, umc_bus_rescan_helper);
	if (err < 0)
		printk(KERN_WARNING "%s: rescan of bus failed: %d\n",
		       KBUILD_MODNAME, err);
}

static int umc_bus_match(struct device *dev, struct device_driver *drv)
{
	struct umc_dev *umc = to_umc_dev(dev);
	struct umc_driver *umc_driver = to_umc_driver(drv);

	if (umc->cap_id == umc_driver->cap_id) {
		if (umc_driver->match)
			return umc_driver->match(umc_driver, umc);
		else
			return 1;
	}
	return 0;
}

static int umc_device_probe(struct device *dev)
{
	struct umc_dev *umc;
	struct umc_driver *umc_driver;
	int err;

	umc_driver = to_umc_driver(dev->driver);
	umc = to_umc_dev(dev);

	get_device(dev);
	err = umc_driver->probe(umc);
	if (err)
		put_device(dev);
	else
		umc_bus_rescan();

	return err;
}

static int umc_device_remove(struct device *dev)
{
	struct umc_dev *umc;
	struct umc_driver *umc_driver;

	umc_driver = to_umc_driver(dev->driver);
	umc = to_umc_dev(dev);

	umc_driver->remove(umc);
	put_device(dev);
	return 0;
}

static int umc_device_suspend(struct device *dev, pm_message_t state)
{
	struct umc_dev *umc;
	struct umc_driver *umc_driver;
	int err = 0;

	umc = to_umc_dev(dev);

	if (dev->driver) {
		umc_driver = to_umc_driver(dev->driver);
		if (umc_driver->suspend)
			err = umc_driver->suspend(umc, state);
	}
	return err;
}

static int umc_device_resume(struct device *dev)
{
	struct umc_dev *umc;
	struct umc_driver *umc_driver;
	int err = 0;

	umc = to_umc_dev(dev);

	if (dev->driver) {
		umc_driver = to_umc_driver(dev->driver);
		if (umc_driver->resume)
			err = umc_driver->resume(umc);
	}
	return err;
}

static ssize_t capability_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct umc_dev *umc = to_umc_dev(dev);

	return sprintf(buf, "0x%02x\n", umc->cap_id);
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct umc_dev *umc = to_umc_dev(dev);

	return sprintf(buf, "0x%04x\n", umc->version);
}

static struct device_attribute umc_dev_attrs[] = {
	__ATTR_RO(capability_id),
	__ATTR_RO(version),
	__ATTR_NULL,
};

struct bus_type umc_bus_type = {
	.name		= "umc",
	.match		= umc_bus_match,
	.probe		= umc_device_probe,
	.remove		= umc_device_remove,
	.suspend        = umc_device_suspend,
	.resume         = umc_device_resume,
	.dev_attrs	= umc_dev_attrs,
};
EXPORT_SYMBOL_GPL(umc_bus_type);

static int __init umc_bus_init(void)
{
	return bus_register(&umc_bus_type);
}
module_init(umc_bus_init);

static void __exit umc_bus_exit(void)
{
	bus_unregister(&umc_bus_type);
}
module_exit(umc_bus_exit);

MODULE_DESCRIPTION("UWB Multi-interface Controller capability bus");
MODULE_AUTHOR("Cambridge Silicon Radio Ltd.");
MODULE_LICENSE("GPL");
