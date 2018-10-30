/*
 * Copyright (C) 2004, 2013 Intel Corporation
 * Author: Naveen B S <naveen.b.s@intel.com>
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * ACPI based HotPlug driver that supports Memory Hotplug
 * This driver fields notifications from firmware for memory add
 * and remove operations and alerts the VM of the affected memory
 * ranges.
 */

#include <linux/acpi.h>
#include <linux/memory.h>
#include <linux/memory_hotplug.h>

#include "internal.h"

#define ACPI_MEMORY_DEVICE_CLASS		"memory"
#define ACPI_MEMORY_DEVICE_HID			"PNP0C80"
#define ACPI_MEMORY_DEVICE_NAME			"Hotplug Mem Device"

#define _COMPONENT		ACPI_MEMORY_DEVICE_COMPONENT

#undef PREFIX
#define 	PREFIX		"ACPI:memory_hp:"

ACPI_MODULE_NAME("acpi_memhotplug");

static const struct acpi_device_id memory_device_ids[] = {
	{ACPI_MEMORY_DEVICE_HID, 0},
	{"", 0},
};

#ifdef CONFIG_ACPI_HOTPLUG_MEMORY

/* Memory Device States */
#define MEMORY_INVALID_STATE	0
#define MEMORY_POWER_ON_STATE	1
#define MEMORY_POWER_OFF_STATE	2

static int acpi_memory_device_add(struct acpi_device *device,
				  const struct acpi_device_id *not_used);
static void acpi_memory_device_remove(struct acpi_device *device);

static struct acpi_scan_handler memory_device_handler = {
	.ids = memory_device_ids,
	.attach = acpi_memory_device_add,
	.detach = acpi_memory_device_remove,
	.hotplug = {
		.enabled = true,
	},
};

struct acpi_memory_info {
	struct list_head list;
	u64 start_addr;		/* Memory Range start physical addr */
	u64 length;		/* Memory Range length */
	unsigned short caching;	/* memory cache attribute */
	unsigned short write_protect;	/* memory read/write attribute */
	unsigned int enabled:1;
};

struct acpi_memory_device {
	struct acpi_device * device;
	unsigned int state;	/* State of the memory device */
	struct list_head res_list;
};

static acpi_status
acpi_memory_get_resource(struct acpi_resource *resource, void *context)
{
	struct acpi_memory_device *mem_device = context;
	struct acpi_resource_address64 address64;
	struct acpi_memory_info *info, *new;
	acpi_status status;

	status = acpi_resource_to_address64(resource, &address64);
	if (ACPI_FAILURE(status) ||
	    (address64.resource_type != ACPI_MEMORY_RANGE))
		return AE_OK;

	list_for_each_entry(info, &mem_device->res_list, list) {
		/* Can we combine the resource range information? */
		if ((info->caching == address64.info.mem.caching) &&
		    (info->write_protect == address64.info.mem.write_protect) &&
		    (info->start_addr + info->length == address64.address.minimum)) {
			info->length += address64.address.address_length;
			return AE_OK;
		}
	}

	new = kzalloc(sizeof(struct acpi_memory_info), GFP_KERNEL);
	if (!new)
		return AE_ERROR;

	INIT_LIST_HEAD(&new->list);
	new->caching = address64.info.mem.caching;
	new->write_protect = address64.info.mem.write_protect;
	new->start_addr = address64.address.minimum;
	new->length = address64.address.address_length;
	list_add_tail(&new->list, &mem_device->res_list);

	return AE_OK;
}

static void
acpi_memory_free_device_resources(struct acpi_memory_device *mem_device)
{
	struct acpi_memory_info *info, *n;

	list_for_each_entry_safe(info, n, &mem_device->res_list, list)
		kfree(info);
	INIT_LIST_HEAD(&mem_device->res_list);
}

