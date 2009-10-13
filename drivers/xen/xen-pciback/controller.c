/*
 * Copyright (C) 2007 Hewlett-Packard Development Company, L.P.
 *      Alex Williamson <alex.williamson@hp.com>
 *
 * PCI "Controller" Backend - virtualize PCI bus topology based on PCI
 * controllers.  Devices under the same PCI controller are exposed on the
 * same virtual domain:bus.  Within a bus, device slots are virtualized
 * to compact the bus.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/acpi.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include "pciback.h"

#define PCI_MAX_BUSSES	255
#define PCI_MAX_SLOTS	32

struct controller_dev_entry {
	struct list_head list;
	struct pci_dev *dev;
	unsigned int devfn;
};

struct controller_list_entry {
	struct list_head list;
	struct pci_controller *controller;
	unsigned int domain;
	unsigned int bus;
	unsigned int next_devfn;
	struct list_head dev_list;
};

struct controller_dev_data {
	struct list_head list;
	unsigned int next_domain;
	unsigned int next_bus;
	spinlock_t lock;
};

struct walk_info {
	struct pciback_device *pdev;
	int resource_count;
	int root_num;
};

struct pci_dev *pciback_get_pci_dev(struct pciback_device *pdev,
				    unsigned int domain, unsigned int bus,
				    unsigned int devfn)
{
	struct controller_dev_data *dev_data = pdev->pci_dev_data;
	struct controller_dev_entry *dev_entry;
	struct controller_list_entry *cntrl_entry;
	struct pci_dev *dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev_data->lock, flags);

	list_for_each_entry(cntrl_entry, &dev_data->list, list) {
		if (cntrl_entry->domain != domain ||
		    cntrl_entry->bus != bus)
			continue;

		list_for_each_entry(dev_entry, &cntrl_entry->dev_list, list) {
			if (devfn == dev_entry->devfn) {
				dev = dev_entry->dev;
				goto found;
			}
		}
	}
found:
	spin_unlock_irqrestore(&dev_data->lock, flags);

	return dev;
}

int pciback_add_pci_dev(struct pciback_device *pdev, struct pci_dev *dev,
			int devid, publish_pci_dev_cb publish_cb)
{
	struct controller_dev_data *dev_data = pdev->pci_dev_data;
	struct controller_dev_entry *dev_entry;
	struct controller_list_entry *cntrl_entry;
	struct pci_controller *dev_controller = PCI_CONTROLLER(dev);
	unsigned long flags;
	int ret = 0, found = 0;

	spin_lock_irqsave(&dev_data->lock, flags);

	/* Look to see if we already have a domain:bus for this controller */
	list_for_each_entry(cntrl_entry, &dev_data->list, list) {
		if (cntrl_entry->controller == dev_controller) {
			found = 1;
			break;
		}
	}

	if (!found) {
		cntrl_entry = kmalloc(sizeof(*cntrl_entry), GFP_ATOMIC);
		if (!cntrl_entry) {
			ret =  -ENOMEM;
			goto out;
		}

		cntrl_entry->controller = dev_controller;
		cntrl_entry->next_devfn = PCI_DEVFN(0, 0);

		cntrl_entry->domain = dev_data->next_domain;
		cntrl_entry->bus = dev_data->next_bus++;
		if (dev_data->next_bus > PCI_MAX_BUSSES) {
			dev_data->next_domain++;
			dev_data->next_bus = 0;
		}

		INIT_LIST_HEAD(&cntrl_entry->dev_list);

		list_add_tail(&cntrl_entry->list, &dev_data->list);
	}

	if (PCI_SLOT(cntrl_entry->next_devfn) > PCI_MAX_SLOTS) {
		/*
		 * While it seems unlikely, this can actually happen if
		 * a controller has P2P bridges under it.
		 */
		xenbus_dev_fatal(pdev->xdev, -ENOSPC, "Virtual bus %04x:%02x "
				 "is full, no room to export %04x:%02x:%02x.%x",
				 cntrl_entry->domain, cntrl_entry->bus,
				 pci_domain_nr(dev->bus), dev->bus->number,
				 PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
		ret = -ENOSPC;
		goto out;
	}

	dev_entry = kmalloc(sizeof(*dev_entry), GFP_ATOMIC);
	if (!dev_entry) {
		if (list_empty(&cntrl_entry->dev_list)) {
			list_del(&cntrl_entry->list);
			kfree(cntrl_entry);
		}
		ret = -ENOMEM;
		goto out;
	}

	dev_entry->dev = dev;
	dev_entry->devfn = cntrl_entry->next_devfn;

	list_add_tail(&dev_entry->list, &cntrl_entry->dev_list);

	cntrl_entry->next_devfn += PCI_DEVFN(1, 0);

out:
	spin_unlock_irqrestore(&dev_data->lock, flags);

	/* TODO: Publish virtual domain:bus:slot.func here. */

	return ret;
}

