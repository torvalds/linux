/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Gasket generic driver. Defines the set of data types and functions necessary
 * to define a driver using the Gasket generic driver framework.
 *
 * Copyright (C) 2018 Google, Inc.
 */
#ifndef __GASKET_CORE_H__
#define __GASKET_CORE_H__

#include <linux/cdev.h>
#include <linux/compiler.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include "gasket_constants.h"

/**
 * struct gasket_num_name - Map numbers to names.
 * @ein_num: Number.
 * @ein_name: Name associated with the number, a char pointer.
 *
 * This structure maps numbers to names. It is used to provide printable enum
 * names, e.g {0, "DEAD"} or {1, "ALIVE"}.
 */
struct gasket_num_name {
	uint snn_num;
	const char *snn_name;
};

/*
 * Register location for packed interrupts.
 * Each value indicates the location of an interrupt field (in units of
 * gasket_driver_desc->interrupt_pack_width) within the containing register.
 * In other words, this indicates the shift to use when creating a mask to
 * extract/set bits within a register for a given interrupt.
 */
enum gasket_interrupt_packing {
	PACK_0 = 0,
	PACK_1 = 1,
	PACK_2 = 2,
	PACK_3 = 3,
	UNPACKED = 4,
};

/* Type of the interrupt supported by the device. */
enum gasket_interrupt_type {
	PCI_MSIX = 0,
	PCI_MSI = 1,
	PLATFORM_WIRE = 2,
};

/*
 * Used to describe a Gasket interrupt. Contains an interrupt index, a register,
 * and packing data for that interrupt. The register and packing data
 * fields are relevant only for PCI_MSIX interrupt type and can be
 * set to 0 for everything else.
 */
struct gasket_interrupt_desc {
	/* Device-wide interrupt index/number. */
	int index;
	/* The register offset controlling this interrupt. */
	u64 reg;
	/* The location of this interrupt inside register reg, if packed. */
	int packing;
};

/* Offsets to the wire interrupt handling registers */
struct gasket_wire_interrupt_offsets {
	u64 pending_bit_array;
	u64 mask_array;
};

/*
 * This enum is used to identify memory regions being part of the physical
 * memory that belongs to a device.
 */
enum mappable_area_type {
	PCI_BAR = 0, /* Default */
	BUS_REGION,  /* For SYSBUS devices, i.e. AXI etc... */
	COHERENT_MEMORY
};

/*
 * Metadata for each BAR mapping.
 * This struct is used so as to track PCI memory, I/O space, AXI and coherent
 * memory area... i.e. memory objects which can be referenced in the device's
 * mmap function.
 */
struct gasket_bar_data {
	/* Virtual base address. */
	u8 __iomem *virt_base;

	/* Physical base address. */
	ulong phys_base;

	/* Length of the mapping. */
	ulong length_bytes;

	/* Type of mappable area */
	enum mappable_area_type type;
};

/* Maintains device open ownership data. */
struct gasket_ownership {
	/* 1 if the device is owned, 0 otherwise. */
	int is_owned;

	/* TGID of the owner. */
	pid_t owner;

	/* Count of current device opens in write mode. */
	int write_open_count;
};

/* Page table modes of operation. */
enum gasket_page_table_mode {
	/* The page table is partitionable as normal, all simple by default. */
	GASKET_PAGE_TABLE_MODE_NORMAL,

	/* All entries are always simple. */
	GASKET_PAGE_TABLE_MODE_SIMPLE,

	/* All entries are always extended. No extended bit is used. */
	GASKET_PAGE_TABLE_MODE_EXTENDED,
};

/* Page table configuration. One per table. */
struct gasket_page_table_config {
	/* The identifier/index of this page table. */
	int id;

	/* The operation mode of this page table. */
	enum gasket_page_table_mode mode;

	/* Total (first-level) entries in this page table. */
	ulong total_entries;

	/* Base register for the page table. */
	int base_reg;

