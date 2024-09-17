// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Backend - Provides restricted access to the real PCI bus topology
 *               to the frontend
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#include <linux/list.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include "pciback.h"

struct passthrough_dev_data {
	/* Access to dev_list must be protected by lock */
	struct list_head dev_list;
	struct mutex lock;
};

static struct pci_dev *__xen_pcibk_get_pci_dev(struct xen_pcibk_device *pdev,
					       unsigned int domain,
					       unsigned int bus,
					       unsigned int devfn)
{
	struct passthrough_dev_data *dev_data = pdev->pci_dev_data;
	struct pci_dev_entry *dev_entry;
	struct pci_dev *dev = NULL;

	mutex_lock(&dev_data->lock);

	list_for_each_entry(dev_entry, &dev_data->dev_list, list) {
		if (domain == (unsigned int)pci_domain_nr(dev_entry->dev->bus)
		    && bus == (unsigned int)dev_entry->dev->bus->number
		    && devfn == dev_entry->dev->devfn) {
			dev = dev_entry->dev;
			break;
		}
	}

	mutex_unlock(&dev_data->lock);

	return dev;
}

static int __xen_pcibk_add_pci_dev(struct xen_pcibk_device *pdev,
				   struct pci_dev *dev,
				   int devid, publish_pci_dev_cb publish_cb)
{
	struct passthrough_dev_data *dev_data = pdev->pci_dev_data;
	struct pci_dev_entry *dev_entry;
	unsigned int domain, bus, devfn;
	int err;

	dev_entry = kmalloc(sizeof(*dev_entry), GFP_KERNEL);
	if (!dev_entry)
		return -ENOMEM;
	dev_entry->dev = dev;

	mutex_lock(&dev_data->lock);
	list_add_tail(&dev_entry->list, &dev_data->dev_list);
	mutex_unlock(&dev_data->lock);

	/* Publish this device. */
	domain = (unsigned int)pci_domain_nr(dev->bus);
	bus = (unsigned int)dev->bus->number;
	devfn = dev->devfn;
	err = publish_cb(pdev, domain, bus, devfn, devid);

	return err;
}

static void __xen_pcibk_release_pci_dev(struct xen_pcibk_device *pdev,
					struct pci_dev *dev, bool lock)
{
	struct passthrough_dev_data *dev_data = pdev->pci_dev_data;
	struct pci_dev_entry *dev_entry, *t;
	struct pci_dev *found_dev = NULL;

	mutex_lock(&dev_data->lock);

	list_for_each_entry_safe(dev_entry, t, &dev_data->dev_list, list) {
		if (dev_entry->dev == dev) {
			list_del(&dev_entry->list);
			found_dev = dev_entry->dev;
			kfree(dev_entry);
		}
	}

	mutex_unlock(&dev_data->lock);

	if (found_dev) {
		if (lock)
			device_lock(&found_dev->dev);
		pcistub_put_pci_dev(found_dev);
		if (lock)
			device_unlock(&found_dev->dev);
	}
}

static int __xen_pcibk_init_devices(struct xen_pcibk_device *pdev)
{
	struct passthrough_dev_data *dev_data;

	dev_data = kmalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;

	mutex_init(&dev_data->lock);

	INIT_LIST_HEAD(&dev_data->dev_list);

	pdev->pci_dev_data = dev_data;

	return 0;
}

static int __xen_pcibk_publish_pci_roots(struct xen_pcibk_device *pdev,
					 publish_pci_root_cb publish_root_cb)
{
	int err = 0;
	struct passthrough_dev_data *dev_data = pdev->pci_dev_data;
	struct pci_dev_entry *dev_entry, *e;
	struct pci_dev *dev;
	int found;
	unsigned int domain, bus;

	mutex_lock(&dev_data->lock);

	list_for_each_entry(dev_entry, &dev_data->dev_list, list) {
		/* Only publish this device as a root if none of its
		 * parent bridges are exported
		 */
		found = 0;
		dev = dev_entry->dev->bus->self;
		for (; !found && dev != NULL; dev = dev->bus->self) {
			list_for_each_entry(e, &dev_data->dev_list, list) {
				if (dev == e->dev) {
					found = 1;
					break;
				}
			}
		}

		domain = (unsigned int)pci_domain_nr(dev_entry->dev->bus);
		bus = (unsigned int)dev_entry->dev->bus->number;

		if (!found) {
			err = publish_root_cb(pdev, domain, bus);
			if (err)
				break;
		}
	}

	mutex_unlock(&dev_data->lock);

	return err;
}

static void __xen_pcibk_release_devices(struct xen_pcibk_device *pdev)
{
	struct passthrough_dev_data *dev_data = pdev->pci_dev_data;
	struct pci_dev_entry *dev_entry, *t;

	list_for_each_entry_safe(dev_entry, t, &dev_data->dev_list, list) {
		struct pci_dev *dev = dev_entry->dev;
		list_del(&dev_entry->list);
		device_lock(&dev->dev);
		pcistub_put_pci_dev(dev);
		device_unlock(&dev->dev);
		kfree(dev_entry);
	}

	kfree(dev_data);
	pdev->pci_dev_data = NULL;
}

static int __xen_pcibk_get_pcifront_dev(struct pci_dev *pcidev,
					struct xen_pcibk_device *pdev,
					unsigned int *domain, unsigned int *bus,
					unsigned int *devfn)
{
	*domain = pci_domain_nr(pcidev->bus);
	*bus = pcidev->bus->number;
	*devfn = pcidev->devfn;
	return 1;
}

const struct xen_pcibk_backend xen_pcibk_passthrough_backend = {
	.name           = "passthrough",
	.init           = __xen_pcibk_init_devices,
	.free		= __xen_pcibk_release_devices,
	.find           = __xen_pcibk_get_pcifront_dev,
	.publish        = __xen_pcibk_publish_pci_roots,
	.release        = __xen_pcibk_release_pci_dev,
	.add            = __xen_pcibk_add_pci_dev,
	.get            = __xen_pcibk_get_pci_dev,
};
