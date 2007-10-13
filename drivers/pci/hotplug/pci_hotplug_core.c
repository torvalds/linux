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
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <asm/uaccess.h>

#define MY_NAME	"pci_hotplug"

#define dbg(fmt, arg...) do { if (debug) printk(KERN_DEBUG "%s: %s: " fmt , MY_NAME , __FUNCTION__ , ## arg); } while (0)
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

struct kset pci_hotplug_slots_subsys;

static ssize_t hotplug_slot_attr_show(struct kobject *kobj,
		struct attribute *attr, char *buf)
{
	struct hotplug_slot *slot = to_hotplug_slot(kobj);
	struct hotplug_slot_attribute *attribute = to_hotplug_attr(attr);
	return attribute->show ? attribute->show(slot, buf) : -EIO;
}

static ssize_t hotplug_slot_attr_store(struct kobject *kobj,
		struct attribute *attr, const char *buf, size_t len)
{
	struct hotplug_slot *slot = to_hotplug_slot(kobj);
	struct hotplug_slot_attribute *attribute = to_hotplug_attr(attr);
	return attribute->store ? attribute->store(slot, buf, len) : -EIO;
}

static struct sysfs_ops hotplug_slot_sysfs_ops = {
	.show = hotplug_slot_attr_show,
	.store = hotplug_slot_attr_store,
};

static void hotplug_slot_release(struct kobject *kobj)
{
	struct hotplug_slot *slot = to_hotplug_slot(kobj);
	if (slot->release)
		slot->release(slot);
}

static struct kobj_type hotplug_slot_ktype = {
	.sysfs_ops = &hotplug_slot_sysfs_ops,
	.release = &hotplug_slot_release,
};

decl_subsys_name(pci_hotplug_slots, slots, &hotplug_slot_ktype, NULL);

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
	if (try_module_get(ops->owner)) {				\
		if (ops->get_##name)					\
			retval = ops->get_##name (slot, value);		\
		else							\
			*value = slot->info->name;			\
		module_put(ops->owner);					\
	}								\
	return retval;							\
}

GET_STATUS(power_status, u8)
GET_STATUS(attention_status, u8)
GET_STATUS(latch_status, u8)
GET_STATUS(adapter_status, u8)
GET_STATUS(address, u32)
GET_STATUS(max_bus_speed, enum pci_bus_speed)
GET_STATUS(cur_bus_speed, enum pci_bus_speed)

static ssize_t power_read_file (struct hotplug_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_power_status (slot, &value);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%d\n", value);
exit:
	return retval;
}