	/*
	 * Register containing the extended page table. This value is unused in
	 * GASKET_PAGE_TABLE_MODE_SIMPLE and GASKET_PAGE_TABLE_MODE_EXTENDED
	 * modes.
	 */
	int extended_reg;

	/* The bit index indicating whether a PT entry is extended. */
	int extended_bit;
};

/* Maintains information about a device node. */
struct gasket_cdev_info {
	/* The internal name of this device. */
	char name[GASKET_NAME_MAX];

	/* Device number. */
	dev_t devt;

	/* Kernel-internal device structure. */
	struct device *device;

	/* Character device for real. */
	struct cdev cdev;

	/* Flag indicating if cdev_add has been called for the devices. */
	int cdev_added;

	/* Pointer to the overall gasket_dev struct for this device. */
	struct gasket_dev *gasket_dev_ptr;

	/* Ownership data for the device in question. */
	struct gasket_ownership ownership;
};

/* Describes the offset and length of mmapable device BAR regions. */
struct gasket_mappable_region {
	u64 start;
	u64 length_bytes;
};

/* Describe the offset, size, and permissions for a device bar. */
struct gasket_bar_desc {
	/*
	 * The size of each PCI BAR range, in bytes. If a value is 0, that BAR
	 * will not be mapped into kernel space at all.
	 * For devices with 64 bit BARs, only elements 0, 2, and 4 should be
	 * populated, and 1, 3, and 5 should be set to 0.
	 * For example, for a device mapping 1M in each of the first two 64-bit
	 * BARs, this field would be set as { 0x100000, 0, 0x100000, 0, 0, 0 }
	 * (one number per bar_desc struct.)
	 */
	u64 size;
	/* The permissions for this bar. (Should be VM_WRITE/VM_READ/VM_EXEC,
	 * and can be or'd.) If set to GASKET_NOMAP, the bar will
	 * not be used for mmapping.
	 */
	ulong permissions;
	/* The memory address corresponding to the base of this bar, if used. */
	u64 base;
	/* The number of mappable regions in this bar. */
	int num_mappable_regions;

	/* The mappable subregions of this bar. */
	const struct gasket_mappable_region *mappable_regions;

	/* Type of mappable area */
	enum mappable_area_type type;
};

/* Describes the offset, size, and permissions for a coherent buffer. */
struct gasket_coherent_buffer_desc {
	/* The size of the coherent buffer. */
	u64 size;

	/* The permissions for this bar. (Should be VM_WRITE/VM_READ/VM_EXEC,
	 * and can be or'd.) If set to GASKET_NOMAP, the bar will
	 * not be used for mmaping.
	 */
	ulong permissions;

	/* device side address. */
	u64 base;
};

/* Coherent buffer structure. */
struct gasket_coherent_buffer {
	/* Virtual base address. */
	u8 __iomem *virt_base;

	/* Physical base address. */
	ulong phys_base;

	/* Length of the mapping. */
	ulong length_bytes;
};

/* Description of Gasket-specific permissions in the mmap field. */
enum gasket_mapping_options { GASKET_NOMAP = 0 };

/* This struct represents an undefined bar that should never be mapped. */
#define GASKET_UNUSED_BAR                                                      \
	{                                                                      \
		0, GASKET_NOMAP, 0, 0, NULL, 0                                 \
	}

/* Internal data for a Gasket device. See gasket_core.c for more information. */
struct gasket_internal_desc;

#define MAX_NUM_COHERENT_PAGES 16

/*
 * Device data for Gasket device instances.
 *
 * This structure contains the data required to manage a Gasket device.
 */
struct gasket_dev {
	/* Pointer to the internal driver description for this device. */
	struct gasket_internal_desc *internal_desc;

	/* PCI subsystem metadata. */
	struct pci_dev *pci_dev;

	/* This device's index into internal_desc->devs. */
	int dev_idx;

