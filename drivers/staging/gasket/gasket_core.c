// SPDX-License-Identifier: GPL-2.0
/*
 * Gasket generic driver framework. This file contains the implementation
 * for the Gasket generic driver framework - the functionality that is common
 * across Gasket devices.
 *
 * Copyright (C) 2018 Google, Inc.
 */
#include "gasket_core.h"

#include "gasket_interrupt.h"
#include "gasket_ioctl.h"
#include "gasket_logging.h"
#include "gasket_page_table.h"
#include "gasket_sysfs.h"

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/of.h>

#ifdef GASKET_KERNEL_TRACE_SUPPORT
#define CREATE_TRACE_POINTS
#include <trace/events/gasket_mmap.h>
#else
#define trace_gasket_mmap_exit(x)
#define trace_gasket_mmap_entry(x, ...)
#endif

/*
 * "Private" members of gasket_driver_desc.
 *
 * Contains internal per-device type tracking data, i.e., data not appropriate
 * as part of the public interface for the generic framework.
 */
struct gasket_internal_desc {
	/* Device-specific-driver-provided configuration information. */
	const struct gasket_driver_desc *driver_desc;

	/* Protects access to per-driver data (i.e. this structure). */
	struct mutex mutex;

	/* Kernel-internal device class. */
	struct class *class;

	/* PCI subsystem metadata associated with this driver. */
	struct pci_driver pci;

	/* Instantiated / present devices of this type. */
	struct gasket_dev *devs[GASKET_DEV_MAX];
};

/* do_map_region() needs be able to return more than just true/false. */
enum do_map_region_status {
	/* The region was successfully mapped. */
	DO_MAP_REGION_SUCCESS,

	/* Attempted to map region and failed. */
	DO_MAP_REGION_FAILURE,

	/* The requested region to map was not part of a mappable region. */
	DO_MAP_REGION_INVALID,
};

/* Function declarations; comments are with definitions. */
static int __init gasket_init(void);
static void __exit gasket_exit(void);

static int gasket_pci_probe(
	struct pci_dev *pci_dev, const struct pci_device_id *id);
static void gasket_pci_remove(struct pci_dev *pci_dev);

static int gasket_setup_pci(struct pci_dev *pci_dev, struct gasket_dev *dev);
static void gasket_cleanup_pci(struct gasket_dev *dev);

static int gasket_map_pci_bar(struct gasket_dev *dev, int bar_num);
static void gasket_unmap_pci_bar(struct gasket_dev *dev, int bar_num);

static int gasket_alloc_dev(
	struct gasket_internal_desc *internal_desc, struct device *dev,
	struct gasket_dev **pdev, const char *kobj_name);
static void gasket_free_dev(struct gasket_dev *dev);

static int gasket_find_dev_slot(
	struct gasket_internal_desc *internal_desc, const char *kobj_name);

static int gasket_add_cdev(
	struct gasket_cdev_info *dev_info,
	const struct file_operations *file_ops, struct module *owner);

static int gasket_enable_dev(
	struct gasket_internal_desc *internal_desc,
	struct gasket_dev *gasket_dev);
static void gasket_disable_dev(struct gasket_dev *gasket_dev);

static struct gasket_internal_desc *lookup_internal_desc(
	struct pci_dev *pci_dev);

static ssize_t gasket_sysfs_data_show(
	struct device *device, struct device_attribute *attr, char *buf);

static int gasket_mmap(struct file *filp, struct vm_area_struct *vma);
static int gasket_open(struct inode *inode, struct file *file);
static int gasket_release(struct inode *inode, struct file *file);
static long gasket_ioctl(struct file *filp, uint cmd, ulong arg);

static int gasket_mm_vma_bar_offset(
	const struct gasket_dev *gasket_dev, const struct vm_area_struct *vma,
	ulong *bar_offset);
static bool gasket_mm_get_mapping_addrs(
	const struct gasket_mappable_region *region, ulong bar_offset,
	ulong requested_length, struct gasket_mappable_region *mappable_region,
	ulong *virt_offset);
static enum do_map_region_status do_map_region(
	const struct gasket_dev *gasket_dev, struct vm_area_struct *vma,
	struct gasket_mappable_region *map_region);

static int gasket_get_hw_status(struct gasket_dev *gasket_dev);

/* Global data definitions. */
/* Mutex - only for framework-wide data. Other data should be protected by
 * finer-grained locks.
 */
static DEFINE_MUTEX(g_mutex);

/* List of all registered device descriptions & their supporting data. */
static struct gasket_internal_desc g_descs[GASKET_FRAMEWORK_DESC_MAX];

/* Mapping of statuses to human-readable strings. Must end with {0,NULL}. */
static const struct gasket_num_name gasket_status_name_table[] = {
	{ GASKET_STATUS_DEAD, "DEAD" },
	{ GASKET_STATUS_ALIVE, "ALIVE" },
	{ GASKET_STATUS_LAMED, "LAMED" },
	{ GASKET_STATUS_DRIVER_EXIT, "DRIVER_EXITING" },
	{ 0, NULL },
};

/* Enumeration of the automatic Gasket framework sysfs nodes. */
enum gasket_sysfs_attribute_type {
	ATTR_BAR_OFFSETS,
	ATTR_BAR_SIZES,
	ATTR_DRIVER_VERSION,
	ATTR_FRAMEWORK_VERSION,
	ATTR_DEVICE_TYPE,
	ATTR_HARDWARE_REVISION,
	ATTR_PCI_ADDRESS,
	ATTR_STATUS,
	ATTR_IS_DEVICE_OWNED,
	ATTR_DEVICE_OWNER,
	ATTR_WRITE_OPEN_COUNT,
	ATTR_RESET_COUNT,
	ATTR_USER_MEM_RANGES
};

/* File operations for all Gasket devices. */
static const struct file_operations gasket_file_ops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.mmap = gasket_mmap,
	.open = gasket_open,
	.release = gasket_release,
	.unlocked_ioctl = gasket_ioctl,
};

/* These attributes apply to all Gasket driver instances. */
static const struct gasket_sysfs_attribute gasket_sysfs_generic_attrs[] = {
	GASKET_SYSFS_RO(bar_offsets, gasket_sysfs_data_show, ATTR_BAR_OFFSETS),
	GASKET_SYSFS_RO(bar_sizes, gasket_sysfs_data_show, ATTR_BAR_SIZES),
	GASKET_SYSFS_RO(driver_version, gasket_sysfs_data_show,
			ATTR_DRIVER_VERSION),
	GASKET_SYSFS_RO(framework_version, gasket_sysfs_data_show,
			ATTR_FRAMEWORK_VERSION),
	GASKET_SYSFS_RO(device_type, gasket_sysfs_data_show, ATTR_DEVICE_TYPE),
	GASKET_SYSFS_RO(revision, gasket_sysfs_data_show,
			ATTR_HARDWARE_REVISION),
	GASKET_SYSFS_RO(pci_address, gasket_sysfs_data_show, ATTR_PCI_ADDRESS),
	GASKET_SYSFS_RO(status, gasket_sysfs_data_show, ATTR_STATUS),
	GASKET_SYSFS_RO(is_device_owned, gasket_sysfs_data_show,
			ATTR_IS_DEVICE_OWNED),
	GASKET_SYSFS_RO(device_owner, gasket_sysfs_data_show,
			ATTR_DEVICE_OWNER),
	GASKET_SYSFS_RO(write_open_count, gasket_sysfs_data_show,
			ATTR_WRITE_OPEN_COUNT),
	GASKET_SYSFS_RO(reset_count, gasket_sysfs_data_show, ATTR_RESET_COUNT),
	GASKET_SYSFS_RO(user_mem_ranges, gasket_sysfs_data_show,
			ATTR_USER_MEM_RANGES),
	GASKET_END_OF_ATTR_ARRAY
};

