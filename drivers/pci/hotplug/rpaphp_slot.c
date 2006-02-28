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
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/slab.h>

#include <asm/rtas.h>
#include "rpaphp.h"

static ssize_t location_read_file (struct hotplug_slot *php_slot, char *buf)
{
	char *value;
	int retval = -ENOENT;
	struct slot *slot = (struct slot *)php_slot->private;

	if (!slot)
		return retval;

	value = slot->location;
	retval = sprintf (buf, "%s\n", value);
	return retval;
}

static struct hotplug_slot_attribute hotplug_slot_attr_location = {
	.attr = {.name = "phy_location", .mode = S_IFREG | S_IRUGO},
	.show = location_read_file,
};

static void rpaphp_sysfs_add_attr_location (struct hotplug_slot *slot)
{
	sysfs_create_file(&slot->kobj, &hotplug_slot_attr_location.attr);
}

static void rpaphp_sysfs_remove_attr_location (struct hotplug_slot *slot)
{
	sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_location.attr);
}

/* free up the memory used by a slot */
static void rpaphp_release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = (struct slot *) hotplug_slot->private;

	dealloc_slot_struct(slot);
}

void dealloc_slot_struct(struct slot *slot)
{
	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
	return;
}

struct slot *alloc_slot_struct(struct device_node *dn, int drc_index, char *drc_name,
		  int power_domain)
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
	slot->hotplug_slot->name = kmalloc(BUS_ID_SIZE + 1, GFP_KERNEL);
	if (!slot->hotplug_slot->name)
		goto error_info;	
	slot->location = kmalloc(strlen(drc_name) + 1, GFP_KERNEL);
	if (!slot->location)
		goto error_name;
	slot->name = slot->hotplug_slot->name;
	slot->dn = dn;
	slot->index = drc_index;
	strcpy(slot->location, drc_name);
	slot->power_domain = power_domain;
	slot->hotplug_slot->private = slot;
	slot->hotplug_slot->ops = &rpaphp_hotplug_slot_ops;
	slot->hotplug_slot->release = &rpaphp_release_slot;
	
	return (slot);

error_name:
	kfree(slot->hotplug_slot->name);
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
	struct slot             *tmp_slot;

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
		__FUNCTION__, slot->name);

	list_del(&slot->rpaphp_slot_list);
	
	/* remove "phy_location" file */
	rpaphp_sysfs_remove_attr_location(php_slot);

	retval = pci_hp_deregister(php_slot);
	if (retval)
		err("Problem unregistering a slot %s\n", slot->name);
	else
		num_slots--;

	dbg("%s - Exit: rc[%d]\n", __FUNCTION__, retval);
	return retval;
}
EXPORT_SYMBOL_GPL(rpaphp_deregister_slot);

int rpaphp_register_slot(struct slot *slot)
{
	int retval;

	dbg("%s registering slot:path[%s] index[%x], name[%s] pdomain[%x] type[%d]\n", 
		__FUNCTION__, slot->dn->full_name, slot->index, slot->name, 
		slot->power_domain, slot->type);
	/* should not try to register the same slot twice */
	if (is_registered(slot)) { /* should't be here */
		err("rpaphp_register_slot: slot[%s] is already registered\n", slot->name);
		rpaphp_release_slot(slot->hotplug_slot);
		return -EAGAIN;
	}	
	retval = pci_hp_register(slot->hotplug_slot);
	if (retval) {
		err("pci_hp_register failed with error %d\n", retval);
		rpaphp_release_slot(slot->hotplug_slot);
		return retval;
	}
	
	/* create "phy_locatoin" file */
	rpaphp_sysfs_add_attr_location(slot->hotplug_slot);	

	/* add slot to our internal list */
	dbg("%s adding slot[%s] to rpaphp_slot_list\n",
	    __FUNCTION__, slot->name);

	list_add(&slot->rpaphp_slot_list, &rpaphp_slot_head);
	info("Slot [%s](PCI location=%s) registered\n", slot->name,
			slot->location);
	num_slots++;
	return 0;
}

int rpaphp_get_power_status(struct slot *slot, u8 * value)
{
	int rc = 0, level;
	
	rc = rtas_get_power_level(slot->power_domain, &level);
	if (rc < 0) {
		err("failed to get power-level for slot(%s), rc=0x%x\n",
			slot->location, rc);
		return rc;
	}

	dbg("%s the power level of slot %s(pwd-domain:0x%x) is %d\n",
		__FUNCTION__, slot->name, slot->power_domain, level);
	*value = level;

	return rc;
}

int rpaphp_set_attention_status(struct slot *slot, u8 status)
{
	int rc;

	/* status: LED_OFF or LED_ON */
	rc = rtas_set_indicator(DR_INDICATOR, slot->index, status);
	if (rc < 0)
		err("slot(name=%s location=%s index=0x%x) set attention-status(%d) failed! rc=0x%x\n",
		    slot->name, slot->location, slot->index, status, rc);

	return rc;
}
