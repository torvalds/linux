// SPDX-License-Identifier: GPL-2.0+
/*
 * PCI HotPlug Controller Core
 *
 * Copyright (C) 2001-2002 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001-2002 IBM Corp.
 *
 * All rights reserved.
 *
 * Send feedback to <kristen.c.accardi@intel.com>
 *
 * Authors:
 *   Greg Kroah-Hartman <greg@kroah.com>
 *   Scott Murray <scottm@somanetworks.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/uaccess.h>
#include "../pci.h"
#include "cpci_hotplug.h"

#define MY_NAME	"pci_hotplug"

#define dbg(fmt, arg...) do { if (debug) printk(KERN_DEBUG "%s: %s: " fmt, MY_NAME, __func__, ## arg); } while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format, MY_NAME, ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format, MY_NAME, ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format, MY_NAME, ## arg)

/* local variables */
static bool debug;

/* Weee, fun with macros... */
#define GET_STATUS(name, type)	\
static int get_##name(struct hotplug_slot *slot, type *value)		\
{									\
	const struct hotplug_slot_ops *ops = slot->ops;			\
	int retval = 0;							\
	if (ops->get_##name)						\
		retval = ops->get_##name(slot, value);			\
	return retval;							\
}

GET_STATUS(power_status, u8)
GET_STATUS(attention_status, u8)
GET_STATUS(latch_status, u8)
GET_STATUS(adapter_status, u8)

static ssize_t power_read_file(struct pci_slot *pci_slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_power_status(pci_slot->hotplug, &value);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t power_write_file(struct pci_slot *pci_slot, const char *buf,
				size_t count)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	unsigned long lpower;
	u8 power;
	int retval = 0;

	lpower = simple_strtoul(buf, NULL, 10);
	power = (u8)(lpower & 0xff);
	dbg("power = %d\n", power);

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
		err("Illegal value specified for power\n");
		retval = -EINVAL;
	}

	if (retval)
		return retval;
	return count;
}

