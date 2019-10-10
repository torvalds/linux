// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2012 Intel Corporation
 *    Author: Liu Jinsong <jinsong.liu@intel.com>
 *    Author: Jiang Yunhong <yunhong.jiang@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <xen/acpi.h>
#include <xen/interface/platform.h>
#include <asm/xen/hypercall.h>

#define PREFIX "ACPI:xen_memory_hotplug:"

struct acpi_memory_info {
	struct list_head list;
	u64 start_addr;		/* Memory Range start physical addr */
	u64 length;		/* Memory Range length */
	unsigned short caching;	/* memory cache attribute */
	unsigned short write_protect;	/* memory read/write attribute */
				/* copied from buffer getting from _CRS */
	unsigned int enabled:1;
};

struct acpi_memory_device {
	struct acpi_device *device;
	struct list_head res_list;
};

static bool acpi_hotmem_initialized __read_mostly;

static int xen_hotadd_memory(int pxm, struct acpi_memory_info *info)
{
	int rc;
	struct xen_platform_op op;

	op.cmd = XENPF_mem_hotadd;
	op.u.mem_add.spfn = info->start_addr >> PAGE_SHIFT;
	op.u.mem_add.epfn = (info->start_addr + info->length) >> PAGE_SHIFT;
	op.u.mem_add.pxm = pxm;

	rc = HYPERVISOR_dom0_op(&op);
	if (rc)
		pr_err(PREFIX "Xen Hotplug Memory Add failed on "
			"0x%lx -> 0x%lx, _PXM: %d, error: %d\n",
			(unsigned long)info->start_addr,
			(unsigned long)(info->start_addr + info->length),
			pxm, rc);

	return rc;
}

static int xen_acpi_memory_enable_device(struct acpi_memory_device *mem_device)
{
	int pxm, result;
	int num_enabled = 0;
	struct acpi_memory_info *info;

	if (!mem_device)
		return -EINVAL;

	pxm = xen_acpi_get_pxm(mem_device->device->handle);
	if (pxm < 0)
		return pxm;

	list_for_each_entry(info, &mem_device->res_list, list) {
		if (info->enabled) { /* just sanity check...*/
			num_enabled++;
			continue;
		}

		if (!info->length)
			continue;

		result = xen_hotadd_memory(pxm, info);
		if (result)
			continue;
		info->enabled = 1;
		num_enabled++;
	}

	if (!num_enabled)
		return -ENODEV;

	return 0;
}

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

static int
acpi_memory_get_device_resources(struct acpi_memory_device *mem_device)
{
	acpi_status status;
	struct acpi_memory_info *info, *n;

	if (!list_empty(&mem_device->res_list))
		return 0;

	status = acpi_walk_resources(mem_device->device->handle,
		METHOD_NAME__CRS, acpi_memory_get_resource, mem_device);

	if (ACPI_FAILURE(status)) {
		list_for_each_entry_safe(info, n, &mem_device->res_list, list)
			kfree(info);
		INIT_LIST_HEAD(&mem_device->res_list);
		return -EINVAL;
	}

	return 0;
}

static int acpi_memory_get_device(acpi_handle handle,
				  struct acpi_memory_device **mem_device)
{
	struct acpi_device *device = NULL;
	int result = 0;

	acpi_scan_lock_acquire();

	acpi_bus_get_device(handle, &device);
	if (acpi_device_enumerated(device))
		goto end;

	/*
	 * Now add the notified device.  This creates the acpi_device
	 * and invokes .add function
	 */
	result = acpi_bus_scan(handle);
	if (result) {
		pr_warn(PREFIX "ACPI namespace scan failed\n");
		result = -EINVAL;
		goto out;
	}
	device = NULL;
	acpi_bus_get_device(handle, &device);
	if (!acpi_device_enumerated(device)) {
		pr_warn(PREFIX "Missing device object\n");
		result = -EINVAL;
		goto out;
	}

end:
	*mem_device = acpi_driver_data(device);
	if (!(*mem_device)) {
		pr_err(PREFIX "driver data not found\n");
		result = -ENODEV;
		goto out;
	}

out:
	acpi_scan_lock_release();
	return result;
}

static int acpi_memory_check_device(struct acpi_memory_device *mem_device)
{
	unsigned long long current_status;

	/* Get device present/absent information from the _STA */
	if (ACPI_FAILURE(acpi_evaluate_integer(mem_device->device->handle,
				"_STA", NULL, &current_status)))
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

static int acpi_memory_disable_device(struct acpi_memory_device *mem_device)
{
	pr_debug(PREFIX "Xen does not support memory hotremove\n");

	return -ENOSYS;
}

static void acpi_memory_device_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_memory_device *mem_device;
	struct acpi_device *device;
	u32 ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE; /* default */

	switch (event) {
	case ACPI_NOTIFY_BUS_CHECK:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"\nReceived BUS CHECK notification for device\n"));
		/* Fall Through */
	case ACPI_NOTIFY_DEVICE_CHECK:
		if (event == ACPI_NOTIFY_DEVICE_CHECK)
			ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"\nReceived DEVICE CHECK notification for device\n"));

		if (acpi_memory_get_device(handle, &mem_device)) {
			pr_err(PREFIX "Cannot find driver data\n");
			break;
		}

		ost_code = ACPI_OST_SC_SUCCESS;
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"\nReceived EJECT REQUEST notification for device\n"));

		acpi_scan_lock_acquire();
		if (acpi_bus_get_device(handle, &device)) {
			acpi_scan_lock_release();
			pr_err(PREFIX "Device doesn't exist\n");
			break;
		}
		mem_device = acpi_driver_data(device);
		if (!mem_device) {
			acpi_scan_lock_release();
			pr_err(PREFIX "Driver Data is NULL\n");
			break;
		}

		/*
		 * TBD: implement acpi_memory_disable_device and invoke
		 * acpi_bus_remove if Xen support hotremove in the future
		 */
		acpi_memory_disable_device(mem_device);
		acpi_scan_lock_release();
		break;

	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		/* non-hotplug event; possibly handled by other handler */
		return;
	}

	(void) acpi_evaluate_ost(handle, event, ost_code, NULL);
	return;
}

