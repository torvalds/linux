/*
 * PCI HotPlug Controller Core
 *
 * Copyright (C) 2001-2002 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001-2002 IBM Corp.
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
 * Send feedback to <kristen.c.accardi@intel.com>
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <asm/uaccess.h>
#include "../pci.h"

#define MY_NAME	"pci_hotplug"

#define dbg(fmt, arg...) do { if (debug) printk(KERN_DEBUG "%s: %s: " fmt , MY_NAME , __func__ , ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format , MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format , MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format , MY_NAME , ## arg)


/* local variables */
static int debug;

#define DRIVER_VERSION	"0.5"
#define DRIVER_AUTHOR	"Greg Kroah-Hartman <greg@kroah.com>, Scott Murray <scottm@somanetworks.com>"
#define DRIVER_DESC	"PCI Hot Plug PCI Core"


//////////////////////////////////////////////////////////////////

static LIST_HEAD(pci_hotplug_slot_list);
static DEFINE_MUTEX(pci_hp_mutex);

/* these strings match up with the values in pci_bus_speed */
static char *pci_bus_speed_strings[] = {
	"33 MHz PCI",		/* 0x00 */
	"66 MHz PCI",		/* 0x01 */
	"66 MHz PCIX", 		/* 0x02 */
	"100 MHz PCIX",		/* 0x03 */
	"133 MHz PCIX",		/* 0x04 */
	NULL,			/* 0x05 */
	NULL,			/* 0x06 */
	NULL,			/* 0x07 */
	NULL,			/* 0x08 */
	"66 MHz PCIX 266",	/* 0x09 */
	"100 MHz PCIX 266",	/* 0x0a */
	"133 MHz PCIX 266",	/* 0x0b */
	NULL,			/* 0x0c */
	NULL,			/* 0x0d */
	NULL,			/* 0x0e */
	NULL,			/* 0x0f */
	NULL,			/* 0x10 */
	"66 MHz PCIX 533",	/* 0x11 */
	"100 MHz PCIX 533",	/* 0x12 */
	"133 MHz PCIX 533",	/* 0x13 */
	"25 GBps PCI-E",	/* 0x14 */
};

#ifdef CONFIG_HOTPLUG_PCI_CPCI
extern int cpci_hotplug_init(int debug);
extern void cpci_hotplug_exit(void);
#else
static inline int cpci_hotplug_init(int debug) { return 0; }
static inline void cpci_hotplug_exit(void) { }
#endif

/* Weee, fun with macros... */
#define GET_STATUS(name,type)	\
static int get_##name (struct hotplug_slot *slot, type *value)		\
{									\
	struct hotplug_slot_ops *ops = slot->ops;			\
	int retval = 0;							\
	if (!try_module_get(ops->owner))				\
		return -ENODEV;						\
	if (ops->get_##name)						\
		retval = ops->get_##name(slot, value);			\
	else								\
		*value = slot->info->name;				\
	module_put(ops->owner);						\
	return retval;							\
}

GET_STATUS(power_status, u8)
GET_STATUS(attention_status, u8)
GET_STATUS(latch_status, u8)
GET_STATUS(adapter_status, u8)
GET_STATUS(max_bus_speed, enum pci_bus_speed)
GET_STATUS(cur_bus_speed, enum pci_bus_speed)

static ssize_t power_read_file(struct pci_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_power_status(slot->hotplug, &value);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%d\n", value);
exit:
	return retval;
}

static ssize_t power_write_file(struct pci_slot *pci_slot, const char *buf,
		size_t count)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	unsigned long lpower;
	u8 power;
	int retval = 0;

	lpower = simple_strtoul (buf, NULL, 10);
	power = (u8)(lpower & 0xff);
	dbg ("power = %d\n", power);

	if (!try_module_get(slot->ops->owner)) {
		retval = -ENODEV;
		goto exit;
	}
	switch (power) {
		case 0:
			if (slot->ops->disable_slot)
				retval = slot->ops->disable_slot(slot);
			break;

		case 1:
			if (slot->ops->enable_slot)
				retval = slot->ops->enable_slot(slot);
			break;

		default:
			err ("Illegal value specified for power\n");
			retval = -EINVAL;
	}
	module_put(slot->ops->owner);

exit:	
	if (retval)
		return retval;
	return count;
}

