// SPDX-License-Identifier: GPL-2.0
/**
 * PCI Endpoint *Function* (EPF) library
 *
 * Copyright (C) 2017 Texas Instruments
 * Author: Kishon Vijay Abraham I <kishon@ti.com>
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/module.h>

#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/pci-ep-cfs.h>

static DEFINE_MUTEX(pci_epf_mutex);

static struct bus_type pci_epf_bus_type;
static const struct device_type pci_epf_type;

/**
 * pci_epf_linkup() - Notify the function driver that EPC device has
 *		      established a connection with the Root Complex.
 * @epf: the EPF device bound to the EPC device which has established
 *	 the connection with the host
 *
 * Invoke to notify the function driver that EPC device has established
 * a connection with the Root Complex.
 */
void pci_epf_linkup(struct pci_epf *epf)
{
	if (!epf->driver) {
		dev_WARN(&epf->dev, "epf device not bound to driver\n");
		return;
	}

	epf->driver->ops->linkup(epf);
}
EXPORT_SYMBOL_GPL(pci_epf_linkup);

/**
 * pci_epf_unbind() - Notify the function driver that the binding between the
 *		      EPF device and EPC device has been lost
 * @epf: the EPF device which has lost the binding with the EPC device
 *
 * Invoke to notify the function driver that the binding between the EPF device
 * and EPC device has been lost.
 */
void pci_epf_unbind(struct pci_epf *epf)
{
	if (!epf->driver) {
		dev_WARN(&epf->dev, "epf device not bound to driver\n");
		return;
	}

	epf->driver->ops->unbind(epf);
	module_put(epf->driver->owner);
}
EXPORT_SYMBOL_GPL(pci_epf_unbind);

/**
 * pci_epf_bind() - Notify the function driver that the EPF device has been
 *		    bound to a EPC device
 * @epf: the EPF device which has been bound to the EPC device
 *
 * Invoke to notify the function driver that it has been bound to a EPC device
 */
int pci_epf_bind(struct pci_epf *epf)
{
	if (!epf->driver) {
		dev_WARN(&epf->dev, "epf device not bound to driver\n");
		return -EINVAL;
	}

	if (!try_module_get(epf->driver->owner))
		return -EAGAIN;

	return epf->driver->ops->bind(epf);
}
EXPORT_SYMBOL_GPL(pci_epf_bind);

/**
 * pci_epf_free_space() - free the allocated PCI EPF register space
 * @addr: the virtual address of the PCI EPF register space
 * @bar: the BAR number corresponding to the register space
 *
 * Invoke to free the allocated PCI EPF register space.
 */
void pci_epf_free_space(struct pci_epf *epf, void *addr, enum pci_barno bar)
{
	struct device *dev = epf->epc->dev.parent;

	if (!addr)
		return;

	dma_free_coherent(dev, epf->bar[bar].size, addr,
			  epf->bar[bar].phys_addr);

	epf->bar[bar].phys_addr = 0;
	epf->bar[bar].size = 0;
	epf->bar[bar].barno = 0;
	epf->bar[bar].flags = 0;
}
EXPORT_SYMBOL_GPL(pci_epf_free_space);

/**
 * pci_epf_alloc_space() - allocate memory for the PCI EPF register space
 * @size: the size of the memory that has to be allocated
 * @bar: the BAR number corresponding to the allocated register space
 *
 * Invoke to allocate memory for the PCI EPF register space.
 */
void *pci_epf_alloc_space(struct pci_epf *epf, size_t size, enum pci_barno bar)
{
	void *space;
	struct device *dev = epf->epc->dev.parent;
	dma_addr_t phys_addr;

	if (size < 128)
		size = 128;
	size = roundup_pow_of_two(size);

	space = dma_alloc_coherent(dev, size, &phys_addr, GFP_KERNEL);
	if (!space) {
		dev_err(dev, "failed to allocate mem space\n");
		return NULL;
	}

	epf->bar[bar].phys_addr = phys_addr;
	epf->bar[bar].size = size;
	epf->bar[bar].barno = bar;
	epf->bar[bar].flags = PCI_BASE_ADDRESS_SPACE_MEMORY;

	return space;
}
EXPORT_SYMBOL_GPL(pci_epf_alloc_space);

static void pci_epf_remove_cfs(struct pci_epf_driver *driver)
{
	struct config_group *group, *tmp;

	if (!IS_ENABLED(CONFIG_PCI_ENDPOINT_CONFIGFS))
		return;

	mutex_lock(&pci_epf_mutex);
	list_for_each_entry_safe(group, tmp, &driver->epf_group, group_entry)
		pci_ep_cfs_remove_epf_group(group);
	list_del(&driver->epf_group);
	mutex_unlock(&pci_epf_mutex);
}

/**
 * pci_epf_unregister_driver() - unregister the PCI EPF driver
 * @driver: the PCI EPF driver that has to be unregistered
 *
 * Invoke to unregister the PCI EPF driver.
 */