	/* The name of this device, as reported by the kernel. */
	char kobj_name[GASKET_NAME_MAX];

	/* Virtual address of mapped BAR memory range. */
	struct gasket_bar_data bar_data[GASKET_NUM_BARS];

	/* Coherent buffer. */
	struct gasket_coherent_buffer coherent_buffer;

	/* Number of page tables for this device. */
	int num_page_tables;

	/* Address translations. Page tables have a private implementation. */
	struct gasket_page_table *page_table[GASKET_MAX_NUM_PAGE_TABLES];

	/* Interrupt data for this device. */
	struct gasket_interrupt_data *interrupt_data;

	/* Status for this device - GASKET_STATUS_ALIVE or _DEAD. */
	uint status;

	/* Number of times this device has been reset. */
	uint reset_count;

	/* Dev information for the cdev node. */
	struct gasket_cdev_info dev_info;

	/* Hardware revision value for this device. */
	int hardware_revision;

	/*
	 * Device-specific data; allocated in gasket_driver_desc.add_dev_cb()
	 * and freed in gasket_driver_desc.remove_dev_cb().
	 */
	void *cb_data;

	/* Protects access to per-device data (i.e. this structure). */
	struct mutex mutex;

	/* cdev hash tracking/membership structure, Accel and legacy. */
	/* Unused until Accel is upstreamed. */
	struct hlist_node hlist_node;
	struct hlist_node legacy_hlist_node;
};

/* Type of the ioctl handler callback. */
typedef long (*gasket_ioctl_handler_cb_t)
		(struct file *file, uint cmd, void __user *argp);
/* Type of the ioctl permissions check callback. See below. */
typedef int (*gasket_ioctl_permissions_cb_t)(
	struct file *filp, uint cmd, void __user *argp);

/*
 * Device type descriptor.
 *
 * This structure contains device-specific data needed to identify and address a
 * type of device to be administered via the Gasket generic driver.
 *
 * Device IDs are per-driver. In other words, two drivers using the Gasket
 * framework will each have a distinct device 0 (for example).
 */
struct gasket_driver_desc {
	/* The name of this device type. */
	const char *name;

	/* The name of this specific device model. */
	const char *chip_model;

	/* The version of the chip specified in chip_model. */
	const char *chip_version;

	/* The version of this driver: "1.0.0", "2.1.3", etc. */
	const char *driver_version;

	/*
	 * Non-zero if we should create "legacy" (device and device-class-
	 * specific) character devices and sysfs nodes.
	 */
	/* Unused until Accel is upstreamed. */
	int legacy_support;

	/* Major and minor numbers identifying the device. */
	int major, minor;

	/* Module structure for this driver. */
	struct module *module;

	/* PCI ID table. */
	const struct pci_device_id *pci_id_table;

	/* The number of page tables handled by this driver. */
	int num_page_tables;

	/* The index of the bar containing the page tables. */
	int page_table_bar_index;

	/* Registers used to control each page table. */
	const struct gasket_page_table_config *page_table_configs;

	/* The bit index indicating whether a PT entry is extended. */
	int page_table_extended_bit;

	/*
	 * Legacy mmap address adjusment for legacy devices only. Should be 0
	 * for any new device.
	 */
	ulong legacy_mmap_address_offset;

	/* Set of 6 bar descriptions that describe all PCIe bars.
	 * Note that BUS/AXI devices (i.e. non PCI devices) use those.
	 */
	struct gasket_bar_desc bar_descriptions[GASKET_NUM_BARS];

	/*
	 * Coherent buffer description.
	 */
	struct gasket_coherent_buffer_desc coherent_buffer_description;

	/* Offset of wire interrupt registers. */
	const struct gasket_wire_interrupt_offsets *wire_interrupt_offsets;

	/* Interrupt type. (One of gasket_interrupt_type). */
	int interrupt_type;

	/* Index of the bar containing the interrupt registers to program. */
	int interrupt_bar_index;

