// SPDX-License-Identifier: GPL-2.0+
/*
 * RPA Virtual I/O device functions
 * Copyright (C) 2004 Linda Xie <lxie@us.ibm.com>
 *
 * All rights reserved.
 *
 * Send feedback to <lxie@us.ibm.com>
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <asm/rtas.h>
#include "rpaphp.h"

/* free up the memory used by a slot */
void dealloc_slot_struct(struct slot *slot)
{
	of_node_put(slot->dn);
	kfree(slot->name);
	kfree(slot);
}

struct slot *alloc_slot_struct(struct device_node *dn,
		int drc_index, char *drc_name, int power_domain)
{
	struct slot *slot;

	slot = kzalloc(sizeof(struct slot), GFP_KERNEL);
	if (!slot)
		goto error_nomem;
	slot->name = kstrdup(drc_name, GFP_KERNEL);
	if (!slot->name)
		goto error_slot;
	slot->dn = of_node_get(dn);
	slot->index = drc_index;
	slot->power_domain = power_domain;
	slot->hotplug_slot.ops = &rpaphp_hotplug_slot_ops;

	return (slot);

error_slot:
	kfree(slot);
error_nomem:
	return NULL;
}

static int is_registered(struct slot *slot)
{
	struct slot *tmp_slot;

	list_for_each_entry(tmp_slot, &rpaphp_slot_head, rpaphp_slot_list) {
		if (!strcmp(tmp_slot->name, slot->name))
			return 1;
	}
	return 0;
}

int rpaphp_deregister_slot(struct slot *slot)
{
	int retval = 0;
	struct hotplug_slot *php_slot = &slot->hotplug_slot;

	 dbg("%s - Entry: deregistering slot=%s\n",
		__func__, slot->name);

	list_del(&slot->rpaphp_slot_list);
	pci_hp_deregister(php_slot);
	dealloc_slot_struct(slot);

	dbg("%s - Exit: rc[%d]\n", __func__, retval);
	return retval;
}
EXPORT_SYMBOL_GPL(rpaphp_deregister_slot);

int rpaphp_register_slot(struct slot *slot)
{
	struct hotplug_slot *php_slot = &slot->hotplug_slot;
	struct device_node *child;
	u32 my_index;
	int retval;
	int slotno = -1;

	dbg("%s registering slot:path[%pOF] index[%x], name[%s] pdomain[%x] type[%d]\n",
		__func__, slot->dn, slot->index, slot->name,
		slot->power_domain, slot->type);

	/* should not try to register the same slot twice */
	if (is_registered(slot)) {
		err("rpaphp_register_slot: slot[%s] is already registered\n", slot->name);
		return -EAGAIN;
	}

	for_each_child_of_node(slot->dn, child) {
		retval = of_property_read_u32(child, "ibm,my-drc-index", &my_index);
		if (my_index == slot->index) {
			slotno = PCI_SLOT(PCI_DN(child)->devfn);
			of_node_put(child);
			break;
		}
	}

	retval = pci_hp_register(php_slot, slot->bus, slotno, slot->name);
	if (retval) {
		err("pci_hp_register failed with error %d\n", retval);
		return retval;
	}

	/* add slot to our internal list */
	list_add(&slot->rpaphp_slot_list, &rpaphp_slot_head);
	info("Slot [%s] registered\n", slot->name);
	return 0;
}
