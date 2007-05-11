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
 */
struct pci_dev *
pci_find_slot(unsigned int bus, unsigned int devfn)
{
	struct pci_dev *dev = NULL;

	while ((dev = pci_find_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		if (dev->bus->number == bus && dev->devfn == devfn)
			return dev;
	}
	return NULL;
}

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
 * pci_get_bus_and_slot - locate PCI device from a given PCI slot
 * @bus: number of PCI bus on which desired PCI device resides
 * @devfn: encodes number of PCI slot in which the desired PCI
 * device resides and the logical device number within that slot
 * in case of multi-function devices.
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
		if (dev->bus->number == bus && dev->devfn == devfn)
			return dev;
	}
	return NULL;
}

/**
 * pci_find_subsys - begin or continue searching for a PCI device by vendor/subvendor/device/subdevice id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @ss_vendor: PCI subsystem vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @ss_device: PCI subsystem device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is
 * found with a matching @vendor, @device, @ss_vendor and @ss_device, a
 * pointer to its device structure is returned.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL as the @from argument.
 * Otherwise if @from is not %NULL, searches continue from next device
 * on the global list.
 *
 * NOTE: Do not use this function any more; use pci_get_subsys() instead, as
 * the PCI device returned by this function can disappear at any moment in
 * time.
 */
static struct pci_dev * pci_find_subsys(unsigned int vendor,
				        unsigned int device,
					unsigned int ss_vendor,
					unsigned int ss_device,
					const struct pci_dev *from)
{
	struct list_head *n;
	struct pci_dev *dev;

	WARN_ON(in_interrupt());

	/*
	 * pci_find_subsys() can be called on the ide_setup() path, super-early
	 * in boot.  But the down_read() will enable local interrupts, which
	 * can cause some machines to crash.  So here we detect and flag that
	 * situation and bail out early.
	 */
	if (unlikely(list_empty(&pci_devices)))
		return NULL;
	down_read(&pci_bus_sem);
	n = from ? from->global_list.next : pci_devices.next;

	while (n && (n != &pci_devices)) {
		dev = pci_dev_g(n);
		if ((vendor == PCI_ANY_ID || dev->vendor == vendor) &&
		    (device == PCI_ANY_ID || dev->device == device) &&
		    (ss_vendor == PCI_ANY_ID || dev->subsystem_vendor == ss_vendor) &&
		    (ss_device == PCI_ANY_ID || dev->subsystem_device == ss_device))
			goto exit;
		n = n->next;
	}
	dev = NULL;
exit:
	up_read(&pci_bus_sem);
	return dev;
}

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
struct pci_dev *
pci_find_device(unsigned int vendor, unsigned int device, const struct pci_dev *from)
{
	return pci_find_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
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
struct pci_dev * 
pci_get_subsys(unsigned int vendor, unsigned int device,
	       unsigned int ss_vendor, unsigned int ss_device,
	       struct pci_dev *from)
{
	struct list_head *n;
	struct pci_dev *dev;

	WARN_ON(in_interrupt());

	/*
	 * pci_get_subsys() can potentially be called by drivers super-early
	 * in boot.  But the down_read() will enable local interrupts, which
	 * can cause some machines to crash.  So here we detect and flag that
	 * situation and bail out early.
	 */
	if (unlikely(list_empty(&pci_devices)))
		return NULL;
	down_read(&pci_bus_sem);
	n = from ? from->global_list.next : pci_devices.next;

	while (n && (n != &pci_devices)) {
		dev = pci_dev_g(n);
		if ((vendor == PCI_ANY_ID || dev->vendor == vendor) &&
		    (device == PCI_ANY_ID || dev->device == device) &&
		    (ss_vendor == PCI_ANY_ID || dev->subsystem_vendor == ss_vendor) &&
		    (ss_device == PCI_ANY_ID || dev->subsystem_device == ss_device))
			goto exit;
		n = n->next;
	}
	dev = NULL;
exit:
	dev = pci_dev_get(dev);
	up_read(&pci_bus_sem);
	pci_dev_put(from);
	return dev;
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
 * pci_get_device_reverse - begin or continue searching for a PCI device by vendor/device id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices in the reverse order of
 * pci_get_device.
 * If a PCI device is found with a matching @vendor and @device, the reference
 * count to the  device is incremented and a pointer to its device structure
 * is returned Otherwise, %NULL is returned.  A new search is initiated by
 * passing %NULL as the @from argument.  Otherwise if @from is not %NULL,
 * searches continue from next device on the global list.  The reference
 * count for @from is always decremented if it is not %NULL.
 */
struct pci_dev *
pci_get_device_reverse(unsigned int vendor, unsigned int device, struct pci_dev *from)
{
	struct list_head *n;
	struct pci_dev *dev;

	WARN_ON(in_interrupt());
	down_read(&pci_bus_sem);
	n = from ? from->global_list.prev : pci_devices.prev;

	while (n && (n != &pci_devices)) {
		dev = pci_dev_g(n);
		if ((vendor == PCI_ANY_ID || dev->vendor == vendor) &&
		    (device == PCI_ANY_ID || dev->device == device))
			goto exit;
		n = n->prev;
	}
	dev = NULL;
exit:
	dev = pci_dev_get(dev);
	up_read(&pci_bus_sem);
	pci_dev_put(from);
	return dev;
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
	struct list_head *n;
	struct pci_dev *dev;

	WARN_ON(in_interrupt());
	down_read(&pci_bus_sem);
	n = from ? from->global_list.next : pci_devices.next;

	while (n && (n != &pci_devices)) {
		dev = pci_dev_g(n);
		if (dev->class == class)
			goto exit;
		n = n->next;
	}
	dev = NULL;
exit:
	dev = pci_dev_get(dev);
	up_read(&pci_bus_sem);
	pci_dev_put(from);
	return dev;
}

const struct pci_device_id *pci_find_present(const struct pci_device_id *ids)
{
	struct pci_dev *dev;
	const struct pci_device_id *found = NULL;

	WARN_ON(in_interrupt());
	down_read(&pci_bus_sem);
	while (ids->vendor || ids->subvendor || ids->class_mask) {
		list_for_each_entry(dev, &pci_devices, global_list) {
			if ((found = pci_match_one_device(ids, dev)) != NULL)
				goto exit;
		}
		ids++;
	}
exit:
	up_read(&pci_bus_sem);
	return found;
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
	return pci_find_present(ids) == NULL ? 0 : 1;
}

EXPORT_SYMBOL(pci_dev_present);
EXPORT_SYMBOL(pci_find_present);

EXPORT_SYMBOL(pci_find_device);
EXPORT_SYMBOL(pci_find_slot);
/* For boot time work */
EXPORT_SYMBOL(pci_find_bus);
EXPORT_SYMBOL(pci_find_next_bus);
/* For everyone */
EXPORT_SYMBOL(pci_get_device);
EXPORT_SYMBOL(pci_get_device_reverse);
EXPORT_SYMBOL(pci_get_subsys);
EXPORT_SYMBOL(pci_get_slot);
EXPORT_SYMBOL(pci_get_bus_and_slot);
EXPORT_SYMBOL(pci_get_class);
