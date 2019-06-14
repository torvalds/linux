// SPDX-License-Identifier: GPL-2.0
/*
 * PCI searching functions
 *
 * Copyright (C) 1993 -- 1997 Drew Eckhardt, Frederic Potter,
 *					David Mosberger-Tang
 * Copyright (C) 1997 -- 2000 Martin Mares <mj@ucw.cz>
 * Copyright (C) 2003 -- 2004 Greg Kroah-Hartman <greg@kroah.com>
 */

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include "pci.h"

DECLARE_RWSEM(pci_bus_sem);
EXPORT_SYMBOL_GPL(pci_bus_sem);

/*
 * pci_for_each_dma_alias - Iterate over DMA aliases for a device
 * @pdev: starting downstream device
 * @fn: function to call for each alias
 * @data: opaque data to pass to @fn
 *
 * Starting @pdev, walk up the bus calling @fn for each possible alias
 * of @pdev at the root bus.
 */
int pci_for_each_dma_alias(struct pci_dev *pdev,
			   int (*fn)(struct pci_dev *pdev,
				     u16 alias, void *data), void *data)
{
	struct pci_bus *bus;
	int ret;

	ret = fn(pdev, pci_dev_id(pdev), data);
	if (ret)
		return ret;

	/*
	 * If the device is broken and uses an alias requester ID for
	 * DMA, iterate over that too.
	 */
	if (unlikely(pdev->dma_alias_mask)) {
		u8 devfn;

		for_each_set_bit(devfn, pdev->dma_alias_mask, U8_MAX) {
			ret = fn(pdev, PCI_DEVID(pdev->bus->number, devfn),
				 data);
			if (ret)
				return ret;
		}
	}

	for (bus = pdev->bus; !pci_is_root_bus(bus); bus = bus->parent) {
		struct pci_dev *tmp;

		/* Skip virtual buses */
		if (!bus->self)
			continue;

		tmp = bus->self;

		/* stop at bridge where translation unit is associated */
		if (tmp->dev_flags & PCI_DEV_FLAGS_BRIDGE_XLATE_ROOT)
			return ret;

		/*
		 * PCIe-to-PCI/X bridges alias transactions from downstream
		 * devices using the subordinate bus number (PCI Express to
		 * PCI/PCI-X Bridge Spec, rev 1.0, sec 2.3).  For all cases
		 * where the upstream bus is PCI/X we alias to the bridge
		 * (there are various conditions in the previous reference
		 * where the bridge may take ownership of transactions, even
		 * when the secondary interface is PCI-X).
		 */
		if (pci_is_pcie(tmp)) {
			switch (pci_pcie_type(tmp)) {
			case PCI_EXP_TYPE_ROOT_PORT:
			case PCI_EXP_TYPE_UPSTREAM:
			case PCI_EXP_TYPE_DOWNSTREAM:
				continue;
			case PCI_EXP_TYPE_PCI_BRIDGE:
				ret = fn(tmp,
					 PCI_DEVID(tmp->subordinate->number,
						   PCI_DEVFN(0, 0)), data);
				if (ret)
					return ret;
				continue;
			case PCI_EXP_TYPE_PCIE_BRIDGE:
				ret = fn(tmp, pci_dev_id(tmp), data);
				if (ret)
					return ret;
				continue;
			}
		} else {
			if (tmp->dev_flags & PCI_DEV_FLAG_PCIE_BRIDGE_ALIAS)
				ret = fn(tmp,
					 PCI_DEVID(tmp->subordinate->number,
						   PCI_DEVFN(0, 0)), data);
			else
				ret = fn(tmp, pci_dev_id(tmp), data);
			if (ret)
				return ret;
		}
	}

	return ret;
}

static struct pci_bus *pci_do_find_bus(struct pci_bus *bus, unsigned char busnr)
{
	struct pci_bus *child;
	struct pci_bus *tmp;

	if (bus->number == busnr)
		return bus;

	list_for_each_entry(tmp, &bus->children, node) {
		child = pci_do_find_bus(tmp, busnr);
		if (child)
			return child;
	}
	return NULL;
}