static int xen_acpi_memory_device_add(struct acpi_device *device)
{
	int result;
	struct acpi_memory_device *mem_device = NULL;


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
		kfree(mem_device);
		return result;
	}

	/*
	 * For booting existed memory devices, early boot code has recognized
	 * memory area by EFI/E820. If DSDT shows these memory devices on boot,
	 * hotplug is not necessary for them.
	 * For hot-added memory devices during runtime, it need hypercall to
	 * Xen hypervisor to add memory.
	 */
	if (!acpi_hotmem_initialized)
		return 0;

	if (!acpi_memory_check_device(mem_device))
		result = xen_acpi_memory_enable_device(mem_device);

	return result;
}

static int xen_acpi_memory_device_remove(struct acpi_device *device)
{
	struct acpi_memory_device *mem_device = NULL;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	mem_device = acpi_driver_data(device);
	kfree(mem_device);

	return 0;
}

/*
 * Helper function to check for memory device
 */
static acpi_status is_memory_device(acpi_handle handle)
{
	char *hardware_id;
	acpi_status status;
	struct acpi_device_info *info;

	status = acpi_get_object_info(handle, &info);
	if (ACPI_FAILURE(status))
		return status;

	if (!(info->valid & ACPI_VALID_HID)) {
		kfree(info);
		return AE_ERROR;
	}

	hardware_id = info->hardware_id.string;
	if ((hardware_id == NULL) ||
	    (strcmp(hardware_id, ACPI_MEMORY_DEVICE_HID)))
		status = AE_ERROR;

	kfree(info);
	return status;
}

static acpi_status
acpi_memory_register_notify_handler(acpi_handle handle,
				    u32 level, void *ctxt, void **retv)
{
	acpi_status status;

	status = is_memory_device(handle);
	if (ACPI_FAILURE(status))
		return AE_OK;	/* continue */

	status = acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
					     acpi_memory_device_notify, NULL);
	/* continue */
	return AE_OK;
}

static acpi_status
acpi_memory_deregister_notify_handler(acpi_handle handle,
				      u32 level, void *ctxt, void **retv)
{
	acpi_status status;

	status = is_memory_device(handle);
	if (ACPI_FAILURE(status))
		return AE_OK;	/* continue */

	status = acpi_remove_notify_handler(handle,
					    ACPI_SYSTEM_NOTIFY,
					    acpi_memory_device_notify);

	return AE_OK;	/* continue */
}

static const struct acpi_device_id memory_device_ids[] = {
	{ACPI_MEMORY_DEVICE_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, memory_device_ids);

static struct acpi_driver xen_acpi_memory_device_driver = {
	.name = "acpi_memhotplug",
	.class = ACPI_MEMORY_DEVICE_CLASS,
	.ids = memory_device_ids,
	.ops = {
		.add = xen_acpi_memory_device_add,
		.remove = xen_acpi_memory_device_remove,
		},
};

static int __init xen_acpi_memory_device_init(void)
{
	int result;
	acpi_status status;

	if (!xen_initial_domain())
		return -ENODEV;

	/* unregister the stub which only used to reserve driver space */
	xen_stub_memory_device_exit();

	result = acpi_bus_register_driver(&xen_acpi_memory_device_driver);
	if (result < 0) {
		xen_stub_memory_device_init();
		return -ENODEV;
	}

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX,
				     acpi_memory_register_notify_handler,
				     NULL, NULL, NULL);

	if (ACPI_FAILURE(status)) {
		pr_warn(PREFIX "walk_namespace failed\n");
		acpi_bus_unregister_driver(&xen_acpi_memory_device_driver);
		xen_stub_memory_device_init();
		return -ENODEV;
	}

	acpi_hotmem_initialized = true;
	return 0;
}

static void __exit xen_acpi_memory_device_exit(void)
{
	acpi_status status;

	if (!xen_initial_domain())
		return;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX,
				     acpi_memory_deregister_notify_handler,
				     NULL, NULL, NULL);
	if (ACPI_FAILURE(status))
		pr_warn(PREFIX "walk_namespace failed\n");

	acpi_bus_unregister_driver(&xen_acpi_memory_device_driver);

	/*
	 * stub reserve space again to prevent any chance of native
	 * driver loading.
	 */
	xen_stub_memory_device_init();
	return;
}

module_init(xen_acpi_memory_device_init);
module_exit(xen_acpi_memory_device_exit);
ACPI_MODULE_NAME("xen-acpi-memhotplug");
MODULE_AUTHOR("Liu Jinsong <jinsong.liu@intel.com>");
MODULE_DESCRIPTION("Xen Hotplug Mem Driver");
MODULE_LICENSE("GPL");
