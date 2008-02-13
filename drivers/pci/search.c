/*
 * 	PCI searching functions.
 *
 *	Copyright (C) 1993 -- 1997 Drew Eckhardt, Frederic Potter,
 *					David Mosberger-Tang
 *	Copyright (C) 1997 -- 2000 Martin Mares <mj@ucw.cz>
 *	Copyright (C) 2003 -- 2004 Greg Kroah-Hartman <greg@kroah.com>
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include "pci.h"

DECLARE_RWSEM(pci_bus_sem);
/*
 * find the upstream PCIE-to-PCI bridge of a PCI device
 * if the device is PCIE, return NULL
 * if the device isn't connected to a PCIE bridge (that is its parent is a
 * legacy PCI bridge and the bridge is directly connected to bus 0), return its
 * parent
 */
struct pci_dev *
pci_find_upstream_pcie_bridge(struct pci_dev *pdev)
{
	struct pci_dev *tmp = NULL;

	if (pdev->is_pcie)
		return NULL;
	while (1) {
		if (!pdev->bus->self)
			break;
		pdev = pdev->bus->self;
		/* a p2p bridge */
		if (!pdev->is_pcie) {
			tmp = pdev;
			continue;
		}
		/* PCI device should connect to a PCIE bridge */
		if (pdev->pcie_type != PCI_EXP_TYPE_PCI_BRIDGE) {
			/* Busted hardware? */
			WARN_ON_ONCE(1);
			return NULL;
		}
		return pdev;
	}

	return tmp;
}

static struct pci_bus *pci_do_find_bus(struct pci_bus *bus, unsigned char busnr)
{
	struct pci_bus* child;
	struct list_head *tmp;

	if(bus->number == busnr)
		return bus;

