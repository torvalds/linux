/*
 * Link physical devices with ACPI devices support
 *
 * Copyright (c) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2005 Intel Corp.
 *
 * This file is released under the GPLv2.
 */
#include <linux/export.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>

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

#define PHYSICAL_NODE_STRING "physical_node"

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -ENODEV;
	if (type && type->match && type->find_device) {
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

static acpi_status do_acpi_find_child(acpi_handle handle, u32 lvl_not_used,
				      void *addr_p, void **ret_p)
{
	unsigned long long addr, sta;
	acpi_status status;

	status = acpi_evaluate_integer(handle, METHOD_NAME__ADR, NULL, &addr);
	if (ACPI_SUCCESS(status) && addr == *((u64 *)addr_p)) {
		*ret_p = handle;
		status = acpi_bus_get_status_handle(handle, &sta);
		if (ACPI_SUCCESS(status) && (sta & ACPI_STA_DEVICE_ENABLED))
			return AE_CTRL_TERMINATE;
	}
	return AE_OK;
}

acpi_handle acpi_get_child(acpi_handle parent, u64 address)
{
	void *ret = NULL;

	if (!parent)
		return NULL;

	acpi_walk_namespace(ACPI_TYPE_DEVICE, parent, 1, NULL,
			    do_acpi_find_child, &address, &ret);
	return (acpi_handle)ret;
}
EXPORT_SYMBOL(acpi_get_child);

int acpi_bind_one(struct device *dev, acpi_handle handle)
{
	struct acpi_device *acpi_dev;
	acpi_status status;
	struct acpi_device_physical_node *physical_node, *pn;
	char physical_node_name[sizeof(PHYSICAL_NODE_STRING) + 2];
	int retval = -EINVAL;

	if (ACPI_HANDLE(dev)) {
		if (handle) {
			dev_warn(dev, "ACPI handle is already set\n");
			return -EINVAL;
		} else {
			handle = ACPI_HANDLE(dev);
		}
	}
	if (!handle)
		return -EINVAL;

	get_device(dev);
	status = acpi_bus_get_device(handle, &acpi_dev);
	if (ACPI_FAILURE(status))
		goto err;

	physical_node = kzalloc(sizeof(*physical_node), GFP_KERNEL);
	if (!physical_node) {
		retval = -ENOMEM;
		goto err;
	}

	mutex_lock(&acpi_dev->physical_node_lock);

	/* Sanity check. */
	list_for_each_entry(pn, &acpi_dev->physical_node_list, node)
		if (pn->dev == dev) {
			dev_warn(dev, "Already associated with ACPI node\n");
			goto err_free;
		}

	/* allocate physical node id according to physical_node_id_bitmap */
	physical_node->node_id =
		find_first_zero_bit(acpi_dev->physical_node_id_bitmap,
		ACPI_MAX_PHYSICAL_NODE);
	if (physical_node->node_id >= ACPI_MAX_PHYSICAL_NODE) {
		retval = -ENOSPC;
		goto err_free;
	}

	set_bit(physical_node->node_id, acpi_dev->physical_node_id_bitmap);
	physical_node->dev = dev;
	list_add_tail(&physical_node->node, &acpi_dev->physical_node_list);
	acpi_dev->physical_node_count++;

	mutex_unlock(&acpi_dev->physical_node_lock);

	if (!ACPI_HANDLE(dev))
		ACPI_HANDLE_SET(dev, acpi_dev->handle);

	if (!physical_node->node_id)
		strcpy(physical_node_name, PHYSICAL_NODE_STRING);
	else
		sprintf(physical_node_name,
			"physical_node%d", physical_node->node_id);
	retval = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
			physical_node_name);
	retval = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
		"firmware_node");

	if (acpi_dev->wakeup.flags.valid)
		device_set_wakeup_capable(dev, true);

	return 0;

 err:
	ACPI_HANDLE_SET(dev, NULL);
	put_device(dev);
	return retval;

 err_free:
	mutex_unlock(&acpi_dev->physical_node_lock);
	kfree(physical_node);
	goto err;
}
EXPORT_SYMBOL_GPL(acpi_bind_one);

int acpi_unbind_one(struct device *dev)
{
	struct acpi_device_physical_node *entry;
	struct acpi_device *acpi_dev;
	acpi_status status;
	struct list_head *node, *next;

	if (!ACPI_HANDLE(dev))
		return 0;

	status = acpi_bus_get_device(ACPI_HANDLE(dev), &acpi_dev);
	if (ACPI_FAILURE(status))
		goto err;

	mutex_lock(&acpi_dev->physical_node_lock);
	list_for_each_safe(node, next, &acpi_dev->physical_node_list) {
		char physical_node_name[sizeof(PHYSICAL_NODE_STRING) + 2];

		entry = list_entry(node, struct acpi_device_physical_node,
			node);
		if (entry->dev != dev)
			continue;

		list_del(node);
		clear_bit(entry->node_id, acpi_dev->physical_node_id_bitmap);

		acpi_dev->physical_node_count--;

		if (!entry->node_id)
			strcpy(physical_node_name, PHYSICAL_NODE_STRING);
		else
			sprintf(physical_node_name,
				"physical_node%d", entry->node_id);

		sysfs_remove_link(&acpi_dev->dev.kobj, physical_node_name);
		sysfs_remove_link(&dev->kobj, "firmware_node");
		ACPI_HANDLE_SET(dev, NULL);
		/* acpi_bind_one increase refcnt by one */
		put_device(dev);
		kfree(entry);
	}
	mutex_unlock(&acpi_dev->physical_node_lock);

	return 0;

err:
	dev_err(dev, "Oops, 'acpi_handle' corrupt\n");
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_unbind_one);

static int acpi_platform_notify(struct device *dev)
{
	struct acpi_bus_type *type = acpi_get_bus_type(dev);
	acpi_handle handle;
	int ret;

	ret = acpi_bind_one(dev, NULL);
	if (ret && type) {
		ret = type->find_device(dev, &handle);
		if (ret) {
			DBG("Unable to get handle for %s\n", dev_name(dev));
			goto out;
		}
		ret = acpi_bind_one(dev, handle);
		if (ret)
			goto out;
	}

	if (type && type->setup)
		type->setup(dev);

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

static int acpi_platform_notify_remove(struct device *dev)
{
	struct acpi_bus_type *type;

	type = acpi_get_bus_type(dev);
	if (type && type->cleanup)
		type->cleanup(dev);

	acpi_unbind_one(dev);
	return 0;
}

int __init init_acpi_device_notify(void)
{
	if (platform_notify || platform_notify_remove) {
		printk(KERN_ERR PREFIX "Can't use platform_notify\n");
		return 0;
	}
	platform_notify = acpi_platform_notify;
	platform_notify_remove = acpi_platform_notify_remove;
	return 0;
}