static struct pci_slot_attribute hotplug_slot_attr_power = {
	.attr = {.name = "power", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.show = power_read_file,
	.store = power_write_file
};

static ssize_t attention_read_file(struct pci_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_attention_status(slot->hotplug, &value);
	if (retval)
		goto exit;
	retval = sprintf(buf, "%d\n", value);

exit:
	return retval;
}

static ssize_t attention_write_file(struct pci_slot *slot, const char *buf,
		size_t count)
{
	struct hotplug_slot_ops *ops = slot->hotplug->ops;
	unsigned long lattention;
	u8 attention;
	int retval = 0;

	lattention = simple_strtoul (buf, NULL, 10);
	attention = (u8)(lattention & 0xff);
	dbg (" - attention = %d\n", attention);

	if (!try_module_get(ops->owner)) {
		retval = -ENODEV;
		goto exit;
	}
	if (ops->set_attention_status)
		retval = ops->set_attention_status(slot->hotplug, attention);
	module_put(ops->owner);

exit:	
	if (retval)
		return retval;
	return count;
}

static struct pci_slot_attribute hotplug_slot_attr_attention = {
	.attr = {.name = "attention", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.show = attention_read_file,
	.store = attention_write_file
};

static ssize_t latch_read_file(struct pci_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_latch_status(slot->hotplug, &value);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%d\n", value);

exit:
	return retval;
}

static struct pci_slot_attribute hotplug_slot_attr_latch = {
	.attr = {.name = "latch", .mode = S_IFREG | S_IRUGO},
	.show = latch_read_file,
};

static ssize_t presence_read_file(struct pci_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_adapter_status(slot->hotplug, &value);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%d\n", value);

exit:
	return retval;
}

static struct pci_slot_attribute hotplug_slot_attr_presence = {
	.attr = {.name = "adapter", .mode = S_IFREG | S_IRUGO},
	.show = presence_read_file,
};

static char *unknown_speed = "Unknown bus speed";

static ssize_t max_bus_speed_read_file(struct pci_slot *slot, char *buf)
{
	char *speed_string;
	int retval;
	enum pci_bus_speed value;
	
	retval = get_max_bus_speed(slot->hotplug, &value);
	if (retval)
		goto exit;

	if (value == PCI_SPEED_UNKNOWN)
		speed_string = unknown_speed;
	else
		speed_string = pci_bus_speed_strings[value];
	
	retval = sprintf (buf, "%s\n", speed_string);

exit:
	return retval;
}

static struct pci_slot_attribute hotplug_slot_attr_max_bus_speed = {
	.attr = {.name = "max_bus_speed", .mode = S_IFREG | S_IRUGO},
	.show = max_bus_speed_read_file,
};

static ssize_t cur_bus_speed_read_file(struct pci_slot *slot, char *buf)
{
	char *speed_string;
	int retval;
	enum pci_bus_speed value;

	retval = get_cur_bus_speed(slot->hotplug, &value);
	if (retval)
		goto exit;

	if (value == PCI_SPEED_UNKNOWN)
		speed_string = unknown_speed;
	else
		speed_string = pci_bus_speed_strings[value];
	
	retval = sprintf (buf, "%s\n", speed_string);

exit:
	return retval;
}

static struct pci_slot_attribute hotplug_slot_attr_cur_bus_speed = {
	.attr = {.name = "cur_bus_speed", .mode = S_IFREG | S_IRUGO},
	.show = cur_bus_speed_read_file,
};

static ssize_t test_write_file(struct pci_slot *pci_slot, const char *buf,
		size_t count)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	unsigned long ltest;
	u32 test;
	int retval = 0;

	ltest = simple_strtoul (buf, NULL, 10);
	test = (u32)(ltest & 0xffffffff);
	dbg ("test = %d\n", test);

