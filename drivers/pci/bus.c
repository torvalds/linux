/*
 *	drivers/pci/bus.c
 *
 * From setup-res.c, by:
 *	Dave Rusling (david.rusling@reo.mts.dec.com)
 *	David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *	Ivan Kokshaysky (ink@jurassic.park.msu.ru)
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/slab.h>

#include "pci.h"

void pci_bus_add_resource(struct pci_bus *bus, struct resource *res,
			  unsigned int flags)
{
	struct pci_bus_resource *bus_res;

	bus_res = kzalloc(sizeof(struct pci_bus_resource), GFP_KERNEL);
	if (!bus_res) {
		dev_err(&bus->dev, "can't add %pR resource\n", res);
		return;
	}

	bus_res->res = res;
	bus_res->flags = flags;
	list_add_tail(&bus_res->list, &bus->resources);
}

struct resource *pci_bus_resource_n(const struct pci_bus *bus, int n)
{
	struct pci_bus_resource *bus_res;

	if (n < PCI_BRIDGE_RESOURCE_NUM)
		return bus->resource[n];

	n -= PCI_BRIDGE_RESOURCE_NUM;
	list_for_each_entry(bus_res, &bus->resources, list) {
		if (n-- == 0)
			return bus_res->res;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(pci_bus_resource_n);

void pci_bus_remove_resources(struct pci_bus *bus)
{
	struct pci_bus_resource *bus_res, *tmp;
	int i;

	for (i = 0; i < PCI_BRIDGE_RESOURCE_NUM; i++)
		bus->resource[i] = 0;

	list_for_each_entry_safe(bus_res, tmp, &bus->resources, list) {
		list_del(&bus_res->list);
		kfree(bus_res);
	}
}

/**
 * pci_bus_alloc_resource - allocate a resource from a parent bus
 * @bus: PCI bus
 * @res: resource to allocate
 * @size: size of resource to allocate
 * @align: alignment of resource to allocate
 * @min: minimum /proc/iomem address to allocate
 * @type_mask: IORESOURCE_* type flags
 * @alignf: resource alignment function
 * @alignf_data: data argument for resource alignment function
 *
 * Given the PCI bus a device resides on, the size, minimum address,
 * alignment and type, try to find an acceptable resource allocation
 * for a specific device resource.
 */
int
pci_bus_alloc_resource(struct pci_bus *bus, struct resource *res,
		resource_size_t size, resource_size_t align,
		resource_size_t min, unsigned int type_mask,
		resource_size_t (*alignf)(void *,
					  const struct resource *,
					  resource_size_t,
					  resource_size_t),
		void *alignf_data)
{
	int i, ret = -ENOMEM;
	struct resource *r;
	resource_size_t max = -1;

	type_mask |= IORESOURCE_IO | IORESOURCE_MEM;

	/* don't allocate too high if the pref mem doesn't support 64bit*/
	if (!(res->flags & IORESOURCE_MEM_64))
		max = PCIBIOS_MAX_MEM_32;

	pci_bus_for_each_resource(bus, r, i) {
		if (!r)
			continue;

		/* type_mask must match */
		if ((res->flags ^ r->flags) & type_mask)
			continue;

		/* We cannot allocate a non-prefetching resource
		   from a pre-fetching area */
		if ((r->flags & IORESOURCE_PREFETCH) &&
		    !(res->flags & IORESOURCE_PREFETCH))
			continue;

		/* Ok, try it out.. */
		ret = allocate_resource(r, res, size,
					r->start ? : min,
					max, align,
					alignf, alignf_data);
		if (ret == 0)
			break;
	}
	return ret;
}

/**
 * pci_bus_add_device - add a single device
 * @dev: device to add
 *
 * This adds a single pci device to the global
 * device list and adds sysfs and procfs entries
 */
int pci_bus_add_device(struct pci_dev *dev)
{
	int retval;
	retval = device_add(&dev->dev);
	if (retval)
		return retval;

	dev->is_added = 1;
	pci_proc_attach_device(dev);
	pci_create_sysfs_dev_files(dev);
	return 0;
}

/**
 * pci_bus_add_child - add a child bus
 * @bus: bus to add
 *
 * This adds sysfs entries for a single bus
 */
