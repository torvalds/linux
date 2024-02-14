// SPDX-License-Identifier: GPL-2.0
/*
 * PCI Backend - Provides a Virtual PCI bus (with real devices)
 *               to the frontend
 *
 *   Author: Ryan Wilson <hap9@epoch.ncsc.mil>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define dev_fmt pr_fmt

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include "pciback.h"

#define PCI_SLOT_MAX 32

struct vpci_dev_data {
	/* Access to dev_list must be protected by lock */
	struct list_head dev_list[PCI_SLOT_MAX];
	struct mutex lock;
};

static inline struct list_head *list_first(struct list_head *head)
{
	return head->next;
}

static struct pci_dev *__xen_pcibk_get_pci_dev(struct xen_pcibk_device *pdev,
					       unsigned int domain,
					       unsigned int bus,
					       unsigned int devfn)
{
	struct pci_dev_entry *entry;
	struct pci_dev *dev = NULL;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;

	if (domain != 0 || bus != 0)
		return NULL;

	if (PCI_SLOT(devfn) < PCI_SLOT_MAX) {
		mutex_lock(&vpci_dev->lock);

		list_for_each_entry(entry,
				    &vpci_dev->dev_list[PCI_SLOT(devfn)],
				    list) {
			if (PCI_FUNC(entry->dev->devfn) == PCI_FUNC(devfn)) {
				dev = entry->dev;
				break;
			}
		}

		mutex_unlock(&vpci_dev->lock);
	}
	return dev;
}

static inline int match_slot(struct pci_dev *l, struct pci_dev *r)
{
	if (pci_domain_nr(l->bus) == pci_domain_nr(r->bus)
	    && l->bus == r->bus && PCI_SLOT(l->devfn) == PCI_SLOT(r->devfn))
		return 1;

	return 0;
}

static int __xen_pcibk_add_pci_dev(struct xen_pcibk_device *pdev,
				   struct pci_dev *dev, int devid,
				   publish_pci_dev_cb publish_cb)
{
	int err = 0, slot, func = PCI_FUNC(dev->devfn);
	struct pci_dev_entry *t, *dev_entry;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;

	if ((dev->class >> 24) == PCI_BASE_CLASS_BRIDGE) {
		err = -EFAULT;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Can't export bridges on the virtual PCI bus");
		goto out;
	}

	dev_entry = kmalloc(sizeof(*dev_entry), GFP_KERNEL);
	if (!dev_entry) {
		err = -ENOMEM;
		xenbus_dev_fatal(pdev->xdev, err,
				 "Error adding entry to virtual PCI bus");
		goto out;
	}

	dev_entry->dev = dev;

	mutex_lock(&vpci_dev->lock);

	/*
	 * Keep multi-function devices together on the virtual PCI bus, except
	 * that we want to keep virtual functions at func 0 on their own. They
	 * aren't multi-function devices and hence their presence at func 0
	 * may cause guests to not scan the other functions.
	 */
	if (!dev->is_virtfn || func) {
		for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
			if (list_empty(&vpci_dev->dev_list[slot]))
				continue;

			t = list_entry(list_first(&vpci_dev->dev_list[slot]),
				       struct pci_dev_entry, list);
			if (t->dev->is_virtfn && !PCI_FUNC(t->dev->devfn))
				continue;

			if (match_slot(dev, t->dev)) {
				dev_info(&dev->dev, "vpci: assign to virtual slot %d func %d\n",
					 slot, func);
				list_add_tail(&dev_entry->list,
					      &vpci_dev->dev_list[slot]);
				goto unlock;
			}
		}
	}

	/* Assign to a new slot on the virtual PCI bus */
	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		if (list_empty(&vpci_dev->dev_list[slot])) {
			dev_info(&dev->dev, "vpci: assign to virtual slot %d\n",
				 slot);
			list_add_tail(&dev_entry->list,
				      &vpci_dev->dev_list[slot]);
			goto unlock;
		}
	}

	err = -ENOMEM;
	xenbus_dev_fatal(pdev->xdev, err,
			 "No more space on root virtual PCI bus");

unlock:
	mutex_unlock(&vpci_dev->lock);

	/* Publish this device. */
	if (!err)
		err = publish_cb(pdev, 0, 0, PCI_DEVFN(slot, func), devid);
	else
		kfree(dev_entry);

out:
	return err;
}

static void __xen_pcibk_release_pci_dev(struct xen_pcibk_device *pdev,
					struct pci_dev *dev, bool lock)
{
	int slot;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;
	struct pci_dev *found_dev = NULL;

	mutex_lock(&vpci_dev->lock);

	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		struct pci_dev_entry *e;

		list_for_each_entry(e, &vpci_dev->dev_list[slot], list) {
			if (e->dev == dev) {
				list_del(&e->list);
				found_dev = e->dev;
				kfree(e);
				goto out;
			}
		}
	}

out:
	mutex_unlock(&vpci_dev->lock);

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
	int slot;
	struct vpci_dev_data *vpci_dev;

	vpci_dev = kmalloc(sizeof(*vpci_dev), GFP_KERNEL);
	if (!vpci_dev)
		return -ENOMEM;

	mutex_init(&vpci_dev->lock);

	for (slot = 0; slot < PCI_SLOT_MAX; slot++)
		INIT_LIST_HEAD(&vpci_dev->dev_list[slot]);

	pdev->pci_dev_data = vpci_dev;

	return 0;
}

static int __xen_pcibk_publish_pci_roots(struct xen_pcibk_device *pdev,
					 publish_pci_root_cb publish_cb)
{
	/* The Virtual PCI bus has only one root */
	return publish_cb(pdev, 0, 0);
}

static void __xen_pcibk_release_devices(struct xen_pcibk_device *pdev)
{
	int slot;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;

	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		struct pci_dev_entry *e, *tmp;
		list_for_each_entry_safe(e, tmp, &vpci_dev->dev_list[slot],
					 list) {
			struct pci_dev *dev = e->dev;
			list_del(&e->list);
			device_lock(&dev->dev);
			pcistub_put_pci_dev(dev);
			device_unlock(&dev->dev);
			kfree(e);
		}
	}

	kfree(vpci_dev);
	pdev->pci_dev_data = NULL;
}

static int __xen_pcibk_get_pcifront_dev(struct pci_dev *pcidev,
					struct xen_pcibk_device *pdev,
					unsigned int *domain, unsigned int *bus,
					unsigned int *devfn)
{
	struct pci_dev_entry *entry;
	struct vpci_dev_data *vpci_dev = pdev->pci_dev_data;
	int found = 0, slot;

	mutex_lock(&vpci_dev->lock);
	for (slot = 0; slot < PCI_SLOT_MAX; slot++) {
		list_for_each_entry(entry,
			    &vpci_dev->dev_list[slot],
			    list) {
			if (entry->dev == pcidev) {
				found = 1;
				*domain = 0;
				*bus = 0;
				*devfn = PCI_DEVFN(slot,
					 PCI_FUNC(pcidev->devfn));
			}
		}
	}
	mutex_unlock(&vpci_dev->lock);
	return found;
}

const struct xen_pcibk_backend xen_pcibk_vpci_backend = {
	.name		= "vpci",
	.init		= __xen_pcibk_init_devices,
	.free		= __xen_pcibk_release_devices,
	.find		= __xen_pcibk_get_pcifront_dev,
	.publish	= __xen_pcibk_publish_pci_roots,
	.release	= __xen_pcibk_release_pci_dev,
	.add		= __xen_pcibk_add_pci_dev,
	.get		= __xen_pcibk_get_pci_dev,
};
