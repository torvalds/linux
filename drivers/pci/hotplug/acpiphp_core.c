/*
 * ACPI PCI Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2002 Hiroshi Aono (h-aono@ap.jp.nec.com)
 * Copyright (C) 2002,2003 Takayoshi Kochi (t-kochi@bq.jp.nec.com)
 * Copyright (C) 2002,2003 NEC Corporation
 * Copyright (C) 2003-2005 Matthew Wilcox (matthew.wilcox@hp.com)
 * Copyright (C) 2003-2005 Hewlett Packard
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
 * Send feedback to <gregkh@us.ibm.com>,
 *		    <t-kochi@bq.jp.nec.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include "pci_hotplug.h"
#include "acpiphp.h"

static LIST_HEAD(slot_list);

#define MY_NAME	"acpiphp"

static int debug;
int acpiphp_debug;

/* local variables */
static int num_slots;
static struct acpiphp_attention_info *attention_info;

#define DRIVER_VERSION	"0.5"
#define DRIVER_AUTHOR	"Greg Kroah-Hartman <gregkh@us.ibm.com>, Takayoshi Kochi <t-kochi@bq.jp.nec.com>, Matthew Wilcox <willy@hp.com>"
#define DRIVER_DESC	"ACPI Hot Plug PCI Controller Driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");
module_param(debug, bool, 0644);

/* export the attention callback registration methods */
EXPORT_SYMBOL_GPL(acpiphp_register_attention);
EXPORT_SYMBOL_GPL(acpiphp_unregister_attention);

static int enable_slot		(struct hotplug_slot *slot);
static int disable_slot		(struct hotplug_slot *slot);
static int set_attention_status (struct hotplug_slot *slot, u8 value);
static int get_power_status	(struct hotplug_slot *slot, u8 *value);
static int get_attention_status (struct hotplug_slot *slot, u8 *value);
static int get_address		(struct hotplug_slot *slot, u32 *value);
static int get_latch_status	(struct hotplug_slot *slot, u8 *value);
static int get_adapter_status	(struct hotplug_slot *slot, u8 *value);

static struct hotplug_slot_ops acpi_hotplug_slot_ops = {
	.owner			= THIS_MODULE,
	.enable_slot		= enable_slot,
	.disable_slot		= disable_slot,
	.set_attention_status	= set_attention_status,
	.get_power_status	= get_power_status,
	.get_attention_status	= get_attention_status,
	.get_latch_status	= get_latch_status,
	.get_adapter_status	= get_adapter_status,
	.get_address		= get_address,
};


/**
 * acpiphp_register_attention - set attention LED callback
 * @info: must be completely filled with LED callbacks
 *
 * Description: this is used to register a hardware specific ACPI
 * driver that manipulates the attention LED.  All the fields in
 * info must be set.
 **/
int acpiphp_register_attention(struct acpiphp_attention_info *info)
{
	int retval = -EINVAL;

	if (info && info->owner && info->set_attn &&
			info->get_attn && !attention_info) {
		retval = 0;
		attention_info = info;
	}
	return retval;
}


/**
 * acpiphp_unregister_attention - unset attention LED callback
 * @info: must match the pointer used to register
 *
 * Description: this is used to un-register a hardware specific acpi
 * driver that manipulates the attention LED.  The pointer to the 
 * info struct must be the same as the one used to set it.
 **/
int acpiphp_unregister_attention(struct acpiphp_attention_info *info)
{
	int retval = -EINVAL;

	if (info && attention_info == info) {
		attention_info = NULL;
		retval = 0;
	}
	return retval;
}


/**
 * enable_slot - power on and enable a slot
 * @hotplug_slot: slot to enable
 *
 * Actual tasks are done in acpiphp_enable_slot()
 *
 */
static int enable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/* enable the specified slot */
	return acpiphp_enable_slot(slot->acpi_slot);
}


/**
 * disable_slot - disable and power off a slot
 * @hotplug_slot: slot to disable
 *
 * Actual tasks are done in acpiphp_disable_slot()
 *
 */
static int disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	/* disable the specified slot */
	return acpiphp_disable_slot(slot->acpi_slot);
}


 /**
  * set_attention_status - set attention LED
 * @hotplug_slot: slot to set attention LED on
 * @status: value to set attention LED to (0 or 1)
 *
 * attention status LED, so we use a callback that
 * was registered with us.  This allows hardware specific
 * ACPI implementations to blink the light for us.
 **/
 static int set_attention_status(struct hotplug_slot *hotplug_slot, u8 status)
 {
	int retval = -ENODEV;

 	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);
 
	if (attention_info && try_module_get(attention_info->owner)) {
		retval = attention_info->set_attn(hotplug_slot, status);
		module_put(attention_info->owner);
	} else
		attention_info = NULL;
	return retval;
 }
 

/**
 * get_power_status - get power status of a slot
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 *
 * Some platforms may not implement _STA method properly.
 * In that case, the value returned may not be reliable.
 *
 */
static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	*value = acpiphp_get_power_status(slot->acpi_slot);

	return 0;
}


 /**
 * get_attention_status - get attention LED status
 * @hotplug_slot: slot to get status from
 * @value: returns with value of attention LED
 *
 * ACPI doesn't have known method to determine the state
 * of the attention status LED, so we use a callback that
 * was registered with us.  This allows hardware specific
 * ACPI implementations to determine its state
 **/
