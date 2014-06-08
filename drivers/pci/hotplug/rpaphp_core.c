/*
 * PCI Hot Plug Controller Driver for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
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
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <asm/eeh.h>       /* for eeh_add_device() */
#include <asm/rtas.h>		/* rtas_call */
#include <asm/pci-bridge.h>	/* for pci_controller */
#include "../pci.h"		/* for pci_add_new_bus */
				/* and pci_do_scan_bus */
#include "rpaphp.h"

bool rpaphp_debug;
LIST_HEAD(rpaphp_slot_head);

#define DRIVER_VERSION	"0.1"
#define DRIVER_AUTHOR	"Linda Xie <lxie@us.ibm.com>"
#define DRIVER_DESC	"RPA HOT Plug PCI Controller Driver"

#define MAX_LOC_CODE 128

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param_named(debug, rpaphp_debug, bool, 0644);

/**
 * set_attention_status - set attention LED
 * @hotplug_slot: target &hotplug_slot
 * @value: LED control value
 *
 * echo 0 > attention -- set LED OFF
 * echo 1 > attention -- set LED ON
 * echo 2 > attention -- set LED ID(identify, light is blinking)
 */
static int set_attention_status(struct hotplug_slot *hotplug_slot, u8 value)
{
	int rc;
	struct slot *slot = (struct slot *)hotplug_slot->private;

	switch (value) {
	case 0:
	case 1:
	case 2:
		break;
	default:
		value = 1;
		break;
	}

	rc = rtas_set_indicator(DR_INDICATOR, slot->index, value);
	if (!rc)
		hotplug_slot->info->attention_status = value;

	return rc;
}

/**
 * get_power_status - get power status of a slot
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 */
static int get_power_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	int retval, level;
	struct slot *slot = (struct slot *)hotplug_slot->private;

	retval = rtas_get_power_level (slot->power_domain, &level);
	if (!retval)
		*value = level;
	return retval;
}

/**
 * get_attention_status - get attention LED status
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 */
static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	struct slot *slot = (struct slot *)hotplug_slot->private;
	*value = slot->hotplug_slot->info->attention_status;
	return 0;
}

static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 * value)
{
	struct slot *slot = (struct slot *)hotplug_slot->private;
	int rc, state;

	rc = rpaphp_get_sensor_state(slot, &state);

	*value = NOT_VALID;
	if (rc)
		return rc;

	if (state == EMPTY)
		*value = EMPTY;
	else if (state == PRESENT)
		*value = slot->state;

	return 0;
}

static enum pci_bus_speed get_max_bus_speed(struct slot *slot)
{
	enum pci_bus_speed speed;
	switch (slot->type) {
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
		speed = PCI_SPEED_33MHz;	/* speed for case 1-6 */
		break;
	case 7:
	case 8:
		speed = PCI_SPEED_66MHz;
		break;
	case 11:
	case 14:
		speed = PCI_SPEED_66MHz_PCIX;
		break;
	case 12:
	case 15:
		speed = PCI_SPEED_100MHz_PCIX;
		break;
	case 13:
	case 16:
		speed = PCI_SPEED_133MHz_PCIX;
		break;
	default:
		speed = PCI_SPEED_UNKNOWN;
		break;
	}

	return speed;
}

static int get_children_props(struct device_node *dn, const int **drc_indexes,
		const int **drc_names, const int **drc_types,
		const int **drc_power_domains)
{
	const int *indexes, *names, *types, *domains;

	indexes = of_get_property(dn, "ibm,drc-indexes", NULL);
	names = of_get_property(dn, "ibm,drc-names", NULL);
	types = of_get_property(dn, "ibm,drc-types", NULL);
	domains = of_get_property(dn, "ibm,drc-power-domains", NULL);

	if (!indexes || !names || !types || !domains) {
		/* Slot does not have dynamically-removable children */
		return -EINVAL;
	}
	if (drc_indexes)
		*drc_indexes = indexes;
	if (drc_names)
		/* &drc_names[1] contains NULL terminated slot names */
		*drc_names = names;
	if (drc_types)
		/* &drc_types[1] contains NULL terminated slot types */
		*drc_types = types;
	if (drc_power_domains)
		*drc_power_domains = domains;

	return 0;
}