	list_for_each(tmp, &bus->children) {
		child = pci_do_find_bus(pci_bus_b(tmp), busnr);
		if(child)
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
struct pci_bus * pci_find_bus(int domain, int busnr)
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

/**
 * pci_find_next_bus - begin or continue searching for a PCI bus
 * @from: Previous PCI bus found, or %NULL for new search.
 *
 * Iterates through the list of known PCI busses.  A new search is
 * initiated by passing %NULL as the @from argument.  Otherwise if
 * @from is not %NULL, searches continue from next device on the
 * global list.
 */
struct pci_bus * 
pci_find_next_bus(const struct pci_bus *from)
{
	struct list_head *n;
	struct pci_bus *b = NULL;

	WARN_ON(in_interrupt());
	down_read(&pci_bus_sem);
	n = from ? from->node.next : pci_root_buses.next;
	if (n != &pci_root_buses)
		b = pci_bus_b(n);
	up_read(&pci_bus_sem);
	return b;
}

#ifdef CONFIG_PCI_LEGACY
/**
 * pci_find_slot - locate PCI device from a given PCI slot
 * @bus: number of PCI bus on which desired PCI device resides
 * @devfn: encodes number of PCI slot in which the desired PCI
 * device resides and the logical device number within that slot
 * in case of multi-function devices.
 *
 * Given a PCI bus and slot/function number, the desired PCI device
 * is located in system global list of PCI devices.  If the device
 * is found, a pointer to its data structure is returned.  If no
 * device is found, %NULL is returned.
 *
 * NOTE: Do not use this function any more; use pci_get_slot() instead, as
 * the PCI device returned by this function can disappear at any moment in
 * time.
 */
struct pci_dev *pci_find_slot(unsigned int bus, unsigned int devfn)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		if (dev->bus->number == bus && dev->devfn == devfn) {
			pci_dev_put(dev);
			return dev;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(pci_find_slot);

/**
 * pci_find_device - begin or continue searching for a PCI device by vendor/device id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is found
 * with a matching @vendor and @device, a pointer to its device structure is
 * returned.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL as the @from argument.
 * Otherwise if @from is not %NULL, searches continue from next device
 * on the global list.
 *
 * NOTE: Do not use this function any more; use pci_get_device() instead, as
 * the PCI device returned by this function can disappear at any moment in
 * time.
 */
struct pci_dev *pci_find_device(unsigned int vendor, unsigned int device,
				const struct pci_dev *from)
{
	struct pci_dev *pdev;

	pdev = pci_get_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
	pci_dev_put(pdev);
	return pdev;
}
EXPORT_SYMBOL(pci_find_device);
#endif /* CONFIG_PCI_LEGACY */

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
struct pci_dev * pci_get_slot(struct pci_bus *bus, unsigned int devfn)
{
	struct list_head *tmp;
	struct pci_dev *dev;

	WARN_ON(in_interrupt());
	down_read(&pci_bus_sem);

	list_for_each(tmp, &bus->devices) {
		dev = pci_dev_b(tmp);
		if (dev->devfn == devfn)
			goto out;
	}

	dev = NULL;
 out:
	pci_dev_get(dev);
	up_read(&pci_bus_sem);
	return dev;
}

/**
 * pci_get_bus_and_slot - locate PCI device from a given PCI bus & slot
 * @bus: number of PCI bus on which desired PCI device resides
 * @devfn: encodes number of PCI slot in which the desired PCI
 * device resides and the logical device number within that slot
 * in case of multi-function devices.
 *
 * Note: the bus/slot search is limited to PCI domain (segment) 0.
 *
 * Given a PCI bus and slot/function number, the desired PCI device
 * is located in system global list of PCI devices.  If the device
 * is found, a pointer to its data structure is returned.  If no
 * device is found, %NULL is returned. The returned device has its
 * reference count bumped by one.
 */

struct pci_dev * pci_get_bus_and_slot(unsigned int bus, unsigned int devfn)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		if (pci_domain_nr(dev->bus) == 0 &&
		   (dev->bus->number == bus && dev->devfn == devfn))
			return dev;
	}
	return NULL;
}

static int match_pci_dev_by_id(struct device *dev, void *data)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct pci_device_id *id = data;

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
					 const struct pci_dev *from)
{
	struct device *dev;
	struct device *dev_start = NULL;
	struct pci_dev *pdev = NULL;

	WARN_ON(in_interrupt());
	if (from) {
		/* FIXME
		 * take the cast off, when bus_find_device is made const.
		 */
		dev_start = (struct device *)&from->dev;
	}
	dev = bus_find_device(&pci_bus_type, dev_start, (void *)id,
			      match_pci_dev_by_id);
	if (dev)
		pdev = to_pci_dev(dev);
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
			       const struct pci_dev *from)
{
	struct pci_dev *pdev;
	struct pci_device_id *id;

	/*
	 * pci_find_subsys() can be called on the ide_setup() path,
	 * super-early in boot.  But the down_read() will enable local
	 * interrupts, which can cause some machines to crash.  So here we
	 * detect and flag that situation and bail out early.
	 */
	if (unlikely(no_pci_devices()))
		return NULL;

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return NULL;
	id->vendor = vendor;
	id->device = device;
	id->subvendor = ss_vendor;
	id->subdevice = ss_device;

	pdev = pci_get_dev_by_id(id, from);
	kfree(id);

	return pdev;
}

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
struct pci_dev *
pci_get_device(unsigned int vendor, unsigned int device, struct pci_dev *from)
{
	return pci_get_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
}

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
	struct pci_dev *dev;
	struct pci_device_id *id;

	id = kzalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return NULL;
	id->vendor = id->device = id->subvendor = id->subdevice = PCI_ANY_ID;
	id->class_mask = PCI_ANY_ID;
	id->class = class;

	dev = pci_get_dev_by_id(id, from);
	kfree(id);
	return dev;
}

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
		if (found)
			goto exit;
		ids++;
	}
exit:
	if (found)
		return 1;
	return 0;
}
EXPORT_SYMBOL(pci_dev_present);

/* For boot time work */
EXPORT_SYMBOL(pci_find_bus);
EXPORT_SYMBOL(pci_find_next_bus);
/* For everyone */
EXPORT_SYMBOL(pci_get_device);
EXPORT_SYMBOL(pci_get_subsys);
EXPORT_SYMBOL(pci_get_slot);
EXPORT_SYMBOL(pci_get_bus_and_slot);
EXPORT_SYMBOL(pci_get_class);