	if (!try_module_get(slot->ops->owner)) {
		retval = -ENODEV;
		goto exit;
	}
	if (slot->ops->hardware_test)
		retval = slot->ops->hardware_test(slot, test);
	module_put(slot->ops->owner);

exit:	
	if (retval)
		return retval;
	return count;
}

static struct pci_slot_attribute hotplug_slot_attr_test = {
	.attr = {.name = "test", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.store = test_write_file
};

static bool has_power_file(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	if ((!slot) || (!slot->ops))
		return false;
	if ((slot->ops->enable_slot) ||
	    (slot->ops->disable_slot) ||
	    (slot->ops->get_power_status))
		return true;
	return false;
}

static bool has_attention_file(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	if ((!slot) || (!slot->ops))
		return false;
	if ((slot->ops->set_attention_status) ||
	    (slot->ops->get_attention_status))
		return true;
	return false;
}

static bool has_latch_file(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	if ((!slot) || (!slot->ops))
		return false;
	if (slot->ops->get_latch_status)
		return true;
	return false;
}

static bool has_adapter_file(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	if ((!slot) || (!slot->ops))
		return false;
	if (slot->ops->get_adapter_status)
		return true;
	return false;
}

static bool has_max_bus_speed_file(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	if ((!slot) || (!slot->ops))
		return false;
	if (slot->ops->get_max_bus_speed)
		return true;
	return false;
}

static bool has_cur_bus_speed_file(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	if ((!slot) || (!slot->ops))
		return false;
	if (slot->ops->get_cur_bus_speed)
		return true;
	return false;
}

static bool has_test_file(struct pci_slot *pci_slot)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	if ((!slot) || (!slot->ops))
		return false;
	if (slot->ops->hardware_test)
		return true;
	return false;
}

static int fs_add_slot(struct pci_slot *slot)
{
	int retval = 0;

	/* Create symbolic link to the hotplug driver module */
	pci_hp_create_module_link(slot);

	if (has_power_file(slot)) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_power.attr);
		if (retval)
			goto exit_power;
	}

	if (has_attention_file(slot)) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_attention.attr);
		if (retval)
			goto exit_attention;
	}

	if (has_latch_file(slot)) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_latch.attr);
		if (retval)
			goto exit_latch;
	}

	if (has_adapter_file(slot)) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_presence.attr);
		if (retval)
			goto exit_adapter;
	}

	if (has_max_bus_speed_file(slot)) {
		retval = sysfs_create_file(&slot->kobj,
					&hotplug_slot_attr_max_bus_speed.attr);
		if (retval)
			goto exit_max_speed;
	}

	if (has_cur_bus_speed_file(slot)) {
		retval = sysfs_create_file(&slot->kobj,
					&hotplug_slot_attr_cur_bus_speed.attr);
		if (retval)
			goto exit_cur_speed;
	}

	if (has_test_file(slot)) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_test.attr);
		if (retval)
			goto exit_test;
	}

	goto exit;

exit_test:
	if (has_cur_bus_speed_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_cur_bus_speed.attr);
exit_cur_speed:
	if (has_max_bus_speed_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_max_bus_speed.attr);
exit_max_speed:
	if (has_adapter_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_presence.attr);
exit_adapter:
	if (has_latch_file(slot))
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_latch.attr);
exit_latch:
	if (has_attention_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_attention.attr);
exit_attention:
	if (has_power_file(slot))
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_power.attr);
exit_power:
	pci_hp_remove_module_link(slot);
exit:
	return retval;
}

static void fs_remove_slot(struct pci_slot *slot)
{
	if (has_power_file(slot))
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_power.attr);

	if (has_attention_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_attention.attr);

	if (has_latch_file(slot))
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_latch.attr);

	if (has_adapter_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_presence.attr);

	if (has_max_bus_speed_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_max_bus_speed.attr);

	if (has_cur_bus_speed_file(slot))
		sysfs_remove_file(&slot->kobj,
				  &hotplug_slot_attr_cur_bus_speed.attr);

	if (has_test_file(slot))
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_test.attr);

	pci_hp_remove_module_link(slot);
}

static struct hotplug_slot *get_slot_from_name (const char *name)
{
	struct hotplug_slot *slot;
	struct list_head *tmp;

	list_for_each (tmp, &pci_hotplug_slot_list) {
		slot = list_entry (tmp, struct hotplug_slot, slot_list);
		if (strcmp(hotplug_slot_name(slot), name) == 0)
			return slot;
	}
	return NULL;
}

/**
 * __pci_hp_register - register a hotplug_slot with the PCI hotplug subsystem
 * @bus: bus this slot is on
 * @slot: pointer to the &struct hotplug_slot to register
 * @devnr: device number
 * @name: name registered with kobject core
 * @owner: caller module owner
 * @mod_name: caller module name
 *
 * Registers a hotplug slot with the pci hotplug subsystem, which will allow
 * userspace interaction to the slot.
 *
 * Returns 0 if successful, anything else for an error.
 */