void pci_epf_unregister_driver(struct pci_epf_driver *driver)
{
	pci_epf_remove_cfs(driver);
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(pci_epf_unregister_driver);

static int pci_epf_add_cfs(struct pci_epf_driver *driver)
{
	struct config_group *group;
	const struct pci_epf_device_id *id;

	if (!IS_ENABLED(CONFIG_PCI_ENDPOINT_CONFIGFS))
		return 0;

	INIT_LIST_HEAD(&driver->epf_group);

	id = driver->id_table;
	while (id->name[0]) {
		group = pci_ep_cfs_add_epf_group(id->name);
		if (IS_ERR(group)) {
			pci_epf_remove_cfs(driver);
			return PTR_ERR(group);
		}

		mutex_lock(&pci_epf_mutex);
		list_add_tail(&group->group_entry, &driver->epf_group);
		mutex_unlock(&pci_epf_mutex);
		id++;
	}

	return 0;
}

/**
 * __pci_epf_register_driver() - register a new PCI EPF driver
 * @driver: structure representing PCI EPF driver
 * @owner: the owner of the module that registers the PCI EPF driver
 *
 * Invoke to register a new PCI EPF driver.
 */
int __pci_epf_register_driver(struct pci_epf_driver *driver,
			      struct module *owner)
{
	int ret;

	if (!driver->ops)
		return -EINVAL;

	if (!driver->ops->bind || !driver->ops->unbind || !driver->ops->linkup)
		return -EINVAL;

	driver->driver.bus = &pci_epf_bus_type;
	driver->driver.owner = owner;

	ret = driver_register(&driver->driver);
	if (ret)
		return ret;

	pci_epf_add_cfs(driver);

	return 0;
}
EXPORT_SYMBOL_GPL(__pci_epf_register_driver);

/**
 * pci_epf_destroy() - destroy the created PCI EPF device
 * @epf: the PCI EPF device that has to be destroyed.
 *
 * Invoke to destroy the PCI EPF device created by invoking pci_epf_create().
 */
void pci_epf_destroy(struct pci_epf *epf)
{
	device_unregister(&epf->dev);
}
EXPORT_SYMBOL_GPL(pci_epf_destroy);

/**
 * pci_epf_create() - create a new PCI EPF device
 * @name: the name of the PCI EPF device. This name will be used to bind the
 *	  the EPF device to a EPF driver
 *
 * Invoke to create a new PCI EPF device by providing the name of the function
 * device.
 */
struct pci_epf *pci_epf_create(const char *name)
{
	int ret;
	struct pci_epf *epf;
	struct device *dev;
	int len;

	epf = kzalloc(sizeof(*epf), GFP_KERNEL);
	if (!epf)
		return ERR_PTR(-ENOMEM);

	len = strchrnul(name, '.') - name;
	epf->name = kstrndup(name, len, GFP_KERNEL);
	if (!epf->name) {
		kfree(epf);
		return ERR_PTR(-ENOMEM);
	}

	dev = &epf->dev;
	device_initialize(dev);
	dev->bus = &pci_epf_bus_type;
	dev->type = &pci_epf_type;

	ret = dev_set_name(dev, "%s", name);
	if (ret) {
		put_device(dev);
		return ERR_PTR(ret);
	}

	ret = device_add(dev);
	if (ret) {
		put_device(dev);
		return ERR_PTR(ret);
	}

	return epf;
}
EXPORT_SYMBOL_GPL(pci_epf_create);

const struct pci_epf_device_id *
pci_epf_match_device(const struct pci_epf_device_id *id, struct pci_epf *epf)
{
	if (!id || !epf)
		return NULL;

	while (*id->name) {
		if (strcmp(epf->name, id->name) == 0)
			return id;
		id++;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(pci_epf_match_device);

static void pci_epf_dev_release(struct device *dev)
{
	struct pci_epf *epf = to_pci_epf(dev);

	kfree(epf->name);
	kfree(epf);
}

static const struct device_type pci_epf_type = {
	.release	= pci_epf_dev_release,
};

static int
pci_epf_match_id(const struct pci_epf_device_id *id, const struct pci_epf *epf)
{
	while (id->name[0]) {
		if (strcmp(epf->name, id->name) == 0)
			return true;
		id++;
	}

	return false;
}

static int pci_epf_device_match(struct device *dev, struct device_driver *drv)
{
	struct pci_epf *epf = to_pci_epf(dev);
	struct pci_epf_driver *driver = to_pci_epf_driver(drv);

	if (driver->id_table)
		return pci_epf_match_id(driver->id_table, epf);

	return !strcmp(epf->name, drv->name);
}

static int pci_epf_device_probe(struct device *dev)
{
	struct pci_epf *epf = to_pci_epf(dev);
	struct pci_epf_driver *driver = to_pci_epf_driver(dev->driver);

	if (!driver->probe)
		return -ENODEV;

	epf->driver = driver;

	return driver->probe(epf);
}

static int pci_epf_device_remove(struct device *dev)
{
	int ret = 0;
	struct pci_epf *epf = to_pci_epf(dev);
	struct pci_epf_driver *driver = to_pci_epf_driver(dev->driver);

	if (driver->remove)
		ret = driver->remove(epf);
	epf->driver = NULL;

	return ret;
}

static struct bus_type pci_epf_bus_type = {
	.name		= "pci-epf",
	.match		= pci_epf_device_match,
	.probe		= pci_epf_device_probe,
	.remove		= pci_epf_device_remove,
};

static int __init pci_epf_init(void)
{
	int ret;

	ret = bus_register(&pci_epf_bus_type);
	if (ret) {
		pr_err("failed to register pci epf bus --> %d\n", ret);
		return ret;
	}

	return 0;
}
module_init(pci_epf_init);

static void __exit pci_epf_exit(void)
{
	bus_unregister(&pci_epf_bus_type);
}
module_exit(pci_epf_exit);

MODULE_DESCRIPTION("PCI EPF Library");
MODULE_AUTHOR("Kishon Vijay Abraham I <kishon@ti.com>");
MODULE_LICENSE("GPL v2");