static int
acpi_memory_get_device_resources(struct acpi_memory_device *mem_device)
{
	acpi_status status;

	if (!list_empty(&mem_device->res_list))
		return 0;

	status = acpi_walk_resources(mem_device->device->handle, METHOD_NAME__CRS,
				     acpi_memory_get_resource, mem_device);
	if (ACPI_FAILURE(status)) {
		acpi_memory_free_device_resources(mem_device);
		return -EINVAL;
	}

	return 0;
}

static int acpi_memory_check_device(struct acpi_memory_device *mem_device)
{
	unsigned long long current_status;

	/* Get device present/absent information from the _STA */
	if (ACPI_FAILURE(acpi_evaluate_integer(mem_device->device->handle,
					       METHOD_NAME__STA, NULL,
					       &current_status)))
		return -ENODEV;
	/*
	 * Check for device status. Device should be
	 * present/enabled/functioning.
	 */
	if (!((current_status & ACPI_STA_DEVICE_PRESENT)
	      && (current_status & ACPI_STA_DEVICE_ENABLED)
	      && (current_status & ACPI_STA_DEVICE_FUNCTIONING)))
		return -ENODEV;

	return 0;
}

static unsigned long acpi_meminfo_start_pfn(struct acpi_memory_info *info)
{
	return PFN_DOWN(info->start_addr);
}

static unsigned long acpi_meminfo_end_pfn(struct acpi_memory_info *info)
{
	return PFN_UP(info->start_addr + info->length-1);
}

static int acpi_bind_memblk(struct memory_block *mem, void *arg)
{
	return acpi_bind_one(&mem->dev, arg);
}

static int acpi_bind_memory_blocks(struct acpi_memory_info *info,
				   struct acpi_device *adev)
{
	return walk_memory_range(acpi_meminfo_start_pfn(info),
				 acpi_meminfo_end_pfn(info), adev,
				 acpi_bind_memblk);
}

static int acpi_unbind_memblk(struct memory_block *mem, void *arg)
{
	acpi_unbind_one(&mem->dev);
	return 0;
}

static void acpi_unbind_memory_blocks(struct acpi_memory_info *info)
{
	walk_memory_range(acpi_meminfo_start_pfn(info),
			  acpi_meminfo_end_pfn(info), NULL, acpi_unbind_memblk);
}

static int acpi_memory_enable_device(struct acpi_memory_device *mem_device)
{
	acpi_handle handle = mem_device->device->handle;
	int result, num_enabled = 0;
	struct acpi_memory_info *info;
	int node;

	node = acpi_get_node(handle);
	/*
	 * Tell the VM there is more memory here...
	 * Note: Assume that this function returns zero on success
	 * We don't have memory-hot-add rollback function,now.
	 * (i.e. memory-hot-remove function)
	 */
	list_for_each_entry(info, &mem_device->res_list, list) {
		if (info->enabled) { /* just sanity check...*/
			num_enabled++;
			continue;
		}
		/*
		 * If the memory block size is zero, please ignore it.
		 * Don't try to do the following memory hotplug flowchart.
		 */
		if (!info->length)
			continue;
		if (node < 0)
			node = memory_add_physaddr_to_nid(info->start_addr);

		result = add_memory(node, info->start_addr, info->length);

		/*
		 * If the memory block has been used by the kernel, add_memory()
		 * returns -EEXIST. If add_memory() returns the other error, it
		 * means that this memory block is not used by the kernel.
		 */
		if (result && result != -EEXIST)
			continue;

		result = acpi_bind_memory_blocks(info, mem_device->device);
		if (result) {
			acpi_unbind_memory_blocks(info);
			return -ENODEV;
		}

		info->enabled = 1;

		/*
		 * Add num_enable even if add_memory() returns -EEXIST, so the
		 * device is bound to this driver.
		 */
		num_enabled++;
	}
	if (!num_enabled) {
		dev_err(&mem_device->device->dev, "add_memory failed\n");
		mem_device->state = MEMORY_INVALID_STATE;
		return -EINVAL;
	}
	/*
	 * Sometimes the memory device will contain several memory blocks.
	 * When one memory block is hot-added to the system memory, it will
	 * be regarded as a success.
	 * Otherwise if the last memory block can't be hot-added to the system
	 * memory, it will be failure and the memory device can't be bound with
	 * driver.
	 */
	return 0;
}

