// SPDX-License-Identifier: GPL-2.0-only
/*
 * Link physical devices with ACPI devices support
 *
 * Copyright (c) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2005 Intel Corp.
 */

#include <linux/acpi_iort.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include "internal.h"

#define ACPI_GLUE_DEBUG	0
#if ACPI_GLUE_DEBUG
#define DBG(fmt, ...)						\
	printk(KERN_DEBUG PREFIX fmt, ##__VA_ARGS__)
#else
#define DBG(fmt, ...)						\
do {								\
	if (0)							\
		printk(KERN_DEBUG PREFIX fmt, ##__VA_ARGS__);	\
} while (0)
#endif
static LIST_HEAD(bus_type_list);
static DECLARE_RWSEM(bus_type_sem);

#define PHYSICAL_NODE_STRING "physical_yesde"
#define PHYSICAL_NODE_NAME_SIZE (sizeof(PHYSICAL_NODE_STRING) + 10)

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -ENODEV;
	if (type && type->match && type->find_companion) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "bus type %s registered\n", type->name);
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
		printk(KERN_INFO PREFIX "bus type %s unregistered\n",
		       type->name);
		return 0;
	}
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(unregister_acpi_bus_type);

static struct acpi_bus_type *acpi_get_bus_type(struct device *dev)
{
	struct acpi_bus_type *tmp, *ret = NULL;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->match(dev)) {
			ret = tmp;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

#define FIND_CHILD_MIN_SCORE	1
#define FIND_CHILD_MAX_SCORE	2

static int find_child_checks(struct acpi_device *adev, bool check_children)
{
	bool sta_present = true;
	unsigned long long sta;
	acpi_status status;

	status = acpi_evaluate_integer(adev->handle, "_STA", NULL, &sta);
	if (status == AE_NOT_FOUND)
		sta_present = false;
	else if (ACPI_FAILURE(status) || !(sta & ACPI_STA_DEVICE_ENABLED))
		return -ENODEV;

	if (check_children && list_empty(&adev->children))
		return -ENODEV;

	/*
	 * If the device has a _HID returning a valid ACPI/PNP device ID, it is
	 * better to make it look less attractive here, so that the other device
	 * with the same _ADR value (that may yest have a valid device ID) can be
	 * matched going forward.  [This means a second spec violation in a row,
	 * so whatever we do here is best effort anyway.]
	 */
	return sta_present && !adev->pnp.type.platform_id ?
			FIND_CHILD_MAX_SCORE : FIND_CHILD_MIN_SCORE;
}

struct acpi_device *acpi_find_child_device(struct acpi_device *parent,
					   u64 address, bool check_children)
{
	struct acpi_device *adev, *ret = NULL;
	int ret_score = 0;

	if (!parent)
		return NULL;

	list_for_each_entry(adev, &parent->children, yesde) {
		unsigned long long addr;
		acpi_status status;
		int score;

		status = acpi_evaluate_integer(adev->handle, METHOD_NAME__ADR,
					       NULL, &addr);
		if (ACPI_FAILURE(status) || addr != address)
			continue;

		if (!ret) {
			/* This is the first matching object.  Save it. */
			ret = adev;
			continue;
		}
		/*
		 * There is more than one matching device object with the same
		 * _ADR value.  That really is unexpected, so we are kind of
		 * beyond the scope of the spec here.  We have to choose which
		 * one to return, though.
		 *
		 * First, check if the previously found object is good eyesugh
		 * and return it if so.  Second, do the same for the object that
		 * we've just found.
		 */
		if (!ret_score) {
			ret_score = find_child_checks(ret, check_children);
			if (ret_score == FIND_CHILD_MAX_SCORE)
				return ret;
		}
		score = find_child_checks(adev, check_children);
		if (score == FIND_CHILD_MAX_SCORE) {
			return adev;
		} else if (score > ret_score) {
			ret = adev;
			ret_score = score;
		}
	}
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_find_child_device);

static void acpi_physyesde_link_name(char *buf, unsigned int yesde_id)
{
	if (yesde_id > 0)
		snprintf(buf, PHYSICAL_NODE_NAME_SIZE,
			 PHYSICAL_NODE_STRING "%u", yesde_id);
	else
		strcpy(buf, PHYSICAL_NODE_STRING);
}

int acpi_bind_one(struct device *dev, struct acpi_device *acpi_dev)
{
	struct acpi_device_physical_yesde *physical_yesde, *pn;
	char physical_yesde_name[PHYSICAL_NODE_NAME_SIZE];
	struct list_head *physyesde_list;
	unsigned int yesde_id;
	int retval = -EINVAL;

	if (has_acpi_companion(dev)) {
		if (acpi_dev) {
			dev_warn(dev, "ACPI companion already set\n");
			return -EINVAL;
		} else {
			acpi_dev = ACPI_COMPANION(dev);
		}
	}
	if (!acpi_dev)
		return -EINVAL;

	get_device(&acpi_dev->dev);
	get_device(dev);
	physical_yesde = kzalloc(sizeof(*physical_yesde), GFP_KERNEL);
	if (!physical_yesde) {
		retval = -ENOMEM;
		goto err;
	}

	mutex_lock(&acpi_dev->physical_yesde_lock);

	/*
	 * Keep the list sorted by yesde_id so that the IDs of removed yesdes can
	 * be recycled easily.
	 */
	physyesde_list = &acpi_dev->physical_yesde_list;
	yesde_id = 0;
	list_for_each_entry(pn, &acpi_dev->physical_yesde_list, yesde) {
		/* Sanity check. */
		if (pn->dev == dev) {
			mutex_unlock(&acpi_dev->physical_yesde_lock);

			dev_warn(dev, "Already associated with ACPI yesde\n");
			kfree(physical_yesde);
			if (ACPI_COMPANION(dev) != acpi_dev)
				goto err;

			put_device(dev);
			put_device(&acpi_dev->dev);
			return 0;
		}
		if (pn->yesde_id == yesde_id) {
			physyesde_list = &pn->yesde;
			yesde_id++;
		}
	}

	physical_yesde->yesde_id = yesde_id;
	physical_yesde->dev = dev;
	list_add(&physical_yesde->yesde, physyesde_list);
	acpi_dev->physical_yesde_count++;

	if (!has_acpi_companion(dev))
		ACPI_COMPANION_SET(dev, acpi_dev);

	acpi_physyesde_link_name(physical_yesde_name, yesde_id);
	retval = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				   physical_yesde_name);
	if (retval)
		dev_err(&acpi_dev->dev, "Failed to create link %s (%d)\n",
			physical_yesde_name, retval);

	retval = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				   "firmware_yesde");
	if (retval)
		dev_err(dev, "Failed to create link firmware_yesde (%d)\n",
			retval);

	mutex_unlock(&acpi_dev->physical_yesde_lock);

	if (acpi_dev->wakeup.flags.valid)
		device_set_wakeup_capable(dev, true);

	return 0;

 err:
	ACPI_COMPANION_SET(dev, NULL);
	put_device(dev);
	put_device(&acpi_dev->dev);
	return retval;
}
EXPORT_SYMBOL_GPL(acpi_bind_one);

