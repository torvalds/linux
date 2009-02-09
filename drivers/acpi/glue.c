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
#define DBG(x...) do { } while(0)
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
	struct acpi_find_child *find = context;

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

/* ToDo: When a PCI bridge is found, return the PCI device behind the bridge
 *       This should work in general, but did not on a Lenovo T61 for the
 *	 graphics card. But this must be fixed when the PCI device is
 *       bound and the kernel device struct is attached to the acpi device
 * Note: A success call will increase reference count by one
 *       Do call put_device(dev) on the returned device then
 */
struct device *acpi_get_physical_pci_device(acpi_handle handle)
{
	struct device *dev;
	long long device_id;
	acpi_status status;

	status =
		acpi_evaluate_integer(handle, "_ADR", NULL, &device_id);

	if (ACPI_FAILURE(status))
		return NULL;

	/* We need to attempt to determine whether the _ADR refers to a
	   PCI device or not. There's no terribly good way to do this,
	   so the best we can hope for is to assume that there'll never
	   be a device in the host bridge */
	if (device_id >= 0x10000) {
		/* It looks like a PCI device. Does it exist? */
		dev = acpi_get_physical_device(handle);
	} else {
		/* It doesn't look like a PCI device. Does its parent
		   exist? */
		acpi_handle phandle;
		if (acpi_get_parent(handle, &phandle))
			return NULL;
		dev = acpi_get_physical_device(phandle);
	}
	if (!dev)
		return NULL;
	return dev;
}
EXPORT_SYMBOL(acpi_get_physical_pci_device);

static int acpi_bind_one(struct device *dev, acpi_handle handle)
{
	struct acpi_device *acpi_dev;
	acpi_status status;

	if (dev->archdata.acpi_handle) {
		dev_warn(dev, "Drivers changed 'acpi_handle'\n");
		return -EINVAL;
	}
	get_device(dev);
	status = acpi_attach_data(handle, acpi_glue_data_handler, dev);
	if (ACPI_FAILURE(status)) {
		put_device(dev);
		return -EINVAL;
	}
	dev->archdata.acpi_handle = handle;

	status = acpi_bus_get_device(handle, &acpi_dev);
	if (!ACPI_FAILURE(status)) {
		int ret;

		ret = sysfs_create_link(&dev->kobj, &acpi_dev->dev.kobj,
				"firmware_node");
		ret = sysfs_create_link(&acpi_dev->dev.kobj, &dev->kobj,
				"physical_node");
		if (acpi_dev->wakeup.flags.valid) {
			device_set_wakeup_capable(dev, true);
			device_set_wakeup_enable(dev,
						acpi_dev->wakeup.state.enabled);
		}
	}

	return 0;
}

static int acpi_unbind_one(struct device *dev)
{
	if (!dev->archdata.acpi_handle)
		return 0;
	if (dev == acpi_get_physical_device(dev->archdata.acpi_handle)) {
		struct acpi_device *acpi_dev;

		/* acpi_get_physical_device increase refcnt by one */
		put_device(dev);

		if (!acpi_bus_get_device(dev->archdata.acpi_handle,
					&acpi_dev)) {
			sysfs_remove_link(&dev->kobj, "firmware_node");
			sysfs_remove_link(&acpi_dev->dev.kobj, "physical_node");
		}

		acpi_detach_data(dev->archdata.acpi_handle,
				 acpi_glue_data_handler);
		dev->archdata.acpi_handle = NULL;
		/* acpi_bind_one increase refcnt by one */
		put_device(dev);
	} else {
		dev_err(dev, "Oops, 'acpi_handle' corrupt\n");
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
		DBG("No ACPI bus support for %s\n", dev_name(dev));
		ret = -EINVAL;
		goto end;
	}
	if ((ret = type->find_device(dev, &handle)) != 0)
		DBG("Can't get handler for %s\n", dev_name(dev));
      end:
	if (!ret)
		acpi_bind_one(dev, handle);

#if ACPI_GLUE_DEBUG
	if (!ret) {
		struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

		acpi_get_name(dev->archdata.acpi_handle,
			      ACPI_FULL_PATHNAME, &buffer);
		DBG("Device %s -> %s\n", dev_name(dev), (char *)buffer.pointer);
		kfree(buffer.pointer);
	} else
		DBG("Device %s -> No ACPI support\n", dev_name(dev));
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