void pciback_release_pci_dev(struct pciback_device *pdev, struct pci_dev *dev)
{
	struct controller_dev_data *dev_data = pdev->pci_dev_data;
	struct controller_list_entry *cntrl_entry;
	struct controller_dev_entry *dev_entry = NULL;
	struct pci_dev *found_dev = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev_data->lock, flags);

	list_for_each_entry(cntrl_entry, &dev_data->list, list) {
		if (cntrl_entry->controller != PCI_CONTROLLER(dev))
			continue;

		list_for_each_entry(dev_entry, &cntrl_entry->dev_list, list) {
			if (dev_entry->dev == dev) {
				found_dev = dev_entry->dev;
				break;
			}
		}
	}

	if (!found_dev) {
		spin_unlock_irqrestore(&dev_data->lock, flags);
		return;
	}

	list_del(&dev_entry->list);
	kfree(dev_entry);

	if (list_empty(&cntrl_entry->dev_list)) {
		list_del(&cntrl_entry->list);
		kfree(cntrl_entry);
	}

	spin_unlock_irqrestore(&dev_data->lock, flags);
	pcistub_put_pci_dev(found_dev);
}

int pciback_init_devices(struct pciback_device *pdev)
{
	struct controller_dev_data *dev_data;

	dev_data = kmalloc(sizeof(*dev_data), GFP_KERNEL);
	if (!dev_data)
		return -ENOMEM;

	spin_lock_init(&dev_data->lock);

	INIT_LIST_HEAD(&dev_data->list);

	/* Starting domain:bus numbers */
	dev_data->next_domain = 0;
	dev_data->next_bus = 0;

	pdev->pci_dev_data = dev_data;

	return 0;
}

static acpi_status write_xenbus_resource(struct acpi_resource *res, void *data)
{
	struct walk_info *info = data;
	struct acpi_resource_address64 addr;
	acpi_status status;
	int i, len, err;
	char str[32], tmp[3];
	unsigned char *ptr, *buf;

	status = acpi_resource_to_address64(res, &addr);

	/* Do we care about this range?  Let's check. */
	if (!ACPI_SUCCESS(status) ||
	    !(addr.resource_type == ACPI_MEMORY_RANGE ||
	      addr.resource_type == ACPI_IO_RANGE) ||
	    !addr.address_length || addr.producer_consumer != ACPI_PRODUCER)
		return AE_OK;

	/*
	 * Furthermore, we really only care to tell the guest about
	 * address ranges that require address translation of some sort.
	 */
	if (!(addr.resource_type == ACPI_MEMORY_RANGE &&
	      addr.info.mem.translation) &&
	    !(addr.resource_type == ACPI_IO_RANGE &&
	      addr.info.io.translation))
		return AE_OK;

	/* Store the resource in xenbus for the guest */
	len = snprintf(str, sizeof(str), "root-%d-resource-%d",
		       info->root_num, info->resource_count);
	if (unlikely(len >= (sizeof(str) - 1)))
		return AE_OK;

	buf = kzalloc((sizeof(*res) * 2) + 1, GFP_KERNEL);
	if (!buf)
		return AE_OK;

	/* Clean out resource_source */
	res->data.address64.resource_source.index = 0xFF;
	res->data.address64.resource_source.string_length = 0;
	res->data.address64.resource_source.string_ptr = NULL;

	ptr = (unsigned char *)res;

	/* Turn the acpi_resource into an ASCII byte stream */
	for (i = 0; i < sizeof(*res); i++) {
		snprintf(tmp, sizeof(tmp), "%02x", ptr[i]);
		strncat(buf, tmp, 2);
	}

	err = xenbus_printf(XBT_NIL, info->pdev->xdev->nodename,
			    str, "%s", buf);

	if (!err)
		info->resource_count++;

	kfree(buf);

	return AE_OK;
}

