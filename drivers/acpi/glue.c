/*
 * Link physical devices with ACPI devices support
 *
 * Copyright (c) 2005 David Shaohua Li <shaohua.li@intel.com>
 * Copyright (c) 2005 Intel Corp.
 *
 * This file is released under the GPLv2.
 */
#include <linux/init.h>
#include <linux/list.h>
#include <linux/device.h>
#include <linux/rwsem.h>
#include <linux/acpi.h>

#define ACPI_GLUE_DEBUG	0
#if ACPI_GLUE_DEBUG
#define DBG(x...) printk(PREFIX x)
#else
#define DBG(x...)
#endif
static LIST_HEAD(bus_type_list);
static DECLARE_RWSEM(bus_type_sem);

int register_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return -ENODEV;
	if (type && type->bus && type->find_device) {
		down_write(&bus_type_sem);
		list_add_tail(&type->list, &bus_type_list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "bus type %s registered\n",
		       type->bus->name);
		return 0;
	}
	return -ENODEV;
}

EXPORT_SYMBOL(register_acpi_bus_type);

int unregister_acpi_bus_type(struct acpi_bus_type *type)
{
	if (acpi_disabled)
		return 0;
	if (type) {
		down_write(&bus_type_sem);
		list_del_init(&type->list);
		up_write(&bus_type_sem);
		printk(KERN_INFO PREFIX "ACPI bus type %s unregistered\n",
		       type->bus->name);
		return 0;
	}
	return -ENODEV;
}

EXPORT_SYMBOL(unregister_acpi_bus_type);

static struct acpi_bus_type *acpi_get_bus_type(struct bus_type *type)
{
	struct acpi_bus_type *tmp, *ret = NULL;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->bus == type) {
			ret = tmp;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

static int acpi_find_bridge_device(struct device *dev, acpi_handle * handle)
{
	struct acpi_bus_type *tmp;
	int ret = -ENODEV;

	down_read(&bus_type_sem);
	list_for_each_entry(tmp, &bus_type_list, list) {
		if (tmp->find_bridge && !tmp->find_bridge(dev, handle)) {
			ret = 0;
			break;
		}
	}
	up_read(&bus_type_sem);
	return ret;
}

/* Get PCI root bridge's handle from its segment and bus number */
struct acpi_find_pci_root {
	unsigned int seg;
	unsigned int bus;
	acpi_handle handle;
};

static acpi_status
do_root_bridge_busnr_callback(struct acpi_resource *resource, void *data)
{
	unsigned long *busnr = (unsigned long *)data;
	struct acpi_resource_address64 address;

	if (resource->type != ACPI_RESOURCE_TYPE_ADDRESS16 &&
	    resource->type != ACPI_RESOURCE_TYPE_ADDRESS32 &&
	    resource->type != ACPI_RESOURCE_TYPE_ADDRESS64)
		return AE_OK;

	acpi_resource_to_address64(resource, &address);
	if ((address.address_length > 0) &&
	    (address.resource_type == ACPI_BUS_NUMBER_RANGE))
		*busnr = address.minimum;

	return AE_OK;
}

static int get_root_bridge_busnr(acpi_handle handle)
{
	acpi_status status;
	unsigned long bus, bbn;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	status = acpi_evaluate_integer(handle, METHOD_NAME__BBN, NULL,
				       &bbn);
	if (status == AE_NOT_FOUND) {
		/* Assume bus = 0 */
		printk(KERN_INFO PREFIX
		       "Assume root bridge [%s] bus is 0\n",
		       (char *)buffer.pointer);
		status = AE_OK;
		bbn = 0;
	}
	if (ACPI_FAILURE(status)) {
		bbn = -ENODEV;
		goto exit;
	}
	if (bbn > 0)
		goto exit;

	/* _BBN in some systems return 0 for all root bridges */
	bus = -1;
	status = acpi_walk_resources(handle, METHOD_NAME__CRS,
				     do_root_bridge_busnr_callback, &bus);
	/* If _CRS failed, we just use _BBN */
	if (ACPI_FAILURE(status) || (bus == -1))
		goto exit;
	/* We select _CRS */
	if (bbn != bus) {
		printk(KERN_INFO PREFIX
		       "_BBN and _CRS returns different value for %s. Select _CRS\n",
		       (char *)buffer.pointer);
		bbn = bus;
	}
      exit:
	kfree(buffer.pointer);
	return (int)bbn;
}

static acpi_status
find_pci_rootbridge(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	struct acpi_find_pci_root *find = (struct acpi_find_pci_root *)context;
	unsigned long seg, bus;
	acpi_status status;
	int tmp;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);

	status = acpi_evaluate_integer(handle, METHOD_NAME__SEG, NULL, &seg);
	if (status == AE_NOT_FOUND) {
		/* Assume seg = 0 */
		status = AE_OK;
		seg = 0;
	}
	if (ACPI_FAILURE(status)) {
		status = AE_CTRL_DEPTH;
		goto exit;
	}

	tmp = get_root_bridge_busnr(handle);
	if (tmp < 0) {
		printk(KERN_ERR PREFIX
		       "Find root bridge failed for %s\n",
		       (char *)buffer.pointer);
		status = AE_CTRL_DEPTH;
		goto exit;
	}
	bus = tmp;

	if (seg == find->seg && bus == find->bus)
	{
		find->handle = handle;
		status = AE_CTRL_TERMINATE;
	}
	else
		status = AE_OK;
      exit:
	kfree(buffer.pointer);
	return status;
}