/**
 * pci_find_bus - locate PCI bus from a given domain and bus number
 * @domain: number of PCI domain to search
 * @busnr: number of desired PCI bus
 *
 * Given a PCI bus number and domain number, the desired PCI bus is located
 * in the global list of PCI buses.  If the bus is found, a pointer to its
 * data structure is returned.  If no bus is found, %NULL is returned.
 */
struct pci_bus *pci_find_bus(int domain, int busnr)
{
	struct pci_bus *bus = NULL;
	struct pci_bus *tmp_bus;

	while ((bus = pci_find_next_bus(bus)) != NULL)  {
		if (pci_domain_nr(bus) != domain)
			continue;
		tmp_bus = pci_do_find_bus(bus, busnr);
		if (tmp_bus)
			return tmp_bus;
	}
	return NULL;
}
EXPORT_SYMBOL(pci_find_bus);

/**
 * pci_find_next_bus - begin or continue searching for a PCI bus
 * @from: Previous PCI bus found, or %NULL for new search.
 *
 * Iterates through the list of known PCI buses.  A new search is
 * initiated by passing %NULL as the @from argument.  Otherwise if
 * @from is not %NULL, searches continue from next device on the
 * global list.
 */
struct pci_bus *pci_find_next_bus(const struct pci_bus *from)
{
	struct list_head *n;
	struct pci_bus *b = NULL;

	WARN_ON(in_interrupt());
	down_read(&pci_bus_sem);
	n = from ? from->node.next : pci_root_buses.next;
	if (n != &pci_root_buses)
		b = list_entry(n, struct pci_bus, node);
	up_read(&pci_bus_sem);
	return b;
}
EXPORT_SYMBOL(pci_find_next_bus);

/**
 * pci_get_slot - locate PCI device for a given PCI slot
 * @bus: PCI bus on which desired PCI device resides
 * @devfn: encodes number of PCI slot in which the desired PCI
 * device resides and the logical device number within that slot
 * in case of multi-function devices.
 *
 * Given a PCI bus and slot/function number, the desired PCI device
 * is located in the list of PCI devices.
 * If the device is found, its reference count is increased and this
 * function returns a pointer to its data structure.  The caller must
 * decrement the reference count by calling pci_dev_put().
 * If no device is found, %NULL is returned.
 */
struct pci_dev *pci_get_slot(struct pci_bus *bus, unsigned int devfn)
{
	struct pci_dev *dev;

	WARN_ON(in_interrupt());
	down_read(&pci_bus_sem);

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->devfn == devfn)
			goto out;
	}

	dev = NULL;
 out:
	pci_dev_get(dev);
	up_read(&pci_bus_sem);
	return dev;
}
EXPORT_SYMBOL(pci_get_slot);

/**
 * pci_get_domain_bus_and_slot - locate PCI device for a given PCI domain (segment), bus, and slot
 * @domain: PCI domain/segment on which the PCI device resides.
 * @bus: PCI bus on which desired PCI device resides
 * @devfn: encodes number of PCI slot in which the desired PCI device
 * resides and the logical device number within that slot in case of
 * multi-function devices.
 *
 * Given a PCI domain, bus, and slot/function number, the desired PCI
 * device is located in the list of PCI devices. If the device is
 * found, its reference count is increased and this function returns a
 * pointer to its data structure.  The caller must decrement the
 * reference count by calling pci_dev_put().  If no device is found,
 * %NULL is returned.
 */
struct pci_dev *pci_get_domain_bus_and_slot(int domain, unsigned int bus,
					    unsigned int devfn)
{
	struct pci_dev *dev = NULL;

	for_each_pci_dev(dev) {
		if (pci_domain_nr(dev->bus) == domain &&
		    (dev->bus->number == bus && dev->devfn == devfn))
			return dev;
	}
	return NULL;
}
EXPORT_SYMBOL(pci_get_domain_bus_and_slot);

static int match_pci_dev_by_id(struct device *dev, const void *data)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	const struct pci_device_id *id = data;

	if (pci_match_one_device(id, pdev))
		return 1;
	return 0;
}

/*
 * pci_get_dev_by_id - begin or continue searching for a PCI device by id
 * @id: pointer to struct pci_device_id to match for the device
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is found
 * with a matching id a pointer to its device structure is returned, and the
 * reference count to the device is incremented.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL as the @from argument.  Otherwise
 * if @from is not %NULL, searches continue from next device on the global
 * list.  The reference count for @from is always decremented if it is not
 * %NULL.
 *
 * This is an internal function for use by the other search functions in
 * this file.
 */