int pciback_publish_pci_roots(struct pciback_device *pdev,
			      publish_pci_root_cb publish_root_cb)
{
	struct controller_dev_data *dev_data = pdev->pci_dev_data;
	struct controller_list_entry *cntrl_entry;
	int i, root_num, len, err = 0;
	unsigned int domain, bus;
	char str[64];
	struct walk_info info;

	spin_lock(&dev_data->lock);

	list_for_each_entry(cntrl_entry, &dev_data->list, list) {
		/* First publish all the domain:bus info */
		err = publish_root_cb(pdev, cntrl_entry->domain,
				      cntrl_entry->bus);
		if (err)
			goto out;

		/*
		 * Now figure out which root-%d this belongs to
		 * so we can associate resources with it.
		 */
		err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename,
				   "root_num", "%d", &root_num);

		if (err != 1)
			goto out;

		for (i = 0; i < root_num; i++) {
			len = snprintf(str, sizeof(str), "root-%d", i);
			if (unlikely(len >= (sizeof(str) - 1))) {
				err = -ENOMEM;
				goto out;
			}

			err = xenbus_scanf(XBT_NIL, pdev->xdev->nodename,
					   str, "%x:%x", &domain, &bus);
			if (err != 2)
				goto out;

			/* Is this the one we just published? */
			if (domain == cntrl_entry->domain &&
			    bus == cntrl_entry->bus)
				break;
		}

		if (i == root_num)
			goto out;

		info.pdev = pdev;
		info.resource_count = 0;
		info.root_num = i;

		/* Let ACPI do the heavy lifting on decoding resources */
		acpi_walk_resources(cntrl_entry->controller->acpi_handle,
				    METHOD_NAME__CRS, write_xenbus_resource,
				    &info);

		/* No resouces.  OK.  On to the next one */
		if (!info.resource_count)
			continue;

		/* Store the number of resources we wrote for this root-%d */
		len = snprintf(str, sizeof(str), "root-%d-resources", i);
		if (unlikely(len >= (sizeof(str) - 1))) {
			err = -ENOMEM;
			goto out;
		}

		err = xenbus_printf(XBT_NIL, pdev->xdev->nodename, str,
				    "%d", info.resource_count);
		if (err)
			goto out;
	}

	/* Finally, write some magic to synchronize with the guest. */
	len = snprintf(str, sizeof(str), "root-resource-magic");
	if (unlikely(len >= (sizeof(str) - 1))) {
		err = -ENOMEM;
		goto out;
	}

	err = xenbus_printf(XBT_NIL, pdev->xdev->nodename, str,
			    "%lx", (sizeof(struct acpi_resource) * 2) + 1);

out:
	spin_unlock(&dev_data->lock);

	return err;
}

void pciback_release_devices(struct pciback_device *pdev)
{
	struct controller_dev_data *dev_data = pdev->pci_dev_data;
	struct controller_list_entry *cntrl_entry, *c;
	struct controller_dev_entry *dev_entry, *d;

	list_for_each_entry_safe(cntrl_entry, c, &dev_data->list, list) {
		list_for_each_entry_safe(dev_entry, d,
					 &cntrl_entry->dev_list, list) {
			list_del(&dev_entry->list);
			pcistub_put_pci_dev(dev_entry->dev);
			kfree(dev_entry);
		}
		list_del(&cntrl_entry->list);
		kfree(cntrl_entry);
	}

	kfree(dev_data);
	pdev->pci_dev_data = NULL;
}

int pciback_get_pcifront_dev(struct pci_dev *pcidev,
		struct pciback_device *pdev,
		unsigned int *domain, unsigned int *bus, unsigned int *devfn)
{
	struct controller_dev_data *dev_data = pdev->pci_dev_data;
	struct controller_dev_entry *dev_entry;
	struct controller_list_entry *cntrl_entry;
	unsigned long flags;
	int found = 0;
	spin_lock_irqsave(&dev_data->lock, flags);

	list_for_each_entry(cntrl_entry, &dev_data->list, list) {
		list_for_each_entry(dev_entry, &cntrl_entry->dev_list, list) {
			if ((dev_entry->dev->bus->number ==
					pcidev->bus->number) &&
				(dev_entry->dev->devfn ==
					pcidev->devfn) &&
				(pci_domain_nr(dev_entry->dev->bus) ==
					pci_domain_nr(pcidev->bus))) {
				found = 1;
				*domain = cntrl_entry->domain;
				*bus = cntrl_entry->bus;
				*devfn = dev_entry->devfn;
				goto out;
			}
		}
	}
out:
	spin_unlock_irqrestore(&dev_data->lock, flags);
	return found;

}

