/*
 * drivers/pci/slot.c
 * Copyright (C) 2006 Matthew Wilcox <matthew@wil.cx>
 * Copyright (C) 2006-2008 Hewlett-Packard Development Company, L.P.
 * 	Alex Chiang <achiang@hp.com>
 */

#include <linux/kobject.h>
#include <linux/pci.h>
#include <linux/err.h>
#include "pci.h"

struct kset *pci_slots_kset;
EXPORT_SYMBOL_GPL(pci_slots_kset);

static ssize_t pci_slot_attr_show(struct kobject *kobj,
					struct attribute *attr, char *buf)
{
	struct pci_slot *slot = to_pci_slot(kobj);
	struct pci_slot_attribute *attribute = to_pci_slot_attr(attr);
	return attribute->show ? attribute->show(slot, buf) : -EIO;
}

static ssize_t pci_slot_attr_store(struct kobject *kobj,
			struct attribute *attr, const char *buf, size_t len)
{
	struct pci_slot *slot = to_pci_slot(kobj);
	struct pci_slot_attribute *attribute = to_pci_slot_attr(attr);
	return attribute->store ? attribute->store(slot, buf, len) : -EIO;
}

static struct sysfs_ops pci_slot_sysfs_ops = {
	.show = pci_slot_attr_show,
	.store = pci_slot_attr_store,
};

static ssize_t address_read_file(struct pci_slot *slot, char *buf)
{
	if (slot->number == 0xff)
		return sprintf(buf, "%04x:%02x\n",
				pci_domain_nr(slot->bus),
				slot->bus->number);
	else
		return sprintf(buf, "%04x:%02x:%02x\n",
				pci_domain_nr(slot->bus),
				slot->bus->number,
				slot->number);
}

static void pci_slot_release(struct kobject *kobj)
{
	struct pci_dev *dev;
	struct pci_slot *slot = to_pci_slot(kobj);

	pr_debug("%s: releasing pci_slot on %x:%d\n", __func__,
		 slot->bus->number, slot->number);

	list_for_each_entry(dev, &slot->bus->devices, bus_list)
		if (PCI_SLOT(dev->devfn) == slot->number)
			dev->slot = NULL;

	list_del(&slot->list);

	kfree(slot);
}

static struct pci_slot_attribute pci_slot_attr_address =
	__ATTR(address, (S_IFREG | S_IRUGO), address_read_file, NULL);

static struct attribute *pci_slot_default_attrs[] = {
	&pci_slot_attr_address.attr,
	NULL,
};

static struct kobj_type pci_slot_ktype = {
	.sysfs_ops = &pci_slot_sysfs_ops,
	.release = &pci_slot_release,
	.default_attrs = pci_slot_default_attrs,
};

/**
 * pci_create_slot - create or increment refcount for physical PCI slot
 * @parent: struct pci_bus of parent bridge
 * @slot_nr: PCI_SLOT(pci_dev->devfn) or -1 for placeholder
 * @name: user visible string presented in /sys/bus/pci/slots/<name>
 *
 * PCI slots have first class attributes such as address, speed, width,
 * and a &struct pci_slot is used to manage them. This interface will
 * either return a new &struct pci_slot to the caller, or if the pci_slot
 * already exists, its refcount will be incremented.
 *
 * Slots are uniquely identified by a @pci_bus, @slot_nr, @name tuple.
 *
 * Placeholder slots:
 * In most cases, @pci_bus, @slot_nr will be sufficient to uniquely identify
 * a slot. There is one notable exception - pSeries (rpaphp), where the
 * @slot_nr cannot be determined until a device is actually inserted into
 * the slot. In this scenario, the caller may pass -1 for @slot_nr.
 *
 * The following semantics are imposed when the caller passes @slot_nr ==
 * -1. First, the check for existing %struct pci_slot is skipped, as the
 * caller may know about several unpopulated slots on a given %struct
 * pci_bus, and each slot would have a @slot_nr of -1.  Uniqueness for
 * these slots is then determined by the @name parameter. We expect
 * kobject_init_and_add() to warn us if the caller attempts to create
 * multiple slots with the same name. The other change in semantics is
 * user-visible, which is the 'address' parameter presented in sysfs will
 * consist solely of a dddd:bb tuple, where dddd is the PCI domain of the
 * %struct pci_bus and bb is the bus number. In other words, the devfn of
 * the 'placeholder' slot will not be displayed.
 */

