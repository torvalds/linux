// SPDX-License-Identifier: GPL-2.0-only
/*
 * Link physical devices with ACPI devices support
 *
 * Copyright (c) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2005 Intel Corp.
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/acpi_iort.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/pci-acpi.h>
#include <linux/platform_device.h>

#include "internal.h"

static LIST_HEAD(bus_type_list);
static DECLARE_RWSEM(bus_type_sem);

#define PHYSICAL_NODE_STRING "physical_node"
#define PHYSICAL_NODE_NAME_SIZE (sizeof(PHYSICAL_NODE_STRING) + 10)

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -ENODEV;
	if (type && type->match && type->find_companion) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		pr_info("bus type %s registered\n", type->name);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(register_acpi_bus_type);

int unregister_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return 0;
	if (type) {
		down_write(&bus_type_sem);
		list_del_init(&type->list);
		up_write(&bus_type_sem);
		pr_info("bus type %s unregistered\n", type->name);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(unregister_acpi_bus_type);

static struct acpi_device *acpi_companion_lookup(struct device *dev)
{
	struct acpi_bus_type *type;

	if (!dev->type)
		return NULL;

	guard(rwsem_read)(&bus_type_sem);

	list_for_each_entry(type, &bus_type_list, list) {
		struct acpi_device *adev;

		if (!type->match(dev))
			continue;

		adev = type->find_companion(dev);
		if (!adev) {
			dev_dbg(dev, "ACPI companion not found\n");
			return NULL;
		}
		if (acpi_bind_one(dev, adev)) {
			dev_dbg(dev, "Binding to ACPI companion failed\n");
			return NULL;
		}
		if (type->setup)
			type->setup(dev);

		return adev;
	}

	return NULL;
}

#define FIND_CHILD_MIN_SCORE	1
#define FIND_CHILD_MID_SCORE	2
#define FIND_CHILD_MAX_SCORE	3

static int match_any(struct acpi_device *adev, void *not_used)
{
	return 1;
}

static bool acpi_dev_has_children(struct acpi_device *adev)
{
	return acpi_dev_for_each_child(adev, match_any, NULL) > 0;
}

static int find_child_checks(struct acpi_device *adev, bool check_children)
{
	unsigned long long sta;
	acpi_status status;

	if (check_children && !acpi_dev_has_children(adev))
		return -ENODEV;

	status = acpi_evaluate_integer(adev->handle, "_STA", NULL, &sta);
	if (status == AE_NOT_FOUND) {
		/*
		 * Special case: backlight device objects without _STA are
		 * preferred to other objects with the same _ADR value, because
		 * it is more likely that they are actually useful.
		 */
		if (adev->pnp.type.backlight)
			return FIND_CHILD_MID_SCORE;

		return FIND_CHILD_MIN_SCORE;
	}

	if (ACPI_FAILURE(status) || !(sta & ACPI_STA_DEVICE_ENABLED))
		return -ENODEV;

	/*
	 * If the device has a _HID returning a valid ACPI/PNP device ID, it is
	 * better to make it look less attractive here, so that the other device
	 * with the same _ADR value (that may not have a valid device ID) can be
	 * matched going forward.  [This means a second spec violation in a row,
	 * so whatever we do here is best effort anyway.]
	 */
	if (adev->pnp.type.platform_id)
		return FIND_CHILD_MIN_SCORE;

	return FIND_CHILD_MAX_SCORE;
}

struct find_child_walk_data {
	struct acpi_device *adev;
	u64 address;
	int score;
	bool check_sta;
	bool check_children;
};

static int check_one_child(struct acpi_device *adev, void *data)
{
	struct find_child_walk_data *wd = data;
	int score;

	if (!adev->pnp.type.bus_address || acpi_device_adr(adev) != wd->address)
		return 0;

	if (!wd->adev) {
		/*
		 * This is the first matching object, so save it.  If it is not
		 * necessary to look for any other matching objects, stop the
		 * search.
		 */
		wd->adev = adev;
		return !(wd->check_sta || wd->check_children);
	}

	/*
	 * There is more than one matching device object with the same _ADR
	 * value.  That really is unexpected, so we are kind of beyond the scope
	 * of the spec here.  We have to choose which one to return, though.
	 *
	 * First, get the score for the previously found object and terminate
	 * the walk if it is maximum.
	*/
	if (!wd->score) {
		score = find_child_checks(wd->adev, wd->check_children);
		if (score == FIND_CHILD_MAX_SCORE)
			return 1;

		wd->score = score;
	}
	/*
	 * Second, if the object that has just been found has a better score,
	 * replace the previously found one with it and terminate the walk if
	 * the new score is maximum.
	 */
	score = find_child_checks(adev, wd->check_children);
	if (score > wd->score) {
		wd->adev = adev;
		if (score == FIND_CHILD_MAX_SCORE)
			return 1;

		wd->score = score;
	}

	/* Continue, because there may be better matches. */
	return 0;
}

static struct acpi_device *acpi_find_child(struct acpi_device *parent,
					   u64 address, bool check_children,
					   bool check_sta)
{
	struct find_child_walk_data wd = {
		.address = address,
		.check_children = check_children,
		.check_sta = check_sta,
		.adev = NULL,
		.score = 0,
	};

	if (parent)
		acpi_dev_for_each_child(parent, check_one_child, &wd);

	return wd.adev;
}

struct acpi_device *acpi_find_child_device(struct acpi_device *parent,
					   u64 address, bool check_children)
{
	return acpi_find_child(parent, address, check_children, true);
}
EXPORT_SYMBOL_GPL(acpi_find_child_device);