	/* Number of interrupts in the gasket_interrupt_desc array */
	int num_interrupts;

	/* Description of the interrupts for this device. */
	const struct gasket_interrupt_desc *interrupts;

	/*
	 * If this device packs multiple interrupt->MSI-X mappings into a
	 * single register (i.e., "uses packed interrupts"), only a single bit
	 * width is supported for each interrupt mapping (unpacked/"full-width"
	 * interrupts are always supported). This value specifies that width. If
	 * packed interrupts are not used, this value is ignored.
	 */
	int interrupt_pack_width;

	/* Driver callback functions - all may be NULL */
	/*
	 * add_dev_cb: Callback when a device is found.
	 * @dev: The gasket_dev struct for this driver instance.
	 *
	 * This callback should initialize the device-specific cb_data.
	 * Called when a device is found by the driver,
	 * before any BAR ranges have been mapped. If this call fails (returns
	 * nonzero), remove_dev_cb will be called.
	 *
	 */
	int (*add_dev_cb)(struct gasket_dev *dev);

	/*
	 * remove_dev_cb: Callback for when a device is removed from the system.
	 * @dev: The gasket_dev struct for this driver instance.
	 *
	 * This callback should free data allocated in add_dev_cb.
	 * Called immediately before a device is unregistered by the driver.
	 * All framework-managed resources will have been cleaned up by the time
	 * this callback is invoked (PCI BARs, character devices, ...).
	 */
	int (*remove_dev_cb)(struct gasket_dev *dev);

	/*
	 * device_open_cb: Callback for when a device node is opened in write
	 * mode.
	 * @dev: The gasket_dev struct for this driver instance.
	 *
	 * This callback should perform device-specific setup that needs to
	 * occur only once when a device is first opened.
	 */
	int (*device_open_cb)(struct gasket_dev *dev);

	/*
	 * device_release_cb: Callback when a device is closed.
	 * @gasket_dev: The gasket_dev struct for this driver instance.
	 *
	 * This callback is called whenever a device node fd is closed, as
	 * opposed to device_close_cb, which is called when the _last_
	 * descriptor for an open file is closed. This call is intended to
	 * handle any per-user or per-fd cleanup.
	 */
	int (*device_release_cb)(
		struct gasket_dev *gasket_dev, struct file *file);

	/*
	 * device_close_cb: Callback for when a device node is closed for the
	 * last time.
	 * @dev: The gasket_dev struct for this driver instance.
	 *
	 * This callback should perform device-specific cleanup that only
	 * needs to occur when the last reference to a device node is closed.
	 *
	 * This call is intended to handle and device-wide cleanup, as opposed
	 * to per-fd cleanup (which should be handled by device_release_cb).
	 */
	int (*device_close_cb)(struct gasket_dev *dev);

	/*
	 * enable_dev_cb: Callback immediately before enabling the device.
	 * @dev: Pointer to the gasket_dev struct for this driver instance.
	 *
	 * This callback is invoked after the device has been added and all BAR
	 * spaces mapped, immediately before registering and enabling the
	 * [character] device via cdev_add. If this call fails (returns
	 * nonzero), disable_dev_cb will be called.
	 *
	 * Note that cdev are initialized but not active
	 * (cdev_add has not yet been called) when this callback is invoked.
	 */
	int (*enable_dev_cb)(struct gasket_dev *dev);

	/*
	 * disable_dev_cb: Callback immediately after disabling the device.
	 * @dev: Pointer to the gasket_dev struct for this driver instance.
	 *
	 * Called during device shutdown, immediately after disabling device
	 * operations via cdev_del.
	 */
	int (*disable_dev_cb)(struct gasket_dev *dev);

	/*
	 * sysfs_setup_cb: Callback to set up driver-specific sysfs nodes.
	 * @dev: Pointer to the gasket_dev struct for this device.
	 *
	 * Called just before enable_dev_cb.
	 *
	 */
	int (*sysfs_setup_cb)(struct gasket_dev *dev);