/* To get the DRC props describing the current node, first obtain it's
 * my-drc-index property.  Next obtain the DRC list from it's parent.  Use
 * the my-drc-index for correlation, and obtain the requested properties.
 */
int rpaphp_get_drc_props(struct device_node *dn, int *drc_index,
		char **drc_name, char **drc_type, int *drc_power_domain)
{
	const int *indexes, *names;
	const int *types, *domains;
	const unsigned int *my_index;
	char *name_tmp, *type_tmp;
	int i, rc;

	my_index = of_get_property(dn, "ibm,my-drc-index", NULL);
	if (!my_index) {
		/* Node isn't DLPAR/hotplug capable */
		return -EINVAL;
	}

	rc = get_children_props(dn->parent, &indexes, &names, &types, &domains);
	if (rc < 0) {
		return -EINVAL;
	}

	name_tmp = (char *) &names[1];
	type_tmp = (char *) &types[1];

	/* Iterate through parent properties, looking for my-drc-index */
	for (i = 0; i < be32_to_cpu(indexes[0]); i++) {
		if ((unsigned int) indexes[i + 1] == *my_index) {
			if (drc_name)
				*drc_name = name_tmp;
			if (drc_type)
				*drc_type = type_tmp;
			if (drc_index)
				*drc_index = be32_to_cpu(*my_index);
			if (drc_power_domain)
				*drc_power_domain = be32_to_cpu(domains[i+1]);
			return 0;
		}
		name_tmp += (strlen(name_tmp) + 1);
		type_tmp += (strlen(type_tmp) + 1);
	}

	return -EINVAL;
}

static int is_php_type(char *drc_type)
{
	unsigned long value;
	char *endptr;

	/* PCI Hotplug nodes have an integer for drc_type */
	value = simple_strtoul(drc_type, &endptr, 10);
	if (endptr == drc_type)
		return 0;

	return 1;
}

/**
 * is_php_dn() - return 1 if this is a hotpluggable pci slot, else 0
 * @dn: target &device_node
 * @indexes: passed to get_children_props()
 * @names: passed to get_children_props()
 * @types: returned from get_children_props()
 * @power_domains:
 *
 * This routine will return true only if the device node is
 * a hotpluggable slot. This routine will return false
 * for built-in pci slots (even when the built-in slots are
 * dlparable.)
 */
static int is_php_dn(struct device_node *dn, const int **indexes,
		const int **names, const int **types, const int **power_domains)
{
	const int *drc_types;
	int rc;

	rc = get_children_props(dn, indexes, names, &drc_types, power_domains);
	if (rc < 0)
		return 0;

	if (!is_php_type((char *) &drc_types[1]))
		return 0;

	*types = drc_types;
	return 1;
}

/**
 * rpaphp_add_slot -- declare a hotplug slot to the hotplug subsystem.
 * @dn: device node of slot
 *
 * This subroutine will register a hotpluggable slot with the
 * PCI hotplug infrastructure. This routine is typically called
 * during boot time, if the hotplug slots are present at boot time,
 * or is called later, by the dlpar add code, if the slot is
 * being dynamically added during runtime.
 *
 * If the device node points at an embedded (built-in) slot, this
 * routine will just return without doing anything, since embedded
 * slots cannot be hotplugged.
 *
 * To remove a slot, it suffices to call rpaphp_deregister_slot().
 */
