/*
 * RPA Virtual I/O device functions 
 * Copyright (C) 2004 Linda Xie <lxie@us.ibm.com>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
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
static void rpaphp_release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = (struct slot *) hotplug_slot->private;
	dealloc_slot_struct(slot);
}

void dealloc_slot_struct(struct slot *slot)
{
	kfree(slot->hotplug_slot->info);
	kfree(slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

struct slot *alloc_slot_struct(struct device_node *dn,
                       int drc_index, char *drc_name, int power_domain)
{
	struct slot *slot;
	
	slot = kzalloc(sizeof(struct slot), GFP_KERNEL);
	if (!slot)
		goto error_nomem;
	slot->hotplug_slot = kzalloc(sizeof(struct hotplug_slot), GFP_KERNEL);
	if (!slot->hotplug_slot)
		goto error_slot;	
	slot->hotplug_slot->info = kzalloc(sizeof(struct hotplug_slot_info),
					   GFP_KERNEL);
	if (!slot->hotplug_slot->info)
		goto error_hpslot;
	slot->name = kstrdup(drc_name, GFP_KERNEL);
	if (!slot->name)
		goto error_info;	
	slot->dn = dn;
	slot->index = drc_index;
	slot->power_domain = power_domain;
	slot->hotplug_slot->private = slot;
	slot->hotplug_slot->ops = &rpaphp_hotplug_slot_ops;
	slot->hotplug_slot->release = &rpaphp_release_slot;
	
	return (slot);

error_info:
	kfree(slot->hotplug_slot->info);
error_hpslot:
	kfree(slot->hotplug_slot);
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
	struct hotplug_slot *php_slot = slot->hotplug_slot;

	 dbg("%s - Entry: deregistering slot=%s\n",
		__func__, slot->name);

	list_del(&slot->rpaphp_slot_list);
	
	retval = pci_hp_deregister(php_slot);
	if (retval)
		err("Problem unregistering a slot %s\n", slot->name);

	dbg("%s - Exit: rc[%d]\n", __func__, retval);
	return retval;
}
EXPORT_SYMBOL_GPL(rpaphp_deregister_slot);

int rpaphp_register_slot(struct slot *slot)
{
	struct hotplug_slot *php_slot = slot->hotplug_slot;
	int retval;
	int slotno;

	dbg("%s registering slot:path[%s] index[%x], name[%s] pdomain[%x] type[%d]\n", 
		__func__, slot->dn->full_name, slot->index, slot->name,
		slot->power_domain, slot->type);

	/* should not try to register the same slot twice */
	if (is_registered(slot)) {
		err("rpaphp_register_slot: slot[%s] is already registered\n", slot->name);
		return -EAGAIN;
	}	

	if (slot->dn->child)
		slotno = PCI_SLOT(PCI_DN(slot->dn->child)->devfn);
	else
		slotno = -1;
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