static struct pci_slot_attribute hotplug_slot_attr_power = {
	.attr = {.name = "power", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.show = power_read_file,
	.store = power_write_file
};

static ssize_t attention_read_file(struct pci_slot *pci_slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_attention_status(pci_slot->hotplug, &value);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t attention_write_file(struct pci_slot *pci_slot, const char *buf,
				    size_t count)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	const struct hotplug_slot_ops *ops = slot->ops;
	unsigned long lattention;
	u8 attention;
	int retval = 0;

	lattention = simple_strtoul(buf, NULL, 10);
	attention = (u8)(lattention & 0xff);
	dbg(" - attention = %d\n", attention);

	if (ops->set_attention_status)
		retval = ops->set_attention_status(slot, attention);

	if (retval)
		return retval;
	return count;
}

static struct pci_slot_attribute hotplug_slot_attr_attention = {
	.attr = {.name = "attention", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.show = attention_read_file,
	.store = attention_write_file
};

static ssize_t latch_read_file(struct pci_slot *pci_slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_latch_status(pci_slot->hotplug, &value);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", value);
}

static struct pci_slot_attribute hotplug_slot_attr_latch = {
	.attr = {.name = "latch", .mode = S_IFREG | S_IRUGO},
	.show = latch_read_file,
};

static ssize_t presence_read_file(struct pci_slot *pci_slot, char *buf)
{
	int retval;
	u8 value;

	retval = get_adapter_status(pci_slot->hotplug, &value);
	if (retval)
		return retval;

	return sysfs_emit(buf, "%d\n", value);
}

static struct pci_slot_attribute hotplug_slot_attr_presence = {
	.attr = {.name = "adapter", .mode = S_IFREG | S_IRUGO},
	.show = presence_read_file,
};

static ssize_t test_write_file(struct pci_slot *pci_slot, const char *buf,
			       size_t count)
{
	struct hotplug_slot *slot = pci_slot->hotplug;
	unsigned long ltest;
	u32 test;
	int retval = 0;

	ltest = simple_strtoul(buf, NULL, 10);
	test = (u32)(ltest & 0xffffffff);
	dbg("test = %d\n", test);

	if (slot->ops->hardware_test)
		retval = slot->ops->hardware_test(slot, test);

	if (retval)
		return retval;
	return count;
}

static struct pci_slot_attribute hotplug_slot_attr_test = {
	.attr = {.name = "test", .mode = S_IFREG | S_IRUGO | S_IWUSR},
	.store = test_write_file
};

static bool has_power_file(struct hotplug_slot *slot)
{
	if ((slot->ops->enable_slot) ||
	    (slot->ops->disable_slot) ||
	    (slot->ops->get_power_status))
		return true;
	return false;
}

static bool has_attention_file(struct hotplug_slot *slot)
{
	if ((slot->ops->set_attention_status) ||
	    (slot->ops->get_attention_status))
		return true;
	return false;
}

static bool has_latch_file(struct hotplug_slot *slot)
{
	if (slot->ops->get_latch_status)
		return true;
	return false;
}

static bool has_adapter_file(struct hotplug_slot *slot)
{
	if (slot->ops->get_adapter_status)
		return true;
	return false;
}

static bool has_test_file(struct hotplug_slot *slot)
{
	if (slot->ops->hardware_test)
		return true;
	return false;
}

static int fs_add_slot(struct hotplug_slot *slot, struct pci_slot *pci_slot)
{
	struct kobject *kobj;
	int retval = 0;

	/* Create symbolic link to the hotplug driver module */
	kobj = kset_find_obj(module_kset, slot->mod_name);
	if (kobj) {
		retval = sysfs_create_link(&pci_slot->kobj, kobj, "module");
		if (retval)
			dev_err(&pci_slot->bus->dev,
				"Error creating sysfs link (%d)\n", retval);
		kobject_put(kobj);
	}

	if (has_power_file(slot)) {
		retval = sysfs_create_file(&pci_slot->kobj,
					   &hotplug_slot_attr_power.attr);
		if (retval)
			goto exit_power;
	}

	if (has_attention_file(slot)) {
		retval = sysfs_create_file(&pci_slot->kobj,
					   &hotplug_slot_attr_attention.attr);
		if (retval)
			goto exit_attention;
	}

	if (has_latch_file(slot)) {
		retval = sysfs_create_file(&pci_slot->kobj,
					   &hotplug_slot_attr_latch.attr);
		if (retval)
			goto exit_latch;
	}

	if (has_adapter_file(slot)) {
		retval = sysfs_create_file(&pci_slot->kobj,
					   &hotplug_slot_attr_presence.attr);
		if (retval)
			goto exit_adapter;
	}

	if (has_test_file(slot)) {
		retval = sysfs_create_file(&pci_slot->kobj,
					   &hotplug_slot_attr_test.attr);
		if (retval)
			goto exit_test;
	}

	goto exit;

exit_test:
	if (has_adapter_file(slot))
		sysfs_remove_file(&pci_slot->kobj,
				  &hotplug_slot_attr_presence.attr);
exit_adapter:
	if (has_latch_file(slot))
		sysfs_remove_file(&pci_slot->kobj, &hotplug_slot_attr_latch.attr);
exit_latch:
	if (has_attention_file(slot))
		sysfs_remove_file(&pci_slot->kobj,
				  &hotplug_slot_attr_attention.attr);
exit_attention:
	if (has_power_file(slot))
		sysfs_remove_file(&pci_slot->kobj, &hotplug_slot_attr_power.attr);
exit_power:
	sysfs_remove_link(&pci_slot->kobj, "module");
exit:
	return retval;
}

static void fs_remove_slot(struct hotplug_slot *slot, struct pci_slot *pci_slot)
{
	if (has_power_file(slot))
		sysfs_remove_file(&pci_slot->kobj, &hotplug_slot_attr_power.attr);

	if (has_attention_file(slot))
		sysfs_remove_file(&pci_slot->kobj,
				  &hotplug_slot_attr_attention.attr);

	if (has_latch_file(slot))
		sysfs_remove_file(&pci_slot->kobj, &hotplug_slot_attr_latch.attr);

	if (has_adapter_file(slot))
		sysfs_remove_file(&pci_slot->kobj,
				  &hotplug_slot_attr_presence.attr);

	if (has_test_file(slot))
		sysfs_remove_file(&pci_slot->kobj, &hotplug_slot_attr_test.attr);

	sysfs_remove_link(&pci_slot->kobj, "module");
}

/**
 * __pci_hp_register - register a hotplug_slot with the PCI hotplug subsystem
 * @slot: pointer to the &struct hotplug_slot to register
 * @bus: bus this slot is on
 * @devnr: device number
 * @name: name registered with kobject core
 * @owner: caller module owner
 * @mod_name: caller module name
 *
 * Prepares a hotplug slot for in-kernel use and immediately publishes it to
 * user space in one go.  Drivers may alternatively carry out the two steps
 * separately by invoking pci_hp_initialize() and pci_hp_add().
 *
 * Returns 0 if successful, anything else for an error.
 */
int __pci_hp_register(struct hotplug_slot *slot, struct pci_bus *bus,
		      int devnr, const char *name,
		      struct module *owner, const char *mod_name)
{
	int result;

	result = __pci_hp_initialize(slot, bus, devnr, name, owner, mod_name);
	if (result)
		return result;

