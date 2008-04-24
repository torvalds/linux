/*
 * Fake PCI Hot Plug Controller Driver
 *
 * Copyright (C) 2003 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2003 IBM Corp.
 * Copyright (C) 2003 Rolf Eike Beer <eike-kernel@sf-tec.de>
 *
 * Based on ideas and code from:
 * 	Vladimir Kondratiev <vladimir.kondratiev@intel.com>
 *	Rolf Eike Beer <eike-kernel@sf-tec.de>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * Send feedback to <greg@kroah.com>
 */

/*
 *
 * This driver will "emulate" removing PCI devices from the system.  If
 * the "power" file is written to with "0" then the specified PCI device
 * will be completely removed from the kernel.
 *
 * WARNING, this does NOT turn off the power to the PCI device.  This is
 * a "logical" removal, not a physical or electrical removal.
 *
 * Use this module at your own risk, you have been warned!
 *
 * Enabling PCI devices is left as an exercise for the reader...
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "../pci.h"

#if !defined(MODULE)
	#define MY_NAME	"fakephp"
#else
	#define MY_NAME	THIS_MODULE->name
#endif

#define dbg(format, arg...)					\
	do {							\
		if (debug)					\
			printk(KERN_DEBUG "%s: " format,	\
				MY_NAME , ## arg); 		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME , ## arg)

#define DRIVER_AUTHOR	"Greg Kroah-Hartman <greg@kroah.com>"
#define DRIVER_DESC	"Fake PCI Hot Plug Controller Driver"

struct dummy_slot {
	struct list_head node;
	struct hotplug_slot *slot;
	struct pci_dev *dev;
	struct work_struct remove_work;
	unsigned long removed;
};

static int debug;
static LIST_HEAD(slot_list);
static struct workqueue_struct *dummyphp_wq;

static void pci_rescan_worker(struct work_struct *work);
static DECLARE_WORK(pci_rescan_work, pci_rescan_worker);

static int enable_slot (struct hotplug_slot *slot);
static int disable_slot (struct hotplug_slot *slot);

static struct hotplug_slot_ops dummy_hotplug_slot_ops = {
	.owner			= THIS_MODULE,
	.enable_slot		= enable_slot,
	.disable_slot		= disable_slot,
};

static void dummy_release(struct hotplug_slot *slot)
{
	struct dummy_slot *dslot = slot->private;

	list_del(&dslot->node);
	kfree(dslot->slot->info);
	kfree(dslot->slot);
	pci_dev_put(dslot->dev);
	kfree(dslot);
}

static int add_slot(struct pci_dev *dev)
{
	struct dummy_slot *dslot;
	struct hotplug_slot *slot;
	int retval = -ENOMEM;

	slot = kzalloc(sizeof(struct hotplug_slot), GFP_KERNEL);
	if (!slot)
		goto error;

	slot->info = kzalloc(sizeof(struct hotplug_slot_info), GFP_KERNEL);
	if (!slot->info)
		goto error_slot;

	slot->info->power_status = 1;
	slot->info->max_bus_speed = PCI_SPEED_UNKNOWN;
	slot->info->cur_bus_speed = PCI_SPEED_UNKNOWN;

	slot->name = &dev->dev.bus_id[0];
	dbg("slot->name = %s\n", slot->name);

	dslot = kzalloc(sizeof(struct dummy_slot), GFP_KERNEL);
	if (!dslot)
		goto error_info;

	slot->ops = &dummy_hotplug_slot_ops;
	slot->release = &dummy_release;
	slot->private = dslot;

	retval = pci_hp_register(slot);
	if (retval) {
		err("pci_hp_register failed with error %d\n", retval);
		goto error_dslot;
	}

	dslot->slot = slot;
	dslot->dev = pci_dev_get(dev);
	list_add (&dslot->node, &slot_list);
	return retval;

error_dslot:
	kfree(dslot);
error_info:
	kfree(slot->info);
error_slot:
	kfree(slot);
error:
	return retval;
}

static int __init pci_scan_buses(void)
{
	struct pci_dev *dev = NULL;
	int retval = 0;

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		retval = add_slot(dev);
		if (retval) {
			pci_dev_put(dev);
			break;
		}
	}

	return retval;
}

static void remove_slot(struct dummy_slot *dslot)
{
	int retval;

	dbg("removing slot %s\n", dslot->slot->name);
	retval = pci_hp_deregister(dslot->slot);
	if (retval)
		err("Problem unregistering a slot %s\n", dslot->slot->name);
}

/* called from the single-threaded workqueue handler to remove a slot */
static void remove_slot_worker(struct work_struct *work)
{
	struct dummy_slot *dslot =
		container_of(work, struct dummy_slot, remove_work);
	remove_slot(dslot);
}

/**
 * pci_rescan_slot - Rescan slot
 * @temp: Device template. Should be set: bus and devfn.
 *
 * Tries hard not to re-enable already existing devices;
 * also handles scanning of subfunctions.
 */
