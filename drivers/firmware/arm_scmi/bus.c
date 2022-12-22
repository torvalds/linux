// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message Protocol bus layer
 *
 * Copyright (C) 2018-2021 ARM Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "common.h"

BLOCKING_NOTIFIER_HEAD(scmi_requested_devices_nh);
EXPORT_SYMBOL_GPL(scmi_requested_devices_nh);

static DEFINE_IDA(scmi_bus_id);

static DEFINE_IDR(scmi_requested_devices);
/* Protect access to scmi_requested_devices */
static DEFINE_MUTEX(scmi_requested_devices_mtx);

struct scmi_requested_dev {
	const struct scmi_device_id *id_table;
	struct list_head node;
};

/**
 * scmi_protocol_device_request  - Helper to request a device
 *
 * @id_table: A protocol/name pair descriptor for the device to be created.
 *
 * This helper let an SCMI driver request specific devices identified by the
 * @id_table to be created for each active SCMI instance.
 *
 * The requested device name MUST NOT be already existent for any protocol;
 * at first the freshly requested @id_table is annotated in the IDR table
 * @scmi_requested_devices and then the requested device is advertised to any
 * registered party via the @scmi_requested_devices_nh notification chain.
 *
 * Return: 0 on Success
 */
static int scmi_protocol_device_request(const struct scmi_device_id *id_table)
{
	int ret = 0;
	unsigned int id = 0;
	struct list_head *head, *phead = NULL;
	struct scmi_requested_dev *rdev;

	pr_debug("Requesting SCMI device (%s) for protocol %x\n",
		 id_table->name, id_table->protocol_id);

	/*
	 * Search for the matching protocol rdev list and then search
	 * of any existent equally named device...fails if any duplicate found.
	 */
	mutex_lock(&scmi_requested_devices_mtx);
	idr_for_each_entry(&scmi_requested_devices, head, id) {
		if (!phead) {
			/* A list found registered in the IDR is never empty */
			rdev = list_first_entry(head, struct scmi_requested_dev,
						node);
			if (rdev->id_table->protocol_id ==
			    id_table->protocol_id)
				phead = head;
		}
		list_for_each_entry(rdev, head, node) {
			if (!strcmp(rdev->id_table->name, id_table->name)) {
				pr_err("Ignoring duplicate request [%d] %s\n",
				       rdev->id_table->protocol_id,
				       rdev->id_table->name);
				ret = -EINVAL;
				goto out;
			}
		}
	}

	/*
	 * No duplicate found for requested id_table, so let's create a new
	 * requested device entry for this new valid request.
	 */
	rdev = kzalloc(sizeof(*rdev), GFP_KERNEL);
	if (!rdev) {
		ret = -ENOMEM;
		goto out;
	}
	rdev->id_table = id_table;

	/*
	 * Append the new requested device table descriptor to the head of the
	 * related protocol list, eventually creating such head if not already
	 * there.
	 */
	if (!phead) {
		phead = kzalloc(sizeof(*phead), GFP_KERNEL);
		if (!phead) {
			kfree(rdev);
			ret = -ENOMEM;
			goto out;
		}
		INIT_LIST_HEAD(phead);

		ret = idr_alloc(&scmi_requested_devices, (void *)phead,
				id_table->protocol_id,
				id_table->protocol_id + 1, GFP_KERNEL);
		if (ret != id_table->protocol_id) {
			pr_err("Failed to save SCMI device - ret:%d\n", ret);
			kfree(rdev);
			kfree(phead);
			ret = -EINVAL;
			goto out;
		}
		ret = 0;
	}
	list_add(&rdev->node, phead);

out:
	mutex_unlock(&scmi_requested_devices_mtx);

	if (!ret)
		blocking_notifier_call_chain(&scmi_requested_devices_nh,
					     SCMI_BUS_NOTIFY_DEVICE_REQUEST,
					     (void *)rdev->id_table);

	return ret;
}

/**
 * scmi_protocol_device_unrequest  - Helper to unrequest a device
 *
 * @id_table: A protocol/name pair descriptor for the device to be unrequested.
 *
 * The unrequested device, described by the provided id_table, is at first
 * removed from the IDR @scmi_requested_devices and then the removal is
 * advertised to any registered party via the @scmi_requested_devices_nh
 * notification chain.
 */
static void scmi_protocol_device_unrequest(const struct scmi_device_id *id_table)
{
	struct list_head *phead;

	pr_debug("Unrequesting SCMI device (%s) for protocol %x\n",
		 id_table->name, id_table->protocol_id);

	mutex_lock(&scmi_requested_devices_mtx);
	phead = idr_find(&scmi_requested_devices, id_table->protocol_id);
	if (phead) {
		struct scmi_requested_dev *victim, *tmp;

		list_for_each_entry_safe(victim, tmp, phead, node) {
			if (!strcmp(victim->id_table->name, id_table->name)) {
				list_del(&victim->node);

				mutex_unlock(&scmi_requested_devices_mtx);
				blocking_notifier_call_chain(&scmi_requested_devices_nh,
							     SCMI_BUS_NOTIFY_DEVICE_UNREQUEST,
							     (void *)victim->id_table);
				kfree(victim);
				mutex_lock(&scmi_requested_devices_mtx);
				break;
			}
		}

		if (list_empty(phead)) {
			idr_remove(&scmi_requested_devices,
				   id_table->protocol_id);
			kfree(phead);
		}
	}
	mutex_unlock(&scmi_requested_devices_mtx);
}