static ssize_t power_write_file (struct hotplug_slot *slot, const char *buf,
		size_t count)
{
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

static struct hotplug_slot_attribute hotplug_slot_attr_power = {
	.attr = {.name = "power", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.show = power_read_file,
	.store = power_write_file
};

static ssize_t attention_read_file (struct hotplug_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_attention_status (slot, &value);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%d\n", value);

exit:
	return retval;
}

static ssize_t attention_write_file (struct hotplug_slot *slot, const char *buf,
		size_t count)
{
	unsigned long lattention;
	u8 attention;
	int retval = 0;

	lattention = simple_strtoul (buf, NULL, 10);
	attention = (u8)(lattention & 0xff);
	dbg (" - attention = %d\n", attention);

	if (!try_module_get(slot->ops->owner)) {
		retval = -ENODEV;
		goto exit;
	}
	if (slot->ops->set_attention_status)
		retval = slot->ops->set_attention_status(slot, attention);
	module_put(slot->ops->owner);

exit:	
	if (retval)
		return retval;
	return count;
}

static struct hotplug_slot_attribute hotplug_slot_attr_attention = {
	.attr = {.name = "attention", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.show = attention_read_file,
	.store = attention_write_file
};

static ssize_t latch_read_file (struct hotplug_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_latch_status (slot, &value);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%d\n", value);

exit:
	return retval;
}

static struct hotplug_slot_attribute hotplug_slot_attr_latch = {
	.attr = {.name = "latch", .mode = S_IFREG | S_IRUGO},
	.show = latch_read_file,
};

static ssize_t presence_read_file (struct hotplug_slot *slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_adapter_status (slot, &value);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%d\n", value);

exit:
	return retval;
}

static struct hotplug_slot_attribute hotplug_slot_attr_presence = {
	.attr = {.name = "adapter", .mode = S_IFREG | S_IRUGO},
	.show = presence_read_file,
};

static ssize_t address_read_file (struct hotplug_slot *slot, char *buf)
{
	int retval;
	u32 address;

	retval = get_address (slot, &address);
	if (retval)
		goto exit;
	retval = sprintf (buf, "%04x:%02x:%02x\n",
			  (address >> 16) & 0xffff,
			  (address >> 8) & 0xff,
			  address & 0xff);

exit:
	return retval;
}

static struct hotplug_slot_attribute hotplug_slot_attr_address = {
	.attr = {.name = "address", .mode = S_IFREG | S_IRUGO},
	.show = address_read_file,
};

static char *unknown_speed = "Unknown bus speed";

static ssize_t max_bus_speed_read_file (struct hotplug_slot *slot, char *buf)
{
	char *speed_string;
	int retval;
	enum pci_bus_speed value;
	
	retval = get_max_bus_speed (slot, &value);
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

static struct hotplug_slot_attribute hotplug_slot_attr_max_bus_speed = {
	.attr = {.name = "max_bus_speed", .mode = S_IFREG | S_IRUGO},
	.show = max_bus_speed_read_file,
};

static ssize_t cur_bus_speed_read_file (struct hotplug_slot *slot, char *buf)
{
	char *speed_string;
	int retval;
	enum pci_bus_speed value;

	retval = get_cur_bus_speed (slot, &value);
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

static struct hotplug_slot_attribute hotplug_slot_attr_cur_bus_speed = {
	.attr = {.name = "cur_bus_speed", .mode = S_IFREG | S_IRUGO},
	.show = cur_bus_speed_read_file,
};

static ssize_t test_write_file (struct hotplug_slot *slot, const char *buf,
		size_t count)
{
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

static struct hotplug_slot_attribute hotplug_slot_attr_test = {
	.attr = {.name = "test", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.store = test_write_file
};

static int has_power_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if ((slot->ops->enable_slot) ||
	    (slot->ops->disable_slot) ||
	    (slot->ops->get_power_status))
		return 0;
	return -ENOENT;
}

static int has_attention_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if ((slot->ops->set_attention_status) ||
	    (slot->ops->get_attention_status))
		return 0;
	return -ENOENT;
}

static int has_latch_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if (slot->ops->get_latch_status)
		return 0;
	return -ENOENT;
}

static int has_adapter_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if (slot->ops->get_adapter_status)
		return 0;
	return -ENOENT;
}

static int has_address_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if (slot->ops->get_address)
		return 0;
	return -ENOENT;
}

static int has_max_bus_speed_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if (slot->ops->get_max_bus_speed)
		return 0;
	return -ENOENT;
}

static int has_cur_bus_speed_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if (slot->ops->get_cur_bus_speed)
		return 0;
	return -ENOENT;
}

static int has_test_file (struct hotplug_slot *slot)
{
	if ((!slot) || (!slot->ops))
		return -ENODEV;
	if (slot->ops->hardware_test)
		return 0;
	return -ENOENT;
}

static int fs_add_slot (struct hotplug_slot *slot)
{
	int retval = 0;

	if (has_power_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj, &hotplug_slot_attr_power.attr);
		if (retval)
			goto exit_power;
	}

	if (has_attention_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_attention.attr);
		if (retval)
			goto exit_attention;
	}

	if (has_latch_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_latch.attr);
		if (retval)
			goto exit_latch;
	}

	if (has_adapter_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_presence.attr);
		if (retval)
			goto exit_adapter;
	}

	if (has_address_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_address.attr);
		if (retval)
			goto exit_address;
	}

	if (has_max_bus_speed_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_max_bus_speed.attr);
		if (retval)
			goto exit_max_speed;
	}

	if (has_cur_bus_speed_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_cur_bus_speed.attr);
		if (retval)
			goto exit_cur_speed;
	}

	if (has_test_file(slot) == 0) {
		retval = sysfs_create_file(&slot->kobj,
					   &hotplug_slot_attr_test.attr);
		if (retval)
			goto exit_test;
	}

	goto exit;

exit_test:
	if (has_cur_bus_speed_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_cur_bus_speed.attr);

exit_cur_speed:
	if (has_max_bus_speed_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_max_bus_speed.attr);

exit_max_speed:
	if (has_address_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_address.attr);

exit_address:
	if (has_adapter_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_presence.attr);

exit_adapter:
	if (has_latch_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_latch.attr);

exit_latch:
	if (has_attention_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_attention.attr);

exit_attention:
	if (has_power_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_power.attr);
exit_power:
exit:
	return retval;
}