int pci_bus_add_child(struct pci_bus *bus)
{
	int retval;

	if (bus->bridge)
		bus->dev.parent = bus->bridge;

	retval = device_register(&bus->dev);
	if (retval)
		return retval;

	bus->is_added = 1;

	retval = device_create_file(&bus->dev, &dev_attr_cpuaffinity);
	if (retval)
		return retval;

	retval = device_create_file(&bus->dev, &dev_attr_cpulistaffinity);

	/* Create legacy_io and legacy_mem files for this bus */
	pci_create_legacy_files(bus);

	return retval;
}

/**
 * pci_bus_add_devices - insert newly discovered PCI devices
 * @bus: bus to check for new devices
 *
 * Add newly discovered PCI devices (which are on the bus->devices
 * list) to the global PCI device list, add the sysfs and procfs
 * entries.  Where a bridge is found, add the discovered bus to
 * the parents list of child buses, and recurse (breadth-first
 * to be compatible with 2.4)
 *
 * Call hotplug for each new devices.
 */
void pci_bus_add_devices(const struct pci_bus *bus)
{
	struct pci_dev *dev;
	struct pci_bus *child;
	int retval;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		/* Skip already-added devices */
		if (dev->is_added)
			continue;
		retval = pci_bus_add_device(dev);
		if (retval)
			dev_err(&dev->dev, "Error adding device, continuing\n");
	}

	list_for_each_entry(dev, &bus->devices, bus_list) {
		BUG_ON(!dev->is_added);

		child = dev->subordinate;
		/*
		 * If there is an unattached subordinate bus, attach
		 * it and then scan for unattached PCI devices.
		 */
		if (!child)
			continue;
		if (list_empty(&child->node)) {
			down_write(&pci_bus_sem);
			list_add_tail(&child->node, &dev->bus->children);
			up_write(&pci_bus_sem);
		}
		pci_bus_add_devices(child);

		/*
		 * register the bus with sysfs as the parent is now
		 * properly registered.
		 */
		if (child->is_added)
			continue;
		retval = pci_bus_add_child(child);
		if (retval)
			dev_err(&dev->dev, "Error adding bus, continuing\n");
	}
}

void pci_enable_bridges(struct pci_bus *bus)
{
	struct pci_dev *dev;
	int retval;

	list_for_each_entry(dev, &bus->devices, bus_list) {
		if (dev->subordinate) {
			if (!pci_is_enabled(dev)) {
				retval = pci_enable_device(dev);
				pci_set_master(dev);
			}
			pci_enable_bridges(dev->subordinate);
		}
	}
}

/** pci_walk_bus - walk devices on/under bus, calling callback.
 *  @top      bus whose devices should be walked
 *  @cb       callback to be called for each device found
 *  @userdata arbitrary pointer to be passed to callback.
 *
 *  Walk the given bus, including any bridged devices
 *  on buses under this bus.  Call the provided callback
 *  on each device found.
 *
 *  We check the return of @cb each time. If it returns anything
 *  other than 0, we break out.
 *
 */
void pci_walk_bus(struct pci_bus *top, int (*cb)(struct pci_dev *, void *),
		  void *userdata)
{
	struct pci_dev *dev;
	struct pci_bus *bus;
	struct list_head *next;
	int retval;

	bus = top;
	down_read(&pci_bus_sem);
	next = top->devices.next;
	for (;;) {
		if (next == &bus->devices) {
			/* end of this bus, go up or finish */
			if (bus == top)
				break;
			next = bus->self->bus_list.next;
			bus = bus->self->bus;
			continue;
		}
		dev = list_entry(next, struct pci_dev, bus_list);
		if (dev->subordinate) {
			/* this is a pci-pci bridge, do its devices next */
			next = dev->subordinate->devices.next;
			bus = dev->subordinate;
		} else
			next = dev->bus_list.next;

		/* Run device routines with the device locked */
		device_lock(&dev->dev);
		retval = cb(dev, userdata);
		device_unlock(&dev->dev);
		if (retval)
			break;
	}
	up_read(&pci_bus_sem);
}

EXPORT_SYMBOL(pci_bus_alloc_resource);
EXPORT_SYMBOL_GPL(pci_bus_add_device);
EXPORT_SYMBOL(pci_bus_add_devices);
EXPORT_SYMBOL(pci_enable_bridges);