struct pci_slot *pci_create_slot(struct pci_bus *parent, int slot_nr,
				 const char *name)
{
	struct pci_dev *dev;
	struct pci_slot *slot;
	int err;

	down_write(&pci_bus_sem);

	if (slot_nr == -1)
		goto placeholder;

	/* If we've already created this slot, bump refcount and return. */
	list_for_each_entry(slot, &parent->slots, list) {
		if (slot->number == slot_nr) {
			kobject_get(&slot->kobj);
			pr_debug("%s: inc refcount to %d on %04x:%02x:%02x\n",
				 __func__,
				 atomic_read(&slot->kobj.kref.refcount),
				 pci_domain_nr(parent), parent->number,
				 slot_nr);
			goto out;
		}
	}

placeholder:
	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot) {
		slot = ERR_PTR(-ENOMEM);
		goto out;
	}

	slot->bus = parent;
	slot->number = slot_nr;

	slot->kobj.kset = pci_slots_kset;
	err = kobject_init_and_add(&slot->kobj, &pci_slot_ktype, NULL,
				   "%s", name);
	if (err) {
		printk(KERN_ERR "Unable to register kobject %s\n", name);
		goto err;
	}

	INIT_LIST_HEAD(&slot->list);
	list_add(&slot->list, &parent->slots);

	list_for_each_entry(dev, &parent->devices, bus_list)
		if (PCI_SLOT(dev->devfn) == slot_nr)
			dev->slot = slot;

	/* Don't care if debug printk has a -1 for slot_nr */
	pr_debug("%s: created pci_slot on %04x:%02x:%02x\n",
		 __func__, pci_domain_nr(parent), parent->number, slot_nr);

 out:
	up_write(&pci_bus_sem);
	return slot;
 err:
	kfree(slot);
	slot = ERR_PTR(err);
	goto out;
}
EXPORT_SYMBOL_GPL(pci_create_slot);

/**
 * pci_update_slot_number - update %struct pci_slot -> number
 * @slot - %struct pci_slot to update
 * @slot_nr - new number for slot
 *
 * The primary purpose of this interface is to allow callers who earlier
 * created a placeholder slot in pci_create_slot() by passing a -1 as
 * slot_nr, to update their %struct pci_slot with the correct @slot_nr.
 */

void pci_update_slot_number(struct pci_slot *slot, int slot_nr)
{
	int name_count = 0;
	struct pci_slot *tmp;

	down_write(&pci_bus_sem);

	list_for_each_entry(tmp, &slot->bus->slots, list) {
		WARN_ON(tmp->number == slot_nr);
		if (!strcmp(kobject_name(&tmp->kobj), kobject_name(&slot->kobj)))
			name_count++;
	}

	if (name_count > 1)
		printk(KERN_WARNING "pci_update_slot_number found %d slots with the same name: %s\n", name_count, kobject_name(&slot->kobj));

	slot->number = slot_nr;
	up_write(&pci_bus_sem);
}
EXPORT_SYMBOL_GPL(pci_update_slot_number);

/**
 * pci_destroy_slot - decrement refcount for physical PCI slot
 * @slot: struct pci_slot to decrement
 *
 * %struct pci_slot is refcounted, so destroying them is really easy; we
 * just call kobject_put on its kobj and let our release methods do the
 * rest.
 */

void pci_destroy_slot(struct pci_slot *slot)
{
	pr_debug("%s: dec refcount to %d on %04x:%02x:%02x\n", __func__,
		 atomic_read(&slot->kobj.kref.refcount) - 1,
		 pci_domain_nr(slot->bus), slot->bus->number, slot->number);

	down_write(&pci_bus_sem);
	kobject_put(&slot->kobj);
	up_write(&pci_bus_sem);
}
EXPORT_SYMBOL_GPL(pci_destroy_slot);

static int pci_slot_init(void)
{
	struct kset *pci_bus_kset;

	pci_bus_kset = bus_get_kset(&pci_bus_type);
	pci_slots_kset = kset_create_and_add("slots", NULL,
						&pci_bus_kset->kobj);
	if (!pci_slots_kset) {
		printk(KERN_ERR "PCI: Slot initialization failure\n");
		return -ENOMEM;
	}
	return 0;
}

subsys_initcall(pci_slot_init);