static void fs_remove_slot (struct hotplug_slot *slot)
{
	if (has_power_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_power.attr);

	if (has_attention_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_attention.attr);

	if (has_latch_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_latch.attr);

	if (has_adapter_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_presence.attr);

	if (has_address_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_address.attr);

	if (has_max_bus_speed_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_max_bus_speed.attr);

	if (has_cur_bus_speed_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_cur_bus_speed.attr);

	if (has_test_file(slot) == 0)
		sysfs_remove_file(&slot->kobj, &hotplug_slot_attr_test.attr);
}

static struct hotplug_slot *get_slot_from_name (const char *name)
{
	struct hotplug_slot *slot;
	struct list_head *tmp;

	list_for_each (tmp, &pci_hotplug_slot_list) {
		slot = list_entry (tmp, struct hotplug_slot, slot_list);
		if (strcmp(slot->name, name) == 0)
			return slot;
	}
	return NULL;
}

/**
 * pci_hp_register - register a hotplug_slot with the PCI hotplug subsystem
 * @slot: pointer to the &struct hotplug_slot to register
 *
 * Registers a hotplug slot with the pci hotplug subsystem, which will allow
 * userspace interaction to the slot.
 *
 * Returns 0 if successful, anything else for an error.
 */
int pci_hp_register (struct hotplug_slot *slot)
{
	int result;

	if (slot == NULL)
		return -ENODEV;
	if ((slot->info == NULL) || (slot->ops == NULL))
		return -EINVAL;
	if (slot->release == NULL) {
		dbg("Why are you trying to register a hotplug slot"
		    "without a proper release function?\n");
		return -EINVAL;
	}

	kobject_set_name(&slot->kobj, "%s", slot->name);
	kobj_set_kset_s(slot, pci_hotplug_slots_subsys);

	/* this can fail if we have already registered a slot with the same name */
	if (kobject_register(&slot->kobj)) {
		err("Unable to register kobject");
		return -EINVAL;
	}
		
	list_add (&slot->slot_list, &pci_hotplug_slot_list);

	result = fs_add_slot (slot);
	dbg ("Added slot %s to the list\n", slot->name);
	return result;
}

/**
 * pci_hp_deregister - deregister a hotplug_slot with the PCI hotplug subsystem
 * @slot: pointer to the &struct hotplug_slot to deregister
 *
 * The @slot must have been registered with the pci hotplug subsystem
 * previously with a call to pci_hp_register().
 *
 * Returns 0 if successful, anything else for an error.
 */
int pci_hp_deregister (struct hotplug_slot *slot)
{
	struct hotplug_slot *temp;

	if (slot == NULL)
		return -ENODEV;

	temp = get_slot_from_name (slot->name);
	if (temp != slot) {
		return -ENODEV;
	}
	list_del (&slot->slot_list);

	fs_remove_slot (slot);
	dbg ("Removed slot %s from the list\n", slot->name);
	kobject_unregister(&slot->kobj);
	return 0;
}

/**
 * pci_hp_change_slot_info - changes the slot's information structure in the core
 * @slot: pointer to the slot whose info has changed
 * @info: pointer to the info copy into the slot's info structure
 *
 * @slot must have been registered with the pci 
 * hotplug subsystem previously with a call to pci_hp_register().
 *
 * Returns 0 if successful, anything else for an error.
 */
int __must_check pci_hp_change_slot_info(struct hotplug_slot *slot,
					 struct hotplug_slot_info *info)
{
	int retval;

	if ((slot == NULL) || (info == NULL))
		return -ENODEV;

	memcpy (slot->info, info, sizeof (struct hotplug_slot_info));

	return 0;
}

static int __init pci_hotplug_init (void)
{
	int result;

	kobj_set_kset_s(&pci_hotplug_slots_subsys, pci_bus_type.subsys);
	result = subsystem_register(&pci_hotplug_slots_subsys);
	if (result) {
		err("Register subsys with error %d\n", result);
		goto exit;
	}
	result = cpci_hotplug_init(debug);
	if (result) {
		err ("cpci_hotplug_init with error %d\n", result);
		goto err_subsys;
	}

	info (DRIVER_DESC " version: " DRIVER_VERSION "\n");
	goto exit;
	
err_subsys:
	subsystem_unregister(&pci_hotplug_slots_subsys);
exit:
	return result;
}

static void __exit pci_hotplug_exit (void)
{
	cpci_hotplug_exit();
	subsystem_unregister(&pci_hotplug_slots_subsys);
}

module_init(pci_hotplug_init);
module_exit(pci_hotplug_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

EXPORT_SYMBOL_GPL(pci_hotplug_slots_subsys);
EXPORT_SYMBOL_GPL(pci_hp_register);
EXPORT_SYMBOL_GPL(pci_hp_deregister);
EXPORT_SYMBOL_GPL(pci_hp_change_slot_info);