static struct pci_dev *pci_get_dev_by_id(const struct pci_device_id *id,
					 struct pci_dev *from)
{
	struct device *dev;
	struct device *dev_start = NULL;
	struct pci_dev *pdev = NULL;

	WARN_ON(in_interrupt());
	if (from)
		dev_start = &from->dev;
	dev = bus_find_device(&pci_bus_type, dev_start, (void *)id,
			      match_pci_dev_by_id);
	if (dev)
		pdev = to_pci_dev(dev);
	pci_dev_put(from);
	return pdev;
}

/**
 * pci_get_subsys - begin or continue searching for a PCI device by vendor/subvendor/device/subdevice id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @ss_vendor: PCI subsystem vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @ss_device: PCI subsystem device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is found
 * with a matching @vendor, @device, @ss_vendor and @ss_device, a pointer to its
 * device structure is returned, and the reference count to the device is
 * incremented.  Otherwise, %NULL is returned.  A new search is initiated by
 * passing %NULL as the @from argument.  Otherwise if @from is not %NULL,
 * searches continue from next device on the global list.
 * The reference count for @from is always decremented if it is not %NULL.
 */
struct pci_dev *pci_get_subsys(unsigned int vendor, unsigned int device,
			       unsigned int ss_vendor, unsigned int ss_device,
			       struct pci_dev *from)
{
	struct pci_device_id id = {
		.vendor = vendor,
		.device = device,
		.subvendor = ss_vendor,
		.subdevice = ss_device,
	};

	return pci_get_dev_by_id(&id, from);
}
EXPORT_SYMBOL(pci_get_subsys);

/**
 * pci_get_device - begin or continue searching for a PCI device by vendor/device id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is
 * found with a matching @vendor and @device, the reference count to the
 * device is incremented and a pointer to its device structure is returned.
 * Otherwise, %NULL is returned.  A new search is initiated by passing %NULL
 * as the @from argument.  Otherwise if @from is not %NULL, searches continue
 * from next device on the global list.  The reference count for @from is
 * always decremented if it is not %NULL.
 */
struct pci_dev *pci_get_device(unsigned int vendor, unsigned int device,
			       struct pci_dev *from)
{
	return pci_get_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
}
EXPORT_SYMBOL(pci_get_device);

/**
 * pci_get_class - begin or continue searching for a PCI device by class
 * @class: search for a PCI device with this class designation
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is
 * found with a matching @class, the reference count to the device is
 * incremented and a pointer to its device structure is returned.
 * Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL as the @from argument.
 * Otherwise if @from is not %NULL, searches continue from next device
 * on the global list.  The reference count for @from is always decremented
 * if it is not %NULL.
 */
struct pci_dev *pci_get_class(unsigned int class, struct pci_dev *from)
{
	struct pci_device_id id = {
		.vendor = PCI_ANY_ID,
		.device = PCI_ANY_ID,
		.subvendor = PCI_ANY_ID,
		.subdevice = PCI_ANY_ID,
		.class_mask = PCI_ANY_ID,
		.class = class,
	};

	return pci_get_dev_by_id(&id, from);
}
EXPORT_SYMBOL(pci_get_class);

/**
 * pci_dev_present - Returns 1 if device matching the device list is present, 0 if not.
 * @ids: A pointer to a null terminated list of struct pci_device_id structures
 * that describe the type of PCI device the caller is trying to find.
 *
 * Obvious fact: You do not have a reference to any device that might be found
 * by this function, so if that device is removed from the system right after
 * this function is finished, the value will be stale.  Use this function to
 * find devices that are usually built into a system, or for a general hint as
 * to if another device happens to be present at this specific moment in time.
 */
int pci_dev_present(const struct pci_device_id *ids)
{
	struct pci_dev *found = NULL;

	WARN_ON(in_interrupt());
	while (ids->vendor || ids->subvendor || ids->class_mask) {
		found = pci_get_dev_by_id(ids, NULL);
		if (found) {
			pci_dev_put(found);
			return 1;
		}
		ids++;
	}

	return 0;
}
EXPORT_SYMBOL(pci_dev_present);
