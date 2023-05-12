// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI Hot Plug Controller Driver for RPA-compliant PPC64 platform.
 * Copyright (C) 2003 Linda Xie <lxie@us.ibm.com>
 *
 * All rights reserved.
 *
 * Send feedback to <lxie@us.ibm.com>
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/of.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <asm/firmware.h>
#include <asm/eeh.h>       /* for eeh_add_device() */
#include <asm/rtas.h>		/* rtas_call */
#include <asm/pci-bridge.h>	/* for pci_controller */
#include <asm/prom.h>
#include "../pci.h"		/* for pci_add_new_bus */
				/* and pci_do_scan_bus */
#include "rpaphp.h"

bool rpaphp_debug;
LIST_HEAD(rpaphp_slot_head);
EXPORT_SYMBOL_GPL(rpaphp_slot_head);

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
	struct slot *slot = to_slot(hotplug_slot);

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
		slot->attention_status = value;

	return rc;
}

/**
 * get_power_status - get power status of a slot
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 */
static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	int retval, level;
	struct slot *slot = to_slot(hotplug_slot);

	retval = rtas_get_power_level(slot->power_domain, &level);
	if (!retval)
		*value = level;
	return retval;
}

/**
 * get_attention_status - get attention LED status
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 */
static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = to_slot(hotplug_slot);
	*value = slot->attention_status;
	return 0;
}

static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = to_slot(hotplug_slot);
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