static const struct scmi_device_id *
scmi_dev_match_id(struct scmi_device *scmi_dev, struct scmi_driver *scmi_drv)
{
	const struct scmi_device_id *id = scmi_drv->id_table;

	if (!id)
		return NULL;

	for (; id->protocol_id; id++)
		if (id->protocol_id == scmi_dev->protocol_id) {
			if (!id->name)
				return id;
			else if (!strcmp(id->name, scmi_dev->name))
				return id;
		}

	return NULL;
}

static int scmi_dev_match(struct device *dev, struct device_driver *drv)
{
	struct scmi_driver *scmi_drv = to_scmi_driver(drv);
	struct scmi_device *scmi_dev = to_scmi_dev(dev);
	const struct scmi_device_id *id;

	id = scmi_dev_match_id(scmi_dev, scmi_drv);
	if (id)
		return 1;

	return 0;
}

static int scmi_match_by_id_table(struct device *dev, void *data)
{
	struct scmi_device *sdev = to_scmi_dev(dev);
	struct scmi_device_id *id_table = data;

	return sdev->protocol_id == id_table->protocol_id &&
		!strcmp(sdev->name, id_table->name);
}

struct scmi_device *scmi_child_dev_find(struct device *parent,
					int prot_id, const char *name)
{
	struct scmi_device_id id_table;
	struct device *dev;

	id_table.protocol_id = prot_id;
	id_table.name = name;

	dev = device_find_child(parent, &id_table, scmi_match_by_id_table);
	if (!dev)
		return NULL;

	return to_scmi_dev(dev);
}

static int scmi_dev_probe(struct device *dev)
{
	struct scmi_driver *scmi_drv = to_scmi_driver(dev->driver);
	struct scmi_device *scmi_dev = to_scmi_dev(dev);

	if (!scmi_dev->handle)
		return -EPROBE_DEFER;

	return scmi_drv->probe(scmi_dev);
}

static void scmi_dev_remove(struct device *dev)
{
	struct scmi_driver *scmi_drv = to_scmi_driver(dev->driver);
	struct scmi_device *scmi_dev = to_scmi_dev(dev);

	if (scmi_drv->remove)
		scmi_drv->remove(scmi_dev);
}

struct bus_type scmi_bus_type = {
	.name =	"scmi_protocol",
	.match = scmi_dev_match,
	.probe = scmi_dev_probe,
	.remove = scmi_dev_remove,
};
EXPORT_SYMBOL_GPL(scmi_bus_type);

int scmi_driver_register(struct scmi_driver *driver, struct module *owner,
			 const char *mod_name)
{
	int retval;

	if (!driver->probe)
		return -EINVAL;

	retval = scmi_protocol_device_request(driver->id_table);
	if (retval)
		return retval;

	driver->driver.bus = &scmi_bus_type;
	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	retval = driver_register(&driver->driver);
	if (!retval)
		pr_debug("Registered new scmi driver %s\n", driver->name);

	return retval;
}
EXPORT_SYMBOL_GPL(scmi_driver_register);

void scmi_driver_unregister(struct scmi_driver *driver)
{
	driver_unregister(&driver->driver);
	scmi_protocol_device_unrequest(driver->id_table);
}
EXPORT_SYMBOL_GPL(scmi_driver_unregister);

static void scmi_device_release(struct device *dev)
{
	kfree(to_scmi_dev(dev));
}

struct scmi_device *
scmi_device_create(struct device_node *np, struct device *parent, int protocol,
		   const char *name)
{
	int id, retval;
	struct scmi_device *scmi_dev;

	scmi_dev = kzalloc(sizeof(*scmi_dev), GFP_KERNEL);
	if (!scmi_dev)
		return NULL;

	scmi_dev->name = kstrdup_const(name ?: "unknown", GFP_KERNEL);
	if (!scmi_dev->name) {
		kfree(scmi_dev);
		return NULL;
	}

	id = ida_alloc_min(&scmi_bus_id, 1, GFP_KERNEL);
	if (id < 0) {
		kfree_const(scmi_dev->name);
		kfree(scmi_dev);
		return NULL;
	}

	scmi_dev->id = id;
	scmi_dev->protocol_id = protocol;
	scmi_dev->dev.parent = parent;
	scmi_dev->dev.of_node = np;
	scmi_dev->dev.bus = &scmi_bus_type;
	scmi_dev->dev.release = scmi_device_release;
	dev_set_name(&scmi_dev->dev, "scmi_dev.%d", id);

	retval = device_register(&scmi_dev->dev);
	if (retval)
		goto put_dev;

	return scmi_dev;
put_dev:
	kfree_const(scmi_dev->name);
	put_device(&scmi_dev->dev);
	ida_free(&scmi_bus_id, id);
	return NULL;
}

void scmi_device_destroy(struct scmi_device *scmi_dev)
{
	kfree_const(scmi_dev->name);
	ida_free(&scmi_bus_id, scmi_dev->id);
	device_unregister(&scmi_dev->dev);
}

static int __scmi_devices_unregister(struct device *dev, void *data)
{
	struct scmi_device *scmi_dev = to_scmi_dev(dev);

	scmi_device_destroy(scmi_dev);
	return 0;
}

static void scmi_devices_unregister(void)
{
	bus_for_each_dev(&scmi_bus_type, NULL, NULL, __scmi_devices_unregister);
}

int __init scmi_bus_init(void)
{
	int retval;

	retval = bus_register(&scmi_bus_type);
	if (retval)
		pr_err("scmi protocol bus register failed (%d)\n", retval);

	return retval;
}

void __exit scmi_bus_exit(void)
{
	scmi_devices_unregister();
	bus_unregister(&scmi_bus_type);
	ida_destroy(&scmi_bus_id);
}