int rpaphp_add_slot(struct device_node *dn)
{
	struct slot *slot;
	int retval = 0;
	int i;
	const int *indexes, *names, *types, *power_domains;
	char *name, *type;

	if (!dn->name || strcmp(dn->name, "pci"))
		return 0;

	/* If this is not a hotplug slot, return without doing anything. */
	if (!is_php_dn(dn, &indexes, &names, &types, &power_domains))
		return 0;

	dbg("Entry %s: dn->full_name=%s\n", __func__, dn->full_name);

	/* register PCI devices */
	name = (char *) &names[1];
	type = (char *) &types[1];
	for (i = 0; i < be32_to_cpu(indexes[0]); i++) {
		int index;

		index = be32_to_cpu(indexes[i + 1]);
		slot = alloc_slot_struct(dn, index, name,
					 be32_to_cpu(power_domains[i + 1]));
		if (!slot)
			return -ENOMEM;

		slot->type = simple_strtoul(type, NULL, 10);

		dbg("Found drc-index:0x%x drc-name:%s drc-type:%s\n",
				index, name, type);

		retval = rpaphp_enable_slot(slot);
		if (!retval)
			retval = rpaphp_register_slot(slot);

		if (retval)
			dealloc_slot_struct(slot);

		name += strlen(name) + 1;
		type += strlen(type) + 1;
	}
	dbg("%s - Exit: rc[%d]\n", __func__, retval);

	/* XXX FIXME: reports a failure only if last entry in loop failed */
	return retval;
}

static void __exit cleanup_slots(void)
{
	struct list_head *tmp, *n;
	struct slot *slot;

	/*
	 * Unregister all of our slots with the pci_hotplug subsystem,
	 * and free up all memory that we had allocated.
	 * memory will be freed in release_slot callback.
	 */

	list_for_each_safe(tmp, n, &rpaphp_slot_head) {
		slot = list_entry(tmp, struct slot, rpaphp_slot_list);
		list_del(&slot->rpaphp_slot_list);
		pci_hp_deregister(slot->hotplug_slot);
	}
	return;
}

static int __init rpaphp_init(void)
{
	struct device_node *dn = NULL;

	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

	while ((dn = of_find_node_by_name(dn, "pci")))
		rpaphp_add_slot(dn);

	return 0;
}

static void __exit rpaphp_exit(void)
{
	cleanup_slots();
}

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = (struct slot *)hotplug_slot->private;
	int state;
	int retval;

	if (slot->state == CONFIGURED)
		return 0;

	retval = rpaphp_get_sensor_state(slot, &state);
	if (retval)
		return retval;

	if (state == PRESENT) {
		pci_lock_rescan_remove();
		pcibios_add_pci_devices(slot->bus);
		pci_unlock_rescan_remove();
		slot->state = CONFIGURED;
	} else if (state == EMPTY) {
		slot->state = EMPTY;
	} else {
		err("%s: slot[%s] is in invalid state\n", __func__, slot->name);
		slot->state = NOT_VALID;
		return -EINVAL;
	}

	slot->bus->max_bus_speed = get_max_bus_speed(slot);
	return 0;
}

static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = (struct slot *)hotplug_slot->private;
	if (slot->state == NOT_CONFIGURED)
		return -EINVAL;

	pci_lock_rescan_remove();
	pcibios_remove_pci_devices(slot->bus);
	pci_unlock_rescan_remove();
	vm_unmap_aliases();

	slot->state = NOT_CONFIGURED;
	return 0;
}

struct hotplug_slot_ops rpaphp_hotplug_slot_ops = {
	.enable_slot = enable_slot,
	.disable_slot = disable_slot,
	.set_attention_status = set_attention_status,
	.get_power_status = get_power_status,
	.get_attention_status = get_attention_status,
	.get_adapter_status = get_adapter_status,
};

module_init(rpaphp_init);
module_exit(rpaphp_exit);

EXPORT_SYMBOL_GPL(rpaphp_add_slot);
EXPORT_SYMBOL_GPL(rpaphp_slot_head);
EXPORT_SYMBOL_GPL(rpaphp_get_drc_props);