struct acpi_device *acpi_find_child_by_adr(struct acpi_device *adev,
					   acpi_bus_address adr)
{
	return acpi_find_child(adev, adr, false, false);
}
EXPORT_SYMBOL_GPL(acpi_find_child_by_adr);

static void acpi_physnode_link_name(char *buf, unsigned int node_id)
{
	if (node_id > 0)
		snprintf(buf, PHYSICAL_NODE_NAME_SIZE,
			 PHYSICAL_NODE_STRING "%u", node_id);
	else
		strcpy(buf, PHYSICAL_NODE_STRING);
}

int acpi_bind_one(struct device *dev, struct acpi_device *acpi_dev)
{
	struct acpi_device_physical_node *physical_node, *pn;
	struct acpi_device *comp_dev = ACPI_COMPANION(dev);
	char physical_node_name[PHYSICAL_NODE_NAME_SIZE];
	struct list_head *physnode_list;
	unsigned int node_id;
	int retval = -EINVAL;

	if (!acpi_dev) {
		if (!comp_dev)
			return -EINVAL;

		/* If the companion has been set upfront, pick it up. */
		acpi_dev = comp_dev;
	} else if (comp_dev && acpi_dev != comp_dev) {
		dev_warn(dev, "ACPI companion already set to %s which is not %s\n",
			 acpi_dev_name(comp_dev), acpi_dev_name(acpi_dev));
		return -EEXIST;
	}

	guard(mutex)(&acpi_dev->physical_node_lock);

	/*
	 * Keep the list sorted by node_id so that the IDs of removed nodes can
	 * be recycled easily.
	 */
	physnode_list = &acpi_dev->physical_node_list;
	node_id = 0;
	list_for_each_entry(pn, &acpi_dev->physical_node_list, node) {
		/* Sanity check. */
		if (pn->dev == dev) {
			if (!comp_dev) {
				/* Really unexpected. */
				ACPI_COMPANION_SET(dev, acpi_dev);
				dev_warn(&acpi_dev->dev,
					 "Physical device list corruption fixed up\n");
			}
			return 0;
		}
		if (pn->node_id == node_id) {
			physnode_list = &pn->node;
			node_id++;
		}
	}

	physical_node = kzalloc_obj(*physical_node);
	if (!physical_node)
		return -ENOMEM;

	acpi_dev_get(acpi_dev);
	get_device(dev);

	physical_node->node_id = node_id;
	physical_node->dev = dev;
	list_add(&physical_node->node, physnode_list);
	acpi_dev->physical_node_count++;

	if (!comp_dev)
		ACPI_COMPANION_SET(dev, acpi_dev);

	acpi_physnode_link_name(physical_node_name, node_id);
	retval = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				   physical_node_name);
	if (retval)
		dev_err(&acpi_dev->dev, "Failed to create link %s (%d)\n",
			physical_node_name, retval);

	retval = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				   "firmware_node");
	if (retval)
		dev_err(dev, "Failed to create link firmware_node (%d)\n",
			retval);

	if (acpi_dev->wakeup.flags.valid)
		device_set_wakeup_capable(dev, true);

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_bind_one);

void acpi_unbind_one(struct device *dev)
{
	struct acpi_device *acpi_dev = ACPI_COMPANION(dev);
	struct acpi_device_physical_node *entry;

	if (!acpi_dev)
		return;

	mutex_lock(&acpi_dev->physical_node_lock);

	list_for_each_entry(entry, &acpi_dev->physical_node_list, node)
		if (entry->dev == dev) {
			char physnode_name[PHYSICAL_NODE_NAME_SIZE];

			list_del(&entry->node);
			acpi_dev->physical_node_count--;

			acpi_physnode_link_name(physnode_name, entry->node_id);
			sysfs_remove_link(&acpi_dev->dev.kobj, physnode_name);
			sysfs_remove_link(&dev->kobj, "firmware_node");
			ACPI_COMPANION_SET(dev, NULL);

			mutex_unlock(&acpi_dev->physical_node_lock);

			/* Drop references taken by acpi_bind_one(). */
			put_device(dev);
			acpi_dev_put(acpi_dev);
			kfree(entry);
			return;
		}

	mutex_unlock(&acpi_dev->physical_node_lock);
}
EXPORT_SYMBOL_GPL(acpi_unbind_one);

void acpi_device_notify(struct device *dev)
{
	struct acpi_device *adev;
	int ret;

	/* ACPI devices have no ACPI companions. */
	if (dev->bus == &acpi_bus_type)
		return;

	ret = acpi_bind_one(dev, NULL);
	if (ret) {
		adev = acpi_companion_lookup(dev);
		if (!adev)
			return;
	} else {
		adev = ACPI_COMPANION(dev);

		if (dev_is_pci(dev)) {
			pci_acpi_setup(dev, adev);
		} else if (dev_is_platform(dev)) {
			acpi_configure_pmsi_domain(dev);

			if (adev->handler && adev->handler->bind)
				adev->handler->bind(dev);
		}
	}

	dev_dbg(dev, "Bound to ACPI device %s\n", acpi_dev_name(adev));
}

void acpi_device_notify_remove(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);

	if (!adev)
		return;

	if (dev_is_pci(dev))
		pci_acpi_cleanup(dev, adev);
	else if (adev->handler && adev->handler->unbind)
		adev->handler->unbind(dev);

	acpi_unbind_one(dev);
}