static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	int retval = -EINVAL;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	if (attention_info && try_module_get(attention_info->owner)) {
		retval = attention_info->get_attn(hotplug_slot, value);
		module_put(attention_info->owner);
	} else
		attention_info = NULL;
	return retval;
}


/**
 * get_latch_status - get latch status of a slot
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 *
 * ACPI doesn't provide any formal means to access latch status.
 * Instead, we fake latch status from _STA
 *
 */
static int get_latch_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	*value = acpiphp_get_latch_status(slot->acpi_slot);

	return 0;
}


/**
 * get_adapter_status - get adapter status of a slot
 * @hotplug_slot: slot to get status
 * @value: pointer to store status
 *
 * ACPI doesn't provide any formal means to access adapter status.
 * Instead, we fake adapter status from _STA
 *
 */
static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	*value = acpiphp_get_adapter_status(slot->acpi_slot);

	return 0;
}


/**
 * get_address - get pci address of a slot
 * @hotplug_slot: slot to get status
 * @value: pointer to struct pci_busdev (seg, bus, dev)
 */
static int get_address(struct hotplug_slot *hotplug_slot, u32 *value)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	*value = acpiphp_get_address(slot->acpi_slot);

	return 0;
}

static int __init init_acpi(void)
{
	int retval;

	/* initialize internal data structure etc. */
	retval = acpiphp_glue_init();

	/* read initial number of slots */
	if (!retval) {
		num_slots = acpiphp_get_num_slots();
		if (num_slots == 0)
			retval = -ENODEV;
	}

	return retval;
}


/**
 * make_slot_name - make a slot name that appears in pcihpfs
 * @slot: slot to name
 *
 */
static void make_slot_name(struct slot *slot)
{
	snprintf(slot->hotplug_slot->name, SLOT_NAME_SIZE, "%u",
		 slot->acpi_slot->sun);
}

/**
 * release_slot - free up the memory used by a slot
 * @hotplug_slot: slot to free
 */
static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __FUNCTION__, hotplug_slot->name);

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot->name);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

/**
 * init_slots - initialize 'struct slot' structures for each slot
 *
 */
static int __init init_slots(void)
{
	struct slot *slot;
	int retval = -ENOMEM;
	int i;

	for (i = 0; i < num_slots; ++i) {
		slot = kmalloc(sizeof(struct slot), GFP_KERNEL);
		if (!slot)
			goto error;
		memset(slot, 0, sizeof(struct slot));

		slot->hotplug_slot = kmalloc(sizeof(struct hotplug_slot), GFP_KERNEL);
		if (!slot->hotplug_slot)
			goto error_slot;
		memset(slot->hotplug_slot, 0, sizeof(struct hotplug_slot));

		slot->hotplug_slot->info = kmalloc(sizeof(struct hotplug_slot_info), GFP_KERNEL);
		if (!slot->hotplug_slot->info)
			goto error_hpslot;
		memset(slot->hotplug_slot->info, 0, sizeof(struct hotplug_slot_info));

		slot->hotplug_slot->name = kmalloc(SLOT_NAME_SIZE, GFP_KERNEL);
		if (!slot->hotplug_slot->name)
			goto error_info;

		slot->number = i;

		slot->hotplug_slot->private = slot;
		slot->hotplug_slot->release = &release_slot;
		slot->hotplug_slot->ops = &acpi_hotplug_slot_ops;

		slot->acpi_slot = get_slot_from_id(i);
		slot->hotplug_slot->info->power_status = acpiphp_get_power_status(slot->acpi_slot);
		slot->hotplug_slot->info->attention_status = 0;
		slot->hotplug_slot->info->latch_status = acpiphp_get_latch_status(slot->acpi_slot);
		slot->hotplug_slot->info->adapter_status = acpiphp_get_adapter_status(slot->acpi_slot);
		slot->hotplug_slot->info->max_bus_speed = PCI_SPEED_UNKNOWN;
		slot->hotplug_slot->info->cur_bus_speed = PCI_SPEED_UNKNOWN;

		make_slot_name(slot);

		retval = pci_hp_register(slot->hotplug_slot);
		if (retval) {
			err("pci_hp_register failed with error %d\n", retval);
			goto error_name;
		}

		/* add slot to our internal list */
		list_add(&slot->slot_list, &slot_list);
		info("Slot [%s] registered\n", slot->hotplug_slot->name);
	}

	return 0;
error_name:
	kfree(slot->hotplug_slot->name);
error_info:
	kfree(slot->hotplug_slot->info);
error_hpslot:
	kfree(slot->hotplug_slot);
error_slot:
	kfree(slot);
error:
	return retval;
}


static void __exit cleanup_slots (void)
{
	struct list_head *tmp, *n;
	struct slot *slot;

	list_for_each_safe (tmp, n, &slot_list) {
		/* memory will be freed in release_slot callback */
		slot = list_entry(tmp, struct slot, slot_list);
		list_del(&slot->slot_list);
		pci_hp_deregister(slot->hotplug_slot);
	}
}


static int __init acpiphp_init(void)
{
	int retval;

	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

	acpiphp_debug = debug;

	/* read all the ACPI info from the system */
	retval = init_acpi();
	if (retval)
		return retval;

	return init_slots();
}


static void __exit acpiphp_exit(void)
{
	cleanup_slots();
	/* deallocate internal data structures etc. */
	acpiphp_glue_exit();
}

module_init(acpiphp_init);
module_exit(acpiphp_exit);