int acpi_unbind_one(struct device *dev)
{
	struct acpi_device *acpi_dev = ACPI_COMPANION(dev);
	struct acpi_device_physical_yesde *entry;

	if (!acpi_dev)
		return 0;

	mutex_lock(&acpi_dev->physical_yesde_lock);

	list_for_each_entry(entry, &acpi_dev->physical_yesde_list, yesde)
		if (entry->dev == dev) {
			char physyesde_name[PHYSICAL_NODE_NAME_SIZE];

			list_del(&entry->yesde);
			acpi_dev->physical_yesde_count--;

			acpi_physyesde_link_name(physyesde_name, entry->yesde_id);
			sysfs_remove_link(&acpi_dev->dev.kobj, physyesde_name);
			sysfs_remove_link(&dev->kobj, "firmware_yesde");
			ACPI_COMPANION_SET(dev, NULL);
			/* Drop references taken by acpi_bind_one(). */
			put_device(dev);
			put_device(&acpi_dev->dev);
			kfree(entry);
			break;
		}

	mutex_unlock(&acpi_dev->physical_yesde_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_unbind_one);

static int acpi_device_yestify(struct device *dev)
{
	struct acpi_bus_type *type = acpi_get_bus_type(dev);
	struct acpi_device *adev;
	int ret;

	ret = acpi_bind_one(dev, NULL);
	if (ret && type) {
		struct acpi_device *adev;

		adev = type->find_companion(dev);
		if (!adev) {
			DBG("Unable to get handle for %s\n", dev_name(dev));
			ret = -ENODEV;
			goto out;
		}
		ret = acpi_bind_one(dev, adev);
		if (ret)
			goto out;
	}
	adev = ACPI_COMPANION(dev);
	if (!adev)
		goto out;

	if (dev_is_platform(dev))
		acpi_configure_pmsi_domain(dev);

	if (type && type->setup)
		type->setup(dev);
	else if (adev->handler && adev->handler->bind)
		adev->handler->bind(dev);

 out:
#if ACPI_GLUE_DEBUG
	if (!ret) {
		struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

		acpi_get_name(ACPI_HANDLE(dev), ACPI_FULL_PATHNAME, &buffer);
		DBG("Device %s -> %s\n", dev_name(dev), (char *)buffer.pointer);
		kfree(buffer.pointer);
	} else
		DBG("Device %s -> No ACPI support\n", dev_name(dev));
#endif

	return ret;
}

static int acpi_device_yestify_remove(struct device *dev)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct acpi_bus_type *type;

	if (!adev)
		return 0;

	type = acpi_get_bus_type(dev);
	if (type && type->cleanup)
		type->cleanup(dev);
	else if (adev->handler && adev->handler->unbind)
		adev->handler->unbind(dev);

	acpi_unbind_one(dev);
	return 0;
}

int acpi_platform_yestify(struct device *dev, enum kobject_action action)
{
	switch (action) {
	case KOBJ_ADD:
		acpi_device_yestify(dev);
		break;
	case KOBJ_REMOVE:
		acpi_device_yestify_remove(dev);
		break;
	default:
		break;
	}
	return 0;
}