	result = pci_hp_add(slot);
	if (result)
		pci_hp_destroy(slot);

	return result;
}
EXPORT_SYMBOL_GPL(__pci_hp_register);

/**
 * __pci_hp_initialize - prepare hotplug slot for in-kernel use
 * @slot: pointer to the &struct hotplug_slot to initialize
 * @bus: bus this slot is on
 * @devnr: slot number
 * @name: name registered with kobject core
 * @owner: caller module owner
 * @mod_name: caller module name
 *
 * Allocate and fill in a PCI slot for use by a hotplug driver.  Once this has
 * been called, the driver may invoke hotplug_slot_name() to get the slot's
 * unique name.  The driver must be prepared to handle a ->reset_slot callback
 * from this point on.
 *
 * Returns 0 on success or a negative int on error.
 */
int __pci_hp_initialize(struct hotplug_slot *slot, struct pci_bus *bus,
			int devnr, const char *name, struct module *owner,
			const char *mod_name)
{
	struct pci_slot *pci_slot;

	if (slot == NULL)
		return -ENODEV;
	if (slot->ops == NULL)
		return -EINVAL;

	slot->owner = owner;
	slot->mod_name = mod_name;

	/*
	 * No problems if we call this interface from both ACPI_PCI_SLOT
	 * driver and call it here again. If we've already created the
	 * pci_slot, the interface will simply bump the refcount.
	 */
	pci_slot = pci_create_slot(bus, devnr, name, slot);
	if (IS_ERR(pci_slot))
		return PTR_ERR(pci_slot);

	slot->pci_slot = pci_slot;
	pci_slot->hotplug = slot;
	return 0;
}
EXPORT_SYMBOL_GPL(__pci_hp_initialize);

/**
 * pci_hp_add - publish hotplug slot to user space
 * @slot: pointer to the &struct hotplug_slot to publish
 *
 * Make a hotplug slot's sysfs interface available and inform user space of its
 * addition by sending a uevent.  The hotplug driver must be prepared to handle
 * all &struct hotplug_slot_ops callbacks from this point on.
 *
 * Returns 0 on success or a negative int on error.
 */
int pci_hp_add(struct hotplug_slot *slot)
{
	struct pci_slot *pci_slot;
	int result;

	if (WARN_ON(!slot))
		return -EINVAL;

	pci_slot = slot->pci_slot;

	result = fs_add_slot(slot, pci_slot);
	if (result)
		return result;

	kobject_uevent(&pci_slot->kobj, KOBJ_ADD);
	return 0;
}
EXPORT_SYMBOL_GPL(pci_hp_add);

/**
 * pci_hp_deregister - deregister a hotplug_slot with the PCI hotplug subsystem
 * @slot: pointer to the &struct hotplug_slot to deregister
 *
 * The @slot must have been registered with the pci hotplug subsystem
 * previously with a call to pci_hp_register().
 */
void pci_hp_deregister(struct hotplug_slot *slot)
{
	pci_hp_del(slot);
	pci_hp_destroy(slot);
}
EXPORT_SYMBOL_GPL(pci_hp_deregister);

/**
 * pci_hp_del - unpublish hotplug slot from user space
 * @slot: pointer to the &struct hotplug_slot to unpublish
 *
 * Remove a hotplug slot's sysfs interface.
 */
void pci_hp_del(struct hotplug_slot *slot)
{
	if (WARN_ON(!slot))
		return;

	fs_remove_slot(slot, slot->pci_slot);
}
EXPORT_SYMBOL_GPL(pci_hp_del);

/**
 * pci_hp_destroy - remove hotplug slot from in-kernel use
 * @slot: pointer to the &struct hotplug_slot to destroy
 *
 * Destroy a PCI slot used by a hotplug driver.  Once this has been called,
 * the driver may no longer invoke hotplug_slot_name() to get the slot's
 * unique name.  The driver no longer needs to handle a ->reset_slot callback
 * from this point on.
 */
void pci_hp_destroy(struct hotplug_slot *slot)
{
	struct pci_slot *pci_slot = slot->pci_slot;

	slot->pci_slot = NULL;
	pci_slot->hotplug = NULL;
	pci_destroy_slot(pci_slot);
}
EXPORT_SYMBOL_GPL(pci_hp_destroy);

static int __init pci_hotplug_init(void)
{
	int result;

	result = cpci_hotplug_init(debug);
	if (result) {
		err("cpci_hotplug_init with error %d\n", result);
		return result;
	}

	return result;
}
device_initcall(pci_hotplug_init);

/*
 * not really modular, but the easiest way to keep compat with existing
 * bootargs behaviour is to continue using module_param here.
 */
module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");
