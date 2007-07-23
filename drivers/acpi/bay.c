/*
 *  bay.c - ACPI removable drive bay driver
 *
 *  Copyright (C) 2006 Kristen Carlson Accardi <kristen.c.accardi@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>

ACPI_MODULE_NAME("bay");
MODULE_AUTHOR("Kristen Carlson Accardi");
MODULE_DESCRIPTION("ACPI Removable Drive Bay Driver");
MODULE_LICENSE("GPL");
#define ACPI_BAY_CLASS "bay"
#define ACPI_BAY_COMPONENT	0x10000000
#define _COMPONENT ACPI_BAY_COMPONENT
#define bay_dprintk(h,s) {\
	char prefix[80] = {'\0'};\
	struct acpi_buffer buffer = {sizeof(prefix), prefix};\
	acpi_get_name(h, ACPI_FULL_PATHNAME, &buffer);\
	printk(KERN_DEBUG PREFIX "%s: %s\n", prefix, s); }
static void bay_notify(acpi_handle handle, u32 event, void *data);

struct bay {
	acpi_handle handle;
	char *name;
	struct list_head list;
	struct platform_device *pdev;
};

static LIST_HEAD(drive_bays);


/*****************************************************************************
 *                         Drive Bay functions                               *
 *****************************************************************************/
/**
 * is_ejectable - see if a device is ejectable
 * @handle: acpi handle of the device
 *
 * If an acpi object has a _EJ0 method, then it is ejectable
 */
static int is_ejectable(acpi_handle handle)
{
	acpi_status status;
	acpi_handle tmp;

	status = acpi_get_handle(handle, "_EJ0", &tmp);
	if (ACPI_FAILURE(status))
		return 0;
	return 1;
}

/**
 * bay_present - see if the bay device is present
 * @bay: the drive bay
 *
 * execute the _STA method.
 */
static int bay_present(struct bay *bay)
{
	unsigned long sta;
	acpi_status status;

	if (bay) {
		status = acpi_evaluate_integer(bay->handle, "_STA", NULL, &sta);
		if (ACPI_SUCCESS(status) && sta)
			return 1;
	}
	return 0;
}

/**
 * eject_device - respond to an eject request
 * @handle - the device to eject
 *
 * Call this devices _EJ0 method.
 */
static void eject_device(acpi_handle handle)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;

	bay_dprintk(handle, "Ejecting device");

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = 1;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_EJ0",
					      &arg_list, NULL)))
		pr_debug("Failed to evaluate _EJ0!\n");
}

/*
 * show_present - read method for "present" file in sysfs
 */
static ssize_t show_present(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct bay *bay = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "%d\n", bay_present(bay));

}
DEVICE_ATTR(present, S_IRUGO, show_present, NULL);

/*
 * write_eject - write method for "eject" file in sysfs
 */
static ssize_t write_eject(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct bay *bay = dev_get_drvdata(dev);

	if (!count)
		return -EINVAL;

	eject_device(bay->handle);
	return count;
}
DEVICE_ATTR(eject, S_IWUSR, NULL, write_eject);

/**
 * is_ata - see if a device is an ata device
 * @handle: acpi handle of the device
 *
 * If an acpi object has one of 4 ATA ACPI methods defined,
 * then it is an ATA device
 */
static int is_ata(acpi_handle handle)
{
	acpi_handle tmp;

	if ((ACPI_SUCCESS(acpi_get_handle(handle, "_GTF", &tmp))) ||
	   (ACPI_SUCCESS(acpi_get_handle(handle, "_GTM", &tmp))) ||
	   (ACPI_SUCCESS(acpi_get_handle(handle, "_STM", &tmp))) ||
	   (ACPI_SUCCESS(acpi_get_handle(handle, "_SDD", &tmp))))
		return 1;

	return 0;
}

/**
 * parent_is_ata(acpi_handle handle)
 *
 */
static int parent_is_ata(acpi_handle handle)
{
	acpi_handle phandle;

	if (acpi_get_parent(handle, &phandle))
		return 0;

	return is_ata(phandle);
}

/**
 * is_ejectable_bay - see if a device is an ejectable drive bay
 * @handle: acpi handle of the device
 *
 * If an acpi object is ejectable and has one of the ACPI ATA
 * methods defined, then we can safely call it an ejectable
 * drive bay
 */
static int is_ejectable_bay(acpi_handle handle)
{
	if ((is_ata(handle) || parent_is_ata(handle)) && is_ejectable(handle))
		return 1;
	return 0;
}

/**
 * eject_removable_drive - try to eject this drive
 * @dev : the device structure of the drive
 *
 * If a device is a removable drive that requires an _EJ0 method
 * to be executed in order to safely remove from the system, do
 * it.  ATM - always returns success
 */
int eject_removable_drive(struct device *dev)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(dev);

	if (handle) {
		bay_dprintk(handle, "Got device handle");
		if (is_ejectable_bay(handle))
			eject_device(handle);
	} else {
		printk("No acpi handle for device\n");
	}

	/* should I return an error code? */
	return 0;
}
EXPORT_SYMBOL_GPL(eject_removable_drive);

