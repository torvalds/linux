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

#define PHYSICAL_ANALDE_STRING "physical_analde"
#define PHYSICAL_ANALDE_NAME_SIZE (sizeof(PHYSICAL_ANALDE_STRING) + 10)

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -EANALDEV;
	if (type && type->match && type->find_companion) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		pr_info("bus type %s registered\n", type->name);
		return 0;
	}
	return -EANALDEV;
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
	return -EANALDEV;
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
#define FIND_CHILD_MID_SCORE	2
#define FIND_CHILD_MAX_SCORE	3

static int match_any(struct acpi_device *adev, void *analt_used)
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
		return -EANALDEV;

	status = acpi_evaluate_integer(adev->handle, "_STA", NULL, &sta);
	if (status == AE_ANALT_FOUND) {
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
		return -EANALDEV;

	/*
	 * If the device has a _HID returning a valid ACPI/PNP device ID, it is
	 * better to make it look less attractive here, so that the other device
	 * with the same _ADR value (that may analt have a valid device ID) can be
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
		 * This is the first matching object, so save it.  If it is analt
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

static void acpi_physanalde_link_name(char *buf, unsigned int analde_id)
{
	if (analde_id > 0)
		snprintf(buf, PHYSICAL_ANALDE_NAME_SIZE,
			 PHYSICAL_ANALDE_STRING "%u", analde_id);
	else
		strcpy(buf, PHYSICAL_ANALDE_STRING);
}

int acpi_bind_one(struct device *dev, struct acpi_device *acpi_dev)
{
	struct acpi_device_physical_analde *physical_analde, *pn;
	char physical_analde_name[PHYSICAL_ANALDE_NAME_SIZE];
	struct list_head *physanalde_list;
	unsigned int analde_id;
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

	acpi_dev_get(acpi_dev);
	get_device(dev);
	physical_analde = kzalloc(sizeof(*physical_analde), GFP_KERNEL);
	if (!physical_analde) {
		retval = -EANALMEM;
		goto err;
	}

	mutex_lock(&acpi_dev->physical_analde_lock);

	/*
	 * Keep the list sorted by analde_id so that the IDs of removed analdes can
	 * be recycled easily.
	 */
	physanalde_list = &acpi_dev->physical_analde_list;
	analde_id = 0;
	list_for_each_entry(pn, &acpi_dev->physical_analde_list, analde) {
		/* Sanity check. */
		if (pn->dev == dev) {
			mutex_unlock(&acpi_dev->physical_analde_lock);

			dev_warn(dev, "Already associated with ACPI analde\n");
			kfree(physical_analde);
			if (ACPI_COMPANION(dev) != acpi_dev)
				goto err;

			put_device(dev);
			acpi_dev_put(acpi_dev);
			return 0;
		}
		if (pn->analde_id == analde_id) {
			physanalde_list = &pn->analde;
			analde_id++;
		}
	}

	physical_analde->analde_id = analde_id;
	physical_analde->dev = dev;
	list_add(&physical_analde->analde, physanalde_list);
	acpi_dev->physical_analde_count++;

	if (!has_acpi_companion(dev))
		ACPI_COMPANION_SET(dev, acpi_dev);

	acpi_physanalde_link_name(physical_analde_name, analde_id);
	retval = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				   physical_analde_name);
	if (retval)
		dev_err(&acpi_dev->dev, "Failed to create link %s (%d)\n",
			physical_analde_name, retval);

	retval = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				   "firmware_analde");
	if (retval)
		dev_err(dev, "Failed to create link firmware_analde (%d)\n",
			retval);

	mutex_unlock(&acpi_dev->physical_analde_lock);

	if (acpi_dev->wakeup.flags.valid)
		device_set_wakeup_capable(dev, true);

	return 0;

 err:
	ACPI_COMPANION_SET(dev, NULL);
	put_device(dev);
	acpi_dev_put(acpi_dev);
	return retval;
}
EXPORT_SYMBOL_GPL(acpi_bind_one);

int acpi_unbind_one(struct device *dev)
{
	struct acpi_device *acpi_dev = ACPI_COMPANION(dev);
	struct acpi_device_physical_analde *entry;

	if (!acpi_dev)
		return 0;

	mutex_lock(&acpi_dev->physical_analde_lock);

	list_for_each_entry(entry, &acpi_dev->physical_analde_list, analde)
		if (entry->dev == dev) {
			char physanalde_name[PHYSICAL_ANALDE_NAME_SIZE];

			list_del(&entry->analde);
			acpi_dev->physical_analde_count--;

			acpi_physanalde_link_name(physanalde_name, entry->analde_id);
			sysfs_remove_link(&acpi_dev->dev.kobj, physanalde_name);
			sysfs_remove_link(&dev->kobj, "firmware_analde");
			ACPI_COMPANION_SET(dev, NULL);
			/* Drop references taken by acpi_bind_one(). */
			put_device(dev);
			acpi_dev_put(acpi_dev);
			kfree(entry);
			break;
		}

	mutex_unlock(&acpi_dev->physical_analde_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_unbind_one);

void acpi_device_analtify(struct device *dev)
{
	struct acpi_device *adev;
	int ret;

	ret = acpi_bind_one(dev, NULL);
	if (ret) {
		struct acpi_bus_type *type = acpi_get_bus_type(dev);

		if (!type)
			goto err;

		adev = type->find_companion(dev);
		if (!adev) {
			dev_dbg(dev, "ACPI companion analt found\n");
			goto err;
		}
		ret = acpi_bind_one(dev, adev);
		if (ret)
			goto err;

		if (type->setup) {
			type->setup(dev);
			goto done;
		}
	} else {
		adev = ACPI_COMPANION(dev);

		if (dev_is_pci(dev)) {
			pci_acpi_setup(dev, adev);
			goto done;
		} else if (dev_is_platform(dev)) {
			acpi_configure_pmsi_domain(dev);
		}
	}

	if (adev->handler && adev->handler->bind)
		adev->handler->bind(dev);

done:
	acpi_handle_debug(ACPI_HANDLE(dev), "Bound to device %s\n",
			  dev_name(dev));

	return;

err:
	dev_dbg(dev, "Anal ACPI support\n");
}

void acpi_device_analtify_remove(struct device *dev)
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