static int get_children_props(struct device_node *dn, const __be32 **drc_indexes,
			      const __be32 **drc_names, const __be32 **drc_types,
			      const __be32 **drc_power_domains)
{
	const __be32 *indexes, *names, *types, *domains;

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


/* Verify the existence of 'drc_name' and/or 'drc_type' within the
 * current node.  First obtain its my-drc-index property.  Next,
 * obtain the DRC info from its parent.  Use the my-drc-index for
 * correlation, and obtain/validate the requested properties.
 */

static int rpaphp_check_drc_props_v1(struct device_node *dn, char *drc_name,
				char *drc_type, unsigned int my_index)
{
	char *name_tmp, *type_tmp;
	const __be32 *indexes, *names;
	const __be32 *types, *domains;
	int i, rc;

	rc = get_children_props(dn->parent, &indexes, &names, &types, &domains);
	if (rc < 0) {
		return -EINVAL;
	}

	name_tmp = (char *) &names[1];
	type_tmp = (char *) &types[1];

	/* Iterate through parent properties, looking for my-drc-index */
	for (i = 0; i < be32_to_cpu(indexes[0]); i++) {
		if (be32_to_cpu(indexes[i + 1]) == my_index)
			break;

		name_tmp += (strlen(name_tmp) + 1);
		type_tmp += (strlen(type_tmp) + 1);
	}

	if (((drc_name == NULL) || (drc_name && !strcmp(drc_name, name_tmp))) &&
	    ((drc_type == NULL) || (drc_type && !strcmp(drc_type, type_tmp))))
		return 0;

	return -EINVAL;
}

static int rpaphp_check_drc_props_v2(struct device_node *dn, char *drc_name,
				char *drc_type, unsigned int my_index)
{
	struct property *info;
	unsigned int entries;
	struct of_drc_info drc;
	const __be32 *value;
	char cell_drc_name[MAX_DRC_NAME_LEN];
	int j;

	info = of_find_property(dn->parent, "ibm,drc-info", NULL);
	if (info == NULL)
		return -EINVAL;

	value = of_prop_next_u32(info, NULL, &entries);
	if (!value)
		return -EINVAL;
	else
		value++;

	for (j = 0; j < entries; j++) {
		of_read_drc_info_cell(&info, &value, &drc);

		/* Should now know end of current entry */

		/* Found it */
		if (my_index >= drc.drc_index_start && my_index <= drc.last_drc_index) {
			int index = my_index - drc.drc_index_start;
			sprintf(cell_drc_name, "%s%d", drc.drc_name_prefix,
				drc.drc_name_suffix_start + index);
			break;
		}
	}

	if (((drc_name == NULL) ||
	     (drc_name && !strcmp(drc_name, cell_drc_name))) &&
	    ((drc_type == NULL) ||
	     (drc_type && !strcmp(drc_type, drc.drc_type))))
		return 0;

	return -EINVAL;
}

int rpaphp_check_drc_props(struct device_node *dn, char *drc_name,
			char *drc_type)
{
	const __be32 *my_index;

	my_index = of_get_property(dn, "ibm,my-drc-index", NULL);
	if (!my_index) {
		/* Node isn't DLPAR/hotplug capable */
		return -EINVAL;
	}

	if (of_property_present(dn->parent, "ibm,drc-info"))
		return rpaphp_check_drc_props_v2(dn, drc_name, drc_type,
						be32_to_cpu(*my_index));
	else
		return rpaphp_check_drc_props_v1(dn, drc_name, drc_type,
						be32_to_cpu(*my_index));
}
EXPORT_SYMBOL_GPL(rpaphp_check_drc_props);


static int is_php_type(char *drc_type)
{
	char *endptr;

	/* PCI Hotplug nodes have an integer for drc_type */
	simple_strtoul(drc_type, &endptr, 10);
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
static int is_php_dn(struct device_node *dn, const __be32 **indexes,
		     const __be32 **names, const __be32 **types,
		     const __be32 **power_domains)
{
	const __be32 *drc_types;
	int rc;

	rc = get_children_props(dn, indexes, names, &drc_types, power_domains);
	if (rc < 0)
		return 0;

	if (!is_php_type((char *) &drc_types[1]))
		return 0;

	*types = drc_types;
	return 1;
}

static int rpaphp_drc_info_add_slot(struct device_node *dn)
{
	struct slot *slot;
	struct property *info;
	struct of_drc_info drc;
	char drc_name[MAX_DRC_NAME_LEN];
	const __be32 *cur;
	u32 count;
	int retval = 0;

	info = of_find_property(dn, "ibm,drc-info", NULL);
	if (!info)
		return 0;

	cur = of_prop_next_u32(info, NULL, &count);
	if (cur)
		cur++;
	else
		return 0;

	of_read_drc_info_cell(&info, &cur, &drc);
	if (!is_php_type(drc.drc_type))
		return 0;

	sprintf(drc_name, "%s%d", drc.drc_name_prefix, drc.drc_name_suffix_start);

	slot = alloc_slot_struct(dn, drc.drc_index_start, drc_name, drc.drc_power_domain);
	if (!slot)
		return -ENOMEM;

	slot->type = simple_strtoul(drc.drc_type, NULL, 10);
	retval = rpaphp_enable_slot(slot);
	if (!retval)
		retval = rpaphp_register_slot(slot);

	if (retval)
		dealloc_slot_struct(slot);

	return retval;
}

static int rpaphp_drc_add_slot(struct device_node *dn)
{
	struct slot *slot;
	int retval = 0;
	int i;
	const __be32 *indexes, *names, *types, *power_domains;
	char *name, *type;

	/* If this is not a hotplug slot, return without doing anything. */
	if (!is_php_dn(dn, &indexes, &names, &types, &power_domains))
		return 0;

	dbg("Entry %s: dn=%pOF\n", __func__, dn);

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
	if (!of_node_name_eq(dn, "pci"))
		return 0;

	if (of_property_present(dn, "ibm,drc-info"))
		return rpaphp_drc_info_add_slot(dn);
	else
		return rpaphp_drc_add_slot(dn);
}
EXPORT_SYMBOL_GPL(rpaphp_add_slot);

static void __exit cleanup_slots(void)
{
	struct slot *slot, *next;

	/*
	 * Unregister all of our slots with the pci_hotplug subsystem,
	 * and free up all memory that we had allocated.
	 */

	list_for_each_entry_safe(slot, next, &rpaphp_slot_head,
				 rpaphp_slot_list) {
		list_del(&slot->rpaphp_slot_list);
		pci_hp_deregister(&slot->hotplug_slot);
		dealloc_slot_struct(slot);
	}
}

static int __init rpaphp_init(void)
{
	struct device_node *dn;

	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

	for_each_node_by_name(dn, "pci")
		rpaphp_add_slot(dn);

	return 0;
}

static void __exit rpaphp_exit(void)
{
	cleanup_slots();
}

static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = to_slot(hotplug_slot);
	int state;
	int retval;

	if (slot->state == CONFIGURED)
		return 0;

	retval = rpaphp_get_sensor_state(slot, &state);
	if (retval)
		return retval;

	if (state == PRESENT) {
		pseries_eeh_init_edev_recursive(PCI_DN(slot->dn));

		pci_lock_rescan_remove();
		pci_hp_add_devices(slot->bus);
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
	struct slot *slot = to_slot(hotplug_slot);
	if (slot->state == NOT_CONFIGURED)
		return -EINVAL;

	pci_lock_rescan_remove();
	pci_hp_remove_devices(slot->bus);
	pci_unlock_rescan_remove();
	vm_unmap_aliases();

	slot->state = NOT_CONFIGURED;
	return 0;
}

const struct hotplug_slot_ops rpaphp_hotplug_slot_ops = {
	.enable_slot = enable_slot,
	.disable_slot = disable_slot,
	.set_attention_status = set_attention_status,
	.get_power_status = get_power_status,
	.get_attention_status = get_attention_status,
	.get_adapter_status = get_adapter_status,
};

module_init(rpaphp_init);
module_exit(rpaphp_exit);