static int acpi_bay_add_fs(struct bay *bay)
{
	int ret;
	struct device *dev = &bay->pdev->dev;

	ret = device_create_file(dev, &dev_attr_present);
	if (ret)
		goto add_fs_err;
	ret = device_create_file(dev, &dev_attr_eject);
	if (ret) {
		device_remove_file(dev, &dev_attr_present);
		goto add_fs_err;
	}
	return 0;

 add_fs_err:
	bay_dprintk(bay->handle, "Error adding sysfs files\n");
	return ret;
}

static void acpi_bay_remove_fs(struct bay *bay)
{
	struct device *dev = &bay->pdev->dev;

	/* cleanup sysfs */
	device_remove_file(dev, &dev_attr_present);
	device_remove_file(dev, &dev_attr_eject);
}

static int bay_is_dock_device(acpi_handle handle)
{
	acpi_handle parent;

	acpi_get_parent(handle, &parent);

	/* if the device or it's parent is dependent on the
	 * dock, then we are a dock device
	 */
	return (is_dock_device(handle) || is_dock_device(parent));
}

static int bay_add(acpi_handle handle, int id)
{
	acpi_status status;
	struct bay *new_bay;
	struct platform_device *pdev;
	struct acpi_buffer nbuffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_get_name(handle, ACPI_FULL_PATHNAME, &nbuffer);

	bay_dprintk(handle, "Adding notify handler");

	/*
	 * Initialize bay device structure
	 */
	new_bay = kzalloc(sizeof(*new_bay), GFP_ATOMIC);
	INIT_LIST_HEAD(&new_bay->list);
	new_bay->handle = handle;
	new_bay->name = (char *)nbuffer.pointer;

	/* initialize platform device stuff */
	pdev = platform_device_register_simple(ACPI_BAY_CLASS, id, NULL, 0);
	if (IS_ERR(pdev)) {
		printk(KERN_ERR PREFIX "Error registering bay device\n");
		goto bay_add_err;
	}
	new_bay->pdev = pdev;
	platform_set_drvdata(pdev, new_bay);

	/*
	 * we want the bay driver to be able to send uevents
	 */
	pdev->dev.uevent_suppress = 0;

	if (acpi_bay_add_fs(new_bay)) {
		platform_device_unregister(new_bay->pdev);
		goto bay_add_err;
	}

	/* register for events on this device */
	status = acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
			bay_notify, new_bay);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Error installing bay notify handler\n");
	}

	/* if we are on a dock station, we should register for dock
	 * notifications.
	 */
	if (bay_is_dock_device(handle)) {
		bay_dprintk(handle, "Is dependent on dock\n");
		register_hotplug_dock_device(handle, bay_notify, new_bay);
	}
	list_add(&new_bay->list, &drive_bays);
	printk(KERN_INFO PREFIX "Bay [%s] Added\n", new_bay->name);
	return 0;

bay_add_err:
	kfree(new_bay->name);
	kfree(new_bay);
	return -ENODEV;
}

/**
 * bay_notify - act upon an acpi bay notification
 * @handle: the bay handle
 * @event: the acpi event
 * @data: our driver data struct
 *
 */
static void bay_notify(acpi_handle handle, u32 event, void *data)
{
	struct bay *bay_dev = (struct bay *)data;
	struct device *dev = &bay_dev->pdev->dev;
	char event_string[12];
	char *envp[] = { event_string, NULL };

	bay_dprintk(handle, "Bay event");
	sprintf(event_string, "BAY_EVENT=%d\n", event);
	kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
}

static acpi_status
find_bay(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *count = (int *)context;

	/*
	 * there could be more than one ejectable bay.
	 * so, just return AE_OK always so that every object
	 * will be checked.
	 */
	if (is_ejectable_bay(handle)) {
		bay_dprintk(handle, "found ejectable bay");
		if (!bay_add(handle, *count))
			(*count)++;
	}
	return AE_OK;
}

static int __init bay_init(void)
{
	int bays = 0;

	INIT_LIST_HEAD(&drive_bays);

	/* look for dockable drive bays */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
		ACPI_UINT32_MAX, find_bay, &bays, NULL);

	if (!bays)
		return -ENODEV;

	return 0;
}

static void __exit bay_exit(void)
{
	struct bay *bay, *tmp;

	list_for_each_entry_safe(bay, tmp, &drive_bays, list) {
		if (is_dock_device(bay->handle))
			unregister_hotplug_dock_device(bay->handle);
		acpi_bay_remove_fs(bay);
		acpi_remove_notify_handler(bay->handle, ACPI_SYSTEM_NOTIFY,
			bay_notify);
		platform_device_unregister(bay->pdev);
		kfree(bay->name);
		kfree(bay);
	}
}

postcore_initcall(bay_init);
module_exit(bay_exit);