acpi_handle acpi_get_pci_rootbridge_handle(unsigned int seg, unsigned int bus)
{
	struct acpi_find_pci_root find = { seg, bus, NULL };

	acpi_get_devices(PCI_ROOT_HID_STRING, find_pci_rootbridge, &find, NULL);
	return find.handle;
}
EXPORT_SYMBOL_GPL(acpi_get_pci_rootbridge_handle);

/* Get device's handler per its address under its parent */
struct acpi_find_child {
	acpi_handle handle;
	acpi_integer address;
};

static acpi_status
do_acpi_find_child(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	acpi_status status;
	struct acpi_device_info *info;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_find_child *find = (struct acpi_find_child *)context;

	status = acpi_get_object_info(handle, &buffer);
	if (ACPI_SUCCESS(status)) {
		info = buffer.pointer;
		if (info->address == find->address)
			find->handle = handle;
		kfree(buffer.pointer);
	}
	return AE_OK;
}

acpi_handle acpi_get_child(acpi_handle parent, acpi_integer address)
{
	struct acpi_find_child find = { NULL, address };

	if (!parent)
		return NULL;
	acpi_walk_namespace(ACPI_TYPE_DEVICE, parent,
			    1, do_acpi_find_child, &find, NULL);
	return find.handle;
}

EXPORT_SYMBOL(acpi_get_child);

/* Link ACPI devices with physical devices */
static void acpi_glue_data_handler(acpi_handle handle,
				   u32 function, void *context)
{
	/* we provide an empty handler */
}

/* Note: a success call will increase reference count by one */
struct device *acpi_get_physical_device(acpi_handle handle)
{
	acpi_status status;
	struct device *dev;

	status = acpi_get_data(handle, acpi_glue_data_handler, (void **)&dev);
	if (ACPI_SUCCESS(status))
		return get_device(dev);
	return NULL;
}

EXPORT_SYMBOL(acpi_get_physical_device);

static int acpi_bind_one(struct device *dev, acpi_handle handle)
{
	acpi_status status;

	if (dev->firmware_data) {
		printk(KERN_WARNING PREFIX
		       "Drivers changed 'firmware_data' for %s\n", dev->bus_id);
		return -EINVAL;
	}
	get_device(dev);
	status = acpi_attach_data(handle, acpi_glue_data_handler, dev);
	if (ACPI_FAILURE(status)) {
		put_device(dev);
		return -EINVAL;
	}
	dev->firmware_data = handle;

	return 0;
}

static int acpi_unbind_one(struct device *dev)
{
	if (!dev->firmware_data)
		return 0;
	if (dev == acpi_get_physical_device(dev->firmware_data)) {
		/* acpi_get_physical_device increase refcnt by one */
		put_device(dev);
		acpi_detach_data(dev->firmware_data, acpi_glue_data_handler);
		dev->firmware_data = NULL;
		/* acpi_bind_one increase refcnt by one */
		put_device(dev);
	} else {
		printk(KERN_ERR PREFIX
		       "Oops, 'firmware_data' corrupt for %s\n", dev->bus_id);
	}
	return 0;
}

static int acpi_platform_notify(struct device *dev)
{
	struct acpi_bus_type *type;
	acpi_handle handle;
	int ret = -EINVAL;

	if (!dev->bus || !dev->parent) {
		/* bridge devices genernally haven't bus or parent */
		ret = acpi_find_bridge_device(dev, &handle);
		goto end;
	}
	type = acpi_get_bus_type(dev->bus);
	if (!type) {
		DBG("No ACPI bus support for %s\n", dev->bus_id);
		ret = -EINVAL;
		goto end;
	}
	if ((ret = type->find_device(dev, &handle)) != 0)
		DBG("Can't get handler for %s\n", dev->bus_id);
      end:
	if (!ret)
		acpi_bind_one(dev, handle);

#if ACPI_GLUE_DEBUG
	if (!ret) {
		struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

		acpi_get_name(dev->firmware_data, ACPI_FULL_PATHNAME, &buffer);
		DBG("Device %s -> %s\n", dev->bus_id, (char *)buffer.pointer);
		kfree(buffer.pointer);
	} else
		DBG("Device %s -> No ACPI support\n", dev->bus_id);
#endif

	return ret;
}

static int acpi_platform_notify_remove(struct device *dev)
{
	acpi_unbind_one(dev);
	return 0;
}

static int __init init_acpi_device_notify(void)
{
	if (acpi_disabled)
		return 0;
	if (platform_notify || platform_notify_remove) {
		printk(KERN_ERR PREFIX "Can't use platform_notify\n");
		return 0;
	}
	platform_notify = acpi_platform_notify;
	platform_notify_remove = acpi_platform_notify_remove;
	return 0;
}

arch_initcall(init_acpi_device_notify);