	/*
	 * sysfs_cleanup_cb: Callback to clean up driver-specific sysfs nodes.
	 * @dev: Pointer to the gasket_dev struct for this device.
	 *
	 * Called just before disable_dev_cb.
	 *
	 */
	int (*sysfs_cleanup_cb)(struct gasket_dev *dev);

	/*
	 * get_mappable_regions_cb: Get descriptors of mappable device memory.
	 * @gasket_dev: Pointer to the struct gasket_dev for this device.
	 * @bar_index: BAR for which to retrieve memory ranges.
	 * @mappable_regions: Out-pointer to the list of mappable regions on the
	 * device/BAR for this process.
	 * @num_mappable_regions: Out-pointer for the size of mappable_regions.
	 *
	 * Called when handling mmap(), this callback is used to determine which
	 * regions of device memory may be mapped by the current process. This
	 * information is then compared to mmap request to determine which
	 * regions to actually map.
	 */
	int (*get_mappable_regions_cb)(
		struct gasket_dev *gasket_dev, int bar_index,
		struct gasket_mappable_region **mappable_regions,
		int *num_mappable_regions);

	/*
	 * ioctl_permissions_cb: Check permissions for generic ioctls.
	 * @filp: File structure pointer describing this node usage session.
	 * @cmd: ioctl number to handle.
	 * @arg: ioctl-specific data pointer.
	 *
	 * Returns 1 if the ioctl may be executed, 0 otherwise. If this callback
	 * isn't specified a default routine will be used, that only allows the
	 * original device opener (i.e, the "owner") to execute state-affecting
	 * ioctls.
	 */
	gasket_ioctl_permissions_cb_t ioctl_permissions_cb;

	/*
	 * ioctl_handler_cb: Callback to handle device-specific ioctls.
	 * @filp: File structure pointer describing this node usage session.
	 * @cmd: ioctl number to handle.
	 * @arg: ioctl-specific data pointer.
	 *
	 * Invoked whenever an ioctl is called that the generic Gasket
	 * framework doesn't support. If no cb is registered, unknown ioctls
	 * return -EINVAL. Should return an error status (either -EINVAL or
	 * the error result of the ioctl being handled).
	 */
	gasket_ioctl_handler_cb_t ioctl_handler_cb;

	/*
	 * device_status_cb: Callback to determine device health.
	 * @dev: Pointer to the gasket_dev struct for this device.
	 *
	 * Called to determine if the device is healthy or not. Should return
	 * a member of the gasket_status_type enum.
	 *
	 */
	int (*device_status_cb)(struct gasket_dev *dev);

	/*
	 * hardware_revision_cb: Get the device's hardware revision.
	 * @dev: Pointer to the gasket_dev struct for this device.
	 *
	 * Called to determine the reported rev of the physical hardware.
	 * Revision should be >0. A negative return value is an error.
	 */
	int (*hardware_revision_cb)(struct gasket_dev *dev);

	/*
	 * device_reset_cb: Reset the hardware in question.
	 * @dev: Pointer to the gasket_dev structure for this device.
	 * @type: Integer representing reset type. (All
	 * Gasket resets have an integer representing their type
	 * defined in (device)_ioctl.h; the specific resets are
	 * device-dependent, but are handled in the device-specific
	 * callback anyways.)
	 *
	 * Called by reset ioctls. This function should not
	 * lock the gasket_dev mutex. It should return 0 on success
	 * and an error on failure.
	 */
	int (*device_reset_cb)(struct gasket_dev *dev, uint reset_type);
};

/*
 * Register the specified device type with the framework.
 * @desc: Populated/initialized device type descriptor.
 *
 * This function does _not_ take ownership of desc; the underlying struct must
 * exist until the matching call to gasket_unregister_device.
 * This function should be called from your driver's module_init function.
 */
int gasket_register_device(const struct gasket_driver_desc *desc);