int __pci_hp_register(struct hotplug_slot *slot, struct pci_bus *bus,
		      int devnr, const char *name,
		      struct module *owner, const char *mod_name)
{
	int result;
	struct pci_slot *pci_slot;

	if (slot == NULL)
		return -ENODEV;
	if ((slot->info == NULL) || (slot->ops == NULL))
		return -EINVAL;
	if (slot->release == NULL) {
		dbg("Why are you trying to register a hotplug slot "
		    "without a proper release function?\n");
		return -EINVAL;
	}

	slot->ops->owner = owner;
	slot->ops->mod_name = mod_name;

	mutex_lock(&pci_hp_mutex);
	/*
	 * No problems if we call this interface from both ACPI_PCI_SLOT
	 * driver and call it here again. If we've already created the
	 * pci_slot, the interface will simply bump the refcount.
	 */
	pci_slot = pci_create_slot(bus, devnr, name, slot);
	if (IS_ERR(pci_slot)) {
		result = PTR_ERR(pci_slot);
		goto out;
	}

	slot->pci_slot = pci_slot;
	pci_slot->hotplug = slot;

	list_add(&slot->slot_list, &pci_hotplug_slot_list);

	result = fs_add_slot(pci_slot);
	kobject_uevent(&pci_slot->kobj, KOBJ_ADD);
	dbg("Added slot %s to the list\n", name);
out:
	mutex_unlock(&pci_hp_mutex);
	return result;
}

/**
 * pci_hp_deregister - deregister a hotplug_slot with the PCI hotplug subsystem
 * @hotplug: pointer to the &struct hotplug_slot to deregister
 *
 * The @slot must have been registered with the pci hotplug subsystem
 * previously with a call to pci_hp_register().
 *
 * Returns 0 if successful, anything else for an error.
 */
int pci_hp_deregister(struct hotplug_slot *hotplug)
{
	struct hotplug_slot *temp;
	struct pci_slot *slot;

	if (!hotplug)
		return -ENODEV;

	mutex_lock(&pci_hp_mutex);
	temp = get_slot_from_name(hotplug_slot_name(hotplug));
	if (temp != hotplug) {
		mutex_unlock(&pci_hp_mutex);
		return -ENODEV;
	}

	list_del(&hotplug->slot_list);

	slot = hotplug->pci_slot;
	fs_remove_slot(slot);
	dbg("Removed slot %s from the list\n", hotplug_slot_name(hotplug));

	hotplug->release(hotplug);
	slot->hotplug = NULL;
	pci_destroy_slot(slot);
	mutex_unlock(&pci_hp_mutex);

	return 0;
}

/**
 * pci_hp_change_slot_info - changes the slot's information structure in the core
 * @hotplug: pointer to the slot whose info has changed
 * @info: pointer to the info copy into the slot's info structure
 *
 * @slot must have been registered with the pci 
 * hotplug subsystem previously with a call to pci_hp_register().
 *
 * Returns 0 if successful, anything else for an error.
 */
int __must_check pci_hp_change_slot_info(struct hotplug_slot *hotplug,
					 struct hotplug_slot_info *info)
{
	struct pci_slot *slot;
	if (!hotplug || !info)
		return -ENODEV;
	slot = hotplug->pci_slot;

	memcpy(hotplug->info, info, sizeof(struct hotplug_slot_info));

	return 0;
}

static int __init pci_hotplug_init (void)
{
	int result;

	result = cpci_hotplug_init(debug);
	if (result) {
		err ("cpci_hotplug_init with error %d\n", result);
		goto err_cpci;
	}

	info (DRIVER_DESC " version: " DRIVER_VERSION "\n");

err_cpci:
	return result;
}

static void __exit pci_hotplug_exit (void)
{
	cpci_hotplug_exit();
}

module_init(pci_hotplug_init);
module_exit(pci_hotplug_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

EXPORT_SYMBOL_GPL(__pci_hp_register);
EXPORT_SYMBOL_GPL(pci_hp_deregister);
EXPORT_SYMBOL_GPL(pci_hp_change_slot_info);