static void acpi_memory_remove_memory(struct acpi_memory_device *mem_device)
{
	acpi_handle handle = mem_device->device->handle;
	struct acpi_memory_info *info, *n;
	int nid = acpi_get_node(handle);

	list_for_each_entry_safe(info, n, &mem_device->res_list, list) {
		if (!info->enabled)
			continue;

		if (nid == NUMA_NO_NODE)
			nid = memory_add_physaddr_to_nid(info->start_addr);

		acpi_unbind_memory_blocks(info);
		__remove_memory(nid, info->start_addr, info->length);
		list_del(&info->list);
		kfree(info);
	}
}

static void acpi_memory_device_free(struct acpi_memory_device *mem_device)
{
	if (!mem_device)
		return;

	acpi_memory_free_device_resources(mem_device);
	mem_device->device->driver_data = NULL;
	kfree(mem_device);
}

static int acpi_memory_device_add(struct acpi_device *device,
				  const struct acpi_device_id *not_used)
{
	struct acpi_memory_device *mem_device;
	int result;

	if (!device)
		return -EINVAL;

	mem_device = kzalloc(sizeof(struct acpi_memory_device), GFP_KERNEL);
	if (!mem_device)
		return -ENOMEM;

	INIT_LIST_HEAD(&mem_device->res_list);
	mem_device->device = device;
	sprintf(acpi_device_name(device), "%s", ACPI_MEMORY_DEVICE_NAME);
	sprintf(acpi_device_class(device), "%s", ACPI_MEMORY_DEVICE_CLASS);
	device->driver_data = mem_device;

	/* Get the range from the _CRS */
	result = acpi_memory_get_device_resources(mem_device);
	if (result) {
		device->driver_data = NULL;
		kfree(mem_device);
		return result;
	}

	/* Set the device state */
	mem_device->state = MEMORY_POWER_ON_STATE;

	result = acpi_memory_check_device(mem_device);
	if (result) {
		acpi_memory_device_free(mem_device);
		return 0;
	}

	result = acpi_memory_enable_device(mem_device);
	if (result) {
		dev_err(&device->dev, "acpi_memory_enable_device() error\n");
		acpi_memory_device_free(mem_device);
		return result;
	}

	dev_dbg(&device->dev, "Memory device configured by ACPI\n");
	return 1;
}

static void acpi_memory_device_remove(struct acpi_device *device)
{
	struct acpi_memory_device *mem_device;

	if (!device || !acpi_driver_data(device))
		return;

	mem_device = acpi_driver_data(device);
	acpi_memory_remove_memory(mem_device);
	acpi_memory_device_free(mem_device);
}

static bool __initdata acpi_no_memhotplug;

void __init acpi_memory_hotplug_init(void)
{
	if (acpi_no_memhotplug) {
		memory_device_handler.attach = NULL;
		acpi_scan_add_handler(&memory_device_handler);
		return;
	}
	acpi_scan_add_handler_with_hotplug(&memory_device_handler, "memory");
}

static int __init disable_acpi_memory_hotplug(char *str)
{
	acpi_no_memhotplug = true;
	return 1;
}
__setup("acpi_no_memhotplug", disable_acpi_memory_hotplug);

#else

static struct acpi_scan_handler memory_device_handler = {
	.ids = memory_device_ids,
};

void __init acpi_memory_hotplug_init(void)
{
	acpi_scan_add_handler(&memory_device_handler);
}

#endif /* CONFIG_ACPI_HOTPLUG_MEMORY */