/*
 * Remove the specified device type from the framework.
 * @desc: Descriptor for the device type to unregister; it should have been
 *        passed to gasket_register_device in a previous call.
 *
 * This function should be called from your driver's module_exit function.
 */
void gasket_unregister_device(const struct gasket_driver_desc *desc);

/*
 * Reset the Gasket device.
 * @gasket_dev: Gasket device struct.
 * @reset_type: Uint representing requested reset type. Should be
 * valid in the underlying callback.
 *
 * Calls device_reset_cb. Returns 0 on success and an error code othewrise.
 * gasket_reset_nolock will not lock the mutex, gasket_reset will.
 *
 */
int gasket_reset(struct gasket_dev *gasket_dev, uint reset_type);
int gasket_reset_nolock(struct gasket_dev *gasket_dev, uint reset_type);

/*
 * Memory management functions. These will likely be spun off into their own
 * file in the future.
 */

/* Unmaps the specified mappable region from a VMA. */
int gasket_mm_unmap_region(
	const struct gasket_dev *gasket_dev, struct vm_area_struct *vma,
	const struct gasket_mappable_region *map_region);

/*
 * Get the ioctl permissions callback.
 * @gasket_dev: Gasket device structure.
 */
gasket_ioctl_permissions_cb_t gasket_get_ioctl_permissions_cb(
	struct gasket_dev *gasket_dev);

/**
 * Lookup a name by number in a num_name table.
 * @num: Number to lookup.
 * @table: Array of num_name structures, the table for the lookup.
 *
 */
const char *gasket_num_name_lookup(
	uint num, const struct gasket_num_name *table);

/* Handy inlines */
static inline ulong gasket_dev_read_64(
	struct gasket_dev *gasket_dev, int bar, ulong location)
{
	return readq(&gasket_dev->bar_data[bar].virt_base[location]);
}

static inline void gasket_dev_write_64(
	struct gasket_dev *dev, u64 value, int bar, ulong location)
{
	writeq(value, &dev->bar_data[bar].virt_base[location]);
}

static inline void gasket_dev_write_32(
	struct gasket_dev *dev, u32 value, int bar, ulong location)
{
	writel(value, &dev->bar_data[bar].virt_base[location]);
}

static inline u32 gasket_dev_read_32(
	struct gasket_dev *dev, int bar, ulong location)
{
	return readl(&dev->bar_data[bar].virt_base[location]);
}

static inline void gasket_read_modify_write_64(
	struct gasket_dev *dev, int bar, ulong location, u64 value,
	u64 mask_width, u64 mask_shift)
{
	u64 mask, tmp;

	tmp = gasket_dev_read_64(dev, bar, location);
	mask = ((1ULL << mask_width) - 1) << mask_shift;
	tmp = (tmp & ~mask) | (value << mask_shift);
	gasket_dev_write_64(dev, tmp, bar, location);
}

static inline void gasket_read_modify_write_32(
	struct gasket_dev *dev, int bar, ulong location, u32 value,
	u32 mask_width, u32 mask_shift)
{
	u32 mask, tmp;

	tmp = gasket_dev_read_32(dev, bar, location);
	mask = ((1 << mask_width) - 1) << mask_shift;
	tmp = (tmp & ~mask) | (value << mask_shift);
	gasket_dev_write_32(dev, tmp, bar, location);
}

/* Get the Gasket driver structure for a given device. */
const struct gasket_driver_desc *gasket_get_driver_desc(struct gasket_dev *dev);

/* Get the device structure for a given device. */
struct device *gasket_get_device(struct gasket_dev *dev);

/* Helper function, Asynchronous waits on a given set of bits. */
int gasket_wait_with_reschedule(
	struct gasket_dev *gasket_dev, int bar, u64 offset, u64 mask, u64 val,
	uint max_retries, u64 delay_ms);

#endif /* __GASKET_CORE_H__ */