MODULE_DESCRIPTION("Google Gasket driver framework");
MODULE_VERSION(GASKET_FRAMEWORK_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Rob Springer <rspringer@google.com>");
module_init(gasket_init);
module_exit(gasket_exit);

/*
 * Perform a standard Gasket callback.
 * @gasket_dev: Device specific pointer to forward.
 * @cb_function: Standard callback to perform.
 */
static inline int check_and_invoke_callback(
	struct gasket_dev *gasket_dev, int (*cb_function)(struct gasket_dev *))
{
	int ret = 0;

	gasket_nodev_error("check_and_invoke_callback %p", gasket_dev);
	if (cb_function) {
		mutex_lock(&gasket_dev->mutex);
		ret = cb_function(gasket_dev);
		mutex_unlock(&gasket_dev->mutex);
	}
	return ret;
}

/*
 * Perform a standard Gasket callback
 * without grabbing gasket_dev->mutex.
 * @gasket_dev: Device specific pointer to forward.
 * @cb_function: Standard callback to perform.
 *
 */
static inline int gasket_check_and_invoke_callback_nolock(
	struct gasket_dev *gasket_dev, int (*cb_function)(struct gasket_dev *))
{
	int ret = 0;

	if (cb_function) {
		gasket_log_info(
			gasket_dev, "Invoking device-specific callback.");
		ret = cb_function(gasket_dev);
	}
	return ret;
}

/*
 * Returns nonzero if the gasket_cdev_info is owned by the current thread group
 * ID.
 * @info: Device node info.
 */
static int gasket_owned_by_current_tgid(struct gasket_cdev_info *info)
{
	return (info->ownership.is_owned &&
		(info->ownership.owner == current->tgid));
}

static int __init gasket_init(void)
{
	int i;

	gasket_nodev_info("Performing one-time init of the Gasket framework.");
	/* Check for duplicates and find a free slot. */
	mutex_lock(&g_mutex);
	for (i = 0; i < GASKET_FRAMEWORK_DESC_MAX; i++) {
		g_descs[i].driver_desc = NULL;
		mutex_init(&g_descs[i].mutex);
	}

	gasket_sysfs_init();

	mutex_unlock(&g_mutex);
	return 0;
}

static void __exit gasket_exit(void)
{
	/* No deinit/dealloc needed at present. */
	gasket_nodev_info("Removing Gasket framework module.");
}

/* See gasket_core.h for description. */
int gasket_register_device(const struct gasket_driver_desc *driver_desc)
{
	int i, ret;
	int desc_idx = -1;
	struct gasket_internal_desc *internal;

	gasket_nodev_info("Initializing Gasket framework device");
	/* Check for duplicates and find a free slot. */
	mutex_lock(&g_mutex);

	for (i = 0; i < GASKET_FRAMEWORK_DESC_MAX; i++) {
		if (g_descs[i].driver_desc == driver_desc) {
			gasket_nodev_error(
				"%s driver already loaded/registered",
				driver_desc->name);
			mutex_unlock(&g_mutex);
			return -EBUSY;
		}
	}

	/* This and the above loop could be combined, but this reads easier. */
	for (i = 0; i < GASKET_FRAMEWORK_DESC_MAX; i++) {
		if (!g_descs[i].driver_desc) {
			g_descs[i].driver_desc = driver_desc;
			desc_idx = i;
			break;
		}
	}
	mutex_unlock(&g_mutex);

	gasket_nodev_info("Loaded %s driver, framework version %s",
			  driver_desc->name, GASKET_FRAMEWORK_VERSION);

	if (desc_idx == -1) {
		gasket_nodev_error("Too many Gasket drivers loaded: %d\n",
				   GASKET_FRAMEWORK_DESC_MAX);
		return -EBUSY;
	}

	/* Internal structure setup. */
	gasket_nodev_info("Performing initial internal structure setup.");
	internal = &g_descs[desc_idx];
	mutex_init(&internal->mutex);
	memset(internal->devs, 0, sizeof(struct gasket_dev *) * GASKET_DEV_MAX);
	memset(&internal->pci, 0, sizeof(internal->pci));
	internal->pci.name = driver_desc->name;
	internal->pci.id_table = driver_desc->pci_id_table;
	internal->pci.probe = gasket_pci_probe;
	internal->pci.remove = gasket_pci_remove;
	internal->class =
		class_create(driver_desc->module, driver_desc->name);

	if (IS_ERR(internal->class)) {
		gasket_nodev_error("Cannot register %s class [ret=%ld]",
				   driver_desc->name, PTR_ERR(internal->class));
		ret = PTR_ERR(internal->class);
		goto unregister_gasket_driver;
	}

	/*
	 * Not using pci_register_driver() (without underscores), as it
	 * depends on KBUILD_MODNAME, and this is a shared file.
	 */
	gasket_nodev_info("Registering PCI driver.");
	ret = __pci_register_driver(
		&internal->pci, driver_desc->module, driver_desc->name);
	if (ret) {
		gasket_nodev_error(
			"cannot register pci driver [ret=%d]", ret);
		goto fail1;
	}

	gasket_nodev_info("Registering char driver.");
	ret = register_chrdev_region(
		MKDEV(driver_desc->major, driver_desc->minor), GASKET_DEV_MAX,
		driver_desc->name);
	if (ret) {
		gasket_nodev_error("cannot register char driver [ret=%d]", ret);
		goto fail2;
	}

	gasket_nodev_info("Driver registered successfully.");
	return 0;

fail2:
	pci_unregister_driver(&internal->pci);

fail1:
	class_destroy(internal->class);

unregister_gasket_driver:
	mutex_lock(&g_mutex);
	g_descs[desc_idx].driver_desc = NULL;
	mutex_unlock(&g_mutex);
	return ret;
}
EXPORT_SYMBOL(gasket_register_device);

/* See gasket_core.h for description. */
void gasket_unregister_device(const struct gasket_driver_desc *driver_desc)
{
	int i, desc_idx;
	struct gasket_internal_desc *internal_desc = NULL;

	mutex_lock(&g_mutex);
	for (i = 0; i < GASKET_FRAMEWORK_DESC_MAX; i++) {
		if (g_descs[i].driver_desc == driver_desc) {
			internal_desc = &g_descs[i];
			desc_idx = i;
			break;
		}
	}
	mutex_unlock(&g_mutex);

	if (!internal_desc) {
		gasket_nodev_error(
			"request to unregister unknown desc: %s, %d:%d",
			driver_desc->name, driver_desc->major,
			driver_desc->minor);
		return;
	}

	unregister_chrdev_region(
		MKDEV(driver_desc->major, driver_desc->minor), GASKET_DEV_MAX);

	pci_unregister_driver(&internal_desc->pci);

	class_destroy(internal_desc->class);

	/* Finally, effectively "remove" the driver. */
	mutex_lock(&g_mutex);
	g_descs[desc_idx].driver_desc = NULL;
	mutex_unlock(&g_mutex);

	gasket_nodev_info("removed %s driver", driver_desc->name);
}
EXPORT_SYMBOL(gasket_unregister_device);

/**
 * Allocate a Gasket device.
 * @internal_desc: Pointer to the internal data for the device driver.
 * @pdev: Pointer to the Gasket device pointer, the allocated device.
 * @kobj_name: PCIe name for the device
 *
 * Description: Allocates and initializes a Gasket device structure.
 *              Adds the device to the device list.
 *
 * Returns 0 if successful, a negative error code otherwise.
 */
static int gasket_alloc_dev(
	struct gasket_internal_desc *internal_desc, struct device *parent,
	struct gasket_dev **pdev, const char *kobj_name)
{
	int dev_idx;
	const struct gasket_driver_desc *driver_desc =
		internal_desc->driver_desc;
	struct gasket_dev *gasket_dev;
	struct gasket_cdev_info *dev_info;

	gasket_nodev_info("Allocating a Gasket device %s.", kobj_name);

	*pdev = NULL;

	dev_idx = gasket_find_dev_slot(internal_desc, kobj_name);
	if (dev_idx < 0)
		return dev_idx;

	gasket_dev = *pdev = kzalloc(sizeof(*gasket_dev), GFP_KERNEL);
	if (!gasket_dev) {
		gasket_nodev_error("no memory for device");
		return -ENOMEM;
	}
	internal_desc->devs[dev_idx] = gasket_dev;

	mutex_init(&gasket_dev->mutex);

	gasket_dev->internal_desc = internal_desc;
	gasket_dev->dev_idx = dev_idx;
	snprintf(gasket_dev->kobj_name, GASKET_NAME_MAX, "%s", kobj_name);
	/* gasket_bar_data is uninitialized. */
	gasket_dev->num_page_tables = driver_desc->num_page_tables;
	/* max_page_table_size and *page table are uninit'ed */
	/* interrupt_data is not initialized. */
	/* status is 0, or GASKET_STATUS_DEAD */

	dev_info = &gasket_dev->dev_info;
	snprintf(dev_info->name, GASKET_NAME_MAX, "%s_%u", driver_desc->name,
		 gasket_dev->dev_idx);
	dev_info->devt =
		MKDEV(driver_desc->major, driver_desc->minor +
			gasket_dev->dev_idx);
	dev_info->device = device_create(internal_desc->class, parent,
		dev_info->devt, gasket_dev, dev_info->name);

	gasket_nodev_info("Gasket device allocated: %p.", dev_info->device);

	/* cdev has not yet been added; cdev_added is 0 */
	dev_info->gasket_dev_ptr = gasket_dev;
	/* ownership is all 0, indicating no owner or opens. */

	return 0;
}

/*
 * Free a Gasket device.
 * @internal_dev: Gasket device pointer; the device to unregister and free.
 *
 * Description: Removes the device from the device list and frees
 *              the Gasket device structure.
 */
static void gasket_free_dev(struct gasket_dev *gasket_dev)
{
	struct gasket_internal_desc *internal_desc = gasket_dev->internal_desc;

	mutex_lock(&internal_desc->mutex);
	internal_desc->devs[gasket_dev->dev_idx] = NULL;
	mutex_unlock(&internal_desc->mutex);

	kfree(gasket_dev);
}

/*
 * Finds the next free gasket_internal_dev slot.
 *
 * Returns the located slot number on success or a negative number on failure.
 */
static int gasket_find_dev_slot(
	struct gasket_internal_desc *internal_desc, const char *kobj_name)
{
	int i;

	mutex_lock(&internal_desc->mutex);

	/* Search for a previous instance of this device. */
	for (i = 0; i < GASKET_DEV_MAX; i++) {
		if (internal_desc->devs[i] &&
		    strcmp(internal_desc->devs[i]->kobj_name, kobj_name) == 0) {
			gasket_nodev_error("Duplicate device %s", kobj_name);
			mutex_unlock(&internal_desc->mutex);
			return -EBUSY;
		}
	}

	/* Find a free device slot. */
	for (i = 0; i < GASKET_DEV_MAX; i++) {
		if (!internal_desc->devs[i])
			break;
	}

	if (i == GASKET_DEV_MAX) {
		gasket_nodev_info(
			"Too many registered devices; max %d", GASKET_DEV_MAX);
		mutex_unlock(&internal_desc->mutex);
		return -EBUSY;
	}

	mutex_unlock(&internal_desc->mutex);
	return i;
}

/**
 * PCI subsystem probe function.
 * @pci_dev: PCI device pointer to the new device.
 * @id: PCI device id structure pointer, the vendor and device ids.
 *
 * Called when a Gasket device is found. Allocates device metadata, maps device
 * memory, and calls gasket_enable_dev to prepare the device for active use.
 *
 * Returns 0 if successful and a negative value otherwise.
 */
static int gasket_pci_probe(
	struct pci_dev *pci_dev, const struct pci_device_id *id)
{
	int ret;
	const char *kobj_name = dev_name(&pci_dev->dev);
	struct gasket_internal_desc *internal_desc;
	struct gasket_dev *gasket_dev;
	const struct gasket_driver_desc *driver_desc;
	struct device *parent;

	gasket_nodev_info("Add Gasket device %s", kobj_name);

	mutex_lock(&g_mutex);
	internal_desc = lookup_internal_desc(pci_dev);
	mutex_unlock(&g_mutex);
	if (!internal_desc) {
		gasket_nodev_info("PCI probe called for unknown driver type");
		return -ENODEV;
	}

	driver_desc = internal_desc->driver_desc;

	parent = &pci_dev->dev;
	ret = gasket_alloc_dev(internal_desc, parent, &gasket_dev, kobj_name);
	if (ret)
		return ret;
	if (IS_ERR_OR_NULL(gasket_dev->dev_info.device)) {
		gasket_nodev_error("Cannot create %s device %s [ret = %ld]",
				   driver_desc->name, gasket_dev->dev_info.name,
				   PTR_ERR(gasket_dev->dev_info.device));
		ret = -ENODEV;
		goto fail1;
	}
	gasket_dev->pci_dev = pci_dev;

	ret = gasket_setup_pci(pci_dev, gasket_dev);
	if (ret)
		goto fail2;

	ret = check_and_invoke_callback(gasket_dev, driver_desc->add_dev_cb);
	if (ret) {
		gasket_log_error(gasket_dev, "Error in add device cb: %d", ret);
		goto fail2;
	}

	ret = gasket_sysfs_create_mapping(
		gasket_dev->dev_info.device, gasket_dev);
	if (ret)
		goto fail3;

	/*
	 * Once we've created the mapping structures successfully, attempt to
	 * create a symlink to the pci directory of this object.
	 */
	ret = sysfs_create_link(&gasket_dev->dev_info.device->kobj,
				&pci_dev->dev.kobj, dev_name(&pci_dev->dev));
	if (ret) {
		gasket_log_error(
			gasket_dev, "Cannot create sysfs pci link: %d", ret);
		goto fail3;
	}
	ret = gasket_sysfs_create_entries(
		gasket_dev->dev_info.device, gasket_sysfs_generic_attrs);
	if (ret)
		goto fail4;

	ret = check_and_invoke_callback(
		gasket_dev, driver_desc->sysfs_setup_cb);
	if (ret) {
		gasket_log_error(
			gasket_dev, "Error in sysfs setup cb: %d", ret);
		goto fail5;
	}

	ret = gasket_enable_dev(internal_desc, gasket_dev);
	if (ret) {
		gasket_nodev_error("cannot setup %s device", driver_desc->name);
		gasket_disable_dev(gasket_dev);
		goto fail5;
	}

	return 0;

fail5:
	check_and_invoke_callback(gasket_dev, driver_desc->sysfs_cleanup_cb);
fail4:
fail3:
	gasket_sysfs_remove_mapping(gasket_dev->dev_info.device);
fail2:
	gasket_cleanup_pci(gasket_dev);
	check_and_invoke_callback(gasket_dev, driver_desc->remove_dev_cb);
	device_destroy(internal_desc->class, gasket_dev->dev_info.devt);
fail1:
	gasket_free_dev(gasket_dev);
	return ret;
}

/*
 * PCI subsystem remove function.
 * @pci_dev: PCI device pointer; the device to remove.
 *
 * Called to remove a Gasket device. Finds the device in the device list and
 * cleans up metadata.
 */
static void gasket_pci_remove(struct pci_dev *pci_dev)
{
	int i;
	struct gasket_internal_desc *internal_desc;
	struct gasket_dev *gasket_dev = NULL;
	const struct gasket_driver_desc *driver_desc;
	/* Find the device desc. */
	mutex_lock(&g_mutex);
	internal_desc = lookup_internal_desc(pci_dev);
	if (!internal_desc) {
		mutex_unlock(&g_mutex);
		return;
	}
	mutex_unlock(&g_mutex);

	driver_desc = internal_desc->driver_desc;

	/* Now find the specific device */
	mutex_lock(&internal_desc->mutex);
	for (i = 0; i < GASKET_DEV_MAX; i++) {
		if (internal_desc->devs[i] &&
		    internal_desc->devs[i]->pci_dev == pci_dev) {
			gasket_dev = internal_desc->devs[i];
			break;
		}
	}
	mutex_unlock(&internal_desc->mutex);

	if (!gasket_dev)
		return;

	gasket_nodev_info(
		"remove %s device %s", internal_desc->driver_desc->name,
		gasket_dev->kobj_name);

	gasket_disable_dev(gasket_dev);
	gasket_cleanup_pci(gasket_dev);

	check_and_invoke_callback(gasket_dev, driver_desc->sysfs_cleanup_cb);
	gasket_sysfs_remove_mapping(gasket_dev->dev_info.device);

	check_and_invoke_callback(gasket_dev, driver_desc->remove_dev_cb);

	device_destroy(internal_desc->class, gasket_dev->dev_info.devt);
	gasket_free_dev(gasket_dev);
}

/*
 * Setup PCI & set up memory mapping for the specified device.
 * @pci_dev: pointer to the particular PCI device.
 * @internal_dev: Corresponding Gasket device pointer.
 *
 * Enables the PCI device, reads the BAR registers and sets up pointers to the
 * device's memory mapped IO space.
 *
 * Returns 0 on success and a negative value otherwise.
 */
static int gasket_setup_pci(
	struct pci_dev *pci_dev, struct gasket_dev *gasket_dev)
{
	int i, mapped_bars, ret;

	gasket_dev->pci_dev = pci_dev;
	ret = pci_enable_device(pci_dev);
	if (ret) {
		gasket_log_error(gasket_dev, "cannot enable PCI device");
		return ret;
	}

	pci_set_master(pci_dev);

	for (i = 0; i < GASKET_NUM_BARS; i++) {
		ret = gasket_map_pci_bar(gasket_dev, i);
		if (ret) {
			mapped_bars = i;
			goto fail;
		}
	}

	return 0;

fail:
	for (i = 0; i < mapped_bars; i++)
		gasket_unmap_pci_bar(gasket_dev, i);

	pci_disable_device(pci_dev);
	return -ENOMEM;
}

/* Unmaps memory and cleans up PCI for the specified device. */
static void gasket_cleanup_pci(struct gasket_dev *gasket_dev)
{
	int i;

	for (i = 0; i < GASKET_NUM_BARS; i++)
		gasket_unmap_pci_bar(gasket_dev, i);

	pci_disable_device(gasket_dev->pci_dev);
}

/*
 * Maps the specified bar into kernel space.
 * @internal_dev: Device possessing the BAR to map.
 * @bar_num: The BAR to map.
 *
 * Returns 0 on success, a negative error code otherwise.
 * A zero-sized BAR will not be mapped, but is not an error.
 */
static int gasket_map_pci_bar(struct gasket_dev *gasket_dev, int bar_num)
{
	struct gasket_internal_desc *internal_desc = gasket_dev->internal_desc;
	const struct gasket_driver_desc *driver_desc =
		internal_desc->driver_desc;
	ulong desc_bytes = driver_desc->bar_descriptions[bar_num].size;
	int ret;

	if (desc_bytes == 0)
		return 0;

	if (driver_desc->bar_descriptions[bar_num].type != PCI_BAR) {
		/* not PCI: skip this entry */
		return 0;
	}
	/*
	 * pci_resource_start and pci_resource_len return a "resource_size_t",
	 * which is safely castable to ulong (which itself is the arg to
	 * request_mem_region).
	 */
	gasket_dev->bar_data[bar_num].phys_base =
		(ulong)pci_resource_start(gasket_dev->pci_dev, bar_num);
	if (!gasket_dev->bar_data[bar_num].phys_base) {
		gasket_log_error(gasket_dev, "Cannot get BAR%u base address",
				 bar_num);
		return -EINVAL;
	}

	gasket_dev->bar_data[bar_num].length_bytes =
		(ulong)pci_resource_len(gasket_dev->pci_dev, bar_num);
	if (gasket_dev->bar_data[bar_num].length_bytes < desc_bytes) {
		gasket_log_error(
			gasket_dev,
			"PCI BAR %u space is too small: %lu; expected >= %lu",
			bar_num, gasket_dev->bar_data[bar_num].length_bytes,
			desc_bytes);
		return -ENOMEM;
	}

	if (!request_mem_region(gasket_dev->bar_data[bar_num].phys_base,
				gasket_dev->bar_data[bar_num].length_bytes,
				gasket_dev->dev_info.name)) {
		gasket_log_error(
			gasket_dev,
			"Cannot get BAR %d memory region %p",
			bar_num, &gasket_dev->pci_dev->resource[bar_num]);
		return -EINVAL;
	}

	gasket_dev->bar_data[bar_num].virt_base =
		ioremap_nocache(gasket_dev->bar_data[bar_num].phys_base,
				gasket_dev->bar_data[bar_num].length_bytes);
	if (!gasket_dev->bar_data[bar_num].virt_base) {
		gasket_log_error(
			gasket_dev,
			"Cannot remap BAR %d memory region %p",
			bar_num, &gasket_dev->pci_dev->resource[bar_num]);
		ret = -ENOMEM;
		goto fail;
	}

	dma_set_mask(&gasket_dev->pci_dev->dev, DMA_BIT_MASK(64));
	dma_set_coherent_mask(&gasket_dev->pci_dev->dev, DMA_BIT_MASK(64));

	return 0;

fail:
	iounmap(gasket_dev->bar_data[bar_num].virt_base);
	release_mem_region(gasket_dev->bar_data[bar_num].phys_base,
			   gasket_dev->bar_data[bar_num].length_bytes);
	return ret;
}

/*
 * Releases PCI BAR mapping.
 * @internal_dev: Device possessing the BAR to unmap.
 *
 * A zero-sized or not-mapped BAR will not be unmapped, but is not an error.
 */
static void gasket_unmap_pci_bar(struct gasket_dev *dev, int bar_num)
{
	ulong base, bytes;
	struct gasket_internal_desc *internal_desc = dev->internal_desc;
	const struct gasket_driver_desc *driver_desc =
		internal_desc->driver_desc;

	if (driver_desc->bar_descriptions[bar_num].size == 0 ||
	    !dev->bar_data[bar_num].virt_base)
		return;

	if (driver_desc->bar_descriptions[bar_num].type != PCI_BAR)
		return;

	iounmap(dev->bar_data[bar_num].virt_base);
	dev->bar_data[bar_num].virt_base = NULL;

	base = pci_resource_start(dev->pci_dev, bar_num);
	if (!base) {
		gasket_log_error(
			dev, "cannot get PCI BAR%u base address", bar_num);
		return;
	}

	bytes = pci_resource_len(dev->pci_dev, bar_num);
	release_mem_region(base, bytes);
}

/*
 * Handle adding a char device and related info.
 * @dev_info: Pointer to the dev_info struct for this device.
 * @file_ops: The file operations for this device.
 * @owner: The owning module for this device.
 */
static int gasket_add_cdev(
	struct gasket_cdev_info *dev_info,
	const struct file_operations *file_ops, struct module *owner)
{
	int ret;

	cdev_init(&dev_info->cdev, file_ops);
	dev_info->cdev.owner = owner;
	ret = cdev_add(&dev_info->cdev, dev_info->devt, 1);
	if (ret) {
		gasket_log_error(
			dev_info->gasket_dev_ptr,
			"cannot add char device [ret=%d]", ret);
		return ret;
	}
	dev_info->cdev_added = 1;

	return 0;
}

/*
 * Performs final init and marks the device as active.
 * @internal_desc: Pointer to Gasket [internal] driver descriptor structure.
 * @internal_dev: Pointer to Gasket [internal] device structure.
 *
 * Currently forwards all work to device-specific callback; a future phase will
 * extract elements of character device registration here.
 */
static int gasket_enable_dev(
	struct gasket_internal_desc *internal_desc,
	struct gasket_dev *gasket_dev)
{
	int tbl_idx;
	int ret;
	const struct gasket_driver_desc *driver_desc =
		internal_desc->driver_desc;

	ret = gasket_interrupt_init(
		gasket_dev, driver_desc->name,
		driver_desc->interrupt_type, driver_desc->interrupts,
		driver_desc->num_interrupts, driver_desc->interrupt_pack_width,
		driver_desc->interrupt_bar_index,
		driver_desc->wire_interrupt_offsets);
	if (ret) {
		gasket_log_error(gasket_dev,
				 "Critical failure to allocate interrupts: %d",
				 ret);
		gasket_interrupt_cleanup(gasket_dev);
		return ret;
	}

	for (tbl_idx = 0; tbl_idx < driver_desc->num_page_tables; tbl_idx++) {
		gasket_log_debug(
			gasket_dev, "Initializing page table %d.", tbl_idx);
		ret = gasket_page_table_init(
			&gasket_dev->page_table[tbl_idx],
			&gasket_dev->bar_data[
				driver_desc->page_table_bar_index],
			&driver_desc->page_table_configs[tbl_idx],
			&gasket_dev->pci_dev->dev, gasket_dev->pci_dev, true);
		if (ret) {
			gasket_log_error(
				gasket_dev,
				"Couldn't init page table %d: %d",
				tbl_idx, ret);
			return ret;
		}
		/*
		 * Make sure that the page table is clear and set to simple
		 * addresses.
		 */
		gasket_page_table_reset(gasket_dev->page_table[tbl_idx]);
	}

	/*
	 * hardware_revision_cb returns a positive integer (the rev) if
	 * successful.)
	 */
	ret = check_and_invoke_callback(
		gasket_dev, driver_desc->hardware_revision_cb);
	if (ret < 0) {
		gasket_log_error(
			gasket_dev, "Error getting hardware revision: %d", ret);
		return ret;
	}
	gasket_dev->hardware_revision = ret;

	ret = check_and_invoke_callback(gasket_dev, driver_desc->enable_dev_cb);
	if (ret) {
		gasket_log_error(
			gasket_dev, "Error in enable device cb: %d", ret);
		return ret;
	}

	/* device_status_cb returns a device status, not an error code. */
	gasket_dev->status = gasket_get_hw_status(gasket_dev);
	if (gasket_dev->status == GASKET_STATUS_DEAD)
		gasket_log_error(gasket_dev, "Device reported as unhealthy.");

	ret = gasket_add_cdev(
		&gasket_dev->dev_info, &gasket_file_ops, driver_desc->module);
	if (ret)
		return ret;

	return 0;
}

/*
 * Disable device operations.
 * @gasket_dev: Pointer to Gasket device structure.
 *
 * Currently forwards all work to device-specific callback; a future phase will
 * extract elements of character device unregistration here.
 */
static void gasket_disable_dev(struct gasket_dev *gasket_dev)
{
	const struct gasket_driver_desc *driver_desc =
		gasket_dev->internal_desc->driver_desc;
	int i;

	/* Only delete the device if it has been successfully added. */
	if (gasket_dev->dev_info.cdev_added)
		cdev_del(&gasket_dev->dev_info.cdev);

	gasket_dev->status = GASKET_STATUS_DEAD;

	gasket_interrupt_cleanup(gasket_dev);

	for (i = 0; i < driver_desc->num_page_tables; ++i) {
		if (gasket_dev->page_table[i]) {
			gasket_page_table_reset(gasket_dev->page_table[i]);
			gasket_page_table_cleanup(gasket_dev->page_table[i]);
		}
	}

	check_and_invoke_callback(gasket_dev, driver_desc->disable_dev_cb);
}

/**
 * Registered descriptor lookup.
 *
 * Precondition: Called with g_mutex held (to avoid a race on return).
 * Returns NULL if no matching device was found.
 */
static struct gasket_internal_desc *lookup_internal_desc(
	struct pci_dev *pci_dev)
{
	int i;

	__must_hold(&g_mutex);
	for (i = 0; i < GASKET_FRAMEWORK_DESC_MAX; i++) {
		if (g_descs[i].driver_desc &&
		    g_descs[i].driver_desc->pci_id_table &&
		    pci_match_id(g_descs[i].driver_desc->pci_id_table, pci_dev))
			return &g_descs[i];
	}

	return NULL;
}

/**
 * Lookup a name by number in a num_name table.
 * @num: Number to lookup.
 * @table: Array of num_name structures, the table for the lookup.
 *
 * Description: Searches for num in the table.  If found, the
 *		corresponding name is returned; otherwise NULL
 *		is returned.
 *
 *		The table must have a NULL name pointer at the end.
 */
const char *gasket_num_name_lookup(
	uint num, const struct gasket_num_name *table)
{
	uint i = 0;

	while (table[i].snn_name) {
		if (num == table[i].snn_num)
			break;
		++i;
	}

	return table[i].snn_name;
}
EXPORT_SYMBOL(gasket_num_name_lookup);

/**
 * Opens the char device file.
 * @inode: Inode structure pointer of the device file.
 * @file: File structure pointer.
 *
 * Description: Called on an open of the device file.  If the open is for
 *              writing, and the device is not owned, this process becomes
 *              the owner.  If the open is for writing and the device is
 *              already owned by some other process, it is an error.  If
 *              this process is the owner, increment the open count.
 *
 *              Returns 0 if successful, a negative error number otherwise.
 */
static int gasket_open(struct inode *inode, struct file *filp)
{
	int ret;
	struct gasket_dev *gasket_dev;
	const struct gasket_driver_desc *driver_desc;
	struct gasket_ownership *ownership;
	char task_name[TASK_COMM_LEN];
	struct gasket_cdev_info *dev_info =
	    container_of(inode->i_cdev, struct gasket_cdev_info, cdev);

	gasket_dev = dev_info->gasket_dev_ptr;
	driver_desc = gasket_dev->internal_desc->driver_desc;
	ownership = &dev_info->ownership;
	get_task_comm(task_name, current);
	filp->private_data = gasket_dev;
	inode->i_size = 0;

	gasket_log_debug(
		gasket_dev,
		"Attempting to open with tgid %u (%s) (f_mode: 0%03o, "
		"fmode_write: %d is_root: %u)",
		current->tgid, task_name, filp->f_mode,
		(filp->f_mode & FMODE_WRITE), capable(CAP_SYS_ADMIN));

	/* Always allow non-writing accesses. */
	if (!(filp->f_mode & FMODE_WRITE)) {
		gasket_log_debug(gasket_dev, "Allowing read-only opening.");
		return 0;
	}

	mutex_lock(&gasket_dev->mutex);

	gasket_log_debug(
		gasket_dev, "Current owner open count (owning tgid %u): %d.",
		ownership->owner, ownership->write_open_count);

	/* Opening a node owned by another TGID is an error (even root.) */
	if (ownership->is_owned && ownership->owner != current->tgid) {
		gasket_log_error(
			gasket_dev,
			"Process %u is opening a node held by %u.",
			current->tgid, ownership->owner);
		mutex_unlock(&gasket_dev->mutex);
		return -EPERM;
	}

	/* If the node is not owned, assign it to the current TGID. */
	if (!ownership->is_owned) {
		ret = gasket_check_and_invoke_callback_nolock(
			gasket_dev, driver_desc->device_open_cb);
		if (ret) {
			gasket_log_error(
				gasket_dev, "Error in device open cb: %d", ret);
			mutex_unlock(&gasket_dev->mutex);
			return ret;
		}
		ownership->is_owned = 1;
		ownership->owner = current->tgid;
		gasket_log_debug(gasket_dev, "Device owner is now tgid %u",
				 ownership->owner);
	}

	ownership->write_open_count++;

	gasket_log_debug(gasket_dev, "New open count (owning tgid %u): %d",
			 ownership->owner, ownership->write_open_count);

	mutex_unlock(&gasket_dev->mutex);
	return 0;
}

/**
 * gasket_release - Close of the char device file.
 * @inode: Inode structure pointer of the device file.
 * @file: File structure pointer.
 *
 * Description: Called on a close of the device file.  If this process
 *              is the owner, decrement the open count.  On last close
 *              by the owner, free up buffers and eventfd contexts, and
 *              release ownership.
 *
 *              Returns 0 if successful, a negative error number otherwise.
 */
static int gasket_release(struct inode *inode, struct file *file)
{
	int i;
	struct gasket_dev *gasket_dev;
	struct gasket_ownership *ownership;
	const struct gasket_driver_desc *driver_desc;
	char task_name[TASK_COMM_LEN];
	struct gasket_cdev_info *dev_info =
		container_of(inode->i_cdev, struct gasket_cdev_info, cdev);

	gasket_dev = dev_info->gasket_dev_ptr;
	driver_desc = gasket_dev->internal_desc->driver_desc;
	ownership = &dev_info->ownership;
	get_task_comm(task_name, current);
	mutex_lock(&gasket_dev->mutex);

	gasket_log_debug(
		gasket_dev,
		"Releasing device node. Call origin: tgid %u (%s) "
		"(f_mode: 0%03o, fmode_write: %d, is_root: %u)",
		current->tgid, task_name, file->f_mode,
		(file->f_mode & FMODE_WRITE), capable(CAP_SYS_ADMIN));
	gasket_log_debug(gasket_dev, "Current open count (owning tgid %u): %d",
			 ownership->owner, ownership->write_open_count);

	if (file->f_mode & FMODE_WRITE) {
		ownership->write_open_count--;
		if (ownership->write_open_count == 0) {
			gasket_log_info(gasket_dev, "Device is now free");
			ownership->is_owned = 0;
			ownership->owner = 0;

			/* Forces chip reset before we unmap the page tables. */
			driver_desc->device_reset_cb(gasket_dev, 0);

			for (i = 0; i < driver_desc->num_page_tables; ++i) {
				gasket_page_table_unmap_all(
					gasket_dev->page_table[i]);
				gasket_page_table_garbage_collect(
					gasket_dev->page_table[i]);
				gasket_free_coherent_memory_all(gasket_dev, i);
			}

			/* Closes device, enters power save. */
			gasket_check_and_invoke_callback_nolock(
				gasket_dev, driver_desc->device_close_cb);
		}
	}

	gasket_log_info(
		gasket_dev, "New open count (owning tgid %u): %d",
		ownership->owner, ownership->write_open_count);
	mutex_unlock(&gasket_dev->mutex);
	return 0;
}

/*
 * Permission and validity checking for mmap ops.
 * @gasket_dev: Gasket device information structure.
 * @vma: Standard virtual memory area descriptor.
 *
 * Verifies that the user has permissions to perform the requested mapping and
 * that the provided descriptor/range is of adequate size to hold the range to
 * be mapped.
 */
static bool gasket_mmap_has_permissions(
	struct gasket_dev *gasket_dev, struct vm_area_struct *vma,
	int bar_permissions)
{
	int requested_permissions;
	/* Always allow sysadmin to access. */
	if (capable(CAP_SYS_ADMIN))
		return true;

	/* Never allow non-sysadmins to access to a dead device. */
	if (gasket_dev->status != GASKET_STATUS_ALIVE) {
		gasket_log_info(gasket_dev, "Device is dead.");
		return false;
	}

	/* Make sure that no wrong flags are set. */
	requested_permissions =
		(vma->vm_flags & (VM_WRITE | VM_READ | VM_EXEC));
	if (requested_permissions & ~(bar_permissions)) {
		gasket_log_info(
			gasket_dev,
			"Attempting to map a region with requested permissions "
			"0x%x, but region has permissions 0x%x.",
			requested_permissions, bar_permissions);
		return false;
	}

	/* Do not allow a non-owner to write. */
	if ((vma->vm_flags & VM_WRITE) &&
	    !gasket_owned_by_current_tgid(&gasket_dev->dev_info)) {
		gasket_log_info(
			gasket_dev,
			"Attempting to mmap a region for write without owning "
			"device.");
		return false;
	}

	return true;
}

/*
 * Checks if an address is within the region
 * allocated for coherent buffer.
 * @driver_desc: driver description.
 * @address: offset of address to check.
 *
 * Verifies that the input address is within the region allocated to coherent
 * buffer.
 */
static bool gasket_is_coherent_region(
	const struct gasket_driver_desc *driver_desc, ulong address)
{
	struct gasket_coherent_buffer_desc coh_buff_desc =
		driver_desc->coherent_buffer_description;

	if (coh_buff_desc.permissions != GASKET_NOMAP) {
		if ((address >= coh_buff_desc.base) &&
		    (address < coh_buff_desc.base + coh_buff_desc.size)) {
			return true;
		}
	}
	return false;
}

static int gasket_get_bar_index(
	const struct gasket_dev *gasket_dev, ulong phys_addr)
{
	int i;
	const struct gasket_driver_desc *driver_desc;

	driver_desc = gasket_dev->internal_desc->driver_desc;
	for (i = 0; i < GASKET_NUM_BARS; ++i) {
		struct gasket_bar_desc bar_desc =
			driver_desc->bar_descriptions[i];

		if (bar_desc.permissions != GASKET_NOMAP) {
			if (phys_addr >= bar_desc.base &&
			    phys_addr < (bar_desc.base + bar_desc.size)) {
				return i;
			}
		}
	}
	/* If we haven't found the address by now, it is invalid. */
	return -EINVAL;
}

/*
 * Sets the actual bounds to map, given the device's mappable region.
 *
 * Given the device's mappable region, along with the user-requested mapping
 * start offset and length of the user region, determine how much of this
 * mappable region can be mapped into the user's region (start/end offsets),
 * and the physical offset (phys_offset) into the BAR where the mapping should
 * begin (either the VMA's or region lower bound).
 *
 * In other words, this calculates the overlap between the VMA
 * (bar_offset, requested_length) and the given gasket_mappable_region.
 *
 * Returns true if there's anything to map, and false otherwise.
 */
static bool gasket_mm_get_mapping_addrs(
	const struct gasket_mappable_region *region, ulong bar_offset,
	ulong requested_length, struct gasket_mappable_region *mappable_region,
	ulong *virt_offset)
{
	ulong range_start = region->start;
	ulong range_length = region->length_bytes;
	ulong range_end = range_start + range_length;

	*virt_offset = 0;
	if (bar_offset + requested_length < range_start) {
		/*
		 * If the requested region is completely below the range,
		 * there is nothing to map.
		 */
		return false;
	} else if (bar_offset <= range_start) {
		/* If the bar offset is below this range's start
		 * but the requested length continues into it:
		 * 1) Only map starting from the beginning of this
		 *      range's phys. offset, so we don't map unmappable
		 *	memory.
		 * 2) The length of the virtual memory to not map is the
		 *	delta between the bar offset and the
		 *	mappable start (and since the mappable start is
		 *	bigger, start - req.)
		 * 3) The map length is the minimum of the mappable
		 *	requested length (requested_length - virt_offset)
		 *	and the actual mappable length of the range.
		 */
		mappable_region->start = range_start;
		*virt_offset = range_start - bar_offset;
		mappable_region->length_bytes =
			min(requested_length - *virt_offset, range_length);
		return true;
	} else if (bar_offset > range_start &&
		   bar_offset < range_end) {
		/*
		 * If the bar offset is within this range:
		 * 1) Map starting from the bar offset.
		 * 2) Because there is no forbidden memory between the
		 *	bar offset and the range start,
		 *	virt_offset is 0.
		 * 3) The map length is the minimum of the requested
		 *	length and the remaining length in the buffer
		 *	(range_end - bar_offset)
		 */
		mappable_region->start = bar_offset;
		*virt_offset = 0;
		mappable_region->length_bytes = min(
			requested_length, range_end - bar_offset);
		return true;
	}

	/*
	 * If the requested [start] offset is above range_end,
	 * there's nothing to map.
	 */
	return false;
}

int gasket_mm_unmap_region(
	const struct gasket_dev *gasket_dev, struct vm_area_struct *vma,
	const struct gasket_mappable_region *map_region)
{
	ulong bar_offset;
	ulong virt_offset;
	struct gasket_mappable_region mappable_region;
	int ret;

	if (map_region->length_bytes == 0)
		return 0;

	ret = gasket_mm_vma_bar_offset(gasket_dev, vma, &bar_offset);
	if (ret)
		return ret;

	if (!gasket_mm_get_mapping_addrs(
		map_region, bar_offset, vma->vm_end - vma->vm_start,
		&mappable_region, &virt_offset))
		return 1;

	/*
	 * The length passed to zap_vma_ptes MUST BE A MULTIPLE OF
	 * PAGE_SIZE! Trust me. I have the scars.
	 *
	 * Next multiple of y: ceil_div(x, y) * y
	 */
	zap_vma_ptes(
		vma, vma->vm_start + virt_offset,
		DIV_ROUND_UP(mappable_region.length_bytes, PAGE_SIZE) *
			PAGE_SIZE);
	return 0;
}
EXPORT_SYMBOL(gasket_mm_unmap_region);

/* Maps a virtual address + range to a physical offset of a BAR. */
static enum do_map_region_status do_map_region(
	const struct gasket_dev *gasket_dev, struct vm_area_struct *vma,
	struct gasket_mappable_region *mappable_region)
{
	/* Maximum size of a single call to io_remap_pfn_range. */
	/* I pulled this number out of thin air. */
	const ulong max_chunk_size = 64 * 1024 * 1024;
	ulong chunk_size, mapped_bytes = 0;

	const struct gasket_driver_desc *driver_desc =
		gasket_dev->internal_desc->driver_desc;

	ulong bar_offset, virt_offset;
	struct gasket_mappable_region region_to_map;
	ulong phys_offset, map_length;
	ulong virt_base, phys_base;
	int bar_index, ret;

	ret = gasket_mm_vma_bar_offset(gasket_dev, vma, &bar_offset);
	if (ret)
		return DO_MAP_REGION_INVALID;

	if (!gasket_mm_get_mapping_addrs(mappable_region, bar_offset,
					 vma->vm_end - vma->vm_start,
					 &region_to_map, &virt_offset))
		return DO_MAP_REGION_INVALID;
	phys_offset = region_to_map.start;
	map_length = region_to_map.length_bytes;

	virt_base = vma->vm_start + virt_offset;
	bar_index =
		gasket_get_bar_index(
			gasket_dev,
			(vma->vm_pgoff << PAGE_SHIFT) +
				driver_desc->legacy_mmap_address_offset);
	phys_base = gasket_dev->bar_data[bar_index].phys_base + phys_offset;
	while (mapped_bytes < map_length) {
		/*
		 * io_remap_pfn_range can take a while, so we chunk its
		 * calls and call cond_resched between each.
		 */
		chunk_size = min(max_chunk_size, map_length - mapped_bytes);

		cond_resched();
		ret = io_remap_pfn_range(
			vma, virt_base + mapped_bytes,
			(phys_base + mapped_bytes) >> PAGE_SHIFT,
			chunk_size, vma->vm_page_prot);
		if (ret) {
			gasket_log_error(
				gasket_dev, "Error remapping PFN range.");
			goto fail;
		}
		mapped_bytes += chunk_size;
	}

	return DO_MAP_REGION_SUCCESS;

fail:
	/* Unmap the partial chunk we mapped. */
	mappable_region->length_bytes = mapped_bytes;
	if (gasket_mm_unmap_region(gasket_dev, vma, mappable_region))
		gasket_log_error(
			gasket_dev,
			"Error unmapping partial region 0x%lx (0x%lx bytes)",
			(ulong)virt_offset,
			(ulong)mapped_bytes);

	return DO_MAP_REGION_FAILURE;
}

/*
 * Calculates the offset where the VMA range begins in its containing BAR.
 * The offset is written into bar_offset on success.
 * Returns zero on success, anything else on error.
 */
static int gasket_mm_vma_bar_offset(
	const struct gasket_dev *gasket_dev, const struct vm_area_struct *vma,
	ulong *bar_offset)
{
	ulong raw_offset;
	int bar_index;
	const struct gasket_driver_desc *driver_desc =
		gasket_dev->internal_desc->driver_desc;

	raw_offset = (vma->vm_pgoff << PAGE_SHIFT) +
		driver_desc->legacy_mmap_address_offset;
	bar_index = gasket_get_bar_index(gasket_dev, raw_offset);
	if (bar_index < 0) {
		gasket_log_error(
			gasket_dev,
			"Unable to find matching bar for address 0x%lx",
			raw_offset);
		trace_gasket_mmap_exit(bar_index);
		return bar_index;
	}
	*bar_offset =
		raw_offset - driver_desc->bar_descriptions[bar_index].base;

	return 0;
}

/*
 * Map a region of coherent memory.
 * @gasket_dev: Gasket device handle.
 * @vma: Virtual memory area descriptor with region to map.
 */
static int gasket_mmap_coherent(
	struct gasket_dev *gasket_dev, struct vm_area_struct *vma)
{
	const struct gasket_driver_desc *driver_desc =
		gasket_dev->internal_desc->driver_desc;
	const ulong requested_length = vma->vm_end - vma->vm_start;
	int ret;
	ulong permissions;

	if (requested_length == 0 || requested_length >
	    gasket_dev->coherent_buffer.length_bytes) {
		trace_gasket_mmap_exit(-EINVAL);
		return -EINVAL;
	}

	permissions = driver_desc->coherent_buffer_description.permissions;
	if (!gasket_mmap_has_permissions(gasket_dev, vma, permissions)) {
		gasket_log_error(gasket_dev, "Permission checking failed.");
		trace_gasket_mmap_exit(-EPERM);
		return -EPERM;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	ret = remap_pfn_range(
		vma, vma->vm_start,
		(gasket_dev->coherent_buffer.phys_base) >> PAGE_SHIFT,
		requested_length, vma->vm_page_prot);
	if (ret) {
		gasket_log_error(
			gasket_dev, "Error remapping PFN range err=%d.", ret);
		trace_gasket_mmap_exit(ret);
		return ret;
	}

	/* Record the user virtual to dma_address mapping that was
	 * created by the kernel.
	 */
	gasket_set_user_virt(
		gasket_dev, requested_length,
		gasket_dev->coherent_buffer.phys_base, vma->vm_start);
	return 0;
}

/*
 * Maps a device's BARs into user space.
 * @filp: File structure pointer describing this node usage session.
 * @vma: Standard virtual memory area descriptor.
 *
 * Maps the entirety of each of the device's BAR ranges into the user memory
 * range specified by vma.
 *
 * Returns 0 on success, a negative errno on error.
 */
static int gasket_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int i, ret;
	int bar_index;
	int has_mapped_anything = 0;
	ulong permissions;
	ulong raw_offset, vma_size;
	bool is_coherent_region;
	const struct gasket_driver_desc *driver_desc;
	struct gasket_dev *gasket_dev = (struct gasket_dev *)filp->private_data;
	const struct gasket_bar_desc *bar_desc;
	struct gasket_mappable_region *map_regions = NULL;
	int num_map_regions = 0;
	enum do_map_region_status map_status;

	if (!gasket_dev) {
		gasket_nodev_error("Unable to retrieve device data");
		trace_gasket_mmap_exit(-EINVAL);
		return -EINVAL;
	}
	driver_desc = gasket_dev->internal_desc->driver_desc;

	if (vma->vm_start & ~PAGE_MASK) {
		gasket_log_error(
			gasket_dev, "Base address not page-aligned: 0x%p\n",
			(void *)vma->vm_start);
		trace_gasket_mmap_exit(-EINVAL);
		return -EINVAL;
	}

	/* Calculate the offset of this range into physical mem. */
	raw_offset = (vma->vm_pgoff << PAGE_SHIFT) +
		driver_desc->legacy_mmap_address_offset;
	vma_size = vma->vm_end - vma->vm_start;
	trace_gasket_mmap_entry(
		gasket_dev->dev_info.name, raw_offset, vma_size);

	/*
	 * Check if the raw offset is within a bar region. If not, check if it
	 * is a coherent region.
	 */
	bar_index = gasket_get_bar_index(gasket_dev, raw_offset);
	is_coherent_region = gasket_is_coherent_region(driver_desc, raw_offset);
	if (bar_index < 0 && !is_coherent_region) {
		gasket_log_error(
			gasket_dev,
			"Unable to find matching bar for address 0x%lx",
			raw_offset);
		trace_gasket_mmap_exit(bar_index);
		return bar_index;
	}
	if (bar_index > 0 && is_coherent_region) {
		gasket_log_error(
			gasket_dev,
			"double matching bar and coherent buffers for address "
			"0x%lx",
			raw_offset);
		trace_gasket_mmap_exit(bar_index);
		return -EINVAL;
	}

	vma->vm_private_data = gasket_dev;

	if (is_coherent_region)
		return gasket_mmap_coherent(gasket_dev, vma);

	/* Everything in the rest of this function is for normal BAR mapping. */

	/*
	 * Subtract the base of the bar from the raw offset to get the
	 * memory location within the bar to map.
	 */
	bar_desc = &driver_desc->bar_descriptions[bar_index];
	permissions = bar_desc->permissions;
	if (!gasket_mmap_has_permissions(gasket_dev, vma, permissions)) {
		gasket_log_error(gasket_dev, "Permission checking failed.");
		trace_gasket_mmap_exit(-EPERM);
		return -EPERM;
	}

	if (driver_desc->get_mappable_regions_cb) {
		ret = driver_desc->get_mappable_regions_cb(
			gasket_dev, bar_index, &map_regions, &num_map_regions);
		if (ret)
			return ret;
	} else {
		if (!gasket_mmap_has_permissions(gasket_dev, vma,
						 bar_desc->permissions)) {
			gasket_log_error(
				gasket_dev, "Permission checking failed.");
			trace_gasket_mmap_exit(-EPERM);
			return -EPERM;
		}
		num_map_regions = bar_desc->num_mappable_regions;
		map_regions = kcalloc(num_map_regions,
				      sizeof(*bar_desc->mappable_regions),
				      GFP_KERNEL);
		if (map_regions) {
			memcpy(map_regions, bar_desc->mappable_regions,
			       num_map_regions *
					sizeof(*bar_desc->mappable_regions));
		}
	}

	if (!map_regions || num_map_regions == 0) {
		gasket_log_error(gasket_dev, "No mappable regions returned!");
		return -EINVAL;
	}

	/* Marks the VMA's pages as uncacheable. */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	for (i = 0; i < num_map_regions; i++) {
		map_status = do_map_region(gasket_dev, vma, &map_regions[i]);
		/* Try the next region if this one was not mappable. */
		if (map_status == DO_MAP_REGION_INVALID)
			continue;
		if (map_status == DO_MAP_REGION_FAILURE) {
			ret = -ENOMEM;
			goto fail;
		}

		has_mapped_anything = 1;
	}

	kfree(map_regions);

	/* If we could not map any memory, the request was invalid. */
	if (!has_mapped_anything) {
		gasket_log_error(
			gasket_dev,
			"Map request did not contain a valid region.");
		trace_gasket_mmap_exit(-EINVAL);
		return -EINVAL;
	}

	trace_gasket_mmap_exit(0);
	return 0;

fail:
	/* Need to unmap any mapped ranges. */
	num_map_regions = i;
	for (i = 0; i < num_map_regions; i++)
		if (gasket_mm_unmap_region(gasket_dev, vma,
					   &bar_desc->mappable_regions[i]))
			gasket_log_error(
				gasket_dev, "Error unmapping range %d.", i);
	kfree(map_regions);

	return ret;
}

/*
 * Determine the health of the Gasket device.
 * @gasket_dev: Gasket device structure.
 *
 * Checks the underlying device health (via the device_status_cb)
 * and the status of initialized Gasket code systems (currently
 * only interrupts), then returns a gasket_status appropriately.
 */
static int gasket_get_hw_status(struct gasket_dev *gasket_dev)
{
	int status;
	int i;
	const struct gasket_driver_desc *driver_desc =
		gasket_dev->internal_desc->driver_desc;

	status = gasket_check_and_invoke_callback_nolock(
		gasket_dev, driver_desc->device_status_cb);
	if (status != GASKET_STATUS_ALIVE) {
		gasket_log_info(gasket_dev, "Hardware reported status %d.",
				status);
		return status;
	}

	status = gasket_interrupt_system_status(gasket_dev);
	if (status != GASKET_STATUS_ALIVE) {
		gasket_log_info(gasket_dev,
				"Interrupt system reported status %d.", status);
		return status;
	}

	for (i = 0; i < driver_desc->num_page_tables; ++i) {
		status = gasket_page_table_system_status(
			gasket_dev->page_table[i]);
		if (status != GASKET_STATUS_ALIVE) {
			gasket_log_info(
				gasket_dev, "Page table %d reported status %d.",
				i, status);
			return status;
		}
	}

	return GASKET_STATUS_ALIVE;
}

/*
 * Gasket ioctl dispatch function.
 * @filp: File structure pointer describing this node usage session.
 * @cmd: ioctl number to handle.
 * @arg: ioctl-specific data pointer.
 *
 * First, checks if the ioctl is a generic ioctl. If not, it passes
 * the ioctl to the ioctl_handler_cb registered in the driver description.
 * If the ioctl is a generic ioctl, the function passes it to the
 * gasket_ioctl_handler in gasket_ioctl.c.
 */
static long gasket_ioctl(struct file *filp, uint cmd, ulong arg)
{
	struct gasket_dev *gasket_dev;
	const struct gasket_driver_desc *driver_desc;
	char path[256];

	if (!filp)
		return -ENODEV;

	gasket_dev = (struct gasket_dev *)filp->private_data;
	if (!gasket_dev) {
		gasket_nodev_error(
			"Unable to find Gasket structure for file %s",
			d_path(&filp->f_path, path, 256));
		return -ENODEV;
	}

	driver_desc = gasket_dev->internal_desc->driver_desc;
	if (!driver_desc) {
		gasket_log_error(
			gasket_dev,
			"Unable to find device descriptor for file %s",
			d_path(&filp->f_path, path, 256));
		return -ENODEV;
	}

	if (!gasket_is_supported_ioctl(cmd)) {
		/*
		 * The ioctl handler is not a standard Gasket callback, since
		 * it requires different arguments. This means we can't use
		 * check_and_invoke_callback.
		 */
		if (driver_desc->ioctl_handler_cb)
			return driver_desc->ioctl_handler_cb(filp, cmd, arg);

		gasket_log_error(
			gasket_dev, "Received unknown ioctl 0x%x", cmd);
		return -EINVAL;
	}

	return gasket_handle_ioctl(filp, cmd, arg);
}

int gasket_reset(struct gasket_dev *gasket_dev, uint reset_type)
{
	int ret;

	mutex_lock(&gasket_dev->mutex);
	ret = gasket_reset_nolock(gasket_dev, reset_type);
	mutex_unlock(&gasket_dev->mutex);
	return ret;
}
EXPORT_SYMBOL(gasket_reset);

int gasket_reset_nolock(struct gasket_dev *gasket_dev, uint reset_type)
{
	int ret;
	int i;
	const struct gasket_driver_desc *driver_desc;

	driver_desc = gasket_dev->internal_desc->driver_desc;
	if (!driver_desc->device_reset_cb)
		return 0;

	/* Perform a device reset of the requested type. */
	ret = driver_desc->device_reset_cb(gasket_dev, reset_type);
	if (ret) {
		gasket_log_error(
			gasket_dev, "Device reset cb returned %d.", ret);
		return ret;
	}

	/* Reinitialize the page tables and interrupt framework. */
	for (i = 0; i < driver_desc->num_page_tables; ++i)
		gasket_page_table_reset(gasket_dev->page_table[i]);

	ret = gasket_interrupt_reinit(gasket_dev);
	if (ret) {
		gasket_log_error(
			gasket_dev, "Unable to reinit interrupts: %d.", ret);
		return ret;
	}

	/* Get current device health. */
	gasket_dev->status = gasket_get_hw_status(gasket_dev);
	if (gasket_dev->status == GASKET_STATUS_DEAD) {
		gasket_log_error(gasket_dev, "Device reported as dead.");
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(gasket_reset_nolock);

gasket_ioctl_permissions_cb_t gasket_get_ioctl_permissions_cb(
	struct gasket_dev *gasket_dev)
{
	return gasket_dev->internal_desc->driver_desc->ioctl_permissions_cb;
}
EXPORT_SYMBOL(gasket_get_ioctl_permissions_cb);

static ssize_t gasket_write_mappable_regions(
	char *buf, const struct gasket_driver_desc *driver_desc, int bar_index)
{
	int i;
	ssize_t written;
	ssize_t total_written = 0;
	ulong min_addr, max_addr;
	struct gasket_bar_desc bar_desc =
		driver_desc->bar_descriptions[bar_index];

	if (bar_desc.permissions == GASKET_NOMAP)
		return 0;
	for (i = 0;
	     i < bar_desc.num_mappable_regions && total_written < PAGE_SIZE;
	     i++) {
		min_addr = bar_desc.mappable_regions[i].start -
			   driver_desc->legacy_mmap_address_offset;
		max_addr = bar_desc.mappable_regions[i].start -
			   driver_desc->legacy_mmap_address_offset +
			   bar_desc.mappable_regions[i].length_bytes;
		written = scnprintf(buf, PAGE_SIZE - total_written,
				    "0x%08lx-0x%08lx\n", min_addr, max_addr);
		total_written += written;
		buf += written;
	}
	return total_written;
}

static ssize_t gasket_sysfs_data_show(
	struct device *device, struct device_attribute *attr, char *buf)
{
	int i, ret = 0;
	ssize_t current_written = 0;
	const struct gasket_driver_desc *driver_desc;
	struct gasket_dev *gasket_dev;
	struct gasket_sysfs_attribute *gasket_attr;
	const struct gasket_bar_desc *bar_desc;
	enum gasket_sysfs_attribute_type sysfs_type;

	gasket_dev = gasket_sysfs_get_device_data(device);
	if (!gasket_dev) {
		gasket_nodev_error(
			"No sysfs mapping found for device 0x%p", device);
		return 0;
	}

	gasket_attr = gasket_sysfs_get_attr(device, attr);
	if (!gasket_attr) {
		gasket_nodev_error(
			"No sysfs attr found for device 0x%p", device);
		gasket_sysfs_put_device_data(device, gasket_dev);
		return 0;
	}

	driver_desc = gasket_dev->internal_desc->driver_desc;

	sysfs_type =
		(enum gasket_sysfs_attribute_type)gasket_attr->data.attr_type;
	switch (sysfs_type) {
	case ATTR_BAR_OFFSETS:
		for (i = 0; i < GASKET_NUM_BARS; i++) {
			bar_desc = &driver_desc->bar_descriptions[i];
			if (bar_desc->size == 0)
				continue;
			current_written =
				snprintf(buf, PAGE_SIZE - ret, "%d: 0x%lx\n", i,
					 (ulong)bar_desc->base);
			buf += current_written;
			ret += current_written;
		}
		break;
	case ATTR_BAR_SIZES:
		for (i = 0; i < GASKET_NUM_BARS; i++) {
			bar_desc = &driver_desc->bar_descriptions[i];
			if (bar_desc->size == 0)
				continue;
			current_written =
				snprintf(buf, PAGE_SIZE - ret, "%d: 0x%lx\n", i,
					 (ulong)bar_desc->size);
			buf += current_written;
			ret += current_written;
		}
		break;
	case ATTR_DRIVER_VERSION:
		ret = snprintf(
			buf, PAGE_SIZE, "%s\n",
			gasket_dev->internal_desc->driver_desc->driver_version);
		break;
	case ATTR_FRAMEWORK_VERSION:
		ret = snprintf(
			buf, PAGE_SIZE, "%s\n", GASKET_FRAMEWORK_VERSION);
		break;
	case ATTR_DEVICE_TYPE:
		ret = snprintf(
			buf, PAGE_SIZE, "%s\n",
			gasket_dev->internal_desc->driver_desc->name);
		break;
	case ATTR_HARDWARE_REVISION:
		ret = snprintf(
			buf, PAGE_SIZE, "%d\n", gasket_dev->hardware_revision);
		break;
	case ATTR_PCI_ADDRESS:
		ret = snprintf(buf, PAGE_SIZE, "%s\n", gasket_dev->kobj_name);
		break;
	case ATTR_STATUS:
		ret = snprintf(
			buf, PAGE_SIZE, "%s\n",
			gasket_num_name_lookup(
				gasket_dev->status, gasket_status_name_table));
		break;
	case ATTR_IS_DEVICE_OWNED:
		ret = snprintf(
			buf, PAGE_SIZE, "%d\n",
			gasket_dev->dev_info.ownership.is_owned);
		break;
	case ATTR_DEVICE_OWNER:
		ret = snprintf(
			buf, PAGE_SIZE, "%d\n",
			gasket_dev->dev_info.ownership.owner);
		break;
	case ATTR_WRITE_OPEN_COUNT:
		ret = snprintf(
			buf, PAGE_SIZE, "%d\n",
			gasket_dev->dev_info.ownership.write_open_count);
		break;
	case ATTR_RESET_COUNT:
		ret = snprintf(buf, PAGE_SIZE, "%d\n", gasket_dev->reset_count);
		break;
	case ATTR_USER_MEM_RANGES:
		for (i = 0; i < GASKET_NUM_BARS; ++i) {
			current_written = gasket_write_mappable_regions(
				buf, driver_desc, i);
			buf += current_written;
			ret += current_written;
		}
		break;
	default:
		gasket_log_error(
			gasket_dev, "Unknown attribute: %s", attr->attr.name);
		ret = 0;
		break;
	}

	gasket_sysfs_put_attr(device, gasket_attr);
	gasket_sysfs_put_device_data(device, gasket_dev);
	return ret;
}

/* Get the driver structure for a given gasket_dev.
 * @dev: pointer to gasket_dev, implementing the requested driver.
 */
const struct gasket_driver_desc *gasket_get_driver_desc(struct gasket_dev *dev)
{
	return dev->internal_desc->driver_desc;
}

/* Get the device structure for a given gasket_dev.
 * @dev: pointer to gasket_dev, implementing the requested driver.
 */
struct device *gasket_get_device(struct gasket_dev *dev)
{
	if (dev->pci_dev)
		return &dev->pci_dev->dev;
	return NULL;
}

/**
 * Asynchronously waits on device.
 * @gasket_dev: Device struct.
 * @bar: Bar
 * @offset: Register offset
 * @mask: Register mask
 * @val: Expected value
 * @max_retries: number of sleep periods
 * @delay_ms: Timeout in milliseconds
 *
 * Description: Busy waits for a specific combination of bits to be set on a
 * Gasket register.
 **/
int gasket_wait_with_reschedule(
	struct gasket_dev *gasket_dev, int bar, u64 offset, u64 mask, u64 val,
	uint max_retries, u64 delay_ms)
{
	uint retries = 0;
	u64 tmp;

	while (retries < max_retries) {
		tmp = gasket_dev_read_64(gasket_dev, bar, offset);
		if ((tmp & mask) == val)
			return 0;
		msleep(delay_ms);
		retries++;
	}
	gasket_log_error(gasket_dev, "%s timeout: reg %llx timeout (%llu ms)",
			 __func__, offset, max_retries * delay_ms);
	return -ETIMEDOUT;
}
EXPORT_SYMBOL(gasket_wait_with_reschedule);