static void pci_rescan_slot(struct pci_dev *temp)
{
	struct pci_bus *bus = temp->bus;
	struct pci_dev *dev;
	int func;
	int retval;
	u8 hdr_type;

	if (!pci_read_config_byte(temp, PCI_HEADER_TYPE, &hdr_type)) {
		temp->hdr_type = hdr_type & 0x7f;
		if ((dev = pci_get_slot(bus, temp->devfn)) != NULL)
			pci_dev_put(dev);
		else {
			dev = pci_scan_single_device(bus, temp->devfn);
			if (dev) {
				dbg("New device on %s function %x:%x\n",
					bus->name, temp->devfn >> 3,
					temp->devfn & 7);
				retval = pci_bus_add_device(dev);
				if (retval)
					dev_err(&dev->dev, "error adding "
						"device, continuing.\n");
				else
					add_slot(dev);
			}
		}
		/* multifunction device? */
		if (!(hdr_type & 0x80))
			return;

		/* continue scanning for other functions */
		for (func = 1, temp->devfn++; func < 8; func++, temp->devfn++) {
			if (pci_read_config_byte(temp, PCI_HEADER_TYPE, &hdr_type))
				continue;
			temp->hdr_type = hdr_type & 0x7f;

			if ((dev = pci_get_slot(bus, temp->devfn)) != NULL)
				pci_dev_put(dev);
			else {
				dev = pci_scan_single_device(bus, temp->devfn);
				if (dev) {
					dbg("New device on %s function %x:%x\n",
						bus->name, temp->devfn >> 3,
						temp->devfn & 7);
					retval = pci_bus_add_device(dev);
					if (retval)
						dev_err(&dev->dev, "error adding "
							"device, continuing.\n");
					else
						add_slot(dev);
				}
			}
		}
	}
}


/**
 * pci_rescan_bus - Rescan PCI bus
 * @bus: the PCI bus to rescan
 *
 * Call pci_rescan_slot for each possible function of the bus.
 */
static void pci_rescan_bus(const struct pci_bus *bus)
{
	unsigned int devfn;
	struct pci_dev *dev;
	dev = alloc_pci_dev();
	if (!dev)
		return;

	dev->bus = (struct pci_bus*)bus;
	dev->sysdata = bus->sysdata;
	for (devfn = 0; devfn < 0x100; devfn += 8) {
		dev->devfn = devfn;
		pci_rescan_slot(dev);
	}
	kfree(dev);
}

/* recursively scan all buses */
static void pci_rescan_buses(const struct list_head *list)
{
	const struct list_head *l;
	list_for_each(l,list) {
		const struct pci_bus *b = pci_bus_b(l);
		pci_rescan_bus(b);
		pci_rescan_buses(&b->children);
	}
}

/* initiate rescan of all pci buses */
static inline void pci_rescan(void) {
	pci_rescan_buses(&pci_root_buses);
}

/* called from the single-threaded workqueue handler to rescan all pci buses */
static void pci_rescan_worker(struct work_struct *work)
{
	pci_rescan();
}

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	/* mis-use enable_slot for rescanning of the pci bus */
	cancel_work_sync(&pci_rescan_work);
	queue_work(dummyphp_wq, &pci_rescan_work);
	return 0;
}

/* find the hotplug_slot for the pci_dev */
static struct hotplug_slot *get_slot_from_dev(struct pci_dev *dev)
{
	struct dummy_slot *dslot;

	list_for_each_entry(dslot, &slot_list, node) {
		if (dslot->dev == dev)
			return dslot->slot;
	}
	return NULL;
}


static int disable_slot(struct hotplug_slot *slot)
{
	struct dummy_slot *dslot;
	struct hotplug_slot *hslot;
	struct pci_dev *dev;
	int func;

	if (!slot)
		return -ENODEV;
	dslot = slot->private;

	dbg("%s - physical_slot = %s\n", __func__, slot->name);

	/* don't disable bridged devices just yet, we can't handle them easily... */
	if (dslot->dev->subordinate) {
		err("Can't remove PCI devices with other PCI devices behind it yet.\n");
		return -ENODEV;
	}
	if (test_and_set_bit(0, &dslot->removed)) {
		dbg("Slot already scheduled for removal\n");
		return -ENODEV;
	}
	/* search for subfunctions and disable them first */
	if (!(dslot->dev->devfn & 7)) {
		for (func = 1; func < 8; func++) {
			dev = pci_get_slot(dslot->dev->bus,
					dslot->dev->devfn + func);
			if (dev) {
				hslot = get_slot_from_dev(dev);
				if (hslot)
					disable_slot(hslot);
				else {
					err("Hotplug slot not found for subfunction of PCI device\n");
					return -ENODEV;
				}
				pci_dev_put(dev);
			} else
				dbg("No device in slot found\n");
		}
	}

	/* remove the device from the pci core */
	pci_remove_bus_device(dslot->dev);

	/* queue work item to blow away this sysfs entry and other parts. */
	INIT_WORK(&dslot->remove_work, remove_slot_worker);
	queue_work(dummyphp_wq, &dslot->remove_work);

	return 0;
}

static void cleanup_slots (void)
{
	struct list_head *tmp;
	struct list_head *next;
	struct dummy_slot *dslot;

	destroy_workqueue(dummyphp_wq);
	list_for_each_safe (tmp, next, &slot_list) {
		dslot = list_entry (tmp, struct dummy_slot, node);
		remove_slot(dslot);
	}
	
}

static int __init dummyphp_init(void)
{
	info(DRIVER_DESC "\n");

	dummyphp_wq = create_singlethread_workqueue(MY_NAME);
	if (!dummyphp_wq)
		return -ENOMEM;

	return pci_scan_buses();
}


static void __exit dummyphp_exit(void)
{
	cleanup_slots();
}

module_init(dummyphp_init);
module_exit(dummyphp_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

